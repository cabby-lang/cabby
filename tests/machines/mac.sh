#!/usr/bin/env bash
# Cabby's checks for the Mac: what the tests can only stand in for. Run it
# at the Mac itself, not over ssh (windows, sound, notifications and
# Bluetooth all need you there, and macOS asks Terminal's permission for
# some the first time), from the cabby folder, after 'make':
#     tests/machines/mac.sh
source "$(dirname "${BASH_SOURCE[0]}")/check.sh"

auto "HTTPS to a real site (Homebrew's OpenSSL)" "$CABBY" https.cby
look "What the Mac says about itself" "Is all of that right (disks, memory, wifi, printers, your name)?" "$CABBY" facts.cby
look "A notification, and a page opened" "Did a notification show, and example.com open in the browser?" "$CABBY" notify.cby
auto "The volume, set and put back" "$CABBY" volume.cby
echo
read -r -p "(Start a song playing in Music, then press Enter) " _ < /dev/tty
look "What's playing in Music" "Is that the song playing?" "$CABBY" media.cby
look "Speaking, and hearing it back" "Did you hear 'Hello from Cabby' (and, if Whisper's here, was it heard)?" "$CABBY" speech.cby
look "A gui window: colors, a key, the menu (⌘Q)" "White on dark blue, the key you pressed shown, the menu showing ⌘Q, and it closing?" "$CABBY" window.cby
if [[ -f "$ROOT/lib/libcabbyvideos.so" ]]; then
	look "A video, with its sound" "Did it play, with a beep each time the colors changed?" env CABBY_SOUND_REPORT=1 "$CABBY" video.cby
fi
look "Three dimensions (tutorial 89, the Mac's OpenGL)" "Did the field of coins show, and could you walk round it?" \
	bash -c "cd '$ROOT/tutorials' && '$CABBY' 89-coins.cby"
auto "Bluetooth, looking only (no pairing)" env CABBY_BLUETOOTH_REAL=1 "$ROOT/tests/run_bluetooth_tests.sh" "$CABBY"
if has initdb || [[ -d /opt/homebrew/opt/postgresql@17 ]]; then
	auto "PostgreSQL: a server of the test's own" "$ROOT/tests/run_postgres_tests.sh" "$CABBY"
else
	skip "PostgreSQL" "not installed (brew install postgresql@17)"
fi
summary "the Mac"
