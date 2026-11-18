#!/usr/bin/env bash
# The interactive session, fed from a pipe (no prompts are printed then).
#   usage: run_repl_tests.sh path/to/cabby
# tests/repl/NAME.in is typed into `cabby`; its stdout and stderr together
# must equal NAME.out. A line `// exit: N` in the input sets the expected
# exit status (default 0).
set -u
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
needs "$CLI"
DIR=$ROOT/tests
LIB=${CABBY_LIB:-$(dirname "$CLI")/lib}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0

ok()  { pass=$((pass+1)); }
bad() { fail=$((fail+1)); echo "FAIL $1"; shift; for l in "$@"; do echo "  $l"; done; }

for in in "$DIR"/repl/*.in; do
	name=$(basename "$in" .in)
	want_exit=$(sed -n 's/^\/\/ exit: //p' "$in" | head -1)
	(cd "$TMP" && CABBY_LIB="$LIB" timeout 20 "$CLI" < "$in" > "$TMP/got" 2>&1); rc=$?
	if cmp -s "$DIR/repl/$name.out" "$TMP/got" && [[ $rc -eq ${want_exit:-0} ]]; then ok; else
		bad "$name (exit $rc, want ${want_exit:-0})"
		diff "$DIR/repl/$name.out" "$TMP/got" | head -20 | sed 's/^/  /'
	fi
done

# ---- the license: the same as the LICENSE file, from the command line and
# in the session, and named by --version
if "$CLI" --license | cmp -s - "$ROOT/LICENSE"; then ok; else bad "cabby --license isn't the LICENSE file (license.h)"; fi
if echo "cabby.license" | (cd "$TMP" && "$CLI") | cmp -s - "$ROOT/LICENSE"; then ok; else bad "cabby.license isn't the LICENSE file"; fi
if "$CLI" --version | grep -q "NO WARRANTY, under the MIT License"; then ok; else bad "cabby --version doesn't name the license"; fi

# ---- saving the session
session=$'int x = 6\nint sq int v {\n\treturn v * v\n}\nsq x\nint a[1]\na[x]\nexit'
(cd "$TMP" && printf '%s\ny\nmine\n' "$session" | CABBY_LIB="$LIB" "$CLI" > out 2>&1)
if [[ -f "$TMP/mine.cby" ]] && grep -q 'Session saved to mine.cby' "$TMP/out" &&
   grep -q '// not kept (it panicked):' "$TMP/mine.cby" &&
   [[ "$(cd "$TMP" && CABBY_LIB="$LIB" "$CLI" mine.cby 2>&1)" == "36" ]]; then ok; else
	bad save_and_rerun "$(cat "$TMP/out")" "$(cat "$TMP/mine.cby" 2>/dev/null)"
fi

# an existing file is not overwritten without asking; .cby is not added twice
(cd "$TMP" && printf 'println 1\nexit\ny\nmine.cby\nn\nother\n' | CABBY_LIB="$LIB" "$CLI" > out 2>&1)
if [[ -f "$TMP/other.cby" ]] && ! grep -q 'println 1' "$TMP/mine.cby" && grep -q 'println 1' "$TMP/other.cby"; then ok; else
	bad no_overwrite "$(cat "$TMP/out")"
fi

# answering no saves nothing; an empty session asks nothing
(cd "$TMP" && rm -f *.cby && printf 'println 1\nexit\nn\n' | CABBY_LIB="$LIB" "$CLI" > out 2>&1)
if [[ -z "$(ls "$TMP"/*.cby 2>/dev/null)" && "$(cat "$TMP/out")" == "1" ]]; then ok; else bad decline_save "$(cat "$TMP/out")"; fi
(cd "$TMP" && printf 'exit\nshould-not-be-read\n' | CABBY_LIB="$LIB" "$CLI" > out 2>&1)
if [[ ! -s "$TMP/out" && -z "$(ls "$TMP"/*.cby 2>/dev/null)" ]]; then ok; else bad empty_session "$(cat "$TMP/out")"; fi

# the session's own commands: cabby.preserve and cabby.restore (in a home of its own)
mkdir -p "$TMP/home"
(cd "$TMP" && printf 'int x = 5\nprintln "hidden on restore"\ncabby.preserve t\ncabby.exit\n' |
	HOME="$TMP/home" CABBY_LIB="$LIB" "$CLI" > out 2>&1)
(cd "$TMP" && printf 'cabby.restore t\nx * 2\nint exit = 1\nexit\ncabby.remove t\ncabby.sessions\ncabby.exit\n' |
	HOME="$TMP/home" CABBY_LIB="$LIB" "$CLI" > out2 2>&1)
if grep -q 'Session preserved (2 inputs)' "$TMP/out" &&
   [[ "$(sed 1d "$TMP/out2")" == $'10\n1\nRemoved the preserved session \'t\'\nNo preserved sessions yet (cabby.preserve keeps one)' ]] &&
   grep -q 'Session restored (2 inputs)' "$TMP/out2"; then ok; else
	bad preserve_restore "$(cat "$TMP/out")" "$(cat "$TMP/out2")"
fi

# typed at a real terminal: the line editor and its prompts
if command -v python3 > /dev/null; then
	if python3 "$DIR/repl_tty.py" "$CLI" > "$TMP/tty" 2>&1; then ok; else bad tty "$(cat "$TMP/tty")"; fi
fi

echo "session tests: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
