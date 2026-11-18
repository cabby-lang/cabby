# archives — zip files, tar files, and gzipped and xz files

```cabby
use archives
```

Many files and folders in one file, squeezed smaller: to send, to back
up, or to unpack something downloaded.

```cabby
use archives

int main {
	archives.make "lessons.zip", {"lessons", "notes.txt"}     // a folder, and a file
	println archives.list("lessons.zip")
	println archives.read("lessons.zip", "notes.txt")         // one file, as text
	archives.unpack "lessons.zip", "unpacked"                 // everything, into a folder
}
```

| Function | What it does |
|---|---|
| `void archives.make const str &Archive, const list str &Paths` | Makes the archive, of those files and folders (a folder: everything in it). Its kind is by its name: `.zip`, `.tar`, `.tar.gz` (or `.tgz`), or `.tar.xz` (or `.txz`). |
| `void archives.unpack const str &Archive, const str &Folder` | Everything in it, into the folder (made if it isn't there). Files that are there already are replaced. |
| `list str archives.list const str &Archive` | The names of what's in it, in order. A folder's name ends with `/`. |
| `str archives.read const str &Archive, const str &Name` | One file from inside, as text (or any bytes: a `str` holds them), without unpacking the rest. |
| `void archives.compress const str &Path` | The file gzipped, as `Path.gz` beside it: `"big.log"` as `"big.log.gz"`. The file itself stays as it is. |
| `void archives.compress_xz const str &Path` | The file squeezed with xz, as `Path.xz`: smaller than gzip's, slower to make. |
| `void archives.decompress const str &Path` | A gzipped or xz file back as it was: `"big.log.gz"` (or `.xz`) as `"big.log"`. |
| `void archives.compress_to const str &Path, const str &To`, `void archives.decompress_to const str &Path, const str &To` | The same, to another name (`compress_to` squeezes with xz for a name ending `.xz`, and with gzip otherwise). |

A file that isn't there, an archive that's broken, or one that isn't an
archive at all, is a panic at the line that asked, saying which.

## Passwords

A zip can be kept with a password:

<!-- check -->
```cabby
use archives

int main {
	archives.make_with_password "private.zip", {"diary.txt"}, "correct horse battery"
	println archives.needs_password("private.zip")     // true
	archives.unpack_with_password "private.zip", "out", "correct horse battery"
}
```

| Function | What it does |
|---|---|
| `void archives.make_with_password const str &Archive, const list str &Paths, const str &Password` | Makes a zip whose files are encrypted with the password. (Only a zip: a tar file has no way to keep one.) |
| `void archives.unpack_with_password const str &Archive, const str &Folder, const str &Password` | Everything in it, into the folder. |
| `str archives.read_with_password const str &Archive, const str &Name, const str &Password` | One file from inside, as text. |
| `bool archives.needs_password const str &Archive` | `true` if anything in it is kept with a password. |

A wrong password is a panic that says so ("the password for 'private.zip'
is wrong"), and so is unpacking a zip with a password without one. The
names of what's in a zip, and their sizes, aren't secret: `archives.list`
reads them without the password, as every zip program does.

Each file is encrypted the way WinZip made usual, and 7-Zip, WinZip,
PeaZip and most zip programs read: AES-256, in counter mode, with a key
made from the password by PBKDF2 (1000 rounds of HMAC-SHA1, and 16 random
bytes of salt for each file), and an HMAC-SHA1 of it at its end, so a
file that's been changed, by accident or not, is caught as it's read.
The Mac's Archive Utility and Linux's `unzip` can't open these (they know
only the old way); 7-Zip (`7z`, `p7zip`) and `bsdtar` can.

Read, as well: AES-128 and AES-192, and PKZIP's old "ZipCrypto", which is
what `zip -e` and older programs make. ZipCrypto is weak (it can be
broken, given time), so Cabby reads it but never makes it.

AES and SHA-1 use the processor's own instructions where it has them
(Intel's and AMD's AES-NI and SHA extensions, and Apple's processors'),
checked against the standards' own examples first, so a big zip with a
password is made or unpacked nearly as quickly as one without.
`CABBY_AES_HARDWARE=none` and `CABBY_SHA_HARDWARE=none` use Cabby's own,
written from the standards, instead.

## Names inside

Each file and folder given to `make` is named in the archive as it's
written: `"lessons"` is `lessons/` and `lessons/ada.txt` and so on, and
`"notes/2026/nov.txt"` keeps its folders. One given from somewhere else on
the computer, `"/home/ada/notes"` or `"../notes"`, is named by its last
part, `notes/`.

**Unpacking is safe.** A name inside an archive that would go outside the
folder it's unpacked into (one starting with `/`, or with a `..` part, as
an archive made to do harm might have) is a panic, and nothing of it is
written.

## Which kind

| Kind | Good for |
|---|---|
| `.zip` | sending to anyone: every computer opens them. Each file is squeezed on its own, so one can be read without the rest. |
| `.tar.gz` | backups, and Linux: the files one after another, squeezed together, which squeezes better. Permissions are kept. |
| `.tar.xz` | the same, squeezed smaller still (as Linux's own source and packages are), but slower to make |
| `.tar` | the same, not squeezed |
| `.gz`, `.xz` | one file, squeezed (`compress`, `compress_xz` and `decompress`) |

Every kind is read and written a piece at a time, so a file of any size
goes in and comes out without having to fit in memory. A zip past 4 GB (a
file in it, or the whole zip, or more than 65,535 files) is written with
zip64's records, which hold the numbers that are too big for zip's own,
as every zip program since 2001 reads; and zip64 files are read too.

What's read is known by what's in it, not by its name.

## How it works

Squeezing (deflate) is zlib's, which every Linux system and Mac has, and
xz's is liblzma's (the `xz` package: xz-utils on Debian, `brew install
xz` on a Mac, if it isn't there): each is loaded the first time it's
needed. If one isn't there, the panic says what to install. The formats around it
are Cabby's own (`runtime/archives.c`), from their specifications:
PKWARE's for zip, and POSIX's ustar for tar, with GNU's long names (read
and written) and POSIX's (read).

## Not yet

`.7z` files, and `.zst` (zstd) files. Not `.rar`: its format isn't open.
