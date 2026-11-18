#!/usr/bin/env bash
# Semantic/ownership tests. Splits tests/sema_cases.cby on "### name" lines
# and runs `cabby --check` on each case.
#   // expect: ok             — must check cleanly
#   // expect: <substring>    — must fail with output containing <substring>
#   // expect: warning: <text> — must check cleanly, warning with <text>
#   // errors: N              — (optional) exact number of errors
set -u
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
needs "$CLI"
CASES=$ROOT/tests/sema_cases.cby
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0

awk -v dir="$TMP" '
	/^### / { name = $2; file = dir "/" name ".cby"; print name > (dir "/names"); next }
	{ if (file) print > file }
' "$CASES"

while read -r name; do
	f="$TMP/$name.cby"
	expect=$(sed -n "s/^\/\/ expect: //p" "$f" | head -1)
	want_n=$(sed -n "s/^\/\/ errors: //p" "$f" | head -1)
	out=$("$CLI" --check "$f" 2>&1); rc=$?
	n=$(printf '%s\n' "$out" | grep -c ': error: ')
	ok=1
	if [[ "$expect" == "ok" ]]; then
		[[ $rc -eq 0 && $n -eq 0 ]] || ok=0
	elif [[ "$expect" == "warning: "* ]]; then
		[[ $rc -eq 0 && $n -eq 0 && "$out" == *"$expect"* ]] || ok=0
	else
		[[ $rc -ne 0 && "$out" == *"$expect"* ]] || ok=0
		[[ -z "$want_n" || "$n" -eq "$want_n" ]] || ok=0
	fi
	if [[ $ok -eq 1 ]]; then pass=$((pass+1)); else
		fail=$((fail+1))
		echo "FAIL $name (rc=$rc, errors=$n${want_n:+, want $want_n})"
		echo "  expect: $expect"
		printf '%s\n' "$out" | head -12 | sed 's/^/  | /'
	fi
done < "$TMP/names"

echo "sema tests: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
