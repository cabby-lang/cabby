# mime — what kind of file a file is

```cabby
use mime
```

A file's kind as a MIME type, the way the web and the desktop name them:
`"image/png"`, `"text/plain"`, `"application/pdf"`. The same on Linux and
on a Mac.

<!-- check -->
```cabby
use mime
use directories

int main {
	for Name in directories.list "." {
		if mime.is_image Name {
			println Name, "is a picture:", mime.of_file(Name)
		}
	}
	println mime.of_name("song.flac")
	return 0
}
```

| Call | What it says |
|---|---|
| `mime.of_file Path` | the file's kind, by what's in it |
| `mime.of_name Name` | the usual kind for a name's ending, without looking inside (`""` for one it doesn't know) |
| `mime.is_image Path` | `true` for a picture (`image/...`) |
| `mime.is_text Path` | `true` for text: `text/...`, and JSON, XML, SVG, YAML and TOML |
| `mime.is_audio Path` | `true` for sound (`audio/...`) |
| `mime.is_video Path` | `true` for video (`video/...`) |

`mime.of_file` looks at how the file starts: a PNG starts with
`\x89PNG`, a PDF with `%PDF-`, a gzipped file with `\x1f\x8b`, and so on,
so a file with the wrong ending, or none, is still told right. For text,
and for zip files (which `.docx`, `.xlsx`, `.epub` and `.jar` files are
too), its ending then says which kind: `notes.md` is `text/markdown`,
`report.docx` a Word document.

What it can say besides a kind:

| | |
|---|---|
| `"application/octet-stream"` | bytes it doesn't know (not text either) |
| `"text/plain"` | text, with no ending that says more |
| `"inode/directory"` | a folder |
| `""` | there's nothing there |

It knows pictures (PNG, JPEG, GIF, WebP, BMP, TIFF, HEIC, AVIF, SVG,
icons), sound (MP3, FLAC, Ogg, WAV, MIDI, AAC), video (MP4, QuickTime,
WebM, Matroska, AVI), archives (zip, tar, gzip, bzip2, xz, zstd, 7z),
PDF, SQLite databases, fonts, programs (Linux's ELF and a Mac's Mach-O),
scripts by their `#!` line, and the usual kinds of text.
