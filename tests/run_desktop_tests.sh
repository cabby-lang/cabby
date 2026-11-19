#!/usr/bin/env bash
# The desktop and power libraries, as on each kind of computer: a Linux
# laptop (tests/desktop/power_supply stands for /sys/class/power_supply),
# a Linux desktop (no battery), and, from what their pmset says, a Mac
# laptop, a full one and a Mac mini. Notifications and opening go to
# show-args, standing for osascript and xdg-open, which says what it was
# given: the Mac's way is checked here too (and Linux's, on a Mac:
# CABBY_DESKTOP_AS says which to use).
#
# The volume, system, users, mime and media libraries, and networks'
# local_address and wifi_name, are checked the same way: tools/ has
# stand-ins for wpctl, pactl, amixer, osascript, iwgetid, nmcli,
# networksetup, ipconfig and sw_vers (CABBY_TOOLS), each there only when
# CABBY_HAVE names it, so each kind of computer can be tried on any.
#
# Usage: run_desktop_tests.sh ./cabby [run|compile]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
CLI=$(from_here "${1:-}" cabby)
MODE=${2:-run}
needs "$CLI"
DIR=$ROOT/tests/desktop
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
# (name, environment, program, what it must say)
check() {
	local name=$1 env=$2 prog=$3 want=$4
	local got
	if [[ "$MODE" == compile ]]; then
		[[ -x "$TMP/$prog" ]] || "$CLI" -c "$DIR/$prog.cby" -o "$TMP/$prog" > "$TMP/err" 2>&1 || {
			fail=$((fail+1)); echo "FAIL $name (compile)"; head -5 "$TMP/err"; return; }
		got=$(env $env "$TMP/$prog" 2>&1)
	else
		got=$(env $env "$CLI" "$DIR/$prog.cby" 2>&1)
	fi
	if [[ "$got" == "$want" ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL $name ($MODE)"; diff <(echo "$want") <(echo "$got") | sed 's/^/  | /'
	fi
}
check "a Linux laptop, charging" "CABBY_DESKTOP_AS=linux CABBY_POWER_SUPPLY=$DIR/power_supply" power \
	"battery: true 87 charging: true plugged in: true full: false"
check "a Linux desktop" "CABBY_DESKTOP_AS=linux CABBY_POWER_SUPPLY=$DIR/nothing" power \
	"battery: false -1 charging: false plugged in: true full: false"
check "a Mac laptop, on its battery" "CABBY_DESKTOP_AS=macos CABBY_PMSET_OUTPUT=$DIR/mac-laptop.txt" power \
	"battery: true 64 charging: false plugged in: false full: false"
check "a Mac laptop, full" "CABBY_DESKTOP_AS=macos CABBY_PMSET_OUTPUT=$DIR/mac-full.txt" power \
	"battery: true 100 charging: false plugged in: true full: true"
check "a Mac mini" "CABBY_DESKTOP_AS=macos CABBY_PMSET_OUTPUT=$DIR/mac-mini.txt" power \
	"battery: false -1 charging: false plugged in: true full: false"
check "a Mac's notifications and opening" "CABBY_DESKTOP_AS=macos CABBY_OSASCRIPT=$DIR/show-args CABBY_OPENER=$DIR/show-args" desktop \
	"  arg: -e
  arg: display notification \"All files copied.\" with title \"Backup \\\"done\\\"\"
  arg: https://example.org/a b
shown: true
opened: true"
check "no notification server" "CABBY_DESKTOP_AS=linux DBUS_SESSION_BUS_ADDRESS= XDG_RUNTIME_DIR=$DIR/nothing CABBY_OPENER=$DIR/show-args" desktop \
	"  arg: https://example.org/a b
shown: false
opened: true"

# ---- the volume, by each way there is of setting it
T=$DIR/tools
STATE=$TMP/state
mkdir -p "$STATE"
fresh() { echo 25 > "$STATE/level"; echo 0 > "$STATE/muted"; rm -f "$STATE"/*.state "$STATE/told"; }
VOLUME_WORKS="level: 25 muted: false can set: true
set: true 40
up: true 55
down past 0: true 0
past 100: true 100
mute: true true
unmute: true false"
for way in "wpctl" "pactl" "amixer" "pactl,amixer"; do
	fresh
	check "the volume, by $way" "CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_HAVE=$way CABBY_TEST_STATE=$STATE" volume "$VOLUME_WORKS"
done
fresh
check "the volume, on a Mac" "CABBY_DESKTOP_AS=macos CABBY_TOOLS=$T CABBY_HAVE=osascript CABBY_TEST_STATE=$STATE" volume "$VOLUME_WORKS"
fresh
check "the volume, with nothing to set it" "CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_HAVE=none CABBY_TEST_STATE=$STATE" volume \
	"level: -1 muted: false can set: false
set: false -1
up: false -1
down past 0: false -1
past 100: false -1
mute: false false
unmute: false false"

# ---- the screen's brightness: a laptop's backlight (its own folder, as
# /sys/class/backlight has), written there, or by brightnessctl or
# logind when it can't be; and a desktop's monitor, by ddcutil
BRIGHTNESS_WORKS="level: 50 can set: true
set: true 40
up: true 55
down past 0: true 0
past 100: true 100"
light() { rm -rf "$TMP/backlight"; mkdir -p "$TMP/backlight/intel_backlight"; echo 937 > "$TMP/backlight/intel_backlight/max_brightness"; echo 468 > "$TMP/backlight/intel_backlight/brightness"; echo 50 > "$STATE/monitor"; rm -f "$STATE/told"; }
LIGHT="CABBY_DESKTOP_AS=linux CABBY_BACKLIGHT=$TMP/backlight CABBY_TOOLS=$T CABBY_TEST_STATE=$STATE"
light
check "the brightness, written" "$LIGHT CABBY_HAVE=none" brightness "$BRIGHTNESS_WORKS"
light
check "the brightness, by brightnessctl" "$LIGHT CABBY_HAVE=brightnessctl,busctl CABBY_BACKLIGHT_LOCKED=1" brightness "$BRIGHTNESS_WORKS"
grep -qx brightnessctl "$STATE/told" && ! grep -q logind "$STATE/told" && pass=$((pass+1)) || { fail=$((fail+1)); echo "FAIL brightnessctl was used ($MODE)"; }
light
check "the brightness, by logind" "$LIGHT CABBY_HAVE=busctl CABBY_BACKLIGHT_LOCKED=1" brightness "$BRIGHTNESS_WORKS"
grep -qx "logind intel_backlight" "$STATE/told" && pass=$((pass+1)) || { fail=$((fail+1)); echo "FAIL logind was used ($MODE)"; }
light
check "a monitor's brightness, by ddcutil" "CABBY_DESKTOP_AS=linux CABBY_BACKLIGHT=$DIR/nothing CABBY_TOOLS=$T CABBY_TEST_STATE=$STATE CABBY_HAVE=ddcutil" brightness "$BRIGHTNESS_WORKS"
check "no brightness to set" "CABBY_DESKTOP_AS=linux CABBY_BACKLIGHT=$DIR/nothing CABBY_TOOLS=$T CABBY_HAVE=none" brightness "level: -1 can set: false
set: false -1
up: false -1
down past 0: false -1
past 100: false -1"

# ---- the system
REST="hostname: true
kernel: true machine: true
processors: true memory: true uptime: true"
check "a Debian" "CABBY_DESKTOP_AS=linux CABBY_OS_RELEASE=$DIR/os-release-debian" system "linux | Debian GNU/Linux 13 (trixie) | 13
$REST"
check "an Arch" "CABBY_DESKTOP_AS=linux CABBY_OS_RELEASE=$DIR/os-release-arch" system "linux | Arch Linux | 
$REST"
check "a Mac's system" "CABBY_DESKTOP_AS=macos CABBY_TOOLS=$T CABBY_HAVE=sw_vers" system "macos | macOS 15.1 | 15.1
$REST"

# (what the stand-ins were asked, in their log)
logged() {
	if [[ "$(cat "$STATE/admin.log" 2>/dev/null)" == "$2" ]]; then
		pass=$((pass+1))
	else
		fail=$((fail+1)); echo "FAIL $1 ($MODE)"; diff <(echo "$2") "$STATE/admin.log" | sed 's/^/  | /'
	fi
}
rm -f "$STATE/admin.log"
check "naming the computer, on Linux" "CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_HAVE=hostnamectl CABBY_TEST_STATE=$STATE" hostname "set: true"
logged "the name it was given, on Linux" "hostnamectl [set-hostname] [music-room]"
rm -f "$STATE/admin.log"
check "naming the computer, on a Mac" "CABBY_DESKTOP_AS=macos CABBY_TOOLS=$T CABBY_HAVE=scutil CABBY_TEST_STATE=$STATE" hostname "set: true"
logged "the names it was given, on a Mac" "scutil [--set] [HostName] [music-room]
scutil [--set] [LocalHostName] [music-room]
scutil [--set] [ComputerName] [music-room]"
check "naming the computer, not allowed" "CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_HAVE=hostnamectl CABBY_ADMIN_FAILS=hostnamectl CABBY_TEST_STATE=$STATE" hostname "set: false"

# ---- printing, through CUPS's lp, lpstat and cancel (the same on a Mac)
printers() { rm -f "$STATE/admin.log" "$STATE/jobs" "$STATE/next"; printf 'Brother_HL\nOffice\n' > "$STATE/printers"; echo Brother_HL > "$STATE/default"; }
PRINTING_WORKS="printers: [Brother_HL, Office] default: Brother_HL can print: true
printed: Brother_HL-12
no such file: 
printed: Office-13
printed text: Brother_HL-14
jobs: [Brother_HL-12, Office-13, Brother_HL-14]
cancel: true true false
cancel again: false
cancel all: true []"
PRINT="CABBY_TOOLS=$T CABBY_HAVE=lp,lpstat,cancel CABBY_TEST_STATE=$STATE"
printers
(cd "$DIR" && check "printing" "$PRINT" printing "$PRINTING_WORKS"; echo "$pass $fail" > "$TMP/counts")
read pass fail < "$TMP/counts"
logged "what lp was asked" "lpstat [-e]
lpstat [-d]
lpstat [-e]
lp [--] [printing.cby]
lp [--] [nothing.pdf]
lp [-d] [Office] [-n] [2] [-t] [Scales] [-o] [sides=two-sided-long-edge] [-o] [page-ranges=1-3] [-o] [print-color-mode=monochrome] [--] [printing.cby]
lp
  read: Lesson at four.
Bring the scales.
lpstat [-o]
cancel [Office-13]
lpstat [-o]
lpstat [-o]
cancel [Office-13]
cancel [-a]
lpstat [-o]"
printers
(cd "$DIR" && check "printing, an older CUPS" "$PRINT CABBY_OLD_CUPS=1" printing "$PRINTING_WORKS"; echo "$pass $fail" > "$TMP/counts")
read pass fail < "$TMP/counts"
printers; : > "$STATE/printers"; : > "$STATE/default"
(cd "$DIR" && check "no printers" "$PRINT" printing "printers: [] default:  can print: false
printed: 
no such file: 
printed: 
printed text: 
jobs: []
cancel: false true true
cancel again: false
cancel all: true []"; echo "$pass $fail" > "$TMP/counts")
read pass fail < "$TMP/counts"
(cd "$DIR" && check "no CUPS" "CABBY_TOOLS=$T CABBY_HAVE=none" printing "printers: [] default:  can print: false
printed: 
no such file: 
printed: 
printed text: 
jobs: []
cancel: false true true
cancel again: false
cancel all: false []"; echo "$pass $fail" > "$TMP/counts")
read pass fail < "$TMP/counts"

# ---- speech: espeak-ng, espeak or spd-say, or a Mac's say
SAID="said: true
said: true
said: true"
ESPEAK_LOG="  read: Hello there.
WAY [-v] [la] [-s] [140] [--stdin]
  read: Gallia est omnis divisa in partes tres.
WAY [-v] [fr] [--stdin]
  read: Bonjour.
WAY [-v] [la] [-s] [140] [-w] [aeneid.wav] [--stdin]
  read: Arma virumque cano."
for way in espeak-ng espeak; do
	rm -f "$STATE/admin.log"
	check "speech, by $way" "CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_HAVE=$way CABBY_TEST_STATE=$STATE" speech "can speak: true voices: [en-gb, grc, la]
$SAID
saved: true"
	logged "what $way was asked" "$way [--stdin]
${ESPEAK_LOG//WAY/$way}"
done
rm -f "$STATE/admin.log"
check "speech, by spd-say" "CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_HAVE=spd-say CABBY_TEST_STATE=$STATE" speech "can speak: true voices: [English_(Great_Britain), Latin]
$SAID
saved: false"
logged "what spd-say was asked" "spd-say [-w] [-e]
  read: Hello there.
spd-say [-w] [-l] [la] [-r] [-20] [-e]
  read: Gallia est omnis divisa in partes tres.
spd-say [-w] [-l] [fr] [-e]
  read: Bonjour."
rm -f "$STATE/admin.log"
check "speech, on a Mac" "CABBY_DESKTOP_AS=macos CABBY_TOOLS=$T CABBY_HAVE=say CABBY_TEST_STATE=$STATE" speech "can speak: true voices: [Daniel, Eddy (English (US)), Amélie]
$SAID
saved: true"
logged "what say was asked" "say
  read: Hello there.
say [-r] [140]
  read: Gallia est omnis divisa in partes tres.
say [-v] [Amélie]
  read: Bonjour.
say [-r] [140] [-o] [aeneid.wav] [--data-format=LEI16@22050]
  read: Arma virumque cano."
check "nothing to speak with" "CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_HAVE=none" speech "can speak: false voices: []
said: false
said: false
said: false
saved: false"

# ---- hearing: whisper.cpp's whisper-cli, and a model where Cabby keeps them
rm -f "$STATE/admin.log"
mkdir -p "$TMP/data/Cabby/whisper"
: > "$TMP/data/Cabby/whisper/ggml-base.en.bin"
SOUND=$(ls "$ROOT"/tutorials/sounds/*.wav | head -1)
check "hearing, by whisper-cli" "CABBY_SAVE_FOLDERS=$TMP/data CABBY_TOOLS=$T CABBY_HAVE=whisper-cli CABBY_TEST_STATE=$STATE CABBY_TEST_SOUND=$SOUND" hearing "can hear: true
model: true
heard: Hello there. The lesson is at four.
in Latin: Gallia est omnis divisa in partes tres."
logged "what whisper-cli was asked" "whisper-cli model=ggml-base.en.bin lang=auto rate=16000 channels=1
whisper-cli model=ggml-base.en.bin lang=la rate=16000 channels=1"

# ---- users: whoever runs this
me=$(id -un)
home=$(eval echo "~$me")
check "users" "WANT_NAME=$me WANT_ID=$(id -u) WANT_HOME=$home" users "name: true id: true
home: true is root: true
groups: true in the first: true in none: false
root: true 0 true
nobody like this: false -1 [] 0
nameless: false -1"

# ---- kinds of file
F=$TMP/files
mkdir -p "$F/folder"
printf '\211PNG\r\n\032\n\0\0\0\rIHDR' > "$F/picture"
printf '%%PDF-1.4\n%%\342\343\n' > "$F/notes.txt"
printf '\n  <!DOCTYPE html>\n<html><body>Hi</body></html>\n' > "$F/page"
printf 'ID3\003\0\0\0\0\0' > "$F/tune.mp3"
printf '{"a": 1}\n' > "$F/data.json"
printf 'Hello, here is some text: caf\303\251.\n' > "$F/readme"
: > "$F/empty.md"
printf '\0\001\002\377\376' > "$F/binary"
printf 'PK\003\004\024\0\0\0' > "$F/report.docx"
printf '#!/usr/bin/env python3\nprint(1)\n' > "$F/script"
check "kinds of file" "FILES=$F" mime "picture: image/png
notes.txt: application/pdf
page: text/html
tune.mp3: audio/mpeg
data.json: application/json
readme: text/plain
empty.md: text/markdown
binary: application/octet-stream
report.docx: application/vnd.openxmlformats-officedocument.wordprocessingml.document
script: text/x-python
folder: inode/directory
nothing: 
by name: image/jpeg application/gzip [] []
is: true true false true false"

# ---- this computer on the network
check "Wi-Fi, by iwgetid" "CABBY_DESKTOP_AS=linux CABBY_LOCAL_ADDRESS=192.168.1.20 CABBY_TOOLS=$T CABBY_HAVE=iwgetid,nmcli" here \
	"connected: true address: 192.168.1.20
wifi: [Cabby Café]"
check "Wi-Fi, by nmcli" "CABBY_DESKTOP_AS=linux CABBY_LOCAL_ADDRESS=192.168.1.20 CABBY_TOOLS=$T CABBY_HAVE=nmcli" here \
	"connected: true address: 192.168.1.20
wifi: [Home:5G]"
check "no network" "CABBY_DESKTOP_AS=linux CABBY_LOCAL_ADDRESS= CABBY_TOOLS=$T CABBY_HAVE=none" here \
	"connected: false address: 
wifi: []"
check "a Mac's Wi-Fi" "CABBY_DESKTOP_AS=macos CABBY_LOCAL_ADDRESS=10.0.0.5 CABBY_TOOLS=$T CABBY_HAVE=networksetup,ipconfig" here \
	"connected: true address: 10.0.0.5
wifi: [Studio]"
check "a Mac's Wi-Fi, from ipconfig" "CABBY_DESKTOP_AS=macos CABBY_LOCAL_ADDRESS=10.0.0.5 CABBY_TOOLS=$T CABBY_HAVE=networksetup,ipconfig CABBY_MAC_WIFI_HIDDEN=1 CABBY_MAC_SSID=Loft" here \
	"connected: true address: 10.0.0.5
wifi: [Loft]"
check "a Mac's Wi-Fi, hidden" "CABBY_DESKTOP_AS=macos CABBY_LOCAL_ADDRESS=10.0.0.5 CABBY_TOOLS=$T CABBY_HAVE=networksetup,ipconfig CABBY_MAC_WIFI_HIDDEN=1" here \
	"connected: true address: 10.0.0.5
wifi: []"

# ---- what's playing, on a Mac (Linux's is in run_dbus_tests.sh)
MAC="CABBY_DESKTOP_AS=macos CABBY_TOOLS=$T CABBY_HAVE=osascript CABBY_TEST_STATE=$STATE"
fresh
check "no player open" "$MAC CABBY_MAC_APPS=" media_mac "player: [] playing: false
 |  | 
play_pause: false playing: false
next: false"
fresh
echo playing > "$STATE/Music.state"
check "Music playing, Spotify open" "$MAC CABBY_MAC_APPS=Spotify,Music" media_mac "player: [Music] playing: true
Clair de Lune (Music) | Claude Debussy | Suite bergamasque
play_pause: true playing: false
next: true"
if grep -q "^Music: next track$" "$STATE/told" && ! grep -q "^Spotify: playpause" "$STATE/told"; then
	pass=$((pass+1))
else
	fail=$((fail+1)); echo "FAIL Music was told, Spotify wasn't ($MODE)"; sed 's/^/  | /' "$STATE/told"
fi
fresh
check "Spotify open, stopped" "$MAC CABBY_MAC_APPS=Spotify" media_mac "player: [Spotify] playing: false
Clair de Lune (Spotify) | Claude Debussy | Suite bergamasque
play_pause: true playing: true
next: true"

# ---- making users and groups: Linux's programs, and a Mac's
ADMIN="useradd,chpasswd,userdel,groupadd,groupdel,gpasswd,sysadminctl,dseditgroup"
rm -f "$STATE/admin.log"
check "making users, on Linux" "CABBY_DESKTOP_AS=linux CABBY_TEST_USERS=1 CABBY_TOOLS=$T CABBY_HAVE=$ADMIN CABBY_TEST_STATE=$STATE" accounts \
"useradd [-m] [-c] [Ada Lovelace] [-s] [/bin/bash] [ada]
chpasswd
  read: ada:analytical engine
useradd [-m] [-c] [Grace Hopper] [-s] [/bin/bash] [-G] [sudo or wheel] [grace]
chpasswd
  read: grace:cobol
chpasswd
  read: ada:difference engine
groupadd [band]
gpasswd [-a] [ada] [band]
gpasswd [-d] [ada] [band]
groupdel [band]
userdel [grace]
userdel [-r] [ada]"
rm -f "$STATE/admin.log"
check "making users, on a Mac" "CABBY_DESKTOP_AS=macos CABBY_TEST_USERS=1 CABBY_TOOLS=$T CABBY_HAVE=$ADMIN CABBY_TEST_STATE=$STATE" accounts \
"sysadminctl [-addUser] [ada] [-fullName] [Ada Lovelace] [-password] [analytical engine]
sysadminctl [-addUser] [grace] [-fullName] [Grace Hopper] [-password] [cobol] [-admin]
sysadminctl [-resetPasswordFor] [ada] [-newPassword] [difference engine]
dseditgroup [-o] [create] [band]
dseditgroup [-o] [edit] [-a] [ada] [-t] [user] [band]
dseditgroup [-o] [edit] [-d] [ada] [-t] [user] [band]
dseditgroup [-o] [delete] [band]
sysadminctl [-deleteUser] [grace] [-keepHome]
sysadminctl [-deleteUser] [ada]"

# ---- screens, mouse and keyboard, each desktop's way. tools/settings-tool
# stands for swaymsg, xrandr, kscreen-doctor, gsettings, xset, setxkbmap
# and xinput, changing the computer computer.json describes; the desktop
# is told by SWAYSOCK, XDG_CURRENT_DESKTOP and DISPLAY. GNOME's monitors
# are asked of fake-mutter.cby, on a session bus of the test's own.
SETTINGS="-u DISPLAY -u WAYLAND_DISPLAY -u SWAYSOCK -u XDG_CURRENT_DESKTOP CABBY_DESKTOP_AS=linux CABBY_TOOLS=$T CABBY_TEST_STATE=$STATE CABBY_XKB_RULES=$DIR/xkb/evdev.lst CABBY_LEDS=$DIR/leds"
settings() {   # (the desktop, its tools, which library, the expected output's name)
	cp "$DIR/computer.json" "$STATE/computer.json"
	check "$3, on $4" "$SETTINGS $1 CABBY_HAVE=$2" "$3" "$(cat "$DIR/expect/$3-$4.out")"
}
for lib in screens mouse keyboard; do
	settings SWAYSOCK=/run/sway.sock swaymsg $lib sway
	settings WAYLAND_DISPLAY=wayland-1 none $lib none
done
cp "$DIR/computer.json" "$STATE/computer.json"
check "keyboard layouts, on sway, without the tests' list" "-u CABBY_XKB_RULES ${SETTINGS/CABBY_XKB_RULES=$DIR\/xkb\/evdev.lst/} SWAYSOCK=/run/sway.sock CABBY_HAVE=swaymsg" keyboard-layouts "layouts: 2"
settings DISPLAY=:0 xrandr screens x11
settings DISPLAY=:0 xinput mouse x11
settings DISPLAY=:0 xset,setxkbmap keyboard x11
settings XDG_CURRENT_DESKTOP=KDE kscreen-doctor screens kde
settings XDG_CURRENT_DESKTOP=GNOME gsettings mouse gnome
settings XDG_CURRENT_DESKTOP=GNOME gsettings keyboard gnome
if command -v dbus-daemon > /dev/null; then
	mkdir -p "$TMP/bus"
	bus=$(dbus-daemon --session --fork --print-address=1 --print-pid=3 --address="unix:path=$TMP/bus/socket" 3> "$TMP/bus/pid" < /dev/null 2> /dev/null)
	if [[ -n "$bus" ]]; then
		DBUS_SESSION_BUS_ADDRESS=$bus "$CLI" "$DIR/fake-mutter.cby" "$DIR/mutter.json" "$TMP/bus/ready" > "$TMP/bus/mutter.log" 2>&1 &
		mutter=$!
		for _ in $(seq 100); do [[ -f "$TMP/bus/ready" ]] && break; sleep 0.1; done
		check "screens, on gnome" "$SETTINGS XDG_CURRENT_DESKTOP=GNOME CABBY_HAVE=none DBUS_SESSION_BUS_ADDRESS=$bus" screens "$(cat "$DIR/expect/screens-gnome.out")"
		kill $mutter 2> /dev/null
		kill "$(cat "$TMP/bus/pid")" 2> /dev/null
	fi
fi
if [[ "$(uname)" != Darwin ]]; then   # (on a Mac, its own screens are there)
	check "screens on a Mac (from Linux)" "$SETTINGS CABBY_DESKTOP_AS=macos CABBY_HAVE=none" screens "$(cat "$DIR/expect/screens-mac.out")"
fi

echo "desktop and power ($MODE): $pass passed, $fail failed"
[[ $fail -eq 0 ]]
