#!/usr/bin/env bash
# Multi-file programs: use, implementation files, static privacy, examples.
#   usage: run_multi_tests.sh path/to/cabby [run|compile]   (run: `cabby prog.cby`, compile: `cabby -c`)
set -u
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
needs "$CLI"
MODE=${2:-run}
DIR=$ROOT/tests
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0

run() {   # run FILE -> stdout of the program
	if [[ "$MODE" == "compile" ]]; then
		"$CLI" -c "$1" -o "$TMP/prog" >/dev/null 2>&1 && "$TMP/prog"
	else
		"$CLI" "$1"
	fi
}

check_output() {   # NAME FILE EXPECTED
	local got; got=$(run "$2" 2>&1)
	if [[ "$got" == "$3" ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1"; echo "  want: $3"; echo "  got:  $got"; fi
}

check_error() {   # NAME FILE MESSAGE
	local out; out=$("$CLI" --check "$2" 2>&1); local rc=$?
	if [[ $rc -ne 0 && "$out" == *"$3"* ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1"; echo "  want: $3"; echo "$out" | sed 's/^/  | /'; fi
}

check_output headers_and_impl "$DIR/multi/main.cby" $'42 true 12\n5 10\nprintf 7'
check_output example "$DIR/../examples/example.cby" $'The char you chose is...\nf\n5'
check_output headerless_module "$DIR/multi/headerless.cby" '42'
check_output local_prefix "$DIR/multi/prefixed.cby" '5.0 7.0'
check_error  static_is_private "$DIR/multi/private.cby" "'helper' is declared static in"

echo "multi-file tests ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
