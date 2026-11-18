# common.sh — the start of every run_*.sh, so each one works from any folder
# (the cabby folder, tests/, or anywhere else). A path given as an argument
# means what it says from where you are; then the tests run from the cabby
# folder, where their own files are.

HERE=$PWD
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# a path made whole: ./ and ../ worked out, as realpath -m does (macOS's
# realpath has no -m, and the path needn't exist yet)
whole_path() {
	local IFS=/ part out=""
	local -a parts kept=()
	read -ra parts <<< "$1"
	for part in ${parts[@]+"${parts[@]}"}; do
		case "$part" in
			""|.) ;;
			..) [[ ${#kept[@]} -gt 0 ]] && unset 'kept[${#kept[@]}-1]' ;;
			*) kept+=("$part") ;;
		esac
	done
	for part in ${kept[@]+"${kept[@]}"}; do out="$out/$part"; done
	echo "${out:-/}"
}

# from_here GIVEN DEFAULT: GIVEN as a full path, or DEFAULT (a path in the
# cabby folder) when nothing was given
from_here() {
	if [[ -z "$1" ]]; then whole_path "$ROOT/$2"
	elif [[ "$1" == /* ]]; then whole_path "$1"
	else whole_path "$HERE/$1"
	fi
}

# timeout SECONDS COMMAND...: as GNU's, for a Mac, which has none (or
# gtimeout, from Homebrew's coreutils): the command is stopped if it
# takes longer, and the status is then 124
# (CABBY_OWN_TIMEOUT=1 uses this one anyway: how the tests are checked as
# a Mac without coreutils would run them)
if [[ -n "${CABBY_OWN_TIMEOUT:-}" ]] || ! command -v timeout > /dev/null; then
	if [[ -z "${CABBY_OWN_TIMEOUT:-}" ]] && command -v gtimeout > /dev/null; then
		timeout() { gtimeout "$@"; }
	else
		timeout() {
			local secs=$1
			shift
			"$@" 0<&0 &                 # (its input kept: a script's jobs get none otherwise)
			local pid=$!
			# (the watcher holds none of the command's output: $(timeout ...)
			# would wait for it otherwise)
			( sleep "$secs" && touch "${TMPDIR:-/tmp}/cabby-timeout.$pid" && kill -TERM "$pid" 2> /dev/null ) \
				< /dev/null > /dev/null 2>&1 &
			local watcher=$!
			wait "$pid"
			local rc=$?
			pkill -P "$watcher" 2> /dev/null   # its sleep
			kill "$watcher" 2> /dev/null
			wait "$watcher" 2> /dev/null
			if [[ -e "${TMPDIR:-/tmp}/cabby-timeout.$pid" ]]; then
				rm -f "${TMPDIR:-/tmp}/cabby-timeout.$pid"
				return 124
			fi
			return $rc
		}
	fi
fi

# needs FILE: stop with a clear message if a built program isn't there
needs() {
	if [[ ! -x "$1" ]]; then
		echo "$(basename "$0"): $1 isn't there: build Cabby first (run 'make' in $ROOT)" >&2
		exit 1
	fi
}

# a library folder given from somewhere else still means the same folder
if [[ -n "${CABBY_LIB:-}" && "$CABBY_LIB" != /* ]]; then
	export CABBY_LIB="$HERE/$CABBY_LIB"
fi

cd "$ROOT" || exit 1

# sound plays to no device in the tests (and as fast as it can), with
# Cabby's own synthesizer, so every computer hears the same
export CABBY_SOUND=none
export CABBY_SOUNDFONT=none
# and what's recorded is this file (a guitar, plucked), not a microphone
export CABBY_SOUND_IN="$ROOT/tests/sound-in.wav"
# the MIDI keyboard is this file (four notes), and what's sent to a MIDI
# synthesizer is written down as one (each test script's own)
export CABBY_MIDI_IN="$ROOT/tests/midi-in.mid"
export CABBY_MIDI_OUT="${TMPDIR:-/tmp}/cabby-test-midi-out-$(id -u)-$$.mid"

# saved games go to a folder of the tests' own: never your own saves
# (directories.save_folder looks here first). Each test script has one of
# its own, named by its process, so two runs at once (one at the Mac, one
# over ssh, say) can't empty each other's; those of scripts no longer
# running are cleared away as the next starts.
SAVES_BASE="${TMPDIR:-/tmp}"
for old_saves in "$SAVES_BASE"/cabby-test-saves-"$(id -u)"-*; do
	[[ -d "$old_saves" ]] || continue
	kill -0 "${old_saves##*-}" 2> /dev/null || rm -rf "$old_saves"
done
rm -rf "$SAVES_BASE/cabby-test-saves-$(id -u)"   # (the one shared folder of earlier versions)
export CABBY_SAVE_FOLDERS="$SAVES_BASE/cabby-test-saves-$(id -u)-$$"
rm -rf "$CABBY_SAVE_FOLDERS"
mkdir -p "$CABBY_SAVE_FOLDERS"
