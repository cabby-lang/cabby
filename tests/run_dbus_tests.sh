#!/usr/bin/env bash
# The dbus library against a bus of its own: a private dbus-daemon is
# started for the tests (your own desktop's bus isn't touched), and each
# program in tests/dbus/ must print what its //> lines say.
#
# bus.cby needs only dbus-daemon. types.cby and signals.cby also need
# tests/dbus/service.py, a small service in Python (dbus-python and
# PyGObject: python-dbus and python-gobject on Arch, python3-dbus and
# python3-gi on Debian); without them they're skipped.
#
# Usage: run_dbus_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
if ! command -v dbus-daemon > /dev/null; then
	echo "dbus ($MODE): skipped (no dbus-daemon)"
	exit 0
fi
DIR=$ROOT/tests/dbus
TMP=$(mktemp -d)
SOCK=$(mktemp -d /tmp/cabby-bus.XXXXXX)   # (short: a Mac's socket paths can be 104 characters at most)
daemon=""
service=""
cleanup() {
	[[ -n "$service" ]] && kill "$service" 2> /dev/null
	[[ -n "$daemon" ]] && kill "$daemon" 2> /dev/null
	rm -rf "$TMP" "$SOCK"
}
trap cleanup EXIT
address=$(dbus-daemon --session --fork --print-address=1 --print-pid=3 --address="unix:path=$SOCK/socket" 3> "$TMP/pid" < /dev/null)
daemon=$(cat "$TMP/pid" 2> /dev/null)
if [[ -z "$address" || -z "$daemon" ]]; then
	echo "dbus ($MODE): skipped (dbus-daemon wouldn't start)"
	exit 0
fi
export DBUS_SESSION_BUS_ADDRESS=$address

# a Python that has dbus-python and PyGObject, for the service
python=""
for p in python3 python3.13 python3.12 python3.11; do
	if command -v "$p" > /dev/null && "$p" -c 'import dbus, dbus.service, dbus.mainloop.glib; from gi.repository import GLib' 2> /dev/null; then
		python=$p
		break
	fi
done
if [[ -n "$python" ]]; then
	"$python" "$DIR/service.py" > "$TMP/service.log" 2>&1 &
	service=$!
	for _ in $(seq 50); do
		grep -q ready "$TMP/service.log" 2> /dev/null && break
		sleep 0.1
	done
	grep -q ready "$TMP/service.log" || { python=""; kill "$service" 2> /dev/null; service=""; }
fi

pass=0; fail=0; skipped=0
for f in "$DIR"/*.cby; do
	name=$(basename "$f" .cby)
	if grep -q 'service.py' "$f" && [[ -z "$python" ]]; then
		skipped=$((skipped+1))
		continue
	fi
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$f" -o "$TMP/$name" > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL dbus/$name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; continue
		fi
		timeout 20 "$TMP/$name" > "$TMP/got" 2> "$TMP/err"
	else
		timeout 20 "$CLI" "$f" > "$TMP/got" 2> "$TMP/err"
	fi
	if diff -q "$TMP/want" "$TMP/got" > /dev/null; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		echo "FAIL dbus/$name ($MODE)"
		diff "$TMP/want" "$TMP/got" | head -10 | sed 's/^/  | /'
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
done
note=""
[[ $skipped -gt 0 ]] && note=" ($skipped skipped: no dbus-python and PyGObject for the test service)"
echo "dbus ($MODE): $pass passed, $fail failed$note"
[[ $fail -eq 0 ]]
