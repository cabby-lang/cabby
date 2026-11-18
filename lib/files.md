# files — reading and writing files

```cabby
use files
```

Whole files in one line each: read one into a `str` or a `list str` of
its lines, write or add to one, and copy, rename and delete them.
Directories have their own library, [directories](directories.md).

| Call | What it does |
|---|---|
| `files.read Path` | The whole file, as a `str`. |
| `files.lines Path` | The file's lines, as a `list str`, without their line ends. A last line with no line end still counts; an empty file gives an empty list. Windows line ends (`\r\n`) are taken off too. |
| `files.write Path, Text` | The file becomes `Text`, made if it's new. |
| `files.append Path, Text` | `Text` goes on the end of the file, made if it's new. Add `"\n"` yourself to end a line. |
| `files.exists Path` | `true` if a file is there (a directory doesn't count). |
| `files.size Path` | The file's size in bytes, as a `long`. |
| `files.copy From, To` | `To` becomes a copy of `From`, replacing any file already there. |
| `files.rename From, To` | A new name, or a new place (`"old/notes.txt"`). Works across disks too, by copying and deleting. |
| `files.delete Path` | Deletes the file. |
| `files.read_bytes Path` | The whole file, as [bytes](bytes.md): for files that aren't text (pictures, sounds, a save file). |
| `files.write_bytes Path, Bytes` | The file becomes those bytes, made if it's new. A `str` works too. |
| `files.append_bytes Path, Bytes` | The bytes go on the end of the file, made if it's new. |
| `files.open Path` | Opens the file for reading, a line at a time: gives a `file`. |
| `files.open_to_write Path` | Opens the file to write into, starting it afresh (made if it's new): gives a `file`. |
| `files.open_to_add Path` | Opens the file to write onto the end of (made if it's new): gives a `file`. |
| `files.open_to_change Path` | Opens a file that's there to read *and* write, anywhere in it: gives a `file`. Nothing in it is lost until it's written over. |
| `F.read_line` | The next line of a file opened with `files.open`, without its line end. After the last line it gives `""` once; reading again is a panic. |
| `F.at_end` | `true` when there is nothing more to read. |
| `F.read_bytes Count`, `F.write_bytes Bytes`, `F.position`, `F.move_to Position`, `F.move_to_end`, `F.size` | Bytes, and moving around in a file: see [below](#bytes-in-an-open-file). |
| `F some_call` | Everything `some_call` prints goes into `F` (opened to write or add), as `stderr some_call` sends it to stderr; the call still gives back its value. `F println "..."` writes a line. |

Every path and text is a `str`. A path is relative to the directory the
program is in (`directories.current`), or starts with `/`.

## When something goes wrong

A missing file, a directory where a file was expected, a disk that's full:
each is a panic that says what and why, the same way a missing key in a
lookup is.

```text
panic at prog.cby:4:16: can't read 'notes.txt': no such file or directory
```

When a file might not be there, check first:

<!-- check -->
```cabby
use files

int main {
	if files.exists "settings.conf" {
		for Line in files.lines "settings.conf" {
			println Line
		}
	} else {
		println "no settings yet"
	}
}
```

or say what to do if it fails, with `else` after the statement (README,
"When something fails"): the panic is caught, and not shown.

<!-- run -->
```cabby
use files

int main {
	str Settings = files.read "cabby-no-settings.conf" else "no settings yet"
	println Settings
	files.delete "cabby-no-settings.conf" else Why {
		println "not deleted:", Why
	}
}
```

```text
no settings yet
not deleted: can't delete 'cabby-no-settings.conf': no such file or directory
```

## Writing, adding and reading back

<!-- run -->
```cabby
use files

int main {
	files.write "cabby-example.txt", "apples 3\npears 5\n"
	files.append "cabby-example.txt", "plums 2\n"
	println files.size "cabby-example.txt"
	for Line in files.lines "cabby-example.txt" {
		list str Parts = Line.split " "
		println Parts[0], "x", Parts[1].to_int
	}
	str All = files.read "cabby-example.txt"
	println All.length, All.ends_with "\n"
	files.delete "cabby-example.txt"
	println files.exists "cabby-example.txt"
}
```

```text
25
apples x 3
pears x 5
plums x 2
25 true
false
```

## Copying and renaming

<!-- run -->
```cabby
use files

int main {
	files.write "cabby-first.txt", "hello"
	files.copy "cabby-first.txt", "cabby-second.txt"
	files.rename "cabby-second.txt", "cabby-third.txt"
	println files.exists("cabby-second.txt"), files.read "cabby-third.txt"
	files.delete "cabby-first.txt"
	files.delete "cabby-third.txt"
}
```

```text
false hello
```

## Open files

A `file` is closed when it's dropped (at the end of its block, or when
the object holding it is dropped), so it can never be left open. It is
moved, not copied; pass it to a function as `file &Log`. A `file`
declared without a value isn't open, and using it is a panic.

<!-- run -->
```cabby
use files

object logger {
	file Out

	void start const str &Path {
		Out = files.open_to_write Path
		Out println "started"
	}

	void note const str &Text {
		Out println "note:", Text
	}
}

int count_lines const str &Path {
	file In = files.open Path
	int Count = 0
	while !In.at_end {
		str Line = In.read_line
		Count++
	}
	return Count
}

int main {
	{
		logger L
		L.start "cabby-log.txt"
		L.note "one"
		L.note "two"
	}                                   // L is dropped: the file is closed
	println count_lines("cabby-log.txt"), "lines"
	files.delete "cabby-log.txt"
}
```

```text
3 lines
```

What's printed into a file is written out in large pieces rather than
line by line, which keeps writing fast. It is all written out:

* when the file is closed;
* before the `files` library reads any file, so a program always reads
  back what it has written, and before a `shell` command runs;
* whenever the program waits: for `read_line` or `input`, between a
  window's handlers, and after each line in the interactive session. So
  another program (`cat`, `tail -f`) sees it while the program is still
  running.

## Bytes in an open file

Some files are made of parts of a fixed size, so a program can go
straight to the part it wants: a save file with a slot for each player, a
picture's header, a database. Reading the whole file and writing it all
back would work, but for a big file it's slow, and for a huge one
impossible. Instead, open it with `files.open_to_change`, move to the
place, and read or write just those bytes.

| Write | What it does |
|---|---|
| `F.read_bytes Count` | the next `Count` bytes, as [bytes](bytes.md); fewer at the end of the file, none after it |
| `F.write_bytes Bytes` | writes them where the file is, over what's there (or onto the end) |
| `F.position` | where the next read or write happens, in bytes from the start (a `long`) |
| `F.move_to Position` | goes there: 0 is the start, `F.size` the end; outside that is a panic |
| `F.move_to_end` | goes to the end, to add to the file |
| `F.size` | how many bytes the file has now |

Reading and writing both move the position on by as many bytes as they
read or wrote, so to change bytes just read, move back first. `read_line`,
`at_end` and `F println` work in the same file, at the same position.
`read_bytes` and `write_bytes` also work in files opened the other ways:
reading with `files.open`, writing with `files.open_to_write` and
`files.open_to_add` (which always writes at the end).

<!-- run -->
```cabby
use files

int main {
	files.write "cabby-scores.dat", "Ada  10\nTom  20\n"
	file F = files.open_to_change "cabby-scores.dat"
	println F.size, "bytes"
	F.move_to 13                  // Tom's score: 8 bytes a line, then 5 in
	println F.read_bytes(2).str
	F.move_to 13
	F.write_bytes "99"            // a str's bytes
	println F.position
	F.move_to 0
	println F.read_line, F.read_line
	files.delete "cabby-scores.dat"
}
```

```text
16 bytes
20
15
Ada  10 Tom  99
```

