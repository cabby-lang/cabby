#!/usr/bin/env bash
# Puts Cabby's Homebrew formula, for this folder's version, in a tap of its
# own (cabby/local), and installs it from there:
#     packaging/homebrew/build-tap.sh           (builds, installs and tests)
#     packaging/homebrew/build-tap.sh --no-install
# Afterwards: brew upgrade cabby/local/cabby (after running it again for a
# newer version), brew uninstall cabby, brew untap cabby/local.
set -e
# (a terminal run through Rosetta, on an Apple processor: as the Apple
# processor instead)
if [[ "$(uname -s)" == "Darwin" && "$(sysctl -n hw.optional.arm64 2> /dev/null)" == "1" && "$(uname -m)" != "arm64" ]]; then
	exec arch -arm64 /bin/bash "$0" "$@"
fi
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=$(sed -n 's/.*CABBY_VERSION "\(.*\)".*/\1/p' "$ROOT/version.h")
OUT=$ROOT/packaging/homebrew/out
TARBALL=$OUT/cabby-$VERSION.tar.gz
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# the source, as a tarball with one folder, cabby (Apple's tar has no
# --transform, so it's copied there first)
mkdir -p "$WORK/cabby" "$OUT"
(cd "$ROOT" && tar cf - --exclude='./build' --exclude='./bench/build' --exclude='*.o' --exclude='./lib/libcabby*' \
	--exclude='./lib/*.link' --exclude='./lib/gtk.hby' --exclude='./lib/tgame.hby' --exclude='./lib/sdlgui.hby' \
	--exclude='__pycache__' --exclude='./packaging/*/out' --exclude='./machine-results.txt' .) | (cd "$WORK/cabby" && tar xf -)
(cd "$WORK" && tar czf "$TARBALL" cabby)
if command -v shasum > /dev/null; then
	SHA=$(shasum -a 256 "$TARBALL" | cut -d' ' -f1)
else
	SHA=$(sha256sum "$TARBALL" | cut -d' ' -f1)
fi
sed -e "s|@URL@|file://$TARBALL|" -e "s|@VERSION@|$VERSION|" -e "s|@HOMEPAGE@|file://$ROOT|" -e "s|@SHA256@|$SHA|" \
	"$ROOT/packaging/homebrew/cabby.rb" > "$OUT/cabby.rb"
echo "formula: $OUT/cabby.rb"
[[ "$1" == "--no-install" ]] && exit 0

# which Homebrew: on an Apple processor, the Apple one in /opt/homebrew,
# never an Intel one (in /usr/local, run through Rosetta), which builds a
# Cabby for Intel against libraries for the Apple processor
BREW=$(command -v brew || true)
if [[ "$(sysctl -n hw.optional.arm64 2> /dev/null)" == "1" ]]; then
	if [[ -x /opt/homebrew/bin/brew ]]; then
		BREW=/opt/homebrew/bin/brew
	else
		echo "This Mac has an Apple processor, but its Homebrew is the Intel one (in /usr/local)."
		echo "Install Homebrew for the Apple processor (see https://brew.sh), in a terminal"
		echo "that isn't opened with Rosetta, and run this again."
		exit 1
	fi
fi
[[ -n "$BREW" ]] || { echo "Homebrew isn't installed (see https://brew.sh)"; exit 1; }
brew() { "$BREW" "$@"; }
TAP=$(brew --repository)/Library/Taps/cabby/homebrew-local
[[ -d "$TAP" ]] || brew tap-new --no-git cabby/local > /dev/null
mkdir -p "$TAP/Formula"
cp "$OUT/cabby.rb" "$TAP/Formula/cabby.rb"
if brew list --formula cabby > /dev/null 2>&1; then
	brew reinstall cabby/local/cabby
else
	brew install cabby/local/cabby
fi
brew test cabby/local/cabby
echo "installed: $(command -v cabby) ($(cabby --version | head -1))"
