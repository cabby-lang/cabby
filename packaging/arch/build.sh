#!/usr/bin/env bash
# Builds Cabby's Arch package from this folder: makes the tarball, fills in
# the version, and runs makepkg. Then: sudo pacman -U cabby-*.pkg.tar.zst
#     packaging/arch/build.sh            (or with -i, to install it too)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=$(sed -n 's/.*CABBY_VERSION "\(.*\)".*/\1/p' "$ROOT/version.h")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
(cd "$ROOT/.." && tar czf "$WORK/cabby-$VERSION.tar.gz" --exclude='*/build' --exclude='*/bench/build' \
	--exclude='*.o' --exclude='*/lib/libcabby*' --exclude='*/lib/*.link' --exclude='*/lib/gtk.hby' \
	--exclude='*/lib/tgame.hby' --exclude='*/lib/sdlgui.hby' --exclude='__pycache__' \
	--exclude='*/machine-results.txt' --exclude='*/packaging/*/out' \
	--transform "s,^$(basename "$ROOT"),cabby," "$(basename "$ROOT")")
sed "s/@VERSION@/$VERSION/" "$ROOT/packaging/arch/PKGBUILD" > "$WORK/PKGBUILD"
(cd "$WORK" && makepkg -f "$@")
mkdir -p "$ROOT/packaging/arch/out"
cp "$WORK"/cabby-*.pkg.tar.* "$ROOT/packaging/arch/out/"
echo "built: $(ls "$ROOT"/packaging/arch/out/cabby-"$VERSION"-*.pkg.tar.*)"
