#!/usr/bin/env bash
# Text in other scripts, in a gui window: each text is laid out (its
# direction, shaped, drawn a glyph at a time), and must show its
# characters in the order tests/text/order.txt has them, left to right.
# The order is Unicode's bidirectional rules', so it's the same whatever
# fonts the computer has; it's checked with HarfBuzz (where there's one)
# and without (CABBY_HARFBUZZ=none), interpreted and compiled. And an
# emoji is drawn in color, from a color font's pictures. Skipped without
# the gui library.
#
# Usage: run_text_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
if [[ ! -f "$LIBDIR/libcabbygui.so" && ! -f "$LIBDIR/libcabbygui.dylib" ]]; then
	echo "text ($MODE): skipped (no gui library)"
	exit 0
fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
prog=("$CLI" "$ROOT/tests/text/scripts.cby")
if [[ "$MODE" == compile ]]; then
	if ! "$CLI" -c "$ROOT/tests/text/scripts.cby" -o "$TMP/scripts" > "$TMP/err" 2>&1; then
		echo "FAIL text (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'
		echo "text ($MODE): 0 passed, 1 failed"
		exit 1
	fi
	prog=("$TMP/scripts")
fi
pass=0; fail=0
for shaping in harfbuzz none; do
	rm -f "$TMP/order"
	# (exported in a subshell, not with env: on a Mac, timeout is a shell
	# function of the tests' own, which env can't run)
	(export CABBY_TEXT_ORDER="$TMP/order" CABBY_HARFBUZZ=$([[ $shaping == none ]] && echo none) CABBY_EMOJI_FONT=none \
		CABBY_GUI_SCRIPT="wait 50; close"; timeout 60 "${prog[@]}") > "$TMP/out" 2> "$TMP/err"
	LC_ALL=C sort -u "$TMP/order" > "$TMP/got" 2> /dev/null   # (by bytes: the same order everywhere)
	if cmp -s "$ROOT/tests/text/order.txt" "$TMP/got"; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		echo "FAIL text ($MODE, shaped by $shaping)"
		diff "$ROOT/tests/text/order.txt" "$TMP/got" | head -10 | sed 's/^/  | /'
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
done
# an emoji in color: tests/text/emoji-test.ttf (two of Noto Color Emoji's,
# its pictures kept in a CBDT table, as Linux's emoji fonts keep them) and
# emoji-test-sbix.ttf (the same, kept as Apple Color Emoji keeps them)
# draw a heart's middle red, not the text's black
eprog=("$CLI" "$ROOT/tests/text/emoji.cby")
if [[ "$MODE" == compile ]]; then
	"$CLI" -c "$ROOT/tests/text/emoji.cby" -o "$TMP/emoji" > "$TMP/err" 2>&1 && eprog=("$TMP/emoji")
fi
for font in emoji-test.ttf emoji-test-sbix.ttf; do
for shaping in harfbuzz none; do
	got=$( (export CABBY_HARFBUZZ=$([[ $shaping == none ]] && echo none) CABBY_EMOJI_FONT="$ROOT/tests/text/$font" \
		CABBY_GUI_SCRIPT="wait 50; pixel 52 50; close"; timeout 60 "${eprog[@]}") 2> "$TMP/err" | sed -n 's/^pixel 52 50: #//p')
	if [[ ${#got} -ge 6 ]] && (( 16#${got:0:2} >= 0xb0 && 16#${got:2:2} < 0x60 && 16#${got:4:2} < 0x60 )); then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		echo "FAIL text: the emoji isn't red ($font, $MODE, shaped by $shaping): #$got"
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
done
done
echo "text ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
