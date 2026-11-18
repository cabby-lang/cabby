#!/usr/bin/env bash
# Cabby's checks for Arch: what the tests can only stand in for. Run it at
# the computer itself (a window and sound are part of it), from the cabby
# folder, after 'make':
#     tests/machines/arch.sh
source "$(dirname "${BASH_SOURCE[0]}")/check.sh"

if has initdb || has pg_ctl; then
	auto "PostgreSQL: a server of the test's own" "$ROOT/tests/run_postgres_tests.sh" "$CABBY"
else
	skip "PostgreSQL" "not installed (sudo pacman -S postgresql)"
fi
auto "Argon2 passwords (Arch's OpenSSL)" bash -c \
	"'$ROOT/tests/run_secrets_tests.sh' '$CABBY' | tee /dev/stderr | grep -q 'secrets (run): 3 passed'"
auto "HTTPS to a real site" "$CABBY" https.cby
look "Linux's sensors and services" "Do the temperatures and the number of services look right?" "$CABBY" sensors.cby
if [[ -f "$ROOT/lib/libcabbyvideos.so" ]]; then
	auto "Videos with Arch's FFmpeg ($(ffmpeg -version 2>/dev/null | head -1 | awk '{print $3}'))" \
		"$ROOT/tests/run_videos_tests.sh" "$CABBY"
	look "A video, with its sound" "Did it play, with a beep each time the colors changed?" env CABBY_SOUND_REPORT=1 "$CABBY" video.cby
else
	skip "Videos" "FFmpeg wasn't found when Cabby was built (sudo pacman -S ffmpeg)"
fi
look "Speaking, and hearing it back" "Did you hear 'Hello from Cabby' (and, if Whisper's here, was it heard)?" "$CABBY" speech.cby
if [[ -f "$ROOT/lib/libcabbygtk.so" ]]; then
	sed 's/^use gui$/use gtk/; s/gui\./gtk./g' window.cby > /tmp/cabby-gtk-window.cby
	look "A gtk window under Sway: colors, a key, the menu" "White on dark blue, the key you pressed shown, and Ctrl+Q closing it?" "$CABBY" /tmp/cabby-gtk-window.cby
else
	skip "A gtk window" "GTK 4 wasn't found when Cabby was built"
fi
look "A gui window under Sway" "White on dark blue, the key you pressed shown, and Ctrl+Q closing it?" "$CABBY" window.cby
summary "Arch"
