#!/bin/bash
# Checks the examples in tutorials/tutorials.md, the Cabby manual.
#
# A hidden comment before a ```cabby block says how to test it:
#
#   <!-- run -->           a whole program; the next ``` block is its output
#   <!-- run: ARGS -->     the same, run with these command-line arguments
#   <!-- both -->          a whole program whose output block shows stdout
#                          and stderr together, as on a terminal
#   <!-- input: TEXT -->   the same as run, with TEXT typed in (\n between lines);
#                          the output shows each typed line, as a terminal does
#   <!-- panic: TEXT -->   a whole program that panics with TEXT; the next
#                          ``` block is what it prints first
#   <!-- gui: SCRIPT -->   a window program, run on a virtual display
#                          following SCRIPT (CABBY_GUI_SCRIPT); the next
#                          ``` block is its output. Skipped without
#                          xvfb-run or the gui library.
#   <!-- tui: SCRIPT -->   a terminal-window program, following SCRIPT
#                          (CABBY_TUI_SCRIPT); the next ``` block is its
#                          output, `screen` commands included
#   <!-- main -->          statements: run inside `int main { }`; the next
#                          ``` block is the output
#   <!-- warning-main: TEXT -->  the same, but checking must warn, with TEXT
#                          in the warning (the output block is stdout alone)
#   <!-- check -->         a whole program that must pass `cabby --check`
#   <!-- error: TEXT -->   a whole program that must fail to check, with
#                          TEXT in the message
#   <!-- error-main: TEXT -->  the same, for statements inside main
#
# Blocks without a marker are fragments and are not run. Every link of the
# form (#anchor) must also lead to a heading in the manual.
#
# Usage: run_manual_tests.sh ./cabby [tutorials/tutorials.md]

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CABBY=$(from_here "${1:-}" cabby)
needs "$CABBY"
MANUAL=$(from_here "${2:-}" tutorials/tutorials.md)
MANUAL=${MANUAL#"$ROOT"/}   # (shown in messages as tutorials/tutorials.md)
if [[ ! -f "$MANUAL" ]]; then echo "$(basename "$0"): there's no $MANUAL to check" >&2; exit 1; fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Split the marked examples out into numbered files.
awk -v dir="$work" '
	function flush_block() { close(file) }
	/^<!-- (run|both|input|main|warning-main|check|error|error-main|panic|gui|tui)(: .*)? -->$/ {
		m = $0
		sub(/^<!-- /, "", m); sub(/ -->$/, "", m)
		kind = m; text = ""
		if (index(m, ": ")) { kind = substr(m, 1, index(m, ": ") - 1); text = substr(m, index(m, ": ") + 2) }
		pending = kind; pending_text = text; pending_line = NR
		next
	}
	/^```/ {
		if (inblock) {
			inblock = 0; flush_block()
			next
		}
		if (pending != "" && $0 == "```cabby") {
			n++
			file = dir "/" n ".cby"
			print pending > (dir "/" n ".kind"); close(dir "/" n ".kind")
			print pending_line > (dir "/" n ".line"); close(dir "/" n ".line")
			printf "%s", pending_text > (dir "/" n ".text"); close(dir "/" n ".text")
			wrap = (pending == "main" || pending == "warning-main" || pending == "error-main")
			if (wrap) print "int main {" > file
			inblock = 1; mode = wrap ? "wrap" : "plain"
			want_output = (pending == "run" || pending == "both" || pending == "input" || pending == "main" || pending == "warning-main" || pending == "panic" || pending == "gui" || pending == "tui") ? n : 0
			pending = ""
			next
		}
		if (want_output) {
			file = dir "/" want_output ".expect"
			printf "" > file
			inblock = 1; mode = "plain"; want_output = 0
			next
		}
		inblock = 1; mode = "skip"; file = "/dev/null"
		next
	}
	inblock {
		if (mode == "skip") next
		if (mode == "wrap") {
			if ($0 == "") print "" > file; else print "\t" $0 > file
		} else print > file
		next
	}
	END {
		for (i = 1; i <= n; i++) {
			k = ""; getline k < (dir "/" i ".kind")
			if (k == "main" || k == "warning-main" || k == "error-main") print "}" >> (dir "/" i ".cby")
		}
	}
' "$MANUAL"

pass=0
fail=0
skipped=0
LIBDIR=${CABBY_LIB:-$(dirname "$CABBY")/lib}
have_gui=0
if [ -f "$LIBDIR/libcabbygui.so" ]; then have_gui=1; fi
have_gtk=0
if command -v xvfb-run > /dev/null && [ -f "$LIBDIR/libcabbygtk.so" ]; then have_gtk=1; fi
report() {
	fail=$((fail + 1))
	echo "FAIL: example at $MANUAL:$(cat "$work/$1.line") ($2)"
	[ -n "$3" ] && echo "$3" | sed 's/^/    /'
}

# Examples may `use local` the tutorials' own folders (geometry/, reverse/).
for d in "$(dirname "$MANUAL")"/*/; do
	[ -d "$d" ] && ln -s "$(cd "$d" && pwd)" "$work/$(basename "$d")"
done

cd "$work"
for kindfile in $(ls *.kind 2>/dev/null | sort -n); do
	n=${kindfile%.kind}
	kind=$(cat "$n.kind")
	src="$n.cby"
	# Examples refer to the tutorial folders (use local geometry/...).
	case "$kind" in
	run|both|input|main|warning-main|panic|gui|tui)
		if [ ! -f "$n.expect" ]; then
			report "$n" "no output block after it"
			continue
		fi
		args=()
		runner=()
		[ "$kind" = run ] && read -ra args < "$n.text"
		if [ "$kind" = gui ] && grep -q '^use game' "$src"; then
			# a game: drawn off screen by SDL, with no display needed
			if [ ! -f "$LIBDIR/libcabbygame.so" ]; then skipped=$((skipped + 1)); continue; fi
			runner=(env CABBY_GUI_SCRIPT="$(cat "$n.text")")
		elif [ "$kind" = gui ] && grep -q '^use world' "$src"; then
			# 3D: drawn off screen by Cabby itself, with no display needed
			if [ ! -f "$LIBDIR/libcabbyworld.so" ] && [ ! -f "$LIBDIR/libcabbyworld.dylib" ]; then skipped=$((skipped + 1)); continue; fi
			runner=(env CABBY_GUI_SCRIPT="$(cat "$n.text")")
		elif [ "$kind" = gui ] && grep -q '^use gtk' "$src"; then
			if [ $have_gtk -eq 0 ]; then skipped=$((skipped + 1)); continue; fi
			# GTK: in a virtual display, drawn without OpenGL (GDK_DISABLE=gl
			# since GTK 4.16, GDK_DEBUG before), and without your own GTK
			# settings, so every setup gives the same
			mkdir -p "$work/config"
			runner=(xvfb-run -a env CABBY_GUI_SCRIPT="$(cat "$n.text")" GSK_RENDERER=cairo GDK_DISABLE=gl GDK_DEBUG=gl-disable
				XDG_CONFIG_HOME="$work/config" GTK_A11Y=none NO_AT_BRIDGE=1)
		elif [ "$kind" = gui ]; then
			# a window: drawn off screen by SDL under a script, with no display needed
			if [ $have_gui -eq 0 ]; then skipped=$((skipped + 1)); continue; fi
			runner=(env CABBY_GUI_SCRIPT="$(cat "$n.text")")
		fi
		if [ "$kind" = tui ]; then
			runner=(env CABBY_TUI_SCRIPT="$(cat "$n.text")")
		fi
		if [ "$kind" = both ]; then
			out=$(timeout 60 "$CABBY" "$src" 2>&1 </dev/null)
			: > "$n.err.raw"
		elif [ "$kind" = input ]; then
			out=$(printf '%b' "$(cat "$n.text")" | CABBY_ECHO_INPUT=1 timeout 60 "$CABBY" "$src" 2>"$n.err.raw")
		else
			out=$(timeout 60 ${runner[@]+"${runner[@]}"} "$CABBY" "$src" ${args[@]+"${args[@]}"} 2>"$n.err.raw" </dev/null)
		fi
		status=$?
		# a virtual display's own chatter is not the program's
		grep -vE 'libEGL|DRI3|session bus|dbus|Unrecognized value.*gl-disable|GDK_DEBUG=help|^$' "$n.err.raw" > "$n.err"
		if [ "$kind" = warning-main ]; then
			if ! grep -q "warning: .*$(cat "$n.text")" "$n.err"; then
				report "$n" "expected a warning with \"$(cat "$n.text")\"" "$(cat "$n.err")"
				continue
			fi
			grep -v "warning: \|^    " "$n.err" > "$n.err.rest"; mv "$n.err.rest" "$n.err"
		fi
		if [ "$kind" = panic ]; then
			if [ $status -ne 101 ] || ! grep -qF -- "$(cat "$n.text")" "$n.err"; then
				report "$n" "expected a panic with \"$(cat "$n.text")\", got status $status" "$(cat "$n.err")"
				continue
			fi
		elif [ $status -ne 0 ] || [ -s "$n.err" ]; then
			report "$n" "exit status $status" "$(cat "$n.err")"
			continue
		fi
		if [ "$out" != "$(cat "$n.expect")" ]; then
			report "$n" "wrong output" "$(diff <(echo "$out") "$n.expect")"
		elif ! style=$("$CABBY" --style "$src" 2>&1); then
			report "$n" "names not in the style of STYLE.md" "$(grep 'style:' <<<"$style")"
		else
			pass=$((pass + 1))
		fi
		;;
	check)
		if ! err=$("$CABBY" --style "$src" 2>&1) || [ -n "$err" ]; then
			report "$n" "does not check cleanly" "$err"
		else
			pass=$((pass + 1))
		fi
		;;
	error|error-main)
		want=$(cat "$n.text")
		if err=$("$CABBY" --check "$src" 2>&1); then
			report "$n" "checked without the expected error"
		elif ! grep -qF -- "$want" <<<"$err"; then
			report "$n" "message lacks \"$want\"" "$err"
		else
			pass=$((pass + 1))
		fi
		;;
	esac
done
cd "$OLDPWD"

# Internal links: GitHub's heading anchors (lower case; spaces become
# hyphens; punctuation other than - and _ dropped; repeats get -1, -2 ...).
anchors=$(grep -E '^#{1,6} ' "$MANUAL" | sed -E 's/^#+ //' | awk '{
	s = tolower($0)
	gsub(/[^a-z0-9 _-]/, "", s)
	gsub(/ /, "-", s)
	if (seen[s]++) s = s "-" (seen[s] - 1)
	print s
}')
links=0
for link in $(grep -oE '\]\(#[^)]+\)' "$MANUAL" | sed -E 's/^\]\(#//; s/\)$//' | sort -u); do
	links=$((links + 1))
	if ! grep -qxF -- "$link" <<<"$anchors"; then
		fail=$((fail + 1))
		echo "FAIL: link #$link leads to no heading"
	fi
done

note=""
[ $skipped -gt 0 ] && note=" ($skipped window examples skipped: no virtual display or gui library)"
echo "manual: $pass examples passed, $links links checked, $fail failed$note"
[ $fail -eq 0 ]
