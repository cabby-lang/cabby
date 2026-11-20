# Cabby: a timeline

What was added to Cabby, and when: only what was kept. Where an idea was
tried and then replaced, only the change to what stayed is shown (for
example, `typedef` becoming `aka`).

**About the dates.** From 30 October on, they come from the dated notes in
`Goals.md` and `bench/README.md`, written down as each thing was done.
The first few days have no notes, so their dates come from when the
oldest files in the source folder were last saved. They're right to within
a day or so, and some of that work may be older than the files show. A
few things have no record of when they happened at all; they're listed at
the end.

## Versions

Versions are numbered from 14 November: `cabby --version` says which one
is installed. The minor number goes up with each stage of new things, and
the patch number with fixes alone.

| Version | Date | What it brought |
|---|---|---|
| 0.1.0 | 14 November | everything up to the end of 14 November: the first numbered |
| 0.2.0 | 15 November | named time zones, other languages and looser text for dates; zip64 and xz for archives |
| 0.2.1 | 15 November | a fix: the interactive session could crash after a panic inside a function; and `cabby --version` |
| 0.2.2 | 15 November | a fix: on a Mac, two tests of the system library failed, over the Mac's `.local` hostname |
| 0.2.3 | 15 November | a fix to the tests: two runs at once (at the Mac and over ssh) shared one folder for saved games |
| 0.3.0 | 15 November | networks: `POST` and every other method, with headers; progress for big transfers; Cabby programs talking to each other, with connections and servers |
| 0.3.1 | 15 November | the MIT License, with Thomas Paul Whalan's copyright: `LICENSE`, `cabby --license`, and a no-warranty line in `--version` and at the session's start |
| 0.4.0 | 15 November | sound: MIDI keyboards (played through an instrument, and told to handlers) and synthesizers; hearing a recording as it's made; sound from workers |
| 0.4.1 | 15 November | a fix: `listen_here` listened at IPv6's address alone where there is one, so a connection to 127.0.0.1 was refused (on Debian and the Mac); and the networks tests on a Mac without `timeout` |
| 0.5.0 | 15 November | pictures: `use images`, shown on a canvas, and painted by workers and handed to a window whole; tab completion for `cabby.version` and `cabby.license` |
| 0.6.0 | 15 November | text in windows in any language: right to left (Hebrew, Arabic), joined (Arabic), shaped by HarfBuzz; emoji in color |
| 0.6.1 | 15 November | a fix to the tests: the text tests ran on a Mac without `timeout` |
| 0.6.2 | 15 November | a fix to the tests: the text tests' lines are sorted by bytes, as a Mac sorts them differently |
| 0.7.0 | 15 November | zip files with a password: AES-256 made, AES and ZipCrypto read; AES and SHA-1 on the processor's own instructions |
| 0.7.1 | 15 November | an unknown name that's a library's (`archives` without `use archives`) says to add its `use` line |
| 0.8.0 | 16 November | making users and groups: `users.make`, `make_admin`, `set_password`, `remove`, groups, `users.all` |
| 0.8.1 | 16 November | a fix: on a Mac, a user just removed still showed as there (the directory service's cache) |
| 0.9.0 | 16 November | screen readers: gui's windows read aloud on Linux (AT-SPI), `set_description` |
| 0.10.0 | 16 November | 3D: `use world`, shapes, light, a camera, text over it; OpenGL, or Cabby's own drawing; tutorial 87 |
| 0.11.0 | 16 November | models from Blender: `add_model` (glTF), their pictures, bones and animations; tutorial 88 |
| 0.12.0 | 16 November | Bluetooth on Linux: `use bluetooth`, devices, searching, pairing, connecting, a watcher |
| 0.13.0 | 16 November | Bluetooth on a Mac: the same library, through the Mac's IOBluetooth |
| 0.14.0 | 16 November | a web server: `networks.web_server`, pages, JSON, files, forms and cookies |
| 0.15.0 | 16 November | email: `use emails`, messages with HTML and attachments, sent by SMTP |
| 0.16.0 | 16 November | secrets: `use secrets`, random codes, password hashes, locking with a password, signatures |
| 0.17.0 | 16 November | the small libraries: `use brightness`, `use printing`, `use speech`, and `system.set_hostname` |
| 0.18.0 | 16 November | videos: `use videos` on FFmpeg, and gui's `add_video`, with its sound and controls |
| 0.19.0 | 16 November | WebSockets, and HTTPS, `wss://` and encrypted connections of Cabby's own (TLS, through OpenSSL) |
| 0.20.0 | 16 November | `use os` (watching files, signals, disks, processors and memory, programs) and `use linux` (sensors, disks and partitions, USB, modules, settings, services) |
| 0.20.1 | 16 November | encrypted connections and secrets find Homebrew's OpenSSL 4 (or whichever "openssl" it has), not only 3 |
| 0.21.0 | 16 November | pictures changed: scaled, cropped, turned, mirrored, gray, blurred; and text on them |
| 0.22.0 | 16 November | reading email: `emails.mailbox` (IMAP) and `emails.received` |
| 0.23.0 | 16 November | speech heard: `speech.hear`, `hear_for` (Whisper, by whisper.cpp), and `sound.convert_to_wav` |
| 0.24.0 | 16 November | the smaller library items: patterns that look behind and name parts, public keys and Argon2, PostgreSQL, a `directory` held open, programs run with their arguments as a list, a process's errors apart, saving email to a folder, Greek months after a day |
| 0.24.1 | 16 November | a test that relied on how Linux's `ls` ends when it fails (a Mac's ends differently) |
| 0.24.2 | 16 November | on a Mac, a program's `command` is how it was started, its arguments too, as on Linux |
| 0.24.3 | 16 November | built without warnings again (Debian's and the Mac's), and shell.md's example on a Mac |
| 0.24.4 | 16 November | Tab after a library's name completes before its `use` too (`bluetooth.`) |
| 0.25.0 | 16 November | `else` after a statement: a panic caught where it happens (`files.read "notes.txt" else ""`, `else Why { }`); tutorial 90 |
| 0.25.1 | 17 November | `else` drops what the failed calls had made, destructors and all, in compiled programs as in the interpreter |
| 0.26.0 | 17 November | Cabby's own for what was `standard`'s: `Price.text 2`, `random 1 to 6`, `random_seed`, `abs` for every number, `exit`; `printf` follows `stderr` |
| 0.26.1 | 17 November | checks for each computer, of what the tests can only stand in for: `tests/machines/debian.sh`, `arch.sh` and `mac.sh` |
| 0.27.0 | 17 November | a base object's list (or variable) holds what's made from it, each still itself; `extended` and `quadruple` reserved |
| 0.28.0 | 17 November | install packages: an Arch package, a Debian `.deb` and a Homebrew formula, each built from the folder by one script |
| 0.28.1 | 17 November | gtk: a menu's shortcut works in a window with `on_key` too |
| 0.28.2 | 17 November | `CABBY_SOUND_REPORT`: what a video's sound did, for the Mac, where it couldn't be heard |
| 0.28.3 | 17 November | `os.disks` leaves out shares mounted in other people's home folders (they showed 0 GB) |
| 0.28.4 | 17 November | the test video beeps, the same in both channels, so a one-speaker Mac mini plays it |
| 0.29.0 | 18 November | the tutorials in a new order, each using only what came before it: fifteen parts, renumbered |
| 0.29.1 | 18 November | the Homebrew package uses the Apple processor's Homebrew, never an Intel one through Rosetta |
| 0.30.0 | 18 November | "More examples" in 26 chapters: what the libraries' tables list but the chapters didn't show |
| 0.31.0 | 18 November | `pad_left`, `pad_right` and `centered`; a number too big for a `long` is an `unsigned long`; the manual says which Cabby it's for |
| 0.32.0 | 18 November | sixteen new chapters (91 to 106): servers, a web server, WebSockets, encryption, email, the computer, and the desktop; one field read while another is lent |
| 0.33.0 | 18 November | a read-through: changing files in place after bytes, every chapter with "In short" and "Try this", links and appendices put right |
| 0.33.1 | 19 November | the interactive session: a `/*` comment carries on until its `*/`, and why `print` looks different there |
| 0.33.2 | 19 November | chapter 4 shows how to check that two floating-point numbers are close, with `abs` |
| 0.33.3 | 19 November | in the interactive session, a block comment's lines start ` * `, as in Vim |
| 0.34.0 | 19 November | `close_to`, and a warning for `==` on rounded floating-point numbers; chapter 6's comparisons and logic in two tables |
| 0.35.0 | 19 November | `use screens`, `use mouse` and `use keyboard`: the monitors, the pointer and cursor, and the layouts, each desktop's way; chapter 107 |
| 0.35.1 | 19 November | the interactive session: a comment after `{` no longer stops the next line's indent, and indentation messages count only the tabs typed |
| 0.36.0 | 19 November | conditions in symbols only: `is`, `is not`, `and`, `or` and `not` are taken out, for `==`, `!=`, `&&`, `\|\|` and `!` |
| 0.36.1 | 19 November | `keyboard.layouts` on Sway stopped when the tests' own list of layouts wasn't named |
| 0.37.0 | 20 November | who's on the network (`answering`, `pings`); a file's own name as a prefix for its functions; chapters 33, 34 and 51 put right |

---

## Late October 2026: the beginnings

### 27 October (or a little before): the language itself

- **Cabby**: a compiled language written in C, inspired by C. A single
  program, `cabby`, holds both a compiler and an interpreter. Programs
  are `.cby` files and headers are `.hby` files. (`.cab` was passed over:
  it's Microsoft's Cabinet format. The program was first `lang_cli`, then
  `cabbyc`, and finally just `cabby`.)
- Blocks are marked by indentation, with tabs only, and there are no
  semicolons. Control flow needs no parentheses: `if X > 3 {`.
- `&&` for "and" and `&` for bits are kept firmly apart.
- **Objects** with methods and **multiple inheritance**. In a diamond,
  the shared parent is one copy, as in Python, not two, as in C++.
- **Ownership**, after Rust: values are moved and dropped (freed)
  automatically, and this is checked before the program runs. References
  (`T &`, `const T &`) are borrows that can't be returned or kept in
  fields.
- String literals can be used as borrowed `&str`, or copied into an owned
  `str`.
- Interpreting is the default (`cabby prog.cby`). `-c` compiles to a
  native program through LLVM.

### 28 October: the interactive session

- Running `cabby` with no file starts an interactive session, as Python
  does. On leaving, it offers to save what was typed as a program.
- The first tests: the session's own (tests/repl), and programs split
  into several files.

### 29 October: Vim, and programs in several files

- Vim support: syntax highlighting, auto-indenting, and recognizing
  `.cby` files.
- Headers come in with `use` (replacing `#use x.hby`: no `#` and no
  ending needed), and a program's own files with `use local`.

### By the end of October (dates not recorded)

- **Bare calls**: everything after a call's name is its arguments
  (`print (a + b) * 2` prints the product); `name(...)` is simply the
  same call in parentheses. Comparisons inside arguments simply work out
  (`2 == 2` gives `true`).
- **For loops in words**: `for i in n`, `for i in a to b`,
  `for i in a down to b`, `by step`; `in`, `to`, `down` and `by` reserved.
- **String tools as methods**: `.length`, `.copy`.
- **Conditions in words**: `is`, `is not`, `and`, `or`, `not`, beside the
  symbols. Loops: `until` and `do ... until`.
- **`aka`** for another name for a type (`double aka celsius`), in place
  of `typedef`.
- **The gui library** (`use gui`), on GTK4: a window, its title, buttons,
  and handlers in the style of hardware interrupts (`Ok.on_click say_hello`).
  Names are namespaced (`gui.window`) so that `tui` could come later
  without clashing.
- **The tutorials and the manual**: a `.cby` tutorial for each feature,
  gathered into one book, `tutorials/tutorials.md`, with contents and
  links.
- `sudo make install`, so `cabby` runs from anywhere.

---

## 30 October

- **The style guide**: `STYLE.md` and the manual's Appendix J. Types and
  functions are lower case with `_`, variables and fields capitalized
  (`DateOfBirth`), constants in capitals. `cabby --style` checks names,
  and `make test` keeps the tutorials, manual, libraries and benchmarks
  following it.
- **A reference page for each library** (`gui.md`, `math.md`, `strings.md`
  and the rest), installed next to the headers.
- **More of the gui**: check boxes, drop-down lists, multi-line text,
  lists, sliders, progress bars, pictures; dialogs (messages, yes/no
  questions, opening and saving files); columns inside rows, separators,
  graying out and hiding; several windows, and timers.
- **Benchmarks**: seven programs, each written in Cabby, Python, C and Rust
  (start-up, calls, loops and arrays, floating point, sorting, strings,
  objects). Time and memory are measured by `bench/run.py`, and a bug was
  found and fixed along the way.

## 31 October

- **Speed, compiled**: objects allocated with `malloc` and zeroed in
  place (the objects benchmark went from 1.8× C's time to level with C);
  `s[i]` checked and read inline; numbers turned into text without
  `printf`; `a + b + c` joined in one allocation (strings went from 1.8× C
  to level).
- **Speed, interpreted**: local variables come from a reusable stack and
  temporaries are recycled (about 30% fewer instructions in code heavy on
  calls).
- Found: fib's gap to C is gcc's own trick. C compiled with clang, as
  Cabby is, takes the same time as Cabby.
- **Streams**: `stdout` and `stderr`. A stream goes in front of a call
  (`stderr report "disk full", 28`) and catches everything that call
  prints, so functions are written once with plain `println`.
- **Input**: `read_line` (a line, without its Enter) and `input "Name: "`.
- **`list`**: `.add`, `.insert`, `.remove_at`, `.take_last`, `.clear`,
  `.contains`, `.find`, `.copy`, slices `L[a to b]`, and `for X in L`
  (over arrays and strings too).
- **String tools**: `.contains`, `.starts_with`, `.ends_with`, `.find`,
  `.replace`, `.trim`, `.upper`, `.lower`, `.repeat`, slices `s[a to b]`,
  `<` and `>` for dictionary order, `.split` and `.join`.
- `cement` reserved, for build-time choices later.

## 1 November

- **Tab completion** in the interactive session: `files.` then Tab lists
  what's there, as do variables' fields and methods, and partial names.
- **`use tui`**: the gui's windows in a terminal, with the same shape
  (`tui.window`, `build`, handlers), focus, shortcuts, colors and boxes.
  Manual chapter 39.
- **`use files`**: `read`, `lines`, `write`, `append`, `exists`, `size`,
  `copy`, `rename`, `delete`. Files are also streams
  (`files.open_to_add`, `open_to_write`, `open` with `read_line` and
  `at_end`), closed when dropped. A missing file is a panic that says
  why; check first with `files.exists`.
- **`use directories`**: `list`, `all_files` (the whole tree), `exists`,
  `make`, `remove`, `rename`, `current`, `change_to`, `home`.
- **`use shell`**: `shell.run`, `shell.status`, and environment variables.
- **Lists**: printed whole with `println`, `.sort` (numbers, `bool`,
  `char`, `str`), and `.reverse`.
- **`lookup`**, Cabby's dictionary: `lookup str int Ages = {"Ada": 36}`,
  `contains`, `remove`, `keys`, `values`, `for Name, Age in Ages` in the
  order added. Manual chapter 17.
- The C headers tidied: `string` renamed `strings`, and `stdio` and
  `stdlib` merged into `standard`. What Cabby's built-ins already do
  (`puts`, `atoi`...) was dropped, and using an old name says what to use
  instead.

## 2 November

- **`use processes`**: start programs, talk to them through pipes, and
  handlers for each line they print and for when they end. Manual chapter
  41.
- **`inner`**: fields and methods for the object and its children only.
- **Made and dropped**: `rocket { }` runs when a rocket is made, pairing
  with `~rocket { }`.
- **Handlers on any object** (`handler` fields, `.call`, `.is_set`,
  `.clear`), not only on windows.
- **One event loop for everything** (`use events`: `timer`, `events.run`),
  so windows, processes and timers all live in one program.
- **`use addresses`**: pointers, by choice, in words (`address int`, no
  `*`): `addresses.of`, `at`, `none`, `.value`, `[i]`, stepping and C
  functions. Memory by hand (`allocate`, `reallocate`, `free`), with the
  interpreter catching use after free, going past the end, double frees
  and leaks.

## 3 November

- **`use dates`**: dates, times of day, weekdays and months, patterns in
  the style of a spreadsheet, days between, `add_months`, and local time
  with daylight saving.
- **`use time`**: `moment`, `duration`, `time.now`, `time.since`,
  `time.sleep`. Built on a new language feature, **kinds**
  (`long kind duration`): numbers of their own that don't mix. Timers
  take durations (`every time.seconds(1)`).
- **`use json`**: `json.read`, `J["name"]`, `J[i]`, `for X in J`, `.str`,
  `.double` and their `_or` forms, `.text` and `.pretty`.
- **Addresses made safer**: one kept after its value is gone is a panic
  when interpreted. `addresses.of_text` and `of_items` give the addresses
  of strings and lists.

## 6 November

- `cabby --style` also flags parentheses that calls don't need.
- **Session commands under `cabby.`**: `cabby.exit`, `clear`, `preserve`,
  `restore`, `remove`, `sessions` and `help`, so the session takes no
  names from programs.
- **JSON to structs and back**: `json.from S` and `J.fill S`.
- **`use networks`**: `download`, `download_to`, `can_reach` and `status`,
  through libcurl. Manual chapter 46.
- **`use html` and `use xml`**: reading (forgiving for HTML, strict for
  XML), CSS selectors, and building pages. Manual chapters 47 and 48.
- **`use bytes`**: binary data, with numbers in either byte order;
  `files.read_bytes`, `write_bytes` and `append_bytes`; and
  `files.open_to_change` to read and write the same file, moving around
  in it. Manual chapters 49 and 50.
- **`read_char`** (one character, leaving nothing behind) and
  **`read_key`** (one key the moment it's pressed, for menus and games).
- `use local util` finds `util.cby` by itself: a library of plain
  functions needs no header.
- **`auto` taken out**: a type is always written, so the reader sees it.
  Writing `auto` gives an error that names the type to write.
- **`#!/usr/bin/env cabby`** as a first line, so a `.cby` file can be run
  directly.

## 7 November

- **The gui grows**, in gui and tui alike:
  - drawing: `add_canvas`, shapes, colors by name, mouse and keys, and
    saving as PNG (chapter 51);
  - menus and shortcuts (52);
  - tabs and scrolling columns (53);
  - tables, sorted by a heading (54);
  - number boxes, and color and font choosers (55).
- **`use game`**: sprites, `on_frame` 60 times a second with the loop out
  of sight, `key_press`, `key_hold` and `key_release`, collisions and
  groups, a bigger world with a camera that follows the hero. Manual
  chapters 56 to 59.
- **The game library on SDL 3**: drawn with the graphics card, resizable
  or full screen; stb_image, stb_truetype and stb_image_write built in.
- **`use sound`**: Cabby's own sound library: sound files, notes, chords
  and instruments made by the library, and MIDI files through
  TinySoundFont and the desktop's SoundFont. Chapter 60.
- **Tutorial 57**: a character walking a small town, tile by tile.
- **A gui drawn by Cabby itself with SDL 3**, built alongside the GTK one
  with the same names and tested by the same scripts. By the evening it
  had every widget, menus, tabs, tables and choosers, and a dark look that
  follows the desktop's.
- **Cabby on macOS** (Apple processor): the interpreter, compiled programs,
  gui, game and sound, with SDL 3 from Homebrew. Window programs run on
  the first thread there.

## 8 November

- **The switch**: `use gui` became the SDL gui, the standard one, and the
  GTK one became `use gtk`, with the same names and usage ("learn once,
  apply to many"). Without SDL 3, a clear message says to install it, or
  to `use gtk`. Every window tutorial runs with both.
- Before the switch, the SDL gui learned:
  - characters a font lacks drawn from another font (accented Greek,
    þ ð æ ƿ, symbols);
  - typing accents and other scripts;
  - undo and redo;
  - double and triple clicks;
  - moving and deleting by words;
  - Cmd for Ctrl on a Mac.
- **`use tgame`**: games in the terminal, drawn with half blocks, with
  real text and exact keys where the terminal allows.
- **A world in files** (tutorial and chapter 61): maps read from files,
  with their signs, doors and people; layers and depth (`set_layer`,
  `sort_by_depth`), so the hero walks behind trees and roofs; people who
  turn to face the hero.

## 9 November

- **Cabby Quest** begins (manual Part XII): one game built a part at a
  time, in sections of at most ten lines, from the map through walking,
  signs, music, people, conversations and a quest, things to carry,
  saving, and menus.
- **Areas joined edge to edge**, as in Pokémon: the area next door drawn
  beyond the edge, walked into with no fade (`set_world_area`, and
  `change_music` and `crossfade_music` in `sound`).
- **Writing MIDI files**: `sound.part` and `sound.song`, in beats at a
  tempo (tutorial 72). `tools/make-music.cby` writes the Quest's music.
- **Recording**: `sound.record`, a choice of inputs, the level for a
  meter, trimming silence, and saving WAV files (tutorial 73).

## 10 November

- **Cabby Quest, part 9** (tutorial 70): people who notice the hero, by a
  watched tile or by line of sight, with a "!" bubble, and Rufus the
  rival with music of his own.
- **Part 10** (tutorial 71): ground at two heights, as in FireRed:
  ledges, high ground, cliffs, stairs and bridges.
- **`use patterns`**: regular expressions (`matches`, `find_all`,
  `replace_all`, `split`...), with `\d`, `\w` and `\s`, in UTF-8. Tutorial
  74.
- **`use hashes`**: SHA-256, SHA-1 and MD5, written from the standards.
  Tutorial 75.
- **Web addresses**, in `networks`: `read_url`, `networks.url`, the query
  as a lookup, `encode` and `decode`. Tutorial 76.
- **`use archives`**: zip, tar and .tar.gz files made and unpacked (safely:
  nothing outside the folder), and one .gz file. Tutorial 77.
- **`use databases`**: SQLite, with values through `?`s, rows as lookups,
  and transactions. Tutorial 78.
- **`use threads`, step 1**: workers (`threads.worker`, `start`, `wait`,
  `is_done`, `threads.cores`), safe by the ownership rules, and checked.
  Real threads when compiled. Tutorial 79.

## 13 November

- **Threads, steps 2 and 3**: `on_done`, called from a window's or a
  game's own loop, so the window carries on (tutorial 80); and messages
  (`tell`, `on_message`) and stopping (`ask_to_stop`, `stopping`) for a
  progress bar with a Cancel button (tutorial 81).
- **The interpreter's bytecode** (`interpreter_bytecode.c`), in three
  stages in one day:
  - each function is made into bytecode the first time it's called;
  - fields, methods, strings and lookups were added;
  - each instruction jumps straight to the next.

  By the end, interpreted Cabby was ahead of Python at all ten
  benchmarks.
- **Arrays of numbers stored compactly**, as C stores them: sieve's 2
  million `bool`s take 2 MB instead of 32, and the whole program 4.8 MB
  against Python's 26.

## 14 November

- **`use dbus`**: D-Bus, which much of the Linux desktop talks. Cabby
  speaks it itself, with no libdbus to install:
  - calls and properties, and lists, dictionaries, variants and structs;
  - signals, with a handler;
  - answering calls, owning a name and sending signals, enough for the
    start of a notification server.
- **Compiled lookups level with C**: whole-number keys are searched inside
  the compiled program itself, with Fibonacci hashing (693 ms against C's
  675).
- **More in bytecode**: `switch` (five times faster in a loop), and making
  an object whose fields are given numbers.
- **`make test` passes on the Mac mini**. To get there:
  - the test scripts were taught macOS's bash 3.2 and BSD tools;
  - workers' message handlers were fixed for ARM64's way of passing
    structs;
  - off-screen test windows were made without OpenGL or vsync.
- **Cabby Quest, part 11** (tutorial 82, manual Part XVI): tall grass,
  wild creatures, and a battle screen in the style of FireRed, with health
  bars, moves and battle music. The creatures and pictures are Cabby's
  own.
- **Part 12** (tutorial 83): Rufus's battle, which can't be run from,
  experience and levels, and the elder who heals.
- **The same on Linux and macOS**, with no `#if`s in the program:
  - `use desktop`: notifications, and opening files, folders and web
    addresses;
  - `use power`: the battery;
  - `use volume`: level, set, up, down, mute;
  - `use media`: what's playing, and play, pause, next, previous; MPRIS
    on Linux (written in Cabby on `dbus`), Spotify and Music on a Mac;
  - `use system`: the system's name and version, hostname, kernel,
    processors, memory and uptime;
  - `use users`: who's running the program, their groups, and anyone by
    name;
  - `use mime`: what kind of file a file is, by what's in it;
  - in `networks`: `connected`, `local_address` and `wifi_name`.
- **Highlighting caught up**, in Vim and the interactive session: every
  library's calls (`archives.make` among them), library types
  (`threads.worker`), the handler members, and `typedef` shown as
  reserved.
- **patterns matches by itself**, the same on Linux and a Mac, in place of
  the system's regular expressions:
  - `\b` and `\B`, the edges of words;
  - lazy repeats (`+?`, `*?`, `{2,4}?`);
  - `\1` inside a pattern;
  - `(?:`, `(?=` and `(?!`.

  Repeats of letters stay quick on any length of text, and a pattern
  that would run for years is stopped with a panic.
- **html and xml selectors** gained pseudo-classes:
  - `:first-child`, `:nth-child(2n+1)` and the `-of-type` ones;
  - `:not()`, `:is()` and `:has()`;
  - `:empty`, `:root`, `:checked` and `:disabled`.

  Also new: the `+` and `~` combinators, and attribute tests like
  `[href^=https]`.
- **Colors and keys of the program's own**, the same in gui, gtk and tui:
  `set_text_color` and `set_background` on any widget, row, column or
  window, and a window's `on_key` and `key`.
- **databases**:
  - statements made once (`prepare`), whose `?`s take text, numbers,
    bytes or nothing;
  - `records`, rows as JSON, for `Row.fill` into a struct;
  - `bytes_value`, a column read back as bytes.
- **dbus**: an object's properties answered by themselves (Get and
  GetAll, with PropertiesChanged sent), and open files passed over the bus
  (`h`).
- **Menus on a Mac** show shortcuts as a Mac's do (⌘S, ⇧⌘S).
- In the interpreter, addresses of a great many strings are watched
  without slowing it down. Before, each new one was slower than the last.
- A struct can now share its name with a method of a library's object
  (a struct called `item`, with json's `item` method).


## 15 November

*Version 0.2.0:*

- **Named time zones** in dates: `M.in_zone "Europe/Berlin"`, `M.text_in`,
  `M.abbreviation_in` (CET, AEDT), `DT.moment_in`, `dates.zones` and
  `dates.zone_here`. They're read from the system's own zone files by
  Cabby itself, so it's the same on Linux and a Mac, and they agree with
  Python's zoneinfo for every zone from 1900 to 2100.
- **Month and weekday names in 13 languages**: `format_in`, `read_in` and
  `text_in`, Latin, Greek and Old English among them.
- **Looser text**: `dates.read_loosely` reads "tomorrow", "next friday",
  "in 2 weeks", "25 Dec" and "friday at 5pm".
- **Zip files of any size**: read and written a piece at a time, with
  zip64's records past 4 GB. A 4.5 GB file goes in and out in ten
  seconds.
- **xz**: `.tar.xz` archives, and single `.xz` files.

*Version 0.2.1:*

- **Version numbers**: `cabby --version`, and the interactive session
  says which Cabby it is as it starts.
- **Fixed**: after a panic inside a function, the interactive session
  freed values through the function's frames after they'd gone, which
  could crash it (as on Debian, in the session tests); it frees them from
  the interpreter's own stack of values instead.

*Version 0.2.2:*

- **Fixed**: `system.hostname` takes a Mac's `.local` off its name
  whichever system's ways are being used, so the tests that use Linux's
  ways pass on a Mac too.

*Version 0.2.3:*

- **Fixed, in the tests**: each test script keeps its saved games in a
  folder of its own, so two runs at once can't empty each other's (as
  when tutorial 69's saved game went, over ssh).

*Version 0.3.0:*

- **Sending to web services**: `networks.post`, `post_json` and
  `post_form`, and `networks.request_to Url` for any method (`PUT`,
  `PATCH`, `DELETE`...), headers, a body of text, JSON, a form or bytes,
  and a time limit. Its answer: `status`, `worked`, `answer`,
  `answer_json`, `answer_bytes`, `answer_header`, `error`.
- **Progress for big transfers**: `on_progress` calls a method as a
  request goes (`received`, `total`, `fraction`), and `save_to` saves
  what comes back as it comes.
- **Programs talking to each other**: `networks.connect Host, Port`
  (`send_line`, `read_line`, `on_line`, `on_close`) and
  `networks.server` (`listen`, `listen_here`, `on_connect`, `on_line`,
  `on_close`, `reply`, `send_line_to_all`), over TCP, through the event
  loop. A Cabby program talks to another, or to `nc` or a Python script.
- **`events.step`**: the event loop gone round once, for a program with
  a loop of its own; and `events.waiting`.
- **The networks tests**: a small web server of their own (in Python) for
  the requests, and two Cabby programs talking for the connections.

*Version 0.3.1:*

- **The MIT License**: Cabby is free to use, change and share, in open or
  closed programs, and comes as it is, with no warranty. `LICENSE` has
  it, with the copyright of Thomas Paul Whalan; `cabby --license` and the
  session's `cabby.license` show it; `cabby --version` and the session's
  first lines say there's no warranty. The session also has
  `cabby.version`.

*Version 0.4.0:*

- **MIDI keyboards**: `sound.open_keyboard` (or `open_keyboard_named`),
  and `sound.midi_inputs`. `play_through "piano"` plays a keyboard through
  one of sound's instruments, heard at once from a thread of its own, the
  sustain pedal and the bend wheel too; `on_note`, `on_control` and
  `on_bend` tell the program's handlers, from the event loop. Through
  ALSA's sequencer on Linux and Core MIDI on a Mac, both loaded when
  they're first needed. Tutorial 84 plays a keyboard as a piano and writes
  down what's played, as a MIDI file.
- **MIDI synthesizers**: `sound.open_synthesizer`: `play_note`,
  `start_note`, `start_key`, `set_instrument`, `set_channel`,
  `set_velocity`, `set_control`, `bend`; and a keyboard's `send_to`, which
  sends what's played straight on.
- **Monitoring**: `sound.set_monitoring true` hears what's being recorded
  as it comes in, mixed with what's playing, and `set_monitor_volume`.
- **Sound from workers**: a worker can play, write songs and record while
  the main program does, checked with a race checker. Cabby's own
  synthesizer has a sustain pedal too.
- **The tests' keyboard and synthesizer**: `CABBY_MIDI_IN` plays a MIDI
  file as the keyboard, and `CABBY_MIDI_OUT` saves what a synthesizer is
  sent as one.

*Version 0.4.1:*

- **Fixed**: `listen_here` listens at both of this computer's addresses,
  IPv4's 127.0.0.1 and IPv6's ::1, since "localhost" may be either; it
  had listened at ::1 alone where there's IPv6 (as on Debian and the
  Mac), so a connection to 127.0.0.1 was refused. A connection from IPv4
  to `listen` is told as its IPv4 address (192.168.1.20, not
  ::ffff:192.168.1.20).
- **Fixed, in the tests**: the networks tests' second program started on
  a Mac without `timeout` (the tests' own is a shell function, which
  `exec` can't run).

*Version 0.5.0:*

- **Pictures: `use images`**: pictures in memory, `images.new` and
  `images.load`; pixels (`set_pixel_rgb`, `pixel`, `red`, `green`,
  `blue`), lines, rectangles and circles, `draw` one on another, and
  `save` as PNG, JPEG or BMP. Colors mean what they mean to the windows
  (one `colors.h` for both).
- **On a canvas**: `picture X, Y, P` in gui, gtk and tui; a window keeps
  what it's shown of each picture, and sends the pixels again only when
  they've changed.
- **Windows from workers, by pictures**: a worker paints pictures of its
  own and hands each to the main program with `tell_picture`, moved and
  not copied; the window's `on_message` handler takes it with `picture`
  (and `has_picture`). Tutorial 85 paints the Mandelbrot set a strip at a
  time, shown as it comes.
- **The session**: tab completion offers `cabby.version` and
  `cabby.license`.
- **The tests**: a window tutorial's `// leaves:` line names a file it
  saves, which the tests tidy away.

*Version 0.6.0:*

- **Right to left**: Hebrew and Arabic in a gui window go right to left,
  numbers and English words in them left to right, by Unicode's
  bidirectional rules (the usual part of them); the text cursor, and
  selections, go where the text is shown.
- **Shaped**: Arabic's letters joined, marks on their letters, and the
  other scripts shaped as they're written, by HarfBuzz, loaded when it's
  first needed; without it (a Mac without Homebrew's harfbuzz), Cabby
  joins Arabic itself, from Unicode's joined forms.
- **Emoji in color**: from the pictures in Noto Color Emoji (CBDT) and
  Apple Color Emoji (sbix), read by Cabby (the font mapped, not read in:
  Apple's is near 200 MB); an emoji of several, a family or a flag, is
  one picture.
- Plain Latin, Greek and Cyrillic text is drawn as before, and other text
  laid out once and kept, so windows stay as quick.
- Tutorial 86, a phrasebook in seven languages; `tests/run_text_tests.sh`
  checks the order text is shown in, and emoji in color, from two tiny
  fonts cut from Noto Color Emoji.

*Version 0.6.1:*

- **Fixed, in the tests**: the text tests run on a Mac without `timeout`
  (the tests' own is a shell function, which `env` can't run), and the
  networks tests no longer say "Terminated" as their web server stops.

*Version 0.6.2:*

- **Fixed, in the tests**: the text tests sort their lines by bytes
  (`LC_ALL=C`): a Mac's `sort` put Hebrew and Arabic in another order.

*Version 0.7.0:*

- **Zips with a password**: `archives.make_with_password`,
  `unpack_with_password`, `read_with_password` and `needs_password`. Made
  with WinZip's AES-256 (as 7-Zip and WinZip read it): a key from the
  password by PBKDF2, a fresh salt for each file, and an HMAC that catches
  a wrong password or a changed file. Read: AES-128 and -192 too, and the
  old ZipCrypto that `zip -e` makes. Checked against an implementation of
  its own (Python's), both ways, for every strength.
- **Quicker hashing**: AES and SHA-1 on the processor's own instructions
  where it has them (AES-NI and the SHA extensions; Apple's processors'
  crypto extension), checked against the standards' examples before
  they're used; SHA-1 of a file is twice as quick, and quicker still in
  C, written out by its phases.
- **Fixed, in the tests**: a case left a file (xzcase-a.txt) in the cabby
  folder.

*Version 0.7.1:*

- **A clearer error**: a library's name without its `use` line
  (`archives.unpack` with no `use archives`) says "add 'use archives' at
  the top of the file", for every library, not only some.

## 16 November

*Version 0.8.0:*

- **Making users**: `users.make` and `users.make_admin` (a login name,
  a full name and a password, and a home folder), `users.set_password`,
  `users.remove` and `remove_keeping_home`; run with `sudo`, and a panic
  that says so without. Linux's useradd, chpasswd and userdel (the
  password read by chpasswd, never on a command line), and a Mac's
  sysadminctl. Cabby won't remove root, or whoever runs the program.
- **Groups**: `users.make_group`, `remove_group`, `add_to_group`,
  `remove_from_group` (groupadd and gpasswd; a Mac's dseditgroup).
- **Everyone**: `users.all` (who can log in) and `users.all_groups`.
- Tried for real on Linux (the password checked against what the system
  keeps), and in the tests with stand-ins for both systems' programs.

*Version 0.8.1:*

- **Fixed**: on a Mac, `users.exists` said a user just removed was still
  there: the Mac's directory service answers from a cache for a moment.
  Cabby now clears it (`dscacheutil -flushcache`) and waits until the
  change shows, after making or removing a user or group.

*Version 0.9.0:*

- **Screen readers**: a gui window is read by Linux's screen reader
  (Orca), with nothing to do in the program. Each window's contents go
  to it over AT-SPI, the desktop's accessibility bus, as GTK's and Qt's
  do: what each thing is, what it's called (a text box by the label
  before it), and whether it's ticked, chosen, grayed out or has the
  keys; menus, a drop-down's and list's options, tabs, and a message or
  question showing. As the program runs, the reader is told what
  changes (the keys moving, text typed, a box ticked), and it can press
  buttons and choose options itself.
- Sent only while a screen reader is running (the desktop says so), so
  a program is no slower without one; `CABBY_ACCESSIBILITY=1` sends it
  anyway, `=0` never. Written with Cabby's own D-Bus: nothing more to
  install.
- **`set_description`** on every widget (gui and gtk): what a reader
  says a picture or canvas is.
- The name "readers" was thought of for a library of its own, and left:
  a screen reader needs nothing from the program, so there's nothing for
  it to hold. ("speech" is kept for reading text aloud, if it comes.)
- A test with a stand-in for the reader, on a bus of its own
  (`tests/run_accessibility_tests.sh`), and a script command, `pause`,
  that really waits, answering what's asked meanwhile.
- A Mac's VoiceOver is next: it can't be tried here.
- Tried on Debian and the Mac: all tests pass. Noted for later: on a Mac,
  `users.exists` still says true for a user just removed.

*Version 0.10.0:*

- **3D: `use world`**, a library of its own, with the game library's
  flow: an object made from `world.window`, `build`, and `on_frame` 60
  times a second, with game's keys, timers, shortcuts and test scripts.
- **Shapes**: boxes, balls, planes, cylinders and cones, in metres, y up,
  each placed by its middle; turned, tilted and rolled in degrees,
  `move_forward` and `move_sideways` the way they face, sized and scaled,
  colored or with pictures on them (repeated, with `set_picture_size`);
  groups, `touches` (the boxes round them), `distance_to`, `top`.
- **Light**: the sun (its way and color), the light all round, and lamps
  whose light falls off with distance, lit at every pixel.
- **A camera**: moved, pointed, turned, walked; `follow` keeps it behind a
  shape, as most 3D games do; `lock_mouse` and `mouse_moved_x` for looking
  round, first person. `shape_at_mouse` for clicking on things.
- **Text over the world**: `add_text`, for a score.
- **Drawn with OpenGL 3.3** (a Mac's 4.1), its functions looked up when
  the window opens, so nothing new to install. Where there's none, and
  under test scripts, **Cabby draws it itself**: triangles cut at the
  camera, filled a pixel at a time and lit the same way, with no fused
  multiply-adds, so it's the same picture on every computer. On Linux the
  tests draw it both ways and check they agree.
- The name: "world", for now; tutorial 87, a walk round a field picking up
  coins; `lib/world.md`.
- Next: models made in Blender (glTF), with their pictures and animations.
- `make test` passed on Debian and on the Mac (over SSH); tutorial 87 tried
  in a window on Debian. "world" kept as the name. A 3D Cabby Quest added
  to the goals.

*Version 0.11.0:*

- **Models from Blender**: `add_model "hero.glb", X, Y, Z`, a model in
  glTF 2.0 (`.glb`, or `.gltf` with its `.bin` and pictures beside it),
  read with cgltf (MIT, in `runtime/third_party`). Each mesh as it hangs
  from its node; its material's color and picture (in the file or beside
  it; Blender's Closest kept sharp); see-through where its picture is.
- **Bones**: a skinned mesh bent to its joints each frame, on the
  processor, so both kinds of drawing show the same.
- **Animations**, by Blender's action names: `play` (looped, and carrying
  on if already playing), `play_once`, `stop_animation`,
  `set_animation_speed` (backwards too), `playing`, `animation_time`,
  `animations`. Smooth, stepped and cubic keyframes (the last as smooth).
- A model faces Blender's front, which becomes -z, the way shapes face,
  and stands with its origin (its feet) at its place.
- Tested with a model made by `tools/make-test-model.py` (a body with a
  picture, a head, a skinned arm, three animations), drawn both ways;
  tutorial 88 walks it round.
- `make test` passed on Debian and the Mac (over SSH). Free models were
  looked for, but none with a license that fits Cabby.

*Version 0.12.0:*

- **Bluetooth: `use bluetooth`**, on Linux's BlueZ, over the dbus
  library. Written mostly in Cabby itself (`lib/bluetooth.hby`), with a
  little C (`runtime/bluetooth.c`): the shared connection, and the agent.
- The computer's Bluetooth: `available`, `is_on`, `turn_on`, `turn_off`,
  `set_visible`.
- Devices: `bluetooth.devices`, `paired_devices`, `connected_devices`,
  `device_named` (by name or address); each with its name, address,
  kind, `paired`, `connected`, `trusted`, signal `strength` and
  `battery`; and `pair`, `connect`, `disconnect`, `set_trusted`,
  `forget`, each true if it worked, `bluetooth.error` saying why not.
- Searching: `bluetooth.search` (for a while, nearest first), or
  `start_search` and `stop_search` for a window.
- Pairing: an agent of the library's own, answering BlueZ from C at once,
  even while the program waits in its call to pair: a keyboard's code is
  shown, a code on both sides agreed to. Paired devices are trusted, as
  the desktop's settings do.
- `bluetooth.watcher`: told when a device is found, connects,
  disconnects, is forgotten, or its battery changes.
- The dbus library's calls can now wait longer than 25 seconds (pairing
  waits for a person).
- Tested against a stand-in for BlueZ, written in Cabby with the dbus
  library, on a bus of its own. A Mac's Bluetooth is still to come.

*Version 0.13.0:*

- **Bluetooth on a Mac**: the same `use bluetooth`, the same programs,
  through the Mac's own IOBluetooth: on and off and visible (the plain C
  functions IOBluetooth has), the paired and recent devices, searching (an
  inquiry, waited for in the run loop), pairing (a delegate made at run
  time, answering codes and confirmations as the Linux agent does),
  connecting, disconnecting and forgetting.
- Reached through the Objective-C runtime from C (`objc_msgSend`, cast to
  each call's exact shape, as an Apple processor needs), everything
  looked up with `dlopen` when first needed: nothing more to link, and it
  compiles on Linux too.
- A watcher on a Mac looks each second at what's changed.
- `CABBY_BLUETOOTH=bluez` uses BlueZ even on a Mac (so the tests' stand-in
  works there), and `CABBY_BLUETOOTH_REAL=1` has the tests look at the
  computer's own Bluetooth too.
- Written without a Mac to try it on: to be tried at the Mac mini.
- 0.12.0's tests passed on Debian. The Mac's Bluetooth is on the goals,
  to try when the Mac's free.

*Version 0.14.0:*

- **A web server: `networks.web_server`**, for browsers (and programs
  fetching pages). Each request is told to `on_request`'s handler, from
  the event loop, as a server's lines are; the handler answers with
  `send_page`, `send_text`, `send_json`, `send` (any status and type),
  `send_status`, `redirect`, `send_file` or `send_from_folder` (a folder
  of a site's files, its `index.html` for a folder, nothing outside it
  ever sent). Unanswered: "404 Not Found".
- The request: `method`, `path` (decoded), `query`, `query_value`,
  `form_value` (a form sent), `body`, `body_json`, `header`, `cookie`,
  `address`; and `add_header` and `set_cookie` for the answer.
- HTTP/1.1, written in C (`runtime/web.h`, beside the connections it
  uses): keep-alive, requests sent one after another on one connection,
  bodies by their length, HEAD, a bad request answered "400". A file's
  type from its name.
- Tested by a Cabby program asking a Cabby web server, through libcurl, as
  a browser would.
- The rest of `networks.hby` tidied to the style guide's parentheses.

*Version 0.15.0:*

- **Email: `use emails`.** An `emails.message`: from, to, Cc, Bcc,
  reply-to, subject, text, an HTML version, attachments; `text` is the
  whole message as it's sent, and `save` writes it as an `.eml` file.
- An `emails.account` (`emails.account_at Server, Login, Password`)
  sends it by SMTP: encrypted from the start (port 465) or with STARTTLS
  (587); `send` gives true if the server took it, and `error` says why
  not.
- The message is made in C (`runtime/emails.c`), as RFC 5322 and MIME say:
  plain text as it is, other text quoted-printable, attachments in base64,
  names and subjects in other languages as RFC 2047 says; sent with
  libcurl, as `networks` downloads.
- Tested against a stand-in email server written in Cabby (with
  `networks.server`), which logs in, takes the message, and writes it to
  a file.

*Version 0.16.0:*

- **Secrets: `use secrets`** (the name for the cryptography goal: what it's
  for, in a word). Random numbers, text, passwords and bytes from the
  system's own generator; passwords kept as PBKDF2-SHA256 hashes (600,000
  rounds, salted, as OWASP advises) and checked; text, bytes and files
  locked with a password (AES-256-GCM, the key from the password, and any
  change noticed); HMAC-SHA256 signatures, and comparing in constant time.
- Built on a well-tested library, as the goal said, never on Cabby's own
  cryptography: OpenSSL's libcrypto on Linux, CommonCrypto on a Mac (or
  Homebrew's OpenSSL), loaded when first needed.
- Tested against the standards' own examples: RFC 7914's PBKDF2, NIST's
  AES-GCM cases, RFC 4231's HMAC.

*Version 0.17.0:*

- **The screen's brightness: `use brightness`** (`level`, `set`, `up`,
  `down`, `can_set`). On Linux a laptop's backlight in
  /sys/class/backlight, written there where the program may, else by
  brightnessctl, else by logind (which lets the person at the computer);
  a desktop's monitor by ddcutil, down its cable. A Mac's own screen
  through DisplayServices, as its brightness keys set it.
- **Printing: `use printing`**, through CUPS's `lp`, `lpstat` and
  `cancel`, the same on Linux and macOS: printers and the default one,
  printing a file or text, `printing.settings` (printer, copies,
  two-sided, pages, landscape, black and white, title), and the jobs
  waiting, `done` and `cancel`.
- **Speech: `use speech`**: `speech.say`, and a `speech.speaker` with a
  voice, a language and a speed in words a minute; `save` into a WAV
  file; `voices`. espeak-ng (or espeak, or spd-say) on Linux, `say` on a
  Mac. espeak-ng speaks Latin (`"la"`) and Ancient Greek (`"grc"`).
- **`system.set_hostname`**: hostnamectl on Linux, scutil's three names
  on a Mac.
- printing and speech are written in Cabby, on a new
  `cby_desktop_capture` that runs any program (with what it's to read,
  if anything) and keeps what it prints.
- Tested with stand-ins for every program (lp, lpstat, cancel,
  espeak-ng, espeak, spd-say, say, brightnessctl, busctl, ddcutil,
  hostnamectl, scutil) and a pretend backlight folder, on both systems'
  ways.

*Version 0.18.0:*

- **Videos: `use videos`**, on FFmpeg's libraries (libavformat,
  libavcodec, libswscale, libswresample), built when they're found:
  `videos.open`, and what's in a video (its size, length, frame rate,
  codecs, title); `play`, `pause`, `seek`, `position`, `set_looping`,
  `set_volume`; `picture_at` (an `images.picture`, for thumbnails) and
  `save_sound` (a WAV file).
- **gui's `add_video`**: a video in a window, given its `source`, its
  picture scaled to fit and its sound played by SDL, kept to the video's
  clock; `show_controls` (a play button, a track, the time), clicks and
  keys, and `on_end`. The two libraries meet through a small table of C
  functions (`runtime/video_source.h`), so neither needs the other linked.
- Under a test script a video plays by the script's clock, so what it
  shows at each moment is the same every time. The test videos
  (`tools/make-test-video.py`) are lossless (FFV1 and FLAC), so their
  colors and samples are checked exactly; the sound's timing was checked
  through SDL's disk driver.
- Tutorial 89, a video player, and chapter 89 (Part XXI).

*Version 0.19.0:*

- **WebSockets** (RFC 6455, `runtime/websockets.h`): a web server's
  `wants_socket` and `accept_socket`, its WebSockets' messages to
  `on_message` (`message`, `reply`, `send_message`,
  `send_message_to_all`, `sockets`, `close_socket`, `on_socket_close`);
  and `networks.open_socket`, a program's own WebSocket to a server
  (`send`, `read_message`, or `on_message`). Pings, pongs, closes, and
  messages in pieces, as browsers send them.
- **HTTPS, `wss://` and encrypted connections** (`runtime/tls.h`): a web
  server's or a server's `set_certificate` (Let's Encrypt's
  `fullchain.pem` and `privkey.pem` as they are); `networks.
  connect_encrypted` and `wss://` addresses, the server's certificate
  checked as a browser checks a site's; `networks.trust_certificate`
  (for connections, WebSockets and downloads alike); and
  `networks.make_certificate`, a certificate of its own for trying it
  (P-256, with its names, for 825 days).
- OpenSSL's libssl, loaded when first needed (Homebrew's on a Mac). The
  sockets of encrypted connections don't wait, so a slow or half-finished
  handshake never holds the event loop up; OpenSSL's writes can't stop
  the program with SIGPIPE when the other end has gone (found when a
  server's session tickets went to a client that had closed).
- Tested with a peer program serving HTTPS, `wss://` and an encrypted
  line server (with a certificate it makes), checked untrusted then
  trusted; a chain from a root and an intermediate, as Let's Encrypt
  gives; and Chromium (TLS 1.3, a page and its `wss://` WebSocket).

*Version 0.20.0:*

- **`use os`**, what every system has, the same on Linux and macOS
  (`runtime/os.c`): an `os.watcher` (`watch`, `watch_all`, `on_change`,
  and `change`, `path`, `old_path`: "created", "changed", "deleted",
  "renamed"), by inotify on Linux and kqueue on a Mac, where a folder's
  looked at again and compared (`CABBY_WATCH_BY_LOOKING=1` does that on
  Linux, so the tests try both ways everywhere); `os.signals` (`catch`,
  `on_signal`, `signal`) and `os.catch_signal`, `got_signal`,
  `ignore_signal`, `usual_signal`, `send_signal`, `pid`; `os.disks`
  (`path`, `device`, `kind`, `name`, `size`, `free`, `used`, `read_only`,
  `removable`) and `free_space`; `os.cpu_use`, `memory_used`,
  `memory_available`, `swap_used`, `load`; `os.programs` and
  `programs_named` (`pid`, `name`, `command`, `user`, `memory`,
  `cpu_seconds`, `stop`, `kill`, `running`).
- Signals are caught by a handler that writes down a pipe, watched by the
  event loop "quietly" (`cby_events_watch_quietly`: it doesn't keep
  `events.run` waiting by itself).
- **`use linux`**, what only Linux has (`runtime/linux.c`):
  `linux.sensors`, `temperatures` and `fans` (hwmon, or the thermal zones
  on a board); `linux.block_devices`, `disks` and `partitions_of` (their
  models, sizes, labels, where they're mounted, SSD or not); USB devices;
  the kernel's modules (`module_loaded`, `load_module`); its settings
  (`linux.setting "net.ipv4.ip_forward"`, `set_setting`); and systemd's
  services, written in Cabby on systemctl (`services`, `service_named`,
  `start`, `stop`, `restart`, `enable`, `disable`, and this person's own).
- Tested with a pretend /sys, /proc and /dev (`CABBY_SYSFS`,
  `CABBY_PROCFS`, `CABBY_DEVFS`) and stand-ins for systemctl and modprobe;
  and on this computer as it is.

*Version 0.20.1:*

- On the Mac, HTTPS's test said OpenSSL wasn't there: Homebrew has moved
  on to openssl@4, and only openssl@3's place was looked in. Encrypted
  connections (and secrets, where CommonCrypto lacks something) now look
  for Homebrew's openssl@3, openssl@4, and whichever its "openssl" is, on
  an Apple processor or Intel; on Linux, libssl.so.4 too. libssl is taken
  from the same place as libcrypto.
- An OpenSSL that's found but lacks a function needed now says which;
  one that can't make certificates still encrypts (only
  make_certificate says it can't).

*Version 0.21.0:*

- **Pictures changed into new ones** (images): `scaled` (smooth: every
  pixel counted when made smaller, blended when made bigger),
  `scaled_to_fit`, `scaled_by`, `cropped`, `turned` (a quarter turn
  exactly; any angle, bigger to hold its corners), `mirrored`, `flipped`,
  `gray`, `blurred`; worked premultiplied, so see-through edges don't
  darken.
- **Text on pictures**: `text`, `images.set_font` (a font of the
  computer's by name, through fontconfig, or a font's file; stb_truetype
  draws it, with kerning) and `images.text_width`; a font built in, for
  no font at all and for the tests.

*Version 0.22.0:*

- **Reading email** (IMAP4rev1, `runtime/imap.h`, on the networks
  library's own connections, so encrypted with its TLS: 993, or STARTTLS
  on 143): `emails.mailbox_at`, `folders`, `open_folder`, `count`,
  `unread_count`, `unread`, `latest`, `all`, `from`, `with_subject`,
  `containing`, `message`, `mark_read`, `mark_unread`, `flag`, `unflag`,
  `move` (MOVE, or COPY and delete), `delete`, `can_connect`, `error`.
  Messages are fetched with PEEK, so reading one doesn't mark it read.
- **A message read** (`emails.received`, in `runtime/emails.c`): headers
  with RFC 2047's encoded words decoded, parts found however they nest,
  base64 and quoted-printable, character sets into UTF-8 (Latin-1,
  Windows-1252, ISO 8859-15, and the rest by iconv), attachments' names
  (RFC 2231's too), `save_attachment`; and `emails.read_message` for a
  saved `.eml`.
- Tested against a stand-in IMAP server written in Cabby
  (tests/emails/imap.cby), plain and (by hand) with TLS.

*Version 0.23.0:*

- **Speech to text** (speech): `speech.hear Path` and `hear_for Wait`
  (recording from the microphone with `sound.record`), and a
  `speech.listener` (`set_model`, `set_language`), by Whisper:
  whisper.cpp's `whisper-cli`, on the computer, nothing sent anywhere.
  Its model is found (`CABBY_WHISPER_MODEL`, Cabby's own folder, or
  whisper.cpp's packages' places), or fetched once from the web:
  `speech.get_model "base.en"`.
- **`sound.convert_to_wav`**: a WAV, MP3, FLAC or Ogg Vorbis file saved
  again as WAV at any rate and number of channels (miniaudio's decoder
  and resampler): 16000 a second, in one, is what Whisper hears.
- Tested with a stand-in whisper-cli (which checks what it's given is
  16 kHz mono WAV); and by hand with whisper.cpp itself, built from its
  source, and its test model.

*Version 0.24.0:*

- **Patterns** look behind, `(?<=\$)\d+` and `(?<!x)`, with any pattern
  inside (tried from the nearest start back); and parts have names,
  `(?<year>\d{4})`, for `P.part "year", Text`, `part_names`, `has_part`,
  `\k<year>` in the pattern, and `\k<year>` in a replacement.
- **Public keys** (secrets): `make_private_key`, `public_key`, `sign` and
  `check_signed`, Ed25519 by OpenSSL (Homebrew's on a Mac), checked
  against RFC 8032's examples. **Argon2id** passwords,
  `hash_password_argon2`, with OpenSSL 3.2 or later, in the format every
  program uses (`$argon2id$v=19$...`); `check_password` knows them, and
  is checked against hashes from Argon2's own reference program.
- **PostgreSQL** (databases): `databases.connect
  "postgresql://tom@localhost/shop"` gives the same
  `databases.database`, by libpq: its `?`s made `$1`, `$2` (left alone in
  quotes and comments), values sent apart, statements made once, records,
  bytes, `last_id` (a serial's) and `changes`. The tests start a server
  of their own.
- **A directory held open** (directories): `directory D =
  directories.open "photos"`, the same directory however it's moved or
  renamed: `next` and `name` an entry at a time, `names`, `has`,
  `is_file`, `read`, `write`, `add_to`, `make`, `remove`, `rename`,
  `open_inside`, `path`.
- **Running programs** (shell): `shell.run_with "sort", Text` gives a
  command input; `shell.run_program {"ls", "-l", Folder}` runs a program
  with its arguments as a list, quoted by no one, and
  `shell.run_program_with` and `shell.program_status` likewise.
- **A process's errors apart** (processes): `P.keep_errors_apart` before
  `start`, then `read_error_line`, `errors_at_end`, `error_line` and
  `on_error_line`; both outputs are read together, so neither fills and
  sticks. `P.write` sends text with no line end.
- **Saving email into a folder** (emails): `Mail.save Message, "Sent"`,
  `save_unread`, `save_text` (IMAP's APPEND).
- **Greek months after a day** (dates): `3 Νοεμβρίου`, but `Νοέμβριος
  2026`, and either read.
- The os and linux tests run on a Mac without GNU's `timeout` (they had
  called it through `env`, which can't see the tests' own stand-in).

*Version 0.24.1:*

- The shell test of a program failing runs `sh -c "exit 2"` rather than
  `ls` on a folder that isn't there, which ends with 2 on Linux but 1 on
  a Mac.

*Version 0.24.2:*

- **`os.program`'s `command` on a Mac** is how the program was started,
  `"sleep 30"`, read from the kernel (KERN_PROCARGS2), as Linux's
  `/proc/<pid>/cmdline` gives it; it was the program's file alone. (Another
  person's program, which can't be read, still gives its file.)

*Version 0.24.3:*

- Built without warnings again: a name in patterns.c and one in dbus.c
  (the Mac's alone) that hid another, and linux.c's room for a mount's
  path made as big as what's read into it (a Mac's `PATH_MAX` is 1024).
- shell.md's `wc -w` example trims what it prints: a Mac's `wc` puts
  spaces before the number.

*Version 0.24.4:*

- **Tab completion** in the interactive session lists a library's
  functions and types before `use` has been typed for it
  (`bluetooth.`, `secrets.`), read from its header's text: the same
  list as once it's used (checked for every library). It had listed
  only `files.`, `directories.`, `shell.`, `addresses.`, `events.` and
  `cabby.` without it. Those lists now have `directories.open` and
  shell's `run_with`, `run_program`, `run_program_with` and
  `program_status`.

*Version 0.25.0:*

- **`else` after a statement**: a panic in it is caught, and not shown,
  and its else part is done instead. `str Text = files.read "notes.txt"
  else ""` gives a declaration's variable a value; `Total += Prices[I]
  else 0` an assignment's; `else { }` runs a block, and `else Why { }`
  runs it with the panic's message in `Why`. After a declaration, a block
  has to leave (`return`, `break`, `continue`). It catches a panic
  anywhere in working the statement out, however deep, in every library
  at once (processes' own panics now go through Cabby's, so they're
  caught too). Underneath, the runtime's `rt_try_push` and `_setjmp`; the
  interpreter frees the abandoned calls' cells, temporaries and extern
  calls' working arrays, and values being built hold a cell of their own
  meanwhile; compiled programs leave the failed calls' locals behind,
  without destructors, as a panic always has. The files and directories
  functions' own copies of paths are freed by a caught panic.
- **Tutorial 90**, a class's marks with mistakes in them, and the manual's
  Part XXII, chapter 90.
- **examples/bluetooth-connect.cby**: finding Bluetooth devices, choosing
  one, pairing and connecting (Thomas's, tidied up), tried against the
  tests' stand-in BlueZ.

*Version 0.25.1:*

- **What a caught panic leaves is dropped**: the calls it jumped out of
  have their locals and temporaries dropped, the last made first, their
  destructors run (a file one had opened is closed), in compiled programs
  as in the interpreter. A compiled function with something to drop tells
  the runtime where each is (`cby_frame_enter`: its slot, its drop flag,
  and an `fdrop` helper for its type) when it starts while an else is
  waiting, and says it's gone as it returns; with no else waiting it
  costs a check of a thread's own counter. Drop flags written inside an
  else's statement are volatile, so they're as the panic left them, and
  its own temporaries are dropped by them. An object's fields that might
  panic are worked out before it's made, so none is left half made; the
  interpreter's values being made are freed, not dropped. Tried at -O0,
  -O2 and -O3, and valgrind-clean in both modes; the benchmarks are as
  they were.

*Version 0.26.0:*

- **`N.text Places`**: a number with that many decimal places, rounded:
  `Price.text 2` is `"4.50"`, `7.text 2` is `"7.00"` (and `-0.00` is
  `0.00`).
- **`random A to B`**: a whole number from A to B, each as likely, or a
  double from A up to B when either is one; **`random_seed N`** makes
  the numbers after it the same each time. xoshiro256**, seeded from the
  system's random numbers, each thread its own; the same numbers in both
  modes.
- **`abs`** for every kind of number, giving the same kind; **`exit
  Status`**, which writes out what open files hold first (and a function
  that ends in it needn't return).
- All four give way to a program's or header's own, so `use standard`'s
  `abs` and `exit` are still C's. **`printf`** (`use standard`) prints
  where Cabby's printing goes (`stderr printf ...`), through the
  runtime's `cby_printf`. Unknown `rand`, `srand`, `labs` and `fabs` say
  what Cabby has instead.
- **examples/bluetooth-connect.cby** offers to forget a paired device
  that won't connect (its keys gone stale) and pair it again.

*Version 0.26.1:*

- **Machine checks** (`tests/machines/`): a short series for each of
  Debian, Arch and the Mac, of what `make test` can only stand in for,
  run at the computer itself. Each check passes by itself, or shows
  what it did and asks whether it looked right; the answers are saved as
  `machine-results.txt`, to send back. Debian: Argon2, HTTPS to a real
  site, sensors and services, the computer's own facts, a notification,
  the volume, speech, a gtk window, a video with sound. Arch: PostgreSQL,
  Argon2, HTTPS, sensors, FFmpeg's videos, speech, gtk and gui windows
  under Sway. The Mac: HTTPS, its facts, a notification, the volume,
  Music, speech, a gui window with ⌘Q, a video's sound, 3D with its
  OpenGL, Bluetooth (looking only) and PostgreSQL.

*Version 0.27.0:*

- **A base type holds what's made from it**: `list shape Shapes` takes
  squares and circles (`shape Held = Square`, and a function's `shape`
  parameter, too), and each still answers as what it is, its fields
  found and its destructors run as what it is, in both modes, with
  multiple inheritance as well. An object that others are made from finds
  its fields through its classinfo and calls its methods through its
  table, as through a borrow (it was by its own type alone); its classinfo
  now has its drop. An object nothing is made from is as fast as before.
  The manual's chapter 25 has a list of animals.
- **`extended` and `quadruple`** are reserved, for types to come (a whole
  number larger than `long`, a floating-point number larger than
  `double`); `const` stays `const`. These were the 1.0 decisions.

*Version 0.28.0:*

- **Install packages**, so each system's package manager installs Cabby,
  upgrades it and takes it away again (`packaging/README.md`):
  `packaging/arch/build.sh` (a `PKGBUILD`, run with `makepkg`, which runs
  the program and tutorial tests), `packaging/debian/build-deb.sh` (a
  `.deb`, its dependencies worked out from what Cabby is linked with), and
  `packaging/homebrew/build-tap.sh` (a formula in a tap of its own,
  `cabby/local`, with a test that runs and compiles a program). They
  install under `/usr`, or Homebrew's folders, with the manual, the Vim
  files and, on Linux, the file icons and file types
  (`make install-extras`).
- **Cabby finds its libraries where it was built for**: `make
  PREFIX=/usr` builds a `cabby` that looks in `/usr/lib/cabby` too. On a
  Mac, `lib/` next to the `cabby` binary is found through a link as well
  (as on Linux), so Homebrew's `/opt/homebrew/bin/cabby` finds its own
  libraries.

*Version 0.28.1:*

- **gtk: shortcuts in a window that has `on_key`**: the window's key
  handler took every key, Ctrl+Q included, before the menu's shortcut
  saw it (found by the Arch machine check, under Sway). Shortcuts now
  go first, as a menu's shortcuts do in other programs, and the rest of
  the keys still go to `on_key`.

*Version 0.28.2:*

- **`CABBY_SOUND_REPORT`**: with it set, a video in a gui window says on
  stderr which sound system and device it's playing to, and every two
  seconds how much sound it has given and how loud. On the Mac, the
  machine check's video played but its clicks couldn't be heard; the
  machine checks now set it for the video, to find out why.

*Version 0.28.3:*

- **`os.disks` leaves out what can't be looked into**: on Debian, the
  network shares mounted in other people's home folders showed as 0 GB,
  since this person can't look inside those folders. They're left out
  now, as `df` leaves them out.
- The machine check's "processors busy" showed 2927%: `os.cpu_use` is
  already from 0 to 100, and the check multiplied it by 100 again.

*Version 0.28.4:*

- **The test video's sound** (`colors.mkv`, in the tutorials and the
  tests) is a beep as each color starts, the same way in both channels.
  It was a slow square wave, the right channel the left upside down: a
  Mac mini has one speaker, which adds the two together, so on it they
  cancelled out and nothing could be heard (the sound report showed the
  sound was given, at a quarter of full loudness). Elsewhere, only the
  edges could be heard, as clicks.

*Version 0.29.0:*

- **The tutorials in a new order**, so that each uses only what came
  before it, and what belongs together is together. Twenty-two parts
  became fifteen: the basics, control flow, functions and data, objects
  (with handlers), close to the machine (calling C, addresses, register
  and volatile, goto), programs in the world, when it fails (panics, then
  `else`), every day, windows (with menus, tables, drawing, text in every
  language, and the terminal), doing things at once, pictures and videos,
  games, sound and music, Cabby Quest (all twelve parts together), and
  three dimensions. Addresses come after objects, as part of what Cabby
  is rather than an afterthought. Every file, chapter and Quest section
  is renumbered, and so is every reference to them, in the manual, the
  README, the libraries' pages, Goals, the tests and the tools. (This
  timeline keeps the numbers they had at the time.)
- whisper.cpp's package on Arch is `whisper.cpp`, as `speech`'s message
  now says.

*Version 0.29.1:*

- **Homebrew on an Apple processor**: `packaging/homebrew/build-tap.sh`
  uses `/opt/homebrew`'s Homebrew, and runs as the Apple processor even
  from a terminal opened with Rosetta. On the Mac mini it had found an
  Intel Homebrew in `/usr/local`, which built for Intel and couldn't
  link. With only an Intel Homebrew there, it says so.

*Version 0.30.0:*

- **"More examples"** at the end of 26 chapters: what each library's
  reference tables list but its chapter and tutorial didn't show, from
  printf's `%ld` to time zones, dates in other languages, requests,
  servers, HTML's selectors, every size of number in bytes, zips with a
  password, rows as JSON, colored text, ovals and arcs, a changing table,
  pictures scaled and cropped, a video's frame rate and sound, sprites
  hidden and removed, and a 3D camera moved by hand. Each section can be
  skipped: nothing later depends on it. Every one is run by `make test`
  (or checked, where it needs the internet, a microphone or a MIDI
  keyboard).
- The chapters that pointed ahead after the reorder as if back (handlers
  to windows' buttons, processes to tui windows, the event loop to tui's
  timers, files in place to JSON) say so plainly now.
- **Fixes the examples found**: an HTML element's `raw_text` added spaces
  around paragraphs and list items inside it (only `text` should); a 3D
  camera's angle could be `-0.0`.
- The compiled programs' names in the tutorials' headers
  (`./26-your-own-handlers`) had kept their old numbers.

*Version 0.31.0:*

- **Columns**: `s.pad_left n`, `s.pad_right n` and `s.centered n` give
  text with spaces in front, after, or either side, to make it `n`
  characters wide (letters, not bytes: "Köln" is four). With `.text(2)`
  for decimals, that's what C's `printf "%-15s%7.2f"` was for, with
  nothing new to learn, and it works for labels and files as well as
  printing. Chapter 12 has a menu in columns.
- **Numbers past a long**: a whole number too big for a `long` but not
  for an `unsigned long` is one (`unsigned long Most =
  18446744073709551615`), as in C; it was an error. A number too big for
  what it's put in says so plainly (`3000000000 is too big for 'W', an
  'int'`), rather than suggesting a cast that couldn't help.
- **The manual and the tutorials say which Cabby they're for**, at the
  top, and `make test` checks it's this one.

*Version 0.32.0:*

- **Three new parts of the manual, chapters 91 to 106**, each with a
  tutorial:
  - *Talking over a network*: a chat server with visitors of its own, a
    recipe web site visited by a worker, a live scoreboard over
    WebSockets, an encrypted site and server with a certificate of their
    own, and a lesson reminder by email.
  - *The computer*: `system` and `os` (a folder watched, a signal caught),
    power, brightness and volume, `users`, `linux`, and `secrets`.
  - *The desktop*: notifications and file kinds, what's playing,
    printing, speech, Bluetooth, and D-Bus.
  The tutorials that would make a noise, print or change something only
  do it when asked (`cabby 101-desktop.cby show`); the ones whose output
  is the computer's own (`// output varies`) are checked to run cleanly,
  and show one computer's as an example.
- **A field read while another field is lent**: `Said.add Port.text`
  (inside an object) and `X.L.add X.N.text` were refused, as if reading
  `Port` read all of the object. Reading one field reads only that part
  of it now, as changing one already did.

*Version 0.33.0:*

- **A read-through of the manual and the tutorials**, before the one done
  together:
  - "Changing files in place" uses bytes, but came before them: it's
    chapter 46 now, after Bytes, and 33 to 45 each moved down one.
  - 53 links whose number said one chapter and led to another
    (Appendix C's table, mostly), put right; Appendix I lists all 45
    libraries' pages (ten were missing).
  - Every chapter has an "In short" and a "Try this" (34 were missing
    one or both), and every tutorial a "Try this" and its output marked
    as the rest are.
  - "Not in Cabby yet" no longer lists menus, tabs and tables; the
    hashes page points to `secrets` for passwords and random numbers;
    tutorial 64 no longer promises channels (chapter 65's `tell` is
    how).
  - More examples for chapters 92 to 106; US spelling (canceled,
    Notification Center); "parentheses", not "bracket".
- Comparing two `bytes` (or anything else with `same_as`) with `==`
  says to use `.same_as`.

*Version 0.33.1:*

- **From the read-through together:**
  - In the interactive session, a `/*` comment left open carries on, with
    the `...` prompt, until its `*/` (it was an error), and the lines
    inside it are colored as a comment.
  - The session starts a new line before each prompt, so a `print` typed
    a line at a time looks different there than in a program. "How to
    read this book", Chapter 1, Appendix B and the READMEs say so, and
    that lines typed between `{` and `}` run as one input.

*Version 0.33.2:*

- Chapter 4 said to check that floating-point results are close rather
  than equal, but not how: it shows `abs(A - B) < limit` now, for a
  `double` and a `float`, and how big the limit should be for each.

*Version 0.33.3:*

- In the interactive session, the lines of a `/*` comment that begins
  its line start ` * `, so the stars line up as they do in Vim, and a `/`
  typed straight after that closes the comment (` */`).

*Version 0.34.0:*

- **`close_to A, B`**, built in: true when two numbers are the same but
  for rounding, to about 9 significant digits (5 for two `float`s), with
  anything within 1e-12 (1e-6) of zero close to zero. `close_to A, B,
  Limit` is true when they're no more than `Limit` apart.
- **A warning for `==` and `!=`** on floating-point numbers when one side
  is arithmetic or a decimal with a fraction (`0.1 + 0.2 == 0.3`), which
  suggests `close_to`. Building closeness into `==` itself was thought
  about and left out: no one limit suits every size of number, and an
  `==` that isn't exact breaks lookups, sorting and `A == B == C`
  reasoning.
- Chapter 4 has "Close enough", and the manual's tests a
  `<!-- warning-main: TEXT -->` marker; chapter 6's table of comparisons
  and logic is two tables.

*Version 0.35.0:*

- **Three new libraries, for the desktop's settings**, each asking each
  desktop its own way (Sway by swaymsg; GNOME by Mutter over D-Bus and
  gsettings; KDE Plasma by kscreen-doctor; X11 by xrandr, xinput,
  setxkbmap and xset; a Mac by CoreGraphics):
  - `use screens`: each monitor's resolution, refresh, scale, rotation,
    place and modes, and changing them, or turning one off.
  - `use mouse`: the pointer's speed, natural scrolling and left-handed
    buttons, the touchpad's own and tapping to click, and the cursor's
    size and theme.
  - `use keyboard`: the layouts (`"us"`, `"de(neo)"`), how a held key
    repeats, and whether Caps Lock and Num Lock are on.
- Chapter 107, "Screens, the mouse and the keyboard", with tutorial
  107-screens.cby; the reference pages screens.md, mouse.md and
  keyboard.md.
- Tested against stand-ins for each desktop's programs, and a stand-in
  Mutter written in Cabby on a session bus of the test's own.

*Version 0.35.1:*

- **From the read-through together**, in the interactive session:
  - `if Count > 0 {    // why` opened its block, but the next line wasn't
    indented for it: the comment hid the `{`.
  - "this line should be indented by 2 tabs, not 1" counted the tab the
    session puts in front of each statement (they're inside `main`):
    it counts only the tabs typed now ("by 1 tab, not 0").
- **WebSockets on a Mac, now and then** (tutorial 93 aborting or hanging
  in GitHub's tests): a server in the main thread and a worker opening
  connections to it could both add to the table of connections at once,
  and one growing it with realloc could free it under the other. Two
  causes, both fixed:
  - Until the first worker, the main thread runs without taking the
    interpreter's lock, though it counts as holding it; when the worker
    started, it still didn't, so the two ran Cabby code together. It
    takes the lock for real when the first worker starts.
  - The tables of connections, servers, watched files and timers never
    move now (a fixed size, far more than a program needs), and a slot
    is claimed under a lock of their own, as library code runs in more
    than one thread at once.
  - Found with ThreadSanitizer, which is clean on tutorials 63, 91 to
    94 now; tutorial 93 ran 300 times without a fault.
- **The MIDI test keyboard's notes, now and then lost** (tutorial 76,
  "0 notes" on GitHub's Linux): `play_through` starts the test's file
  playing, which is read as fast as it can be, and its notes came before
  the next line's `on_note`, so they were told to no one. The test's file
  keeps its notes for the handler (a real keyboard's notes from before a
  handler still aren't told: no one could play them that soon).

*Version 0.36.0:*

- **Conditions in symbols only.** `is`, `is not`, `and`, `or` and `not`
  (since the early versions, as words for `==`, `!=`, `&&`, `||` and `!`)
  are taken out: two ways of writing each meant learning both to read
  Cabby's own code, which used both about equally, and `==` is known at a
  glance. Each is now an error saying what to write ("Cabby writes '==',
  not 'is'"), and stays reserved; the session shows them in red as
  they're typed.
  - `tools/words-to-symbols.py` rewrote the libraries, tutorials, manual,
    examples and tests (95 files), leaving strings and comments alone.
    `not` took the whole comparison after it, so `not Tries is 0`
    became `Tries != 0`, and `not files.exists P` became
    `!(files.exists P)`.
  - Chapter 6's "In words" became "`!` and a comparison"; the style
    guide's rule for words became one for writing the opposite
    comparison (`Tries != 0`, not `!(Tries == 0)`).
  - `for X in L` stays: `in` isn't another way of writing an operator.

*Version 0.36.1:*

- On a computer running Sway, `keyboard.layouts` (and tutorial 107)
  stopped with "the environment variable 'CABBY_XKB_RULES' isn't set":
  the tests name their own list of layouts with it, and asking for it
  with `shell.get` when it isn't there is a panic. It asks `shell.has`
  first, and the tests check the layouts without it too.

*Version 0.37.0:*

- **Who's on the network.** `networks.answering Hosts, Port, Wait` asks
  every one of `Hosts` at once whether a program's listening on `Port`
  (as `nmap` does), and takes `Wait` however many there are;
  `networks.nearby_addresses` is the other 253 addresses on a home or
  office network. `networks.pings Host, Wait` and `ping_time` send a
  ping, by an ICMP socket of the program's own where the system lets
  any program (a Mac; Linux with systemd's ping_group_range), and by the
  `ping` command otherwise. Chapter 41 has "Who's on the network".
- **A file's own name is a prefix for its functions**, as a library's
  is: `use local kit/tools` lets `tools.cby` define `tools.twice`, with
  nothing else declared. Chapter 33's Namespaces shows it.
- From the read-through together: chapter 34 has "A Cabby program on the
  other end", on input (`send`, `read_line`) and arguments (`Argv`) and
  what happens sending to a program that's ended; chapter 51 no longer
  says gui's windows can't be read by a screen reader (they can, on
  Linux).

## Along the way (dates not recorded)

- **Names**: plain whole words for libraries (`strings`, `directories`,
  `networks`, with no underscores where they can be avoided), and US
  spelling throughout (`set_color`).
- **`lookup`**, chosen over "dict" and "map", as a word of Cabby's own.
- **The icons**: green pages in the style of Papirus Dark, with white
  "cby" and "hby" lettering and the top corner folded over. (The first
  colors, yellow and blue, were replaced; the cabbage logo is kept for
  Cabby's website.)
- **Text to numbers**: `.to_int`, `.to_long` and `.to_double`, with
  `.is_int`, `.is_long` and `.is_number` to check first, and
  `end_of_input` to tell an empty line from no more input.
- **`extended`** decided as a fixed 16-byte integer (not built yet).
