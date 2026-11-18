# Installing Cabby as a package

Each script builds a package from this folder, for the version in
`version.h`, so the system's package manager can install it, upgrade it
and take it away again. They're run from anywhere, and leave what they
make in an `out` folder next to them.

| System | Build it with | Install it with | Remove it with |
|---|---|---|---|
| Arch | `packaging/arch/build.sh` | `sudo pacman -U packaging/arch/out/cabby-*.pkg.tar.zst` | `sudo pacman -R cabby` |
| Debian | `packaging/debian/build-deb.sh` | `sudo apt install ./packaging/debian/out/cabby_*.deb` | `sudo apt remove cabby` |
| Mac | `packaging/homebrew/build-tap.sh` | (it installs it too) | `brew uninstall cabby` |

Everything goes under `/usr` (Arch and Debian) or Homebrew's own folders
(the Mac):

- `cabby` itself
- the headers, runtime and libraries, in `lib/cabby`
- the manual and tutorials, in `share/doc/cabby`
- the Vim files, in `share/vim/vimfiles`, which Vim finds by itself on Arch
  and Debian. On a Mac, add the line the install prints to `~/.vimrc`.
- on Arch and Debian, the file icons and the `.cby` and `.hby` file types

## Arch

`build.sh` needs `base-devel`. It makes the source tarball, fills in the
`PKGBUILD`'s version, and runs `makepkg`. `makepkg` builds Cabby and runs
the program and tutorial tests (`--nocheck` skips them). Anything given to
`build.sh` goes to `makepkg`: `packaging/arch/build.sh -i` installs the
package as well.

The `PKGBUILD` also works on its own, next to a `cabby-X.Y.Z.tar.gz` with a
`cabby` folder inside (as the release tarballs have).

## Debian

`build-deb.sh` needs what building Cabby needs, plus `dpkg-dev`:

    sudo apt install build-essential dpkg-dev pkg-config libffi-dev clang

The package's dependencies come from what it's linked with:

- **Depends:** libffi, and clang for `cabby -c`.
- **Recommends:** SDL 3, GTK 4 and FFmpeg, if they were installed when it
  was built, and SQLite, OpenSSL and curl. The libraries that use them are
  only loaded when a program asks for them.
- **Suggests:** the rest (PostgreSQL, eSpeak NG, CUPS, BlueZ).

## Mac

`build-tap.sh` needs Homebrew. It makes the source tarball and a formula
for it, `out/cabby.rb`, and puts the formula in a tap of its own,
`cabby/local`. Then it installs Cabby from there and runs the formula's
test, which runs a program and compiles it.

- SDL 3 comes with it.
- FFmpeg and GTK 4 are optional:
  `brew reinstall cabby/local/cabby --with-ffmpeg`.
- `--no-install` only makes the formula.
- `brew untap cabby/local` takes the tap away again, after
  `brew uninstall cabby`.

Compiling with `cabby -c` uses the clang of Apple's Command Line Tools
(`xcode-select --install`).

On a Mac with an Apple processor, it uses the Homebrew for that processor
(`/opt/homebrew`), and builds for it even from a terminal opened with
Rosetta. An Intel Homebrew (`/usr/local`) on such a Mac would build an
Intel Cabby against the Apple processor's libraries, which can't be
linked, so it says so instead.

## Without a package

`sudo make install` still works on all three, into `/usr/local`, and
`sudo make uninstall` takes it away (README §14).
