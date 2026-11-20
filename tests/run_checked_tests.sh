#!/usr/bin/env bash
# cabby -c --checked: programs built with AddressSanitizer. Reading past an
# array, using memory after it's freed, and (on Linux) memory never freed
# are each caught and said; a program that does none runs as it would.
# Skipped where clang has no AddressSanitizer (Debian's libclang-rt-dev,
# Arch's compiler-rt).
#   usage: run_checked_tests.sh path/to/cabby
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
needs "$CLI"
DIR=$ROOT/tests/checked
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CC=${CABBY_CC:-clang}
printf 'int main(void) { return 0; }\n' > "$TMP/probe.c"
if ! "$CC" -fsanitize=address "$TMP/probe.c" -o "$TMP/probe" > /dev/null 2>&1; then
	echo "checked builds: skipped ($CC has no AddressSanitizer)"
	exit 0
fi
pass=0; fail=0
# (name, what the report must say, or "" for a clean run with this output)
check() {
	local name=$1 want=$2 out=$3
	if ! "$CLI" -c --checked "$DIR/$name.cby" -o "$TMP/$name" > "$TMP/build" 2>&1; then
		fail=$((fail+1)); echo "FAIL $name (build)"; head -5 "$TMP/build" | sed 's/^/  | /'; return
	fi
	(cd "$TMP" && ASAN_OPTIONS=detect_leaks=$LEAKS ./"$name" > "$TMP/got" 2>&1); local rc=$?
	if [[ -n "$want" ]]; then
		if [[ $rc -ne 0 ]] && grep -q "$want" "$TMP/got"; then pass=$((pass+1)); else
			fail=$((fail+1)); echo "FAIL $name: expected \"$want\" (exit $rc)"; head -5 "$TMP/got" | sed 's/^/  | /'; fi
	else
		if [[ $rc -eq 0 && "$(cat "$TMP/got")" == "$out" ]]; then pass=$((pass+1)); else
			fail=$((fail+1)); echo "FAIL $name (exit $rc)"; head -5 "$TMP/got" | sed 's/^/  | /'; fi
	fi
}
LEAKS=1
[[ "$(uname)" == Darwin ]] && LEAKS=0   # (no leak checker on a Mac's Apple processors)
check past "stack-buffer-overflow"
check freed "heap-use-after-free"
[[ $LEAKS == 1 ]] && check leak "detected memory leaks"
check fine "" $'7\n[a, b]'
echo "checked builds: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
