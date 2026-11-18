#!/usr/bin/env bash
# The gtk library (`use gtk`: the gui library drawn by GTK 4) against the
# window tutorials: each tutorial that uses gui is run again with gtk
# instead, following the same script, and must print the same. A tutorial
# that uses something gtk doesn't have is counted as "not yet", not as a
# failure.
#
# Usage: run_gtk_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
if [[ ! -f "$LIBDIR/libcabbygtk.so" ]] || ! command -v xvfb-run > /dev/null; then
	echo "gtk ($MODE): skipped (no GTK 4)"
	exit 0
fi
DIR=$ROOT/tutorials
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# GTK in a virtual display, drawn without OpenGL (GDK_DISABLE=gl since GTK
# 4.16, GDK_DEBUG before), and without your own GTK settings
mkdir -p "$TMP/config"
gtk_env=(xvfb-run -a env GSK_RENDERER=cairo GDK_DISABLE=gl GDK_DEBUG=gl-disable XDG_CONFIG_HOME="$TMP/config" GTK_A11Y=none NO_AT_BRIDGE=1)
pass=0; fail=0; notyet=0
for f in "$DIR"/*.cby; do
	rm -rf "${CABBY_SAVE_FOLDERS:?}"/*   # (each tutorial starts with no saved games)
	grep -q '^use gui$' "$f" || continue
	script=$(sed -n 's/^\/\/ script: //p' "$f" | head -1)
	[[ -n "$script" ]] || continue
	name=$(basename "$f" .cby)
	src="$TMP/$name.cby"
	sed 's/^use gui$/use gtk/; s/\([^a-z_]\)gui\./\1gtk./g; s/^gui\./gtk./' "$f" > "$src"
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$src" -o "$TMP/$name" > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL $name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; continue
		fi
		(cd "$DIR" && timeout 60 ${gtk_env[@]+"${gtk_env[@]}"} CABBY_GUI_SCRIPT="$script" "$TMP/$name") > "$TMP/got" 2> "$TMP/err.raw"
	else
		(cd "$DIR" && timeout 60 ${gtk_env[@]+"${gtk_env[@]}"} CABBY_GUI_SCRIPT="$script" "$CLI" "$src") > "$TMP/got" 2> "$TMP/err.raw"
	fi
	# (notes GTK and the virtual display print on some systems, not the program's:
	# an older GTK doesn't know GDK_DEBUG=gl-disable, and says so)
	grep -vE 'libEGL|DRI3|session bus|dbus|Unrecognized value.*gl-disable|GDK_DEBUG=help|^$' "$TMP/err.raw" > "$TMP/err"
	if grep -q "isn't in the gtk library\|aren't in the gtk library" "$TMP/err" "$TMP/got"; then
		notyet=$((notyet+1))
	elif cmp -s "$TMP/want" "$TMP/got" && [[ ! -s "$TMP/err" ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL $name"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  /'
		head -5 "$TMP/err" | sed 's/^/  | /'
	fi
done
echo "gtk ($MODE): $pass passed, $fail failed, $notyet not yet (they use what gtk doesn't have)"
[[ $fail -eq 0 ]]
