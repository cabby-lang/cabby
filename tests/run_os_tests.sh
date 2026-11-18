#!/usr/bin/env bash
# The os library: each program in tests/os/ run in a folder of its own,
# printing what its //> lines say (and panicking as its // panic: line
# says). watch.cby runs twice: watching as Linux does (inotify), and
# (CABBY_WATCH_BY_LOOKING=1) as a Mac does, looking again and comparing,
# which works on Linux too, so it's tested everywhere.
#
# Usage: run_os_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
DIR=$ROOT/tests/os
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
for f in "$DIR"/*.cby; do
	name=$(basename "$f" .cby)
	ways=("")
	[[ "$name" == watch ]] && ways=("CABBY_WATCH_BY_LOOKING=0" "CABBY_WATCH_BY_LOOKING=1")
	if [[ "$MODE" == compile ]] && ! "$CLI" -c "$f" -o "$TMP/$name" > "$TMP/err" 2>&1; then
		fail=$((fail+1)); echo "FAIL os/$name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; continue
	fi
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	want_panic=$(sed -n 's/^\/\/ panic: //p' "$f" | head -1)
	for way in "${ways[@]}"; do
		rm -rf "$TMP/run"; mkdir "$TMP/run"
		if [[ "$MODE" == compile ]]; then
			(cd "$TMP/run" && { [[ -z "$way" ]] || export $way; } && timeout 60 "$TMP/$name") > "$TMP/got" 2> "$TMP/err"
		else
			(cd "$TMP/run" && { [[ -z "$way" ]] || export $way; } && timeout 60 "$CLI" "$f") > "$TMP/got" 2> "$TMP/err"
		fi
		rc=$?
		ok=1
		cmp -s "$TMP/want" "$TMP/got" || ok=0
		if [[ -n "$want_panic" ]]; then
			[[ $rc -eq 101 ]] && grep -qF -- "$want_panic" "$TMP/err" || ok=0
		elif [[ $rc -ne 0 ]]; then
			ok=0
		fi
		if [[ $ok -eq 1 ]]; then
			pass=$((pass+1))
		else
			fail=$((fail+1)); echo "FAIL os/$name ($MODE${way:+, $way}, exit $rc)"
			diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  | /'
			head -5 "$TMP/err" | sed 's/^/  ! /'
		fi
	done
done
echo "os ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
