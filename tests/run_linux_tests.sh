#!/usr/bin/env bash
# The linux library. tests/linux/linux.cby against a pretend /sys, /proc and
# /dev (tests/linux/sys, proc and dev: CABBY_SYSFS, CABBY_PROCFS and
# CABBY_DEVFS), and stand-ins for systemctl and modprobe (tests/linux/tools,
# their state in a folder of the test's own); board.cby against a board's
# sensors (tests/linux/board: thermal zones); and, on Linux, here.cby on
# this computer as it is. Each must print what its //> lines say.
#
# Usage: run_linux_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
DIR=$ROOT/tests/linux
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
# (name, environment)
check() {
	local name=$1 env=$2
	rm -rf "$TMP/run"; mkdir "$TMP/run"; cp -R "$DIR/." "$TMP/run/"
	local prog=("$CLI" "$DIR/$name.cby")
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$DIR/$name.cby" -o "$TMP/$name" > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL linux/$name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; return
		fi
		prog=("$TMP/$name")
	fi
	(cd "$TMP/run" && { [[ -z "$env" ]] || export ${env//@/$TMP/run}; } && timeout 60 "${prog[@]}") > "$TMP/got" 2> "$TMP/err"
	local rc=$?
	sed -n 's/^\/\/> \{0,1\}//p' "$DIR/$name.cby" > "$TMP/want"
	local want_panic ok=1
	want_panic=$(sed -n 's/^\/\/ panic: //p' "$DIR/$name.cby" | head -1)
	cmp -s "$TMP/want" "$TMP/got" || ok=0
	if [[ -n "$want_panic" ]]; then
		[[ $rc -eq 101 ]] && grep -qF -- "$want_panic" "$TMP/err" || ok=0
	elif [[ $rc -ne 0 ]]; then
		ok=0
	fi
	if [[ $ok -eq 1 ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL linux/$name ($MODE, exit $rc)"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  | /'
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
}
check linux "CABBY_SYSFS=@/sys CABBY_PROCFS=@/proc CABBY_DEVFS=@/dev CABBY_TOOLS=@/tools CABBY_TEST_STATE=@"
check board "CABBY_SYSFS=@/board CABBY_TOOLS=@/tools CABBY_TEST_STATE=@ CABBY_NOT_ROOT=1"
if [[ "$(uname)" == Linux ]]; then
	check here ""
fi
echo "linux ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
