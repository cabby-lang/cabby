#!/bin/bash
# `cabby --style` must report exactly the expected names in each file of
# tests/style/ (NAME.cby against NAME.out).
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CABBY=$(from_here "${1:-}" cabby)
needs "$CABBY"
pass=0
fail=0
for f in tests/style/*.cby; do
	want="${f%.cby}.out"
	got=$("$CABBY" --style "$f" 2>&1 | grep -E "warning|to change" | sed 's|^tests/style/||')
	if [ "$got" == "$(cat "$want")" ]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		echo "FAIL: $f"
		diff <(echo "$got") "$want" | head -20 | sed 's/^/    /'
	fi
done
echo "style tests: $pass passed, $fail failed"
[ $fail -eq 0 ]
