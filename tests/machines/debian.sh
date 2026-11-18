#!/usr/bin/env bash
# Cabby's checks for Debian: what the tests can only stand in for. Run it
# at the computer itself (a window and sound are part of it), from the
# cabby folder, after 'make':
#     tests/machines/debian.sh
source "$(dirname "${BASH_SOURCE[0]}")/check.sh"

auto "Argon2 passwords (Debian 13's OpenSSL 3.5)" bash -c \
	"'$ROOT/tests/run_secrets_tests.sh' '$CABBY' | tee /dev/stderr | grep -q 'secrets (run): 3 passed'"
auto "HTTPS to a real site" "$CABBY" https.cby
look "Linux's sensors and services" "Do the temperatures and the number of services look right?" "$CABBY" sensors.cby
look "What the computer says about itself" "Is all of that right (disks, memory, wifi, printers)?" "$CABBY" facts.cby
look "A notification, and a page opened" "Did a notification show, and example.com open in the browser?" "$CABBY" notify.cby
auto "The volume, set and put back" "$CABBY" volume.cby
look "Speaking, and hearing it back" "Did you hear 'Hello from Cabby' (and, if Whisper's here, was it heard)?" "$CABBY" speech.cby
if [[ -f "$ROOT/lib/libcabbygtk.so" ]]; then
	sed 's/^use gui$/use gtk/; s/gui\./gtk./g' window.cby > /tmp/cabby-gtk-window.cby
	look "A gtk window: colors, a key, the menu" "White on dark blue, the key you pressed shown, and Ctrl+Q closing it?" "$CABBY" /tmp/cabby-gtk-window.cby
else
	skip "A gtk window: colors, a key, the menu" "GTK 4 wasn't found when Cabby was built"
fi
if [[ -f "$ROOT/lib/libcabbyvideos.so" ]]; then
	look "A video, with its sound (FFmpeg $(ffmpeg -version 2>/dev/null | head -1 | awk '{print $3}'))" "Did it play, with a beep each time the colors changed?" env CABBY_SOUND_REPORT=1 "$CABBY" video.cby
else
	skip "A video, with its sound" "FFmpeg wasn't found when Cabby was built"
fi
summary "Debian"
