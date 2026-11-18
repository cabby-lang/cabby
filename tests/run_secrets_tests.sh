#!/usr/bin/env bash
# The secrets library: tests/secrets/secrets.cby checks what it's built on
# against standard examples (PBKDF2's RFC 7914, AES-GCM's NIST cases,
# HMAC's RFC 4231), then each of its functions; it must print its //>
# lines, then panic as its // panic: line says. keys.cby is public keys
# (RFC 8032's examples), and argon2.cby Argon2id passwords.
#
# Usage: run_secrets_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
DIR=$ROOT/tests/secrets
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
# (Argon2 is OpenSSL 3.2's and later: CABBY_LIBCRYPTO can name a newer one)
printf 'use secrets\nint main {\n\tprintln secrets.can_use_argon2\n\treturn 0\n}\n' > "$TMP/probe.cby"
argon2=$("$CLI" "$TMP/probe.cby" 2>/dev/null)
for f in "$DIR"/*.cby; do
	name=$(basename "$f" .cby)
	if [[ $name == argon2 && $argon2 != true ]]; then
		echo "(secrets/argon2 skipped: this OpenSSL is older than 3.2)"
		continue
	fi
	prog=("$CLI" "$f")
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$f" -o "$TMP/$name" > "$TMP/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL secrets/$name (compile)"; head -10 "$TMP/err" | sed 's/^/  | /'; continue
		fi
		prog=("$TMP/$name")
	fi
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$TMP/want"
	want_panic=$(sed -n 's/^\/\/ panic: //p' "$f" | head -1)
	(cd "$TMP" && timeout 120 "${prog[@]}") > "$TMP/got" 2> "$TMP/err"
	rc=$?
	ok=1
	cmp -s "$TMP/want" "$TMP/got" || ok=0
	if [[ -n "$want_panic" ]]; then
		[[ $rc -eq 101 ]] && grep -qF -- "$want_panic" "$TMP/err" || ok=0
	elif [[ $rc -ne 0 ]]; then
		ok=0
	fi
	if [[ $ok -eq 1 ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL secrets/$name ($MODE, exit $rc)"
		diff "$TMP/want" "$TMP/got" | head -20 | sed 's/^/  | /'
		head -5 "$TMP/err" | sed 's/^/  ! /'
	fi
done
echo "secrets ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
