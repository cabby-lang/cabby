#!/usr/bin/env bash
# Screen readers, on Linux: a gui window on an accessibility bus of the
# test's own (a private dbus-daemon: your desktop's isn't touched), read
# by tests/accessibility/reader.cby, a stand-in for the AT-SPI registry
# and a screen reader. It's joined by tests/accessibility/app.cby, then
# reads its tree of objects, hears what it says as it's typed into and
# its focus moves, and presses its buttons. Each program's output must
# match the //> lines in this file's folder: reader.out and app.out.
#
# Skipped on a Mac (VoiceOver reads a program another way), and without
# dbus-daemon or the gui library.
#
# Usage: run_accessibility_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
if [[ "$(uname)" == Darwin ]]; then
	echo "accessibility ($MODE): skipped (a Mac's are VoiceOver's)"
	exit 0
fi
if [[ ! -f "$LIBDIR/libcabbygui.so" ]]; then
	echo "accessibility ($MODE): skipped (no gui library)"
	exit 0
fi
if ! command -v dbus-daemon > /dev/null; then
	echo "accessibility ($MODE): skipped (no dbus-daemon)"
	exit 0
fi
DIR=$ROOT/tests/accessibility
TMP=$(mktemp -d)
SOCK=$(mktemp -d /tmp/cabby-a11y.XXXXXX)
daemon=""
reader=""
cleanup() {
	[[ -n "$reader" ]] && kill "$reader" 2> /dev/null
	[[ -n "$daemon" ]] && kill "$daemon" 2> /dev/null
	rm -rf "$TMP" "$SOCK"
}
trap cleanup EXIT
address=$(dbus-daemon --session --fork --print-address=1 --print-pid=3 --address="unix:path=$SOCK/socket" 3> "$TMP/pid" < /dev/null)
daemon=$(cat "$TMP/pid" 2> /dev/null)
if [[ -z "$address" || -z "$daemon" ]]; then
	echo "accessibility ($MODE): skipped (dbus-daemon wouldn't start)"
	exit 0
fi

reader_prog=("$CLI" "$DIR/reader.cby")
app_prog=("$CLI" "$DIR/app.cby")
if [[ "$MODE" == compile ]]; then
	for p in reader app; do
		if ! "$CLI" -c "$DIR/$p.cby" -o "$TMP/$p" > "$TMP/err" 2>&1; then
			echo "FAIL accessibility/$p (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'
			echo "accessibility ($MODE): 0 passed, 1 failed"
			exit 1
		fi
	done
	reader_prog=("$TMP/reader")
	app_prog=("$TMP/app")
fi

# the reader first, as the registry the window joins
(export DBUS_SESSION_BUS_ADDRESS=$address; exec "${reader_prog[@]}") > "$TMP/reader.got" 2> "$TMP/reader.err" &
reader=$!
for _ in $(seq 100); do
	grep -q "reader: waiting" "$TMP/reader.got" 2> /dev/null && break
	sleep 0.1
done
# then the window: typed into, Tab to the check box, and Space to check it
# (the reader then chooses an option and presses a button itself)
(export CABBY_ACCESSIBILITY=1 AT_SPI_BUS_ADDRESS=$address DBUS_SESSION_BUS_ADDRESS=$address \
	CABBY_GUI_SCRIPT="pause 1000; text Livia; pause 300; press-key Tab; pause 300; press-key space; pause 2000; close"
	timeout 60 "${app_prog[@]}") > "$TMP/app.got" 2> "$TMP/app.err"
for _ in $(seq 100); do
	kill -0 "$reader" 2> /dev/null || break
	sleep 0.1
done
kill "$reader" 2> /dev/null
reader=""

pass=0; fail=0
for p in reader app; do
	sed -n 's/^\/\/> \{0,1\}//p' "$DIR/$p.out" > "$TMP/$p.want"
	if cmp -s "$TMP/$p.want" "$TMP/$p.got"; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		echo "FAIL accessibility/$p ($MODE)"
		diff "$TMP/$p.want" "$TMP/$p.got" | head -20 | sed 's/^/  | /'
		head -5 "$TMP/$p.err" | sed 's/^/  ! /'
	fi
done
echo "accessibility ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
