#!/usr/bin/env bash
# The bluetooth library against a stand-in for BlueZ: a private
# dbus-daemon is started as the system bus (your computer's isn't
# touched), tests/bluetooth/bluez.cby answers on it as BlueZ would, and
# tests/bluetooth/client.cby must print what its //> lines say, and show
# the keyboard's pairing code.
#
# On a Mac, CABBY_BLUETOOTH=bluez has the library use the stand-in (BlueZ,
# over D-Bus, from Homebrew's dbus) rather than the Mac's own Bluetooth.
# With CABBY_BLUETOOTH_REAL=1, tests/bluetooth/real.cby looks at the
# computer's own Bluetooth too (it changes nothing), on Linux or a Mac.
#
# Usage: run_bluetooth_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
export CABBY_BLUETOOTH=bluez
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
if ! command -v dbus-daemon > /dev/null; then
	echo "bluetooth ($MODE): skipped (no dbus-daemon)"
	exit 0
fi
DIR=$ROOT/tests/bluetooth
TMP=$(mktemp -d)
SOCK=$(mktemp -d /tmp/cabby-bt.XXXXXX)   # (short: a Mac's socket paths can be 104 characters at most)
daemon=""
bluez=""
cleanup() {
	[[ -n "$bluez" ]] && kill "$bluez" 2> /dev/null
	[[ -n "$daemon" ]] && kill "$daemon" 2> /dev/null
	rm -rf "$TMP" "$SOCK"
}
trap cleanup EXIT
address=$(dbus-daemon --session --fork --print-address=1 --print-pid=3 --address="unix:path=$SOCK/socket" 3> "$TMP/pid" < /dev/null)
daemon=$(cat "$TMP/pid" 2> /dev/null)
if [[ -z "$address" || -z "$daemon" ]]; then
	echo "bluetooth ($MODE): skipped (dbus-daemon wouldn't start)"
	exit 0
fi

pass=0; fail=0
progs=("$CLI" "$DIR/bluez.cby")
client=("$CLI" "$DIR/client.cby")
if [[ "$MODE" == compile ]]; then
	for p in bluez client; do
		if ! "$CLI" -c "$DIR/$p.cby" -o "$TMP/$p" > "$TMP/err" 2>&1; then
			echo "FAIL bluetooth/$p (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'
			echo "bluetooth ($MODE): 0 passed, 1 failed"
			exit 1
		fi
	done
	progs=("$TMP/bluez")
	client=("$TMP/client")
fi

(export DBUS_SYSTEM_BUS_ADDRESS=$address; exec "${progs[@]}") > "$TMP/bluez.out" 2>&1 &
bluez=$!
for _ in $(seq 100); do
	grep -q ready "$TMP/bluez.out" 2> /dev/null && break
	sleep 0.1
done

sed -n 's/^\/\/> \{0,1\}//p' "$DIR/client.cby" > "$TMP/want"
(export DBUS_SYSTEM_BUS_ADDRESS=$address; timeout 60 "${client[@]}") > "$TMP/got" 2> "$TMP/err"
if cmp -s "$TMP/want" "$TMP/got" && grep -q "type 482913 on it" "$TMP/err" && grep -q "an agent, wanting KeyboardDisplay" "$TMP/bluez.out"; then
	pass=$((pass+1))
else
	fail=$((fail+1)); echo "FAIL bluetooth/client ($MODE)"
	diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  | /'
	head -5 "$TMP/err" | sed 's/^/  ! /'
	head -5 "$TMP/bluez.out" | sed 's/^/  bluez: /'
fi

# without BlueZ at all: nothing there, and no panic
(export DBUS_SYSTEM_BUS_ADDRESS="unix:path=$SOCK/nothing"; timeout 20 "$CLI" "$DIR/none.cby") > "$TMP/got" 2> "$TMP/err"
sed -n 's/^\/\/> \{0,1\}//p' "$DIR/none.cby" > "$TMP/want"
if cmp -s "$TMP/want" "$TMP/got"; then
	pass=$((pass+1))
else
	fail=$((fail+1)); echo "FAIL bluetooth/none ($MODE)"
	diff "$TMP/want" "$TMP/got" | head -10 | sed 's/^/  | /'
	head -5 "$TMP/err" | sed 's/^/  ! /'
fi

# the computer's own, when asked for
if [[ "${CABBY_BLUETOOTH_REAL:-}" == 1 ]]; then
	(unset CABBY_BLUETOOTH DBUS_SYSTEM_BUS_ADDRESS; timeout 60 "$CLI" "$DIR/real.cby") > "$TMP/got" 2> "$TMP/err"
	if [[ $? -eq 0 ]] && grep -q "^done" "$TMP/got"; then
		pass=$((pass+1))
		sed 's/^/  real: /' "$TMP/got"
	else
		fail=$((fail+1)); echo "FAIL bluetooth/real ($MODE)"
		sed 's/^/  | /' "$TMP/got" | head -20
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
fi

echo "bluetooth ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
