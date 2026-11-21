#!/usr/bin/env bash
# The world library (3D). tests/world/api.cby prints its numbers (moving,
# turning, touching, the camera, the mouse, keys, timers), checked against
# its //> lines. tests/world/scene.cby is looked at: the colors drawn at
# the places in scene.script must be scene.pixels's (and shadows.cby's,
# shadows.script's and shadows.pixels's), give or take 3 (a
# little either way where a computer's sin and cos differ in the last
# digit). Under a script, Cabby draws the world itself, the same on every
# computer; on Linux, it's drawn with OpenGL too (when the computer has
# it, or Mesa's llvmpipe), which must agree, give or take 8. Skipped
# without the world library.
#
# Usage: run_world_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
LIBDIR=${CABBY_LIB:-$(dirname "$CLI")/lib}
if [[ ! -f "$LIBDIR/libcabbyworld.so" && ! -f "$LIBDIR/libcabbyworld.dylib" ]]; then
	echo "world ($MODE): skipped (no world library)"
	exit 0
fi
DIR=$ROOT/tests/world
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0

# the program, interpreted or compiled
prog_for() {
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$DIR/$1.cby" -o "$TMP/$1" > "$TMP/err" 2>&1; then
			echo "FAIL world/$1 (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'
			return 1
		fi
		prog=("$TMP/$1")
	else
		prog=("$CLI" "$DIR/$1.cby")
	fi
}

# api: its output
if prog_for api; then
	script=$(sed -n 's/^\/\/ script: //p' "$DIR/api.cby")
	sed -n 's/^\/\/> \{0,1\}//p' "$DIR/api.cby" > "$TMP/want"
	(cd "$TMP" && export CABBY_GUI_SCRIPT="$script"; timeout 60 "${prog[@]}") > "$TMP/got" 2> "$TMP/err"
	if cmp -s "$TMP/want" "$TMP/got"; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL world/api ($MODE)"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  | /'
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
else
	fail=$((fail+1))
fi

# scene: its colors, drawn by Cabby, and (on Linux) by OpenGL
close_enough() {   # want got tolerance
	awk -v tol="$3" '
		FNR == NR { want[FNR] = $0; n = FNR; next }
		{ got[FNR] = $0; m = FNR }
		function hex(s, i) { return index("0123456789abcdef", substr(s, i, 1)) - 1 }
		function chan(s, k,   p) { p = index(s, "#") + 1 + 2 * k; return hex(s, p) * 16 + hex(s, p + 1) }
		END {
			if (n != m) { print "  | " n " places wanted, " m " drawn"; exit 1 }
			bad = 0
			for (i = 1; i <= n; i++) {
				split(want[i], a, ":"); split(got[i], b, ":")
				if (a[1] != b[1]) { print "  | " want[i] " / " got[i]; bad = 1; continue }
				for (k = 0; k < 3; k++) {
					d = chan(want[i], k) - chan(got[i], k)
					if (d < 0) d = -d
					if (d > tol) { print "  | wanted " want[i] ", drawn" b[2]; bad = 1; break }
				}
			}
			exit bad
		}' "$1" "$2"
}

# scene and model: their colors, drawn by Cabby, and (on Linux) by OpenGL;
# model's other output is its //> lines, and it ends with its panic
kinds=(cabby)
[[ "$(uname)" != Darwin ]] && kinds+=(opengl)
cp "$DIR/robot.glb" "$TMP/"
for name in scene shadows model; do
	prog_for $name || { fail=$((fail+1)); continue; }
	if [[ $name == scene ]]; then
		script="frames 2; $(cat "$DIR/scene.script"); close"
	elif [[ $name == shadows ]]; then
		script="frames 1; $(cat "$DIR/shadows.script"); close"
	else
		script=$(sed -n 's/^\/\/ script: //p' "$DIR/model.cby")
	fi
	for kind in "${kinds[@]}"; do
		tol=3
		[[ $kind == opengl ]] && tol=8
		(cd "$TMP" && export CABBY_WORLD_DRAWING=$kind CABBY_GUI_SCRIPT="$script"; timeout 60 "${prog[@]}") \
			> "$TMP/out" 2> "$TMP/err"
		ok=1
		close_enough "$DIR/$name.pixels" <(grep '^pixel' "$TMP/out") $tol > "$TMP/why" || ok=0
		if [[ $name == scene ]]; then
			grep -q '^frames: 2$' "$TMP/out" || { ok=0; echo "  | no 'frames: 2'" >> "$TMP/why"; }
		elif [[ $name == shadows ]]; then
			[[ "$(grep '^shadows:' "$TMP/out" | tr '\n' ' ')" == "shadows: true shadows: false " ]] ||
				{ ok=0; echo "  | not 'shadows: true', then 'shadows: false'" >> "$TMP/why"; }
		else
			sed -n 's/^\/\/> \{0,1\}//p' "$DIR/model.cby" > "$TMP/want"
			grep -v '^pixel' "$TMP/out" > "$TMP/got"
			cmp -s "$TMP/want" "$TMP/got" || { ok=0; diff "$TMP/want" "$TMP/got" | head -10 | sed 's/^/  | /' >> "$TMP/why"; }
			panic=$(sed -n 's/^\/\/ panic: //p' "$DIR/model.cby")
			grep -qF -- "$panic" "$TMP/err" || { ok=0; echo "  | no panic: $panic" >> "$TMP/why"; }
		fi
		if [[ $ok -eq 1 ]]; then
			pass=$((pass+1))
		else
			fail=$((fail+1)); echo "FAIL world/$name, drawn by $kind ($MODE)"
			cat "$TMP/why"; head -5 "$TMP/err" | sed 's/^/  ! /'
		fi
	done
done

echo "world ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
