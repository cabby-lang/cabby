#!/usr/bin/env bash
# Builds Cabby's Debian package (.deb) from this folder, for this
# computer's processor, then: sudo apt install ./packaging/debian/out/cabby_*.deb
#     packaging/debian/build-deb.sh
# It needs what building Cabby needs (build-essential, libffi-dev, clang,
# pkg-config, and the -dev packages of SDL 3, GTK 4 and FFmpeg for what
# uses them), and dpkg-dev, for dpkg-shlibdeps.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=$(sed -n 's/.*CABBY_VERSION "\(.*\)".*/\1/p' "$ROOT/version.h")
ARCH=$(dpkg --print-architecture)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
PKG=$WORK/cabby_${VERSION}_$ARCH

# built afresh, for /usr
SRC=$WORK/src
mkdir -p "$SRC"
(cd "$ROOT" && tar cf - --exclude='./build' --exclude='./bench/build' --exclude='*.o' --exclude='./lib/libcabby*' \
	--exclude='./lib/*.link' --exclude='./lib/gtk.hby' --exclude='./lib/tgame.hby' --exclude='./lib/sdlgui.hby' \
	--exclude='./packaging/*/out' --exclude='./machine-results.txt' .) | (cd "$SRC" && tar xf -)
make -C "$SRC" PREFIX=/usr -j"$(nproc)"
make -C "$SRC" install install-extras PREFIX=/usr DESTDIR="$PKG"
install -Dm644 "$SRC/LICENSE" "$PKG/usr/share/doc/cabby/copyright"

# what it needs: what the programs and libraries are linked with, and clang
# for `cabby -c`; the libraries that use SDL, GTK or FFmpeg are only loaded
# when a program asks for them, so what they're linked with is recommended
mkdir -p "$WORK/debian"
printf 'Source: cabby\n\nPackage: cabby\nArchitecture: any\n' > "$WORK/debian/control"
needs() {
	(cd "$WORK" && dpkg-shlibdeps --ignore-missing-info -l"$PKG/usr/lib/cabby" -O "$@" 2> /dev/null | sed -n 's/^shlibs:Depends=//p')
}
DEPENDS=$(needs "$PKG/usr/bin/cabby")
# (one at a time: a library built against something not from a package,
# SDL built by hand, say, is left out rather than spoiling the rest)
RECOMMENDS=$(for f in "$PKG"/usr/lib/cabby/*.so; do needs "$f" | tr ',' '\n'; done \
	| sed 's/^ *//; /^$/d; /^libc6 /d' | sort -u | paste -sd, | sed 's/,/, /g')
DEPENDS="${DEPENDS:+$DEPENDS, }clang"
RECOMMENDS="${RECOMMENDS:+$RECOMMENDS, }libsqlite3-0, libssl3 | libssl3t64, libcurl4 | libcurl4t64"
SIZE=$(du -sk "$PKG/usr" | cut -f1)

mkdir -p "$PKG/DEBIAN"
cat > "$PKG/DEBIAN/control" <<CONTROL
Package: cabby
Version: $VERSION
Architecture: $ARCH
Maintainer: Thomas Paul Whalan
Installed-Size: $SIZE
Depends: $DEPENDS
Recommends: $RECOMMENDS
Suggests: libpq5, espeak-ng, cups-client, bluez, dbus, xvfb
Section: devel
Priority: optional
Description: a natively compiled, C-like programming language
 Cabby is tab-indented, with objects, multiple inheritance and ownership
 as Rust has it. It runs programs straight away (an interpreter, with an
 interactive session), or compiles them through LLVM. Its libraries cover
 files, windows, games, 3D, sound, videos, networks, email, databases,
 the desktop and more.
CONTROL

mkdir -p "$ROOT/packaging/debian/out"
dpkg-deb --root-owner-group --build "$PKG" "$ROOT/packaging/debian/out/" > /dev/null
echo "built: $ROOT/packaging/debian/out/cabby_${VERSION}_$ARCH.deb"
echo "install it with: sudo apt install $ROOT/packaging/debian/out/cabby_${VERSION}_$ARCH.deb"
