#!/usr/bin/env bash
# Regression tests for scripts/check_secrets.sh in throwaway repos. Fake credentials are built
# at runtime so this file itself never contains a matching literal.
# Usage: bash scripts/check_secrets.test.sh   (CHECK_SECRETS=<path> overrides the script under test)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$(cd "$(dirname "${CHECK_SECRETS:-$here/check_secrets.sh}")" && pwd)/$(basename "${CHECK_SECRETS:-check_secrets.sh}")"
[[ -f "$SCRIPT" ]] || {
	echo "missing $SCRIPT" >&2
	exit 2
}

work="$(mktemp -d "${TMPDIR:-/tmp}/check_secrets_test.XXXXXX")"
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
export HOME="$work/home" GIT_CONFIG_NOSYSTEM=1 GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@example.invalid \
	GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@example.invalid
mkdir -p "$HOME"
git config --global init.defaultBranch main
git config --global core.hooksPath /dev/null

fake_gh="gh""p_$(printf 'A%.0s' $(seq 1 36))"
fake_aws="AK""IA$(printf 'Q%.0s' $(seq 1 16))"
ZERO=0000000000000000000000000000000000000000

pass=0 fail=0
expect() { # expect <rc> <name> <cmd...>; rc is the exact required exit status
	local want="$1" name="$2" got=0 out
	shift 2
	out="$("$@" 2>&1)" || got=$?
	if [[ "$got" == "$want" ]]; then
		pass=$((pass + 1)) && echo "ok   $name (rc=$got)"
	else
		fail=$((fail + 1)) && echo "FAIL $name: want rc=$want got rc=$got"$'\n'"$out" | sed 's/^/     /'
	fi
	if [[ "$out" == *"$fake_gh"* || "$out" == *"$fake_aws"* ]]; then
		fail=$((fail + 1)) && echo "FAIL $name: secret value printed"
	fi
}
scan() { (cd "$repo" && bash "$SCRIPT" "$@"); }
commit() { (cd "$repo" && git add -A && git commit -qm "$1" && git rev-parse HEAD); }

repo="$work/r" && git init -q "$repo"
echo "hello" >"$repo/a.txt"
c1="$(commit c1)"
echo "world" >>"$repo/a.txt"
c2="$(commit c2)"

# Staged changes.
echo "clean line" >"$repo/b.txt" && (cd "$repo" && git add b.txt)
expect 0 "clean staged change passes" scan
expect 0 "clean staged change passes (--cached)" scan --cached
printf 'key = "%s"\n' "$fake_gh" >"$repo/c.txt" && (cd "$repo" && git add c.txt)
expect 1 "staged synthetic GitHub token fails" scan
(cd "$repo" && git rm -q --cached c.txt && rm c.txt && git reset -q b.txt && rm b.txt)
expect 0 "empty staged change set passes" scan

# Invalid revisions / ranges.
expect 2 "unknown base revision fails" scan --range "deadbeef..$c2"
expect 2 "unknown head revision fails" scan --range "$c1..nope"
expect 2 "tree as base (not zero/empty tree) fails" scan --range "$(cd "$repo" && git rev-parse "$c1^{tree}")..$c2"
expect 2 "range without dots fails" scan --range "$c2"
expect 2 "missing range argument fails" scan --range
expect 2 "unknown option fails" scan --bogus
expect 0 "empty valid range passes" scan --range "$c2..$c2"
expect 0 "clean push range passes" scan --range "$c1..$c2"

# Secret added then removed inside the scanned range: the final tree is clean, history is not.
printf 'aws = %s\n' "$fake_aws" >"$repo/leak.ini"
c3="$(commit leak)"
rm "$repo/leak.ini"
c4="$(commit unleak)"
expect 0 "tree diff alone would look clean (sanity)" bash -c "cd '$repo' && git diff --quiet $c2 $c4"
expect 1 "secret introduced then removed fails" scan --range "$c2..$c4"
expect 0 "range after the removal passes" scan --range "$c4..$c4"

# Initial push: before is 40 zeros or the empty tree; the whole history of head is scanned.
expect 1 "first push (zero base) scans full history" scan --range "$ZERO..$c4"
expect 1 "empty-tree base scans full history" scan --range "$(git hash-object -t tree /dev/null)..$c4"
expect 1 "empty-tree three-dot base (old CI form) scans full history" scan --range "$(git hash-object -t tree /dev/null)...$c4"
expect 0 "first push of clean history passes" scan --range "$ZERO..$c2"
expect 1 "secret in root commit is found" bash -c "
	set -e; r='$work/root'; git init -q \"\$r\"; cd \"\$r\"
	printf '%s\n' '$fake_gh' >k.txt; git add k.txt; git commit -qm root
	bash '$SCRIPT' --range '$ZERO..HEAD'"

# Pull request: base...head scans only the PR branch's own commits since the merge base.
(cd "$repo" && git checkout -q -b feature "$c2")
echo "feature work" >"$repo/f.txt"
f1="$(commit f1)"
expect 0 "clean PR (target has a removed leak) passes" scan --range "$c4...$f1"
printf 'token: %s\n' "$fake_gh" >"$repo/f.txt"
f2="$(commit f2)"
expect 1 "PR adding a secret fails" scan --range "$c4...$f2"
(cd "$repo" && git checkout -q main && git merge -q --no-ff -m merge feature)
expect 1 "merge commit range carrying a secret fails" scan --range "$c4..main"

# Evil merge: the secret exists only in the merge commit itself.
(cd "$repo" && git checkout -q -b side "$c4" && echo side >s.txt && git add s.txt && git commit -qm side &&
	git checkout -q main && git merge -q --no-commit --no-ff side && printf 'x=%s\n' "$fake_aws" >evil.txt &&
	git add evil.txt && git commit -qm evil-merge)
expect 1 "secret only in a merge commit fails" scan --range "main^1..main"
expect 1 "evil merge found despite log.diffMerges=combined" bash -c "cd '$repo' && git -c log.diffMerges=combined log -1 >/dev/null && GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=log.diffMerges GIT_CONFIG_VALUE_0=combined bash '$SCRIPT' --range 'main^1..main'"
m="$(cd "$repo" && git rev-parse main)"

# Binary / -diff attribute must not hide added lines.
printf '* -diff\n' >"$repo/.gitattributes"
printf 'tok %s\n' "$fake_gh" >"$repo/bin.dat"
mb="$(commit binattr)"
expect 1 "secret hidden behind '* -diff' attribute fails" scan --range "$m..$mb"
rm "$repo/.gitattributes" "$repo/bin.dat"
printf 'a\0b %s\n' "$fake_aws" >"$repo/nul.bin"
mn="$(commit nulfile)"
expect 1 "secret in a NUL-containing file fails" scan --range "$mb..$mn"

# Symlink replaced by a regular file holding a secret (type change T).
(cd "$repo" && ln -s a.txt link && git add link && git commit -qm link)
ml="$(cd "$repo" && git rev-parse HEAD)"
rm "$repo/link" && printf '%s\n' "$fake_gh" >"$repo/link"
mt="$(commit typechange)"
expect 1 "symlink -> file type change with secret fails" scan --range "$ml..$mt"

# Parser edge cases: added lines that look like diff headers ("+++ b/x", "+++ /dev/null" in a hunk).
reports() { # reports <needle> <name> <cmd...>: rc 1 and the report names <needle>
	local needle="$1" name="$2" got=0 out
	shift 2
	out="$("$@" 2>&1)" || got=$?
	if [[ "$got" == 1 && "$out" == *"$needle"* ]]; then
		pass=$((pass + 1)) && echo "ok   $name"
	else
		fail=$((fail + 1)) && echo "FAIL $name: rc=$got"$'\n'"$out" | sed 's/^/     /'
	fi
	if [[ "$out" == *"$fake_gh"* || "$out" == *"$fake_aws"* ]]; then
		fail=$((fail + 1)) && echo "FAIL $name: secret value printed"
	fi
}
printf '++ b/%s\n' "$fake_gh" >"$repo/hdr.txt"
mh="$(commit hdr)"
reports "hdr.txt:1 rule=github-token" "secret on an added line '++ b/<token>' fails" scan --range "$mt..$mh"
printf '++ /dev/null\n%s\n' "$fake_aws" >"$repo/dn.txt"
md="$(commit devnull)"
reports "dn.txt:2 rule=aws-access-key" "added line '++ /dev/null' does not hide next secret" scan --range "$mh..$md"
printf '%s\r\n' "$fake_gh" >"$repo/sp ace é.txt"
mq="$(commit quoted)"
reports "sp ace" "quoted non-ASCII/space filename + CRLF fails" scan --range "$md..$mq"
printf '%s' "$fake_aws" >"$repo/nonl.txt"
ms="$(commit nonl)"
reports "nonl.txt:1 rule=aws-access-key" "file without trailing newline fails" scan --range "$mq..$ms"
printf '\377 %s\n' "$fake_aws" >"$repo/ff.txt"
mf="$(commit ffbyte)"
utf8="$(locale -a 2>/dev/null | grep -iE '^(en_US|C)\.utf-?8$' | sed -n 1p || true)"
if [[ -n "$utf8" ]]; then
	reports "ff.txt:1" "invalid UTF-8 byte before secret fails in $utf8" env LC_ALL="$utf8" LANG="$utf8" bash -c "cd '$repo' && bash '$SCRIPT' --range '$mq..$mf'"
else
	echo "skip invalid-UTF-8 case: no UTF-8 locale installed"
fi
ms="$mf"

# Every existing rule, and the Unreal placeholder exception.
printf -- '-----BEGIN RSA %s KEY-----\n' "PRIVATE" >"$repo/k.pem" && mk="$(commit pem)"
expect 1 "private-key rule fails" scan --range "$ms..$mk"
printf 'k=sk-%s\n' "$(printf 'z%.0s' $(seq 1 24))" >"$repo/o.txt" && mo="$(commit openai)"
expect 1 "openai-style-key rule fails" scan --range "$mk..$mo"
printf '[S]\nSecurityToken=%s\n' "$(printf 'F%.0s' $(seq 1 32))" >"$repo/E.ini" && me="$(commit afs)"
expect 1 "unreal-afs-security-token rule fails" scan --range "$mo..$me"
printf '[S]\nSecurityToken=[AUTO]\n' >"$repo/E.ini" && mp="$(commit placeholder)"
expect 0 "SecurityToken=[AUTO] placeholder passes" scan --range "$me..$mp"
printf '[S]\nSecurityToken=%s SecurityToken=\n' "$(printf 'F%.0s' $(seq 1 32))" >"$repo/E.ini" && mx="$(commit afs-trailing)"
expect 1 "real token followed by a bare 'SecurityToken=' still fails" scan --range "$mp..$mx"

# git replace must not swap a leaking commit for a clean one.
echo "clean twin" >"$repo/twin.txt" && mclean="$(commit twin)"
(cd "$repo" && git reset -q --hard "$mx")
printf 'aws = %s\n' "$fake_aws" >"$repo/twin.txt" && mleak="$(commit leaktwin)"
(cd "$repo" && git replace "$mleak" "$mclean")
expect 1 "replace ref cannot hide a leaking commit" scan --range "$mx..$mleak"
(cd "$repo" && git replace -d "$mleak" >/dev/null)
mp="$mleak"

# More malformed ranges.
expect 2 "leading-dash base fails" scan --range "-x..$mp"
expect 2 "a..b..c fails" scan --range "$c1..$c2..$mp"
expect 2 "empty base (missing push before) fails" scan --range "..$mp"

# --worktree (documented in docs/HARNESS.md): unstaged change vs HEAD.
echo clean >>"$repo/a.txt"
expect 0 "--worktree clean change passes" scan --worktree
printf '%s\n' "$fake_gh" >>"$repo/a.txt"
expect 1 "--worktree unstaged secret fails" scan --worktree
(cd "$repo" && git checkout -q -- a.txt)

# Git command failures must not read as clean.
mkdir -p "$work/bin"
real_git="$(command -v git)"
cat >"$work/bin/git" <<EOF
#!/usr/bin/env bash
case "\$1" in diff|log) echo "fatal: injected failure" >&2; exit 128 ;; esac
exec "$real_git" "\$@"
EOF
chmod +x "$work/bin/git"
expect 2 "failing git diff (--cached) fails" env PATH="$work/bin:$PATH" bash -c "cd '$repo' && bash '$SCRIPT'"
expect 2 "failing git log (--range) fails" env PATH="$work/bin:$PATH" bash -c "cd '$repo' && bash '$SCRIPT' --range '$c1..$c2'"
blob="$(cd "$repo" && git rev-parse "$c3:leak.ini")"
cp -R "$repo" "$work/corrupt"
rm -f "$work/corrupt/.git/objects/${blob:0:2}/${blob:2}"
expect 2 "missing object in history fails" bash -c "cd '$work/corrupt' && bash '$SCRIPT' --range '$c2..$c4'"
expect 2 "outside a git repository fails" bash -c "cd '$work/home' && GIT_CEILING_DIRECTORIES='$work' bash '$SCRIPT'"

echo "check_secrets tests: $pass passed, $fail failed"
((fail == 0))
