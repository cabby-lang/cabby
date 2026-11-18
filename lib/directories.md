# directories — what's in a directory, and making and changing them

```cabby
use directories
```

Listing what a directory holds, making and removing directories, and
moving between them. The files in them are read and written with the
[files](files.md) library.

| Call | What it does |
|---|---|
| `directories.list Path` | The names of everything in the directory (files and directories, hidden ones too), sorted, as a `list str`. Just the names: `"a.txt"`, not `"photos/a.txt"`. |
| `directories.all_files Path` | Every file below the directory, in it and in the directories in it, as paths from it (`"2026/beach.jpg"`), sorted. Directories themselves aren't listed. |
| `directories.exists Path` | `true` if a directory is there (a file doesn't count). |
| `directories.make Path` | Makes the directory, and any missing directory above it (`"backup/2026/nov"`). A directory that's already there is fine. |
| `directories.remove Path` | Removes the directory, which must be empty. |
| `directories.rename From, To` | A new name, or a new place. |
| `directories.current` | The directory the program is in, as a full path. Relative paths start here. |
| `directories.change_to Path` | Moves the program to another directory. |
| `directories.home` | Your home directory, as a full path. |
| `directories.open Path` | The directory, held open as a `directory` (below). |
| `directories.save_folder Name` | Where a program keeps what it saves (saved games, options): the place the computer has for that, by the program's name, made if it isn't there. `~/.local/share/My Game` on Linux, `~/Library/Application Support/My Game` on a Mac. |

As with files, anything that goes wrong is a panic that says why (`can't
remove 'backup': it isn't empty (delete what's in it first)`); check first
with `directories.exists` or `files.exists` when you aren't sure.

## Going through a directory

`directories.list` gives names, so join them to the directory's path to use
them. `files.exists` and `directories.exists` tell files and directories
apart:

<!-- run -->
```cabby
use files
use directories

int main {
	directories.make "cabby-example/pictures"
	files.write "cabby-example/notes.txt", "hello"
	files.write "cabby-example/pictures/cat.png", ""
	for Name in directories.list "cabby-example" {
		str Path = "cabby-example/" + Name
		if directories.exists Path {
			println Name, "(a directory)"
		} else {
			println Name, files.size(Path), "bytes"
		}
	}
	println directories.all_files "cabby-example"

	files.delete "cabby-example/pictures/cat.png"
	files.delete "cabby-example/notes.txt"
	directories.remove "cabby-example/pictures"
	directories.remove "cabby-example"
	println directories.exists "cabby-example"
}
```

```text
notes.txt 5 bytes
pictures (a directory)
[notes.txt, pictures/cat.png]
false
```

## Where the program is

<!-- check -->
```cabby
use directories

int main {
	str Start = directories.current
	directories.change_to directories.home
	println "now in", directories.current
	directories.change_to Start
}
```

## A directory held open

`directories.open` gives a `directory`: the directory opened once, and
closed when the variable is dropped, as a [file](files.md) is. It stays
the same directory however it's renamed or moved, and wherever the
program changes directory to, and what's in it is reached from it, by
name, rather than by a path:

<!-- run -->
```cabby
use files
use directories

int main {
	directories.make "cabby-held"
	directory Held = directories.open "cabby-held"
	Held.write "notes.txt", "first"
	Held.add_to "notes.txt", ", second"
	directories.rename "cabby-held", "cabby-moved"
	println Held.read("notes.txt"), Held.path.ends_with("cabby-moved")
	while Held.next {
		println Held.name, Held.size(Held.name), "bytes"
	}
	Held.remove "notes.txt"
	directories.remove "cabby-moved"
}
```

```text
first, second true
notes.txt 13 bytes
```

| `directory` | What it does |
|---|---|
| `D.path` | Where it is now, as a full path. |
| `D.next` | Goes to the next thing in it: `true`, or `false` at the end. They come in no particular order, and `.` and `..` are left out. |
| `D.name` | The name `next` stopped at. |
| `D.rewind` | Back to the start, for `next` (and what's in it now). |
| `D.names` | The names of everything in it, sorted. |
| `D.has Name`, `D.is_file Name`, `D.is_directory Name` | Whether there's something by that name in it, and what. |
| `D.size Name`, `D.changed Name` | A file's size in bytes; when it was last changed, in seconds since 1970. |
| `D.path_of Name` | Its path, from where the directory is now. |
| `D.read Name`, `D.write Name, Text`, `D.add_to Name, Text` | A file in it, read whole, written, or added to. |
| `D.make Name`, `D.remove Name`, `D.rename From, To` | A directory made in it; a file or empty directory removed; something in it renamed. |
| `D.open_inside Name` | A directory in it, held as one of its own. |

A name is just a name in the directory: `"notes.txt"`, not a path such as
`"../notes.txt"` or `"2026/notes.txt"` (that's a panic that says so), so
nothing outside it is reached by accident. `open_inside` goes further
down, a directory at a time.

