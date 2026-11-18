#!/usr/bin/env bash
# The videos library. tests/videos/api.cby prints what's in the test
# videos (made by tools/make-test-video.py: lossless, so what's decoded is
# the same on every computer), pictures from them and their sound saved,
# against api.out. tests/videos/window.cby plays one in a gui window,
# following window.script (the script's clock: clicks, keys, the colors
# shown at each moment, on_end and looping), against window.out; and
# missing.cby must panic. Skipped without the videos library (FFmpeg's
# libraries weren't found when Cabby was built); the window, without gui.
#
# Usage: run_videos_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
if [[ ! -f "$LIBDIR/libcabbyvideos.so" && ! -f "$LIBDIR/libcabbyvideos.dylib" ]]; then
	echo "videos ($MODE): skipped (no videos library: FFmpeg's libraries weren't found)"
	exit 0
fi
DIR=$ROOT/tests/videos
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0

# the program, interpreted or compiled
prog_for() {
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$DIR/$1.cby" -o "$TMP/$1" > "$TMP/err" 2>&1; then
			echo "FAIL videos/$1 (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'
			return 1
		fi
		prog=("$TMP/$1")
	else
		prog=("$CLI" "$DIR/$1.cby")
	fi
}

# (name, what it printed, what it should have)
same() {
	if cmp -s "$2" "$3"; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL videos/$1 ($MODE)"; diff "$3" "$2" | head -20 | sed 's/^/  | /'
	fi
}

if prog_for api; then
	(cd "$DIR" && "${prog[@]}") > "$TMP/api.got" 2>&1
	same api "$TMP/api.got" "$DIR/api.out"
	[[ ! -e "$DIR/sound.wav" ]] || { fail=$((fail+1)); echo "FAIL videos/api left sound.wav"; rm -f "$DIR/sound.wav"; }
else
	fail=$((fail+1))
fi

if prog_for missing; then
	(cd "$DIR" && "${prog[@]}") > "$TMP/missing.got" 2>&1
	if grep -q "can't open the video 'nowhere.mp4'" "$TMP/missing.got"; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL videos/missing ($MODE)"; sed 's/^/  | /' "$TMP/missing.got"
	fi
else
	fail=$((fail+1))
fi

if [[ -f "$LIBDIR/libcabbygui.so" || -f "$LIBDIR/libcabbygui.dylib" ]]; then
	if prog_for window; then
		(cd "$DIR" && CABBY_GUI_SCRIPT="$(cat "$DIR/window.script")" timeout 60 "${prog[@]}") > "$TMP/window.got" 2>&1
		same window "$TMP/window.got" "$DIR/window.out"
	else
		fail=$((fail+1))
	fi
else
	echo "videos ($MODE): the window skipped (no gui library)"
fi

echo "videos ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
