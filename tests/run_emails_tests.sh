#!/usr/bin/env bash
# The emails library: sending to a stand-in for an email server
# (tests/emails/smtp.cby, written in Cabby with networks.server), which
# writes what it's given to a file, and reading a stand-in's mailboxes
# (tests/emails/imap.cby: IMAP). tests/emails/send.cby and read.cby must
# print what their //> lines say.
#
# Usage: run_emails_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
DIR=$ROOT/tests/emails
TMP=$(mktemp -d)
server=""
cleanup() {
	[[ -n "$server" ]] && { kill "$server" 2> /dev/null; wait "$server" 2> /dev/null; }
	rm -rf "$TMP"
}
trap cleanup EXIT
pass=0; fail=0
# (the server's program, and the test's, run or compiled)
pair() {
	local srv=$1 test=$2
	local s=("$CLI" "$DIR/$srv.cby") t=("$CLI" "$DIR/$test.cby")
	if [[ "$MODE" == compile ]]; then
		for p in "$srv" "$test"; do
			if ! "$CLI" -c "$DIR/$p.cby" -o "$TMP/$p" > "$TMP/err" 2>&1; then
				fail=$((fail+1)); echo "FAIL emails/$p (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'
				return
			fi
		done
		s=("$TMP/$srv")
		t=("$TMP/$test")
	fi
	rm -rf "$TMP/run"; mkdir "$TMP/run"
	(cd "$TMP/run" && exec "${s[@]}") > "$TMP/port-$srv" 2> "$TMP/$srv.err" &
	server=$!
	for _ in $(seq 100); do
		grep -qE '^[0-9]+$' "$TMP/port-$srv" 2> /dev/null && break
		sleep 0.1
	done
	sed -n 's/^\/\/> \{0,1\}//p' "$DIR/$test.cby" > "$TMP/want"
	(cd "$TMP/run" && export CABBY_TEST_PEER="$(head -1 "$TMP/port-$srv")" CABBY_EMAIL_DATE="Mon, 16 Nov 2026 10:00:00 +1100"
		timeout 60 "${t[@]}") > "$TMP/got" 2> "$TMP/err"
	if cmp -s "$TMP/want" "$TMP/got"; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL emails/$test ($MODE)"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  | /'
		head -5 "$TMP/err" "$TMP/$srv.err" | sed 's/^/  ! /'
	fi
	kill "$server" 2> /dev/null
	wait "$server" 2> /dev/null
	server=""
}
pair smtp send
pair imap read
echo "emails ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
