#!/usr/bin/env bash
# Kiln, the 3D modeller in apps/kiln: each of its tests (apps/kiln/tests/
# *.cby) must print exactly its //> lines, interpreted or compiled, run in
# apps/kiln; one with a `// script:` line is the editor, in a window,
# following that script (skipped without the world library). And all of
# Kiln follows STYLE.md.
#
# Usage: run_kiln_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
DIR=$ROOT/apps/kiln
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0 skipped=0
for f in "$DIR"/tests/*-test.cby; do   # (the rest are what they use: a script)
	name=$(basename "$f" .cby)
	script=$(sed -n 's/^\/\/ script: //p' "$f" | head -1)
	if [[ -n "$script" && ! -f "$LIBDIR/libcabbyworld.so" && ! -f "$LIBDIR/libcabbyworld.dylib" ]]; then
		skipped=$((skipped+1)); continue
	fi
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	runner=(env CABBY_GUI_SCRIPT="$script" CABBY="$CLI")
	if [[ "$MODE" == compile ]]; then
		if ! (cd "$DIR" && "$CLI" -c "$f" -o "$TMP/$name") > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL kiln/$name (compile)"; head -20 "$TMP/err" | sed 's/^/  | /'; continue
		fi
		(cd "$DIR" && timeout 120 "${runner[@]}" "$TMP/$name") > "$TMP/got" 2> "$TMP/err"; rc=$?
	else
		(cd "$DIR" && timeout 120 "${runner[@]}" "$CLI" "$f") > "$TMP/got" 2> "$TMP/err"; rc=$?
	fi
	grep -vE 'libEGL|DRI3|^$' "$TMP/err" > "$TMP/err2"
	if [[ $rc -eq 0 ]] && cmp -s "$TMP/want" "$TMP/got" && [[ ! -s "$TMP/err2" ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL kiln/$name (exit $rc)"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  | /'
		head -10 "$TMP/err2" | sed 's/^/  ! /'
	fi
done
if [[ "$MODE" != compile ]]; then
	for f in "$DIR"/kiln.cby "$DIR"/tests/*.cby; do   # (with what they use: model and the rest)
		[[ -f "$f" ]] || continue
		if ! (cd "$DIR" && "$CLI" --style "$f") > "$TMP/style" 2>&1; then
			fail=$((fail+1)); echo "FAIL kiln style: $(basename "$f")"; head -10 "$TMP/style" | sed 's/^/  | /'
		fi
	done
fi
echo "kiln ($MODE): $pass passed, $fail failed$([[ $skipped -gt 0 ]] && echo " ($skipped skipped: no world library)")"
[[ $fail -eq 0 ]]
