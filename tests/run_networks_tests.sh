#!/usr/bin/env bash
# The networks library against servers of the tests' own, on this
# computer: requests (POST and the rest, progress) against
# tests/networks/server.py, a small web server in Python (skipped without
# Python 3); and programs talking to each other, Cabby to Cabby. Each
# program in tests/networks/ must print what its //> lines say, and panic
# as its // panic: line says. A test with a "// peer: peer_X.cby" line
# talks to that program, started first (run or compiled, as the test is):
# the first line it prints is its port, given to the test as
# CABBY_TEST_PEER. The peer_ programs aren't tests of their own.
#
# Usage: run_networks_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
DIR=$ROOT/tests/networks
TMP=$(mktemp -d)
server=""
peer=""
cleanup() {
	# (waited for too, so the shell doesn't say "Terminated")
	[[ -n "$server" ]] && { kill "$server" 2> /dev/null; wait "$server" 2> /dev/null; }
	[[ -n "$peer" ]] && kill "$peer" 2> /dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT
cp -R "$DIR/public" "$TMP/"   # (the files peer_web.cby sends, beside where it runs)
python=""
for p in python3 python3.13 python3.12 python3.11; do
	if command -v "$p" > /dev/null && "$p" -c 'import http.server' 2> /dev/null; then
		python=$p
		break
	fi
done
if [[ -n "$python" ]]; then
	"$python" "$DIR/server.py" > "$TMP/port" 2> "$TMP/server.log" &
	server=$!
	for _ in $(seq 50); do
		[[ -s "$TMP/port" ]] && break
		sleep 0.1
	done
	export CABBY_TEST_SERVER="http://127.0.0.1:$(head -1 "$TMP/port")"
fi
pass=0; fail=0; skipped=0
for f in "$DIR"/*.cby; do
	name=$(basename "$f" .cby)
	[[ "$name" == peer_* ]] && continue
	if grep -q 'CABBY_TEST_SERVER' "$f" && [[ -z "$python" ]]; then
		skipped=$((skipped+1))
		continue
	fi
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	want_panic=$(sed -n 's/^\/\/ panic: //p' "$f" | head -1)
	peer_file=$(sed -n 's/^\/\/ peer: //p' "$f" | head -1)
	peer=""
	if [[ -n "$peer_file" ]]; then
		peer_name=$(basename "$peer_file" .cby)
		if [[ "$MODE" == compile ]]; then
			if ! "$CLI" -c "$DIR/$peer_file" -o "$TMP/$peer_name" > "$TMP/err" 2>&1; then
				fail=$((fail+1)); echo "FAIL networks/$peer_name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; continue
			fi
			(cd "$TMP" && exec "$TMP/$peer_name") > "$TMP/peer-port-$name" 2> "$TMP/peer-err" &
		else
			(cd "$TMP" && exec "$CLI" "$DIR/$peer_file") > "$TMP/peer-port-$name" 2> "$TMP/peer-err" &
		fi
		peer=$!
		# (a file of its own, and a whole line, the port, before it's read)
		for _ in $(seq 100); do
			grep -qE '^[0-9]+( [0-9]+)*$' "$TMP/peer-port-$name" 2> /dev/null && break
			sleep 0.1
		done
		if ! grep -qE '^[0-9]+( [0-9]+)*$' "$TMP/peer-port-$name" 2> /dev/null; then
			fail=$((fail+1)); echo "FAIL networks/$name ($MODE): $peer_file didn't start"; head -5 "$TMP/peer-err" | sed 's/^/  ! /'
			kill "$peer" 2> /dev/null; peer=""; continue
		fi
		export CABBY_TEST_PEER="$(head -1 "$TMP/peer-port-$name")"
	fi
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$f" -o "$TMP/$name" > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL networks/$name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; continue
		fi
		(cd "$TMP" && timeout 60 "$TMP/$name") > "$TMP/got" 2> "$TMP/err"
	else
		(cd "$TMP" && timeout 60 "$CLI" "$f") > "$TMP/got" 2> "$TMP/err"
	fi
	rc=$?
	if [[ -n "$peer" ]]; then
		# (it ends when the test's done with it; stopped if it hasn't: it's
		# run without timeout, which on a Mac is a function of the tests'
		# own, that exec can't run)
		for _ in $(seq 50); do
			kill -0 "$peer" 2> /dev/null || break
			sleep 0.1
		done
		kill "$peer" 2> /dev/null
		wait "$peer" 2> /dev/null
		peer=""
	fi
	ok=1
	diff -q "$TMP/want" "$TMP/got" > /dev/null || ok=0
	if [[ -n "$want_panic" ]]; then
		[[ $rc -eq 101 ]] && grep -qF -- "$want_panic" "$TMP/err" || ok=0
	elif [[ $rc -ne 0 ]]; then
		ok=0
	fi
	if [[ $ok -eq 1 ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		echo "FAIL networks/$name ($MODE, exit $rc)"
		diff "$TMP/want" "$TMP/got" | head -10 | sed 's/^/  | /'
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
done
note=""
[[ $skipped -gt 0 ]] && note=" ($skipped skipped: no Python 3 for the test web server)"
echo "networks ($MODE): $pass passed, $fail failed$note"
[[ $fail -eq 0 ]]
