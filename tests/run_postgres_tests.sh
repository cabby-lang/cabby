#!/usr/bin/env bash
# PostgreSQL, through the databases library: a server of the test's own is
# started (initdb and pg_ctl, in a folder of its own, listening only on a
# socket there), then tests/databases/postgres.cby is run against it (its
# //> lines, then its panic). Skipped where PostgreSQL's server programs
# aren't installed (the 'postgresql' package; Homebrew's postgresql@17).
#
# Usage: run_postgres_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"

# its programs: on the PATH (Arch, Homebrew's linked one), or where
# Debian and Homebrew keep each version
bin=""
if command -v initdb > /dev/null && command -v pg_ctl > /dev/null; then
	bin=$(dirname "$(command -v initdb)")
else
	for d in /usr/lib/postgresql/*/bin /opt/homebrew/opt/postgresql@*/bin /usr/local/opt/postgresql@*/bin /opt/homebrew/opt/postgresql/bin; do
		[[ -x "$d/initdb" && -x "$d/pg_ctl" ]] && bin=$d
	done
fi
if [[ -z "$bin" ]]; then
	echo "postgres ($MODE): skipped (PostgreSQL's server isn't installed)"
	exit 0
fi
# (PostgreSQL won't run as root: then as its own user, if there is one)
as=()
if [[ $(id -u) -eq 0 ]]; then
	if id postgres > /dev/null 2>&1 && command -v runuser > /dev/null; then
		as=(runuser -u postgres --)
	else
		echo "postgres ($MODE): skipped (it won't run as root, and there's no 'postgres' user)"
		exit 0
	fi
fi

DIR=$(mktemp -d /tmp/cabby-pg.XXXXXX)
[[ ${#as[@]} -gt 0 ]] && chown postgres "$DIR"
PORT=$((54000 + $$ % 900))
stop() {
	(cd / && "${as[@]}" "$bin/pg_ctl" -D "$DIR/data" -m immediate stop > /dev/null 2>&1)
	rm -rf "$DIR"
}
trap stop EXIT
if ! (cd / && "${as[@]}" "$bin/initdb" -D "$DIR/data" -A trust -U cabby > "$DIR/initdb.log" 2>&1); then
	echo "FAIL postgres: initdb didn't work"; sed 's/^/  | /' "$DIR/initdb.log" | tail -10
	exit 1
fi
if ! (cd / && "${as[@]}" "$bin/pg_ctl" -D "$DIR/data" -l "$DIR/server.log" -w \
	-o "-k $DIR -p $PORT -c listen_addresses=''" start > /dev/null 2>&1); then
	echo "FAIL postgres: the server didn't start"; sed 's/^/  | /' "$DIR/server.log" | tail -10
	exit 1
fi
export CABBY_TEST_POSTGRES="postgresql://cabby@/postgres?host=$DIR&port=$PORT"

pass=0; fail=0
for f in "$ROOT"/tests/databases/*.cby; do
	name=$(basename "$f" .cby)
	prog=("$CLI" "$f")
	if [[ "$MODE" == compile ]]; then
		if ! "$CLI" -c "$f" -o "$DIR/$name" > "$DIR/err" 2>&1; then
			fail=$((fail+1)); echo "FAIL databases/$name (compile)"; head -10 "$DIR/err" | sed 's/^/  | /'; continue
		fi
		prog=("$DIR/$name")
	fi
	sed -n 's/^\/\/> \{0,1\}//p' "$f" > "$DIR/want"
	want_panic=$(sed -n 's/^\/\/ panic: //p' "$f" | head -1)
	timeout 60 "${prog[@]}" > "$DIR/got" 2> "$DIR/err"
	rc=$?
	ok=1
	cmp -s "$DIR/want" "$DIR/got" || ok=0
	if [[ -n "$want_panic" ]]; then
		[[ $rc -eq 101 ]] && grep -qF -- "$want_panic" "$DIR/err" || ok=0
	elif [[ $rc -ne 0 ]]; then
		ok=0
	fi
	if [[ $ok -eq 1 ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL databases/$name ($MODE, exit $rc)"
		diff "$DIR/want" "$DIR/got" | head -20 | sed 's/^/  | /'
		head -5 "$DIR/err" | sed 's/^/  ! /'
	fi
done
echo "postgres ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
