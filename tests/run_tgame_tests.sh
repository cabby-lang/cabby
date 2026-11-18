#!/usr/bin/env bash
# The tgame library (`use tgame`: a game shown in a terminal) against the
# game tutorials: each tutorial that uses game is run again with tgame
# instead, following the same script, and must print the same. (Under a
# script, tgame draws off screen as game does; the terminal itself is
# checked by tests/tgame_terminal.py.)
#
# Usage: run_tgame_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
if [[ ! -f "$LIBDIR/libcabbytgame.so" ]]; then
	echo "tgame ($MODE): skipped (no SDL 3)"
	exit 0
fi
DIR=$ROOT/tutorials
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
for f in "$DIR"/*.cby; do
	rm -rf "${CABBY_SAVE_FOLDERS:?}"/*   # (each tutorial starts with no saved games)
	grep -q '^use game$' "$f" || continue
	script=$(sed -n 's/^\/\/ script: //p' "$f" | head -1)
	[[ -n "$script" ]] || continue
	name=$(basename "$f" .cby)
	src="$TMP/$name.cby"
	sed 's/^use game$/use tgame/; s/\([^a-z_]\)game\./\1tgame./g; s/^game\./tgame./' "$f" > "$src"
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$src" -o "$TMP/$name" > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL $name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; continue
		fi
		(cd "$DIR" && CABBY_GUI_SCRIPT="$script" timeout 60 "$TMP/$name") > "$TMP/got" 2> "$TMP/err"
	else
		(cd "$DIR" && CABBY_GUI_SCRIPT="$script" timeout 60 "$CLI" "$src") > "$TMP/got" 2> "$TMP/err"
	fi
	if cmp -s "$TMP/want" "$TMP/got" && [[ ! -s "$TMP/err" ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL $name"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  /'
		head -5 "$TMP/err" | sed 's/^/  | /'
	fi
done
echo "tgame ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
