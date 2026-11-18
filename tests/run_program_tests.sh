#!/usr/bin/env bash
# Runs every program in tests/run_cases.cby and checks its output.
#   usage: run_program_tests.sh path/to/cabby [run|compile]   (run: `cabby prog.cby`, compile: `cabby -c`)
# Case annotations:
#   //> text        expected stdout line (in order; must match exactly)
#   // exit: N      expected exit status (default 0)
#   // panic: text  expect exit status 101 and stderr containing text
#   // args: a b    command-line arguments for the program
#   // only: run     the case is for one mode (the interpreter's own checks)
set -u
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
needs "$CLI"
MODE=${2:-run}
CASES=$ROOT/tests/run_cases.cby
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0

awk -v dir="$TMP" '
	/^### / { name = $2; file = dir "/" name ".cby"; print name > (dir "/names"); next }
	{ if (file) print > file }
' "$CASES"

while read -r name; do
	f="$TMP/$name.cby"
	only=$(sed -n 's/^\/\/ only: //p' "$f" | head -1)
	[[ -n "$only" && "$only" != "$MODE" ]] && continue
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	want_exit=$(sed -n 's/^\/\/ exit: //p' "$f" | head -1)
	want_panic=$(sed -n 's/^\/\/ panic: //p' "$f" | head -1)
	read -r -a args <<< "$(sed -n 's/^\/\/ args: //p' "$f" | head -1)"
	printf '%b' "$(sed -n 's/^\/\/ stdin: //p' "$f" | head -1)" > "$TMP/stdin"
	[[ -n "$want_panic" ]] && want_exit=101
	want_exit=${want_exit:-0}

	if [[ "$MODE" == "compile" ]]; then
		if ! "$CLI" -c "$f" -o "$TMP/$name.bin" > "$TMP/cout" 2>&1; then
			fail=$((fail+1)); echo "FAIL $name (compile error)"; sed 's/^/  | /' "$TMP/cout" | head -15; continue
		fi
		timeout 20 "$TMP/$name.bin" ${args[@]+"${args[@]}"} < "$TMP/stdin" > "$TMP/got" 2> "$TMP/err"; rc=$?
	else
		timeout 20 "$CLI" "$f" ${args[@]+"${args[@]}"} < "$TMP/stdin" > "$TMP/got" 2> "$TMP/err"; rc=$?
	fi

	ok=1
	cmp -s "$TMP/want" "$TMP/got" || ok=0
	[[ $rc -eq $want_exit ]] || ok=0
	if [[ -n "$want_panic" ]] && ! grep -qF -- "$want_panic" "$TMP/err"; then ok=0; fi
	if [[ -z "$want_panic" && -s "$TMP/err" ]] && grep -q 'Sanitizer\|runtime error' "$TMP/err"; then ok=0; fi

	if [[ $ok -eq 1 ]]; then pass=$((pass+1)); else
		fail=$((fail+1))
		echo "FAIL $name (exit $rc, want $want_exit)"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  /'
		head -20 "$TMP/err" | sed 's/^/  err| /'
	fi
done < "$TMP/names"

echo "program tests ($MODE${BYTECODE_LABEL:-}): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
