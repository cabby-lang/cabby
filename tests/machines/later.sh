#!/usr/bin/env bash
# The checks still waiting on a real computer (2026-11-20), one at a
# time. Run it from the cabby folder, after 'make', at the computer itself:
#     tests/machines/later.sh screens     the Mac: its monitors, read and set
#     tests/machines/later.sh beeps       the Mac: a video's sound, beeping
#     tests/machines/later.sh hearing     Arch: why speech.can_hear is false
# Copy everything it prints back to Claude.
source "$(dirname "${BASH_SOURCE[0]}")/check.sh"

case "${1:-}" in
screens)
	echo "== The monitors, as the desktop says"
	"$CABBY" screens.cby
	echo
	read -r -p "Set the main screen to the mode and place it has now (the screen may blink)? [y/N] " Yes < /dev/tty
	if [[ "$Yes" == y* ]]; then
		"$CABBY" screens.cby change
		echo "exit status: $?"
	fi
	;;
beeps)
	if [[ ! -f "$ROOT/lib/libcabbyvideos.so" ]]; then
		echo "no videos library: install ffmpeg (brew install ffmpeg), then make"
		exit 1
	fi
	look "A video, with its sound" "Did you hear a short beep at the start of each second, while the colors changed?" \
		env CABBY_SOUND_REPORT=1 "$CABBY" video.cby
	;;
hearing)
	echo "== The programs Cabby looks for (whisper-cli, then whisper-cpp)"
	for P in whisper-cli whisper-cpp whisper main; do
		if command -v "$P" > /dev/null; then
			echo "  $P: $(command -v "$P")"
			"$P" --help > /dev/null 2>&1
			echo "    '$P --help' exits with $? (Cabby needs 0)"
		else
			echo "  $P: not on the PATH"
		fi
	done
	echo "== whisper-cli anywhere else (a build folder, say)"
	find "$HOME" /usr/local /opt -name 'whisper-cli' -type f 2> /dev/null | head -5 | sed 's/^/  /'
	echo "== Models (ggml-*.bin) Cabby looks for, and any elsewhere"
	echo "  CABBY_WHISPER_MODEL=${CABBY_WHISPER_MODEL:-(not set)}"
	find "$HOME" /usr/share /usr/local/share /opt -name 'ggml-*.bin' 2> /dev/null | head -10 | sed 's/^/  /'
	echo "== What Cabby says"
	cat > "${TMPDIR:-/tmp}/cabby-hearing.cby" <<'CBY'
use speech

int main {
	println "model folder (speech.download_model's):", speech.model_folder
	println "model found:", speech.model
	println "can hear:", speech.can_hear, "| can speak:", speech.can_speak
}
CBY
	"$CABBY" "${TMPDIR:-/tmp}/cabby-hearing.cby"
	rm -f "${TMPDIR:-/tmp}/cabby-hearing.cby"
	;;
*)
	echo "which check? tests/machines/later.sh screens | beeps | hearing"
	exit 2
	;;
esac
