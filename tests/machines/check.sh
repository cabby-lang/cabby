# check.sh — what debian.sh, arch.sh and mac.sh share: each check is run,
# and either passes by itself (auto) or is shown to you to say whether it
# looked right (look). At the end, a summary to send back, also saved as
# machine-results.txt in the cabby folder.
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CABBY=$ROOT/cabby
RESULTS=$ROOT/machine-results.txt
: > "$RESULTS.tmp"
n=0
cd "$HERE" || exit 1
if [[ ! -x "$CABBY" ]]; then
	echo "build Cabby first: run 'make' in $ROOT"
	exit 1
fi

note() { echo "$1" >> "$RESULTS.tmp"; }

title() {
	n=$((n + 1))
	printf '\n\033[1m%d. %s\033[0m\n' "$n" "$1"
}

# auto NAME COMMAND...: passes if the command succeeds (77: skipped)
auto() {
	local name=$1
	shift
	title "$name"
	"$@"
	local rc=$?
	if [[ $rc -eq 0 ]]; then
		echo "  -> PASS"; note "PASS  $name"
	elif [[ $rc -eq 77 ]]; then
		echo "  -> SKIPPED"; note "SKIP  $name"
	else
		echo "  -> FAIL (exit $rc)"; note "FAIL  $name (exit $rc)"
	fi
}

# look NAME QUESTION COMMAND...: runs it, then asks you
look() {
	local name=$1 question=$2
	shift 2
	title "$name"
	"$@"
	local rc=$?
	if [[ $rc -eq 77 ]]; then
		echo "  -> SKIPPED"; note "SKIP  $name"
		return
	fi
	local answer
	read -r -p "  $question (y/n, or s to skip) " answer < /dev/tty
	case "$answer" in
		y*|Y*) echo "  -> PASS"; note "PASS  $name" ;;
		s*|S*) echo "  -> SKIPPED"; note "SKIP  $name" ;;
		*) read -r -p "  What happened? " why < /dev/tty; note "FAIL  $name: $why" ;;
	esac
}

# skip NAME WHY
skip() {
	title "$1"
	echo "  -> SKIPPED: $2"
	note "SKIP  $1 ($2)"
}

has() { command -v "$1" > /dev/null 2>&1; }

summary() {
	{
		echo "Cabby machine checks: $1, $("$CABBY" --version | head -1), $(date '+%Y-%m-%d %H:%M')"
		cat "$RESULTS.tmp"
	} > "$RESULTS"
	rm -f "$RESULTS.tmp"
	printf '\n\033[1mSummary\033[0m (saved as %s, to send back):\n\n' "$RESULTS"
	cat "$RESULTS"
}
