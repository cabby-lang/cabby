#!/usr/bin/env bash
# Every tutorial must print exactly the output listed at its end (`//> `
# lines), both interpreted and compiled. One with `// output varies` (what
# the computer says about itself) must run cleanly; its output is an
# example.
#   usage: run_tutorial_tests.sh path/to/cabby [run|compile]
set -u
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
needs "$CLI"
MODE=${2:-run}
DIR=$ROOT/tutorials
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0 skipped=0
# GUI tutorials (a `// script:` line) run in a virtual display, following the script
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
have_gui=0
if [[ -f "$LIBDIR/libcabbygui.so" ]]; then have_gui=1; fi
for f in "$DIR"/*.cby; do
	rm -rf "${CABBY_SAVE_FOLDERS:?}"/*   # (each tutorial starts with no saved games)
	name=$(basename "$f" .cby)
	# (videos need the videos library, built only where FFmpeg was found)
	if grep -q '^use videos' "$f" && [[ ! -f "$LIBDIR/libcabbyvideos.so" ]]; then skipped=$((skipped+1)); continue; fi
	script=$(sed -n 's/^\/\/ script: //p' "$f" | head -1)
	tscript=$(sed -n 's/^\/\/ tui-script: //p' "$f" | head -1)
	runner=()
	rundir="$TMP"
	if [[ -n "$tscript" ]]; then
		runner=(env CABBY_TUI_SCRIPT="$tscript")
	fi
	if [[ -n "$script" ]]; then
		rundir="$DIR"   # window tutorials may open files next to them (pictures/)
		if grep -q '^use game' "$f"; then
			# a game: drawn off screen by SDL, with no display needed
			if [[ ! -f "$LIBDIR/libcabbygame.so" ]]; then skipped=$((skipped+1)); continue; fi
		elif [[ $have_gui -eq 0 ]]; then skipped=$((skipped+1)); continue; fi
		# windows (gui and game) are drawn off screen by SDL under a script:
		# no display is needed, and the look is always the light one
		runner=(env CABBY_GUI_SCRIPT="$script")
	fi
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	want_exit=$(sed -n 's/^\/\/ exit: //p' "$f" | head -1)
	read -r -a args <<< "$(sed -n 's/^\/\/ args: //p' "$f" | head -1)"
	if [[ "$MODE" == "compile" ]]; then
		if ! (cd "$DIR" && "$CLI" -c "$f" -o "$TMP/$name") > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL $name (compile)"; head -20 "$TMP/err" | sed 's/^/  | /'; continue
		fi
		(cd "$rundir" && timeout 60 ${runner[@]+"${runner[@]}"} "$TMP/$name" ${args[@]+"${args[@]}"}) > "$TMP/got" 2> "$TMP/err.raw"; rc=$?
	else
		(cd "$rundir" && timeout 60 ${runner[@]+"${runner[@]}"} "$CLI" "$f" ${args[@]+"${args[@]}"}) > "$TMP/got" 2> "$TMP/err.raw"; rc=$?
	fi
	# what a window tutorial saves next to itself (a `// leaves:` line) is
	# the test's to tidy away
	for left in $(sed -n 's/^\/\/ leaves: //p' "$f"); do rm -f "$rundir/$left"; done
	# the virtual display's own chatter is not the program's
	grep -vE 'libEGL|DRI3|session bus|dbus|Unrecognized value.*gl-disable|GDK_DEBUG=help|^$' "$TMP/err.raw" > "$TMP/err"
	want_panic=$(sed -n 's/^\/\/ panic: //p' "$f" | head -1)
	[[ -n "$want_panic" ]] && want_exit=101
	quiet=1   # no warnings either: a tutorial should be spotless
	if [[ -z "$want_panic" && -s "$TMP/err" ]]; then quiet=0; fi
	if [[ -n "$want_panic" ]] && ! grep -qF -- "$want_panic" "$TMP/err"; then quiet=0; fi
	if [[ "$MODE" != "compile" && -z "$want_panic" ]] && ! "$CLI" --check "$f" > /dev/null 2>&1; then quiet=0; fi
	# and every tutorial follows STYLE.md
	if [[ "$MODE" != "compile" ]] && ! (cd "$DIR" && "$CLI" --style "$f") > "$TMP/style" 2>&1; then quiet=0; cat "$TMP/style" >> "$TMP/err"; fi
	# what the computer says about itself (`// output varies`) is shown as
	# an example: it has to run, cleanly, not print the same
	if grep -q '^// output varies' "$f"; then cp "$TMP/want" "$TMP/got"; fi
	if cmp -s "$TMP/want" "$TMP/got" && [[ $rc -eq ${want_exit:-0} && $quiet -eq 1 ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $name (exit $rc)"
		diff "$TMP/want" "$TMP/got" | head -30 | sed 's/^/  /'
		head -20 "$TMP/err" | sed 's/^/  err| /'
	fi
done
if [[ $skipped -gt 0 ]]; then
	echo "tutorials ($MODE): $pass passed, $fail failed ($skipped gui tutorials skipped: no virtual display or GTK)"
else
	echo "tutorials ($MODE): $pass passed, $fail failed"
fi
[[ $fail -eq 0 ]]
