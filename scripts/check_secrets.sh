#!/usr/bin/env bash
# Refuse known secret patterns in staged changes (default) or the working-tree diff (--worktree).
# Never prints secret values: only file, line number, and rule name.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

mode="--cached"
[[ "${1:-}" == "--worktree" ]] && mode="HEAD"
[[ "${1:-}" == "--range" ]] && mode="${2:?usage: --range <base>...<head>}"

# rule-name<TAB>extended regex applied to added lines only.
rules=$'unreal-afs-security-token\t^\\+[[:space:]]*SecurityToken=[^[:space:]]+
private-key\t^\\+.*-----BEGIN [A-Z ]*PRIVATE KEY-----
github-token\t^\\+.*(ghp|gho|ghs|ghu|github_pat)_[A-Za-z0-9_]{20,}
openai-style-key\t^\\+.*sk-[A-Za-z0-9_-]{20,}
aws-access-key\t^\\+.*AKIA[0-9A-Z]{16}'

found=0
while IFS= read -r file; do
	[[ -z "$file" ]] && continue
	diff_text="$(git diff "$mode" --no-color --no-ext-diff --find-renames -U0 -- "$file")"
	while IFS=$'\t' read -r name pattern; do
		line=0
		while IFS= read -r row; do
			if [[ "$row" =~ ^@@\ -[0-9,]+\ \+([0-9]+) ]]; then
				line=$((BASH_REMATCH[1] - 1))
				continue
			fi
			[[ "$row" == +* && "$row" != +++* ]] || continue
			line=$((line + 1))
			if [[ "$row" =~ $pattern ]]; then
				# Unreal's own placeholder; the editor replaces it with a fresh GUID.
				[[ "$name" == unreal-afs-security-token && "$row" =~ SecurityToken=(\[AUTO\])?[[:space:]]*$ ]] && continue
				printf 'SECRET_FOUND: %s:%d rule=%s\n' "$file" "$line" "$name" >&2
				found=1
			fi
		done <<<"$diff_text"
	done <<<"$rules"
done < <(git diff "$mode" --name-only --find-renames --diff-filter=ACMR)

if ((found)); then
	echo "Refusing: remove the values above (Unreal AFS token: set SecurityToken=[AUTO] or drop the generated block)." >&2
	exit 1
fi
echo "check_secrets: clean ($mode)"
