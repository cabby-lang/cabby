#!/bin/bash
# Checks the Vim indent file: every tutorial and library file, with its
# indentation stripped, must come back exactly as it was after `gg=G`.
# Skipped when vim is not installed.
if ! command -v vim > /dev/null; then
	echo "vim tests: skipped (vim is not installed)"
	exit 0
fi
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/vimrc" <<VIMRC
set nocompatible
let &rtp = '$PWD/editor/vim,' . &rtp
filetype plugin indent on
syntax on
VIMRC
pass=0
fail=0
for f in tutorials/*.cby tutorials/*/*.cby tutorials/*/*.hby lib/*.hby tests/run_cases.cby; do
	flat="$work/$(echo "$f" | tr / _)"
	sed 's/^\t*//' "$f" > "$flat"
	vim -u "$work/vimrc" -N -es -c 'normal! gg=G' -c 'x' "$flat" < /dev/null > /dev/null 2>&1
	if cmp -s "$f" "$flat"; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		echo "FAIL: $f"
		diff "$f" "$flat" | head -10 | sed 's/^/    /'
	fi
done
echo "vim indent tests: $pass passed, $fail failed"
[ $fail -eq 0 ]
