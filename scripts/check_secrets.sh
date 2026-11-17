#!/usr/bin/env bash
# Refuse known secret patterns in added lines.
#   (default)              staged changes (git diff --cached)
#   --worktree             working tree vs HEAD
#   --range <base>..<head> every commit in base..head, each commit's own patch, so a secret
#                          added and later removed still fails. <base>...<head> scans the
#                          commits since the merge base (PR form). A base of 40 zeros or the
#                          empty tree means "no previous commit": every commit reachable from head.
# Exit: 0 clean, 1 secret found, 2 usage/revision/git error. Never prints secret values:
# only commit, file, line number, and rule name. Bash 3.2 compatible (macOS /bin/bash).
set -euo pipefail
# Byte-wise regex matching: in a UTF-8 locale `.*` may not match invalid bytes and hide a secret.
export LC_ALL=C
# Never let replace refs (git replace) substitute clean history for the real commits.
export GIT_NO_REPLACE_OBJECTS=1

die() {
	printf 'check_secrets: ERROR: %s\n' "$1" >&2
	exit 2
}

top="$(git rev-parse --show-toplevel)" || die "not a git repository"
cd "$top"

EMPTY_TREE="$(git hash-object -t tree /dev/null)" || die "git hash-object failed"
ZERO_SHA="0000000000000000000000000000000000000000"

commit_of() { # resolve to a full commit sha or die
	local sha
	sha="$(git rev-parse --verify --quiet --end-of-options "$1^{commit}")" || die "not a commit: $1"
	[[ -n "$sha" ]] || die "not a commit: $1"
	printf '%s\n' "$sha"
}

patch="$(mktemp "${TMPDIR:-/tmp}/check_secrets.XXXXXX")"
trap 'rm -f "$patch" "$patch.txt"' EXIT

# --text: binary/-diff files (e.g. via .gitattributes) still show their added lines.
# No --diff-filter: type changes (symlink -> file) carry added lines too; deletions only add "-".
diff_opts=(--text --no-textconv --no-color --no-ext-diff --find-renames -U0)
case "${1:-}" in
"" | --cached)
	label="--cached"
	git diff --cached "${diff_opts[@]}" >"$patch" || die "git diff --cached failed"
	;;
--worktree)
	label="HEAD"
	git diff HEAD "${diff_opts[@]}" >"$patch" || die "git diff HEAD failed"
	;;
--range)
	spec="${2:-}"
	[[ -n "$spec" ]] || die "usage: --range <base>..<head> | <base>...<head>"
	if [[ "$spec" == *...* ]]; then
		base="${spec%%...*}" head="${spec#*...}" sym=1
	elif [[ "$spec" == *..* ]]; then
		base="${spec%%..*}" head="${spec#*..}" sym=0
	else
		die "range must be <base>..<head> or <base>...<head>: $spec"
	fi
	[[ -n "$base" && -n "$head" && "$head" != *..* ]] || die "malformed range: $spec"
	head_sha="$(commit_of "$head")"
	if [[ "$base" == "$ZERO_SHA" || "$base" == "$EMPTY_TREE" ]]; then
		revs=("$head_sha") # no previous commit: whole history of head
	else
		base_sha="$(commit_of "$base")"
		if ((sym)); then
			base_sha="$(git merge-base "$base_sha" "$head_sha")" || die "no merge base for $spec"
		fi
		revs=("$base_sha..$head_sha")
	fi
	label="$spec"
	count="$(git rev-list --count "${revs[@]}")" || die "git rev-list failed for $spec"
	# --root: a root commit's patch is its full content. --diff-merges=separate: each merge is
	# diffed against every parent, whatever log.diffMerges says (never a combined "diff --cc").
	git log --root --diff-merges=separate --format='commit %H' -p "${diff_opts[@]}" "${revs[@]}" >"$patch" ||
		die "git log -p failed for $spec"
	if ((count > 0)) && ! grep -q '^commit [0-9a-f]\{40\}' "$patch"; then
		die "git log produced no commits for a non-empty range ($count): $spec"
	fi
	;;
*) die "unknown option: $1" ;;
esac

# rule names and extended regexes, applied to added lines only.
rule_names=(unreal-afs-security-token private-key github-token openai-style-key aws-access-key)
rule_res=(
	'^\+[[:space:]]*SecurityToken=[^[:space:]]+'
	'^\+.*-----BEGIN [A-Z ]*PRIVATE KEY-----'
	'^\+.*(ghp|gho|ghs|ghu|github_pat)_[A-Za-z0-9_]{20,}'
	'^\+.*sk-[A-Za-z0-9_-]{20,}'
	'^\+.*AKIA[0-9A-Z]{16}'
)

# bash `read` cannot carry NUL bytes and would drop the rest of the line; blank them first.
LC_ALL=C tr '\000' ' ' <"$patch" >"$patch.txt" || die "cannot normalise diff output"

found=0 commit="" file="" line=0 inhunk=0
while IFS= read -r row || [[ -n "$row" ]]; do
	# Headers only outside a hunk: an added line "++ b/x" shows as "+++ b/x" inside one.
	if ((!inhunk)) || [[ "$row" != [-+\\]* ]]; then
		case "$row" in
		"commit "*)
			commit="${row#commit }" file="" inhunk=0
			continue
			;;
		"diff "*)
			file="" inhunk=0
			continue
			;;
		"+++ /dev/null")
			file=""
			continue
			;;
		"+++ b/"*)
			file="${row#+++ b/}"
			continue
			;;
		"+++ "*)
			file="${row#+++ }"
			continue
			;; # quoted path: still scanned
		esac
	fi
	if [[ "$row" =~ ^@@\ -[0-9,]+\ \+([0-9]+) ]]; then
		line=$((BASH_REMATCH[1] - 1)) inhunk=1
		continue
	fi
	[[ -n "$file" && "$row" == +* ]] || continue
	line=$((line + 1))
	for i in "${!rule_names[@]}"; do
		[[ "$row" =~ ${rule_res[i]} ]] || continue
		name="${rule_names[i]}"
		# Unreal's own placeholder (whole line); the editor replaces it with a fresh GUID.
		[[ "$name" == unreal-afs-security-token && "$row" =~ ^\+[[:space:]]*SecurityToken=(\[AUTO\])?[[:space:]]*$ ]] && continue
		printf 'SECRET_FOUND: %s%s:%d rule=%s\n' "${commit:+${commit:0:12} }" "$file" "$line" "$name" >&2
		found=1
	done
done <"$patch.txt"

if ((found)); then
	echo "Refusing: remove the values above (Unreal AFS token: set SecurityToken=[AUTO] or drop the generated block)." >&2
	exit 1
fi
echo "check_secrets: clean ($label)"
