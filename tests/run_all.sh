#!/usr/bin/env bash
# Every test suite, as `make test` runs them. Works from any folder:
#   tests/run_all.sh            (or ./run_all.sh inside tests/)
# Build Cabby first with `make`. Stops at the first suite that fails.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
BUILD=${BUILD:-build}
t=tests
set -e
$t/run_lexer_tests.sh "$BUILD/lexdump"
CABBY_LIB=lib $t/run_parser_tests.sh "$BUILD/astdump"
$t/run_sema_tests.sh ./cabby
$t/run_program_tests.sh ./cabby run
# the same, without the interpreter's bytecode: the tree-walker alone
# gives the same answers (CABBY_BYTECODE=0)
CABBY_BYTECODE=0 BYTECODE_LABEL=" without bytecode" $t/run_program_tests.sh ./cabby run
$t/run_multi_tests.sh ./cabby run
$t/run_repl_tests.sh ./cabby
$t/run_tutorial_tests.sh ./cabby run
$t/run_tutorial_tests.sh ./cabby compile
$t/run_gtk_tests.sh ./cabby run
$t/run_gtk_tests.sh ./cabby compile
$t/run_dbus_tests.sh ./cabby run
$t/run_dbus_tests.sh ./cabby compile
$t/run_desktop_tests.sh ./cabby run
$t/run_desktop_tests.sh ./cabby compile
$t/run_networks_tests.sh ./cabby run
$t/run_networks_tests.sh ./cabby compile
$t/run_text_tests.sh ./cabby run
$t/run_text_tests.sh ./cabby compile
$t/run_accessibility_tests.sh ./cabby run
$t/run_accessibility_tests.sh ./cabby compile
$t/run_world_tests.sh ./cabby run
$t/run_world_tests.sh ./cabby compile
$t/run_bluetooth_tests.sh ./cabby run
$t/run_bluetooth_tests.sh ./cabby compile
$t/run_emails_tests.sh ./cabby run
$t/run_emails_tests.sh ./cabby compile
$t/run_secrets_tests.sh ./cabby run
$t/run_secrets_tests.sh ./cabby compile
$t/run_postgres_tests.sh ./cabby run
$t/run_postgres_tests.sh ./cabby compile
$t/run_videos_tests.sh ./cabby run
$t/run_videos_tests.sh ./cabby compile
$t/run_os_tests.sh ./cabby run
$t/run_os_tests.sh ./cabby compile
$t/run_linux_tests.sh ./cabby run
$t/run_linux_tests.sh ./cabby compile
$t/run_tgame_tests.sh ./cabby run
$t/run_tgame_tests.sh ./cabby compile
if [[ -f lib/libcabbytgame.so ]] && command -v python3 > /dev/null; then
	python3 $t/tgame_terminal.py ./cabby
else
	echo "tgame terminal: skipped (no SDL 3, or no python3)"
fi
$t/run_manual_tests.sh ./cabby tutorials/tutorials.md
$t/run_manual_tests.sh ./cabby STYLE.md
$t/run_style_tests.sh ./cabby
if command -v python3 > /dev/null; then
	python3 tools/check-sections.py tutorials/*-quest-*.cby && echo "quest sections: at most 10 lines each"
fi
# the manual and the tutorials say which Cabby they're for: this one
V=$(sed -n 's/.*CABBY_VERSION "\(.*\)".*/\1/p' version.h)
D=$(sed -n 's/.*CABBY_VERSION_DATE "\(.*\)".*/\1/p' version.h)
for f in tutorials/tutorials.md tutorials/README.md; do
	if ! grep -qF "*For Cabby $V ($D).*" "$f"; then
		echo "FAIL: $f doesn't say it's for Cabby $V ($D): change its '*For Cabby ...*' line"
		exit 1
	fi
done
echo "the manual and tutorials are for Cabby $V"
for f in lib/*.md; do
	# (videos.md's examples need the videos library, built only where FFmpeg was found)
	[[ "$f" == lib/videos.md && ! -f lib/libcabbyvideos.so ]] && continue
	$t/run_manual_tests.sh ./cabby "$f"
done
$t/run_vim_tests.sh
$t/run_program_tests.sh ./cabby compile
$t/run_multi_tests.sh ./cabby compile
echo "all tests passed"
