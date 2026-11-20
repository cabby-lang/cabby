# Cabby tutorials

*For Cabby 0.37.1 (2026-11-20).*

A hundred and six short programs, each about one keyword, feature or library, in
an order where each uses only what came before it. Each one
explains itself in comments, runs as it is, and ends with ideas to try and
the exact output it prints. Read one, run it, then change it.

**[The Cabby Manual](tutorials.md)** (`tutorials.md`) goes with them: a book
with one chapter per program, explaining each topic in more depth with short
examples, plus appendices for looking things up (keywords, types,
operators, the `cabby` command, the interactive session and the gui
library). Read a chapter, then run its program.

## Getting started

Install Cabby once, from the source folder:

```
make
sudo make install
```

`cabby` then works from any folder, and these tutorials are installed in
`/usr/local/share/doc/cabby/tutorials`. Copy them somewhere of your own
before editing them:

```
cp -r /usr/local/share/doc/cabby/tutorials ~/cabby-tutorials
cd ~/cabby-tutorials
cabby 01-hello.cby
```

Three ways to run Cabby:

| | |
|---|---|
| `cabby file.cby` | run a program (with the interpreter) |
| `cabby -c file.cby` | compile it to a native program, then run `./file` |
| `cabby` | the interactive session: type Cabby a line at a time |

The interactive session is the quickest way to experiment while you read.
Declare a variable, call a function, see the answer straight away. Type
`exit` when you're done; Cabby offers to save what you typed as a `.cby`
file. One difference from a program: each prompt starts on a new line, so
output that a `print` leaves unfinished carries on below it. Type the lines
between `{` and `}` to run them as one input and see them as a program would. (`sudo make uninstall` removes everything `make install` put in place.)

## Conventions in these files

* The block at the end of each file, after `// ---- output`, lists what the
  program prints. Each line starts with `//> `.
* Lines such as `// args:` or `// panic:` tell Cabby's test suite how to
  run a tutorial. `make test` runs every tutorial, interpreted and compiled,
  and every example in the manual, and checks their output, so what you
  read is what you'll get.
* "Try this" suggestions at the end of a file often produce an error on
  purpose. Reading Cabby's error messages is half of learning the language.
* The tutorials follow the style guide (`STYLE.md`, and the manual's
  Appendix J): variables start each word with a capital (`TotalPrice`),
  functions and types are lower case (`say_hello`, `point`), and constants
  are capitals (`DAYS_PER_WEEK`). `cabby --style` checks a file's names.

## The tutorials

**Part I: The basics**

| | File | Covers |
|---|---|---|
| 1 | `01-hello.cby` | `main`, `print`, `println`, comments, layout rules |
| 2 | `02-variables.cby` | `int` `double` `char` `bool` `true` `false` `str` |
| 3 | `03-integers.cby` | `char` `short` `int` `long` `signed` `unsigned` `sizeof`, wrapping, bits |
| 4 | `04-floating-point.cby` | `float` `double`, conversions, casts |
| 5 | `05-const.cby` | `const` |

**Part II: Control flow**

| | File | Covers |
|---|---|---|
| 6 | `06-if-else.cby` | `if` `else`, `&&` `\|\|` `!`, conditions |
| 7 | `07-while-do.cby` | `while` `do` |
| 8 | `08-for-break-continue.cby` | `for` `in` `to` `down` `by` `break` `continue` |
| 9 | `09-switch.cby` | `switch` `case` `default`, fall-through |

**Part III: Functions and data**

| | File | Covers |
|---|---|---|
| 10 | `10-functions.cby` | `void` `return`, parameters, calls with and without parentheses, recursion |
| 11 | `11-arrays.cby` | arrays, `.length`, `T name[]` parameters, bounds checks |
| 12 | `12-strings.cby` | `str`: `.length`, `s[i]`, `+`, `.copy`, `.text`, comparing |
| 13 | `13-ownership.cby` | copies, moves, drops: the heart of Cabby |
| 14 | `14-references.cby` | `T &` and `const T &`, the borrowing rules |
| 15 | `15-lists.cby` | `list`: `.add`, `.remove_at`, `for X in L`, `.sort`, `.split` and `.join` |
| 16 | `16-lookups.cby` | `lookup`: values by key, `for Key, Value in L`, counting |
| 17 | `17-struct.cby` | `struct` |
| 18 | `18-union.cby` | `union` |
| 19 | `19-enum.cby` | `enum` |
| 20 | `20-aka.cby` | `aka`: another name for a type |
| 21 | `21-static-and-globals.cby` | `static`, globals |

**Part IV: Objects**

| | File | Covers |
|---|---|---|
| 22 | `22-objects.cby` | `object`, methods, `self`, `const` and `static` methods |
| 23 | `23-destructors.cby` | `name { }` when an object is made, `~name` when it is dropped, and the order |
| 24 | `24-inheritance.cby` | `object a : b`, overriding, virtual calls |
| 25 | `25-multiple-inheritance.cby` | several parents, diamonds, method order |
| 26 | `26-your-own-handlers.cby` | `handler` fields, `.call`, `.is_set`, `.clear`: objects that call back their owner |

**Part V: Close to the machine**

| | File | Covers |
|---|---|---|
| 27 | `27-c-functions.cby` | `extern`, `use`, the C library |
| 28 | `28-addresses.cby` | `use addresses`: `address int`, `addresses.of`, `.value`, stepping, C pointers |
| 29 | `29-register-volatile.cby` | `register` `volatile` |
| 30 | `30-goto.cby` | `goto`, labels |

**Part VI: Programs in the world**

| | File | Covers |
|---|---|---|
| 31 | `31-main-arguments.cby` | `argc`/`argv`, exit status |
| 32 | `32-files.cby` | `use files`, `use directories`: reading, writing, listing |
| 33 | `33-your-own-headers.cby` | `use local`, `.hby` headers, `geometry/` |
| 34 | `34-processes.cby` | `use processes`: starting programs, sending and reading lines, `wait`, `stop`, handlers |
| 35 | `35-events.cby` | `use events`: timers, `events.run`, `events.stop`, one loop for timers, processes and windows |
| 36 | `36-reverse.cby` | a whole program: your reverse, with `reverse/` |

**Part VII: When it fails**

| | File | Covers |
|---|---|---|
| 37 | `37-panics.cby` | run-time errors |
| 38 | `38-when-it-fails.cby` | `else` after a statement: a file that may not be there, a class's marks with mistakes in them skipped and told why (`else Why { }`), `else` on an assignment and a call |

**Part VIII: Every day**

| | File | Covers |
|---|---|---|
| 39 | `39-dates.cby` | `use time` and `use dates`: `duration`, `moment`, `date`, patterns like `"d mmmm yyyy"` |
| 40 | `40-json.cby` | `use json`: `json.read`, `J["name"]`, `.str` `.long` and `_or`, building JSON, `.pretty`, structs with `json.from` and `.fill` |
| 41 | `41-downloads.cby` | `use networks`: `networks.download`, `download_to`, `can_reach`, `status` |
| 42 | `42-web-addresses.cby` | `networks.read_url` and `networks.url`: an address's parts, its query, encoding, making an address |
| 43 | `43-html.cby` | `use html`: `html.read`, CSS selectors with `find` and `find_all`, `.text`, `.attribute`, building a page |
| 44 | `44-xml.cby` | `use xml`: a news feed, `xml.can_read`, writing XML |
| 45 | `45-picture-size.cby` | `use bytes`: hexadecimal, `B[i]`, numbers inside bytes and byte order, reading pictures' sizes, making a BMP picture |
| 46 | `46-save-game.cby` | `files.open_to_change`, `move_to`, `read_bytes`, `write_bytes`: a save file changed in place |
| 47 | `47-patterns.cby` | `use patterns`: regular expressions to find, check, take apart, replace and split text, in any alphabet |
| 48 | `48-hashes.cby` | `use hashes`: SHA-256 and MD5 of text and files, checking a download, finding which files changed |
| 49 | `49-archives.cby` | `use archives`: zipping a folder, listing and reading inside, unpacking safely, a .tar.gz backup, gzipping one file |
| 50 | `50-databases.cby` | `use databases`: a recipe book in SQLite, tables, `?`s, joins and sums for costs, a shopping list, begin, commit and rollback |

**Part IX: Windows**

| | File | Covers |
|---|---|---|
| 51 | `51-first-window.cby` | `gui.window`, `build`, labels, buttons, text boxes, handlers |
| 52 | `52-small-app.cby` | rows, several handlers, `on_change`, numbers as text (`.text`) |
| 53 | `53-more-widgets.cby` | check boxes, drop-downs, sliders, text areas, lists, columns |
| 54 | `54-timers.cby` | `every`, `after`, `stop_timers`, progress bars |
| 55 | `55-dialogs.cby` | `ask`, `ask_open_file`, `show_message`, pictures (`pictures/`) |
| 56 | `56-menus.cby` | `add_menu`, `add_item`, shortcuts like `"Ctrl+U"`, `add_shortcut`, graying out with `set_enabled`: text tools |
| 57 | `57-tabs.cby` | `add_tabs`, `add_tab`, `on_change`, `add_scrolling_column`: a recipe card |
| 58 | `58-tables.cby` | `add_table`, `add_row`, `cell`, `set_cell`, `on_select`, `remove_row`, sorting: the planets |
| 59 | `59-choosers.cby` | `add_number`, `add_color_button`, `add_font_button`, `on_change`, a canvas's `set_font`: a name badge |
| 60 | `60-drawing.cby` | `add_canvas`, `on_draw`, `redraw`, shapes and colors, `on_click`, `on_key`: a rainfall chart |
| 61 | `61-phrasebook.cby` | text in any script in a window: Hebrew and Arabic right to left, Arabic joined, emoji in color: a phrasebook in seven languages |
| 62 | `62-terminal-window.cby` | `tui.window`: the same widgets in a terminal, keys, a list kept behind a `tui.list` |

**Part X: Doing things at once**

| | File | Covers |
|---|---|---|
| 63 | `63-workers.cby` | `use threads`: workers counting primes a quarter each, another job alongside, `start`, `wait`, `is_done`, what keeps threads safe |
| 64 | `64-done-later.cby` | `on_done`: a window that counts primes on a worker, and carries on meanwhile; `set_enabled` |
| 65 | `65-progress.cby` | `tell`, `on_message` and `ask_to_stop`: tutorial 64's window with a progress bar and a Cancel button |

**Part XI: Pictures and videos**

| | File | Covers |
|---|---|---|
| 66 | `66-painting.cby` | `use images`, `images.new`, `set_pixel_rgb`, `tell_picture` and `picture`: a worker paints the Mandelbrot set a strip at a time, and a window shows each strip as it comes |
| 67 | `67-video-player.cby` | `use videos`: a window that plays a video, with its controls; what's in a video, `seek`, `on_end`, `picture_at` |

**Part XII: Games**

| | File | Covers |
|---|---|---|
| 68 | `68-games.cby` | `game.window`, sprites, `on_frame`, `key_hold`, `every`, `random`, `pause`, drawing over the stage: catch the apples |
| 69 | `69-walking.cby` | `animate`, `set_picture`, a map of tiles, walking tile by tile, facing, reading a sign: a small town |
| 70 | `70-touching.cby` | `touches`, `set_group`, `sprites_in`, `touching`, `on_stage`, `move_forward`, `sound.play`: space rocks |
| 71 | `71-exploring.cby` | `set_world_size`, `follow`, the camera, a lookup of signs: a bigger world to explore |
| 72 | `72-sound.cby` | `use sound`: `start_note`, `stop_note`, `play_notes`, `play_chord`, `set_instrument`: a keyboard to play |
| 73 | `73-world-in-files.cby` | maps read from files (`maps/`), areas and ways between them, `set_layer`, `sort_by_depth`, people who turn and talk: the woods and Cabby Town |

**Part XIII: Sound and music**

| | File | Covers |
|---|---|---|
| 74 | `74-songs.cby` | `sound.part` and `sound.song`: a twelve bar blues written in beats, saved as a MIDI file, read back and transposed |
| 75 | `75-recording.cby` | `sound.record`, `sound.inputs`, a level meter, `trim_silence`, saving a WAV file, recording in time with a click: a recorder |
| 76 | `76-keyboard.cby` | `sound.open_keyboard`, `play_through`, `on_note` and `on_control`: a MIDI keyboard played as a piano, its notes written down and saved as MIDI |

**Part XIV: Cabby Quest**

| | File | Covers |
|---|---|---|
| 77 | `77-quest-map.cby` | the map from a file, ground and what stands on it, layers, the camera |
| 78 | `78-quest-walking.cby` | a `walker` object, walking tile by tile, what's in the way, signs, words in a box |
| 79 | `79-quest-areas.cby` | areas joined edge to edge (and drawn beyond it), `set_world_area`, an area's name, a door into the bakery, fading to black and back |
| 80 | `80-quest-sound.cby` | MIDI music for each area, fading it, sound effects, words a word at a time |
| 81 | `81-quest-people.cby` | townspeople as walkers, wandering, who's in the way, turning to talk |
| 82 | `82-quest-talking.cby` | questions with answers to choose, remembering them, the baker's quest |
| 83 | `83-quest-saving.cby` | things to pick up and give, `directories.save_folder`, saving and loading with JSON |
| 84 | `84-quest-menus.cby` | a title screen (New game, Continue, Options), the game's menu, options: the whole game |
| 85 | `85-quest-noticed.cby` | people who notice the hero: a watched tile, line of sight, a "!" bubble, coming over to talk, a rival with his own music |
| 86 | `86-quest-hills.cby` | ground with two heights, as in FireRed: ledges to hop down, high ground and stairs up to it, a bridge to walk over and under, layers for what's over whom |
| 87 | `87-quest-battle.cby` | Cabby Quest, part 11: tall grass, and a battle in it as FireRed's are: a menu, moves, health bars, who goes first, how hard a hit is |
| 88 | `88-quest-rival.cby` | Cabby Quest, part 12: Rufus's battle, which can't be run from; experience and levels; the elder heals |

**Part XV: Three dimensions**

| | File | Covers |
|---|---|---|
| 89 | `89-coins.cby` | `use world`: 3D. Boxes, balls, planes, cylinders and cones, in metres; turning and walking, a camera that follows, touching, text over the world, a lamp: a walk round a field picking up coins |
| 90 | `90-robot.cby` | `add_model`: a model made in Blender (a glTF `.glb` file, in `models/`), its picture, its bones and its animations: `play`, `play_once`, `stop_animation`, `playing` |

**Part XVI: Talking over a network**

| | File | Covers |
|---|---|---|
| 91 | `91-chat-server.cby` | `networks.server` and `networks.connection`: a chat room on a port, each connection's name in a lookup, two visitors who join, talk and leave |
| 92 | `92-web-server.cby` | `networks.web_server`: a recipe site with pages, a search, a form, JSON, a cookie and files from a folder, visited by a worker |
| 93 | `93-websockets.cby` | WebSockets: a live scoreboard, with fans who see each goal as it's scored |
| 94 | `94-encrypted.cby` | HTTPS and TLS: a site and a server with a certificate of their own, and a visitor who trusts it |
| 95 | `95-emails.cby` | `use emails`: a lesson reminder with a copy, a hidden copy, HTML and an attachment, saved and read back |

**Part XVII: The computer**

| | File | Covers |
|---|---|---|
| 96 | `96-the-computer.cby` | `system` and `os`: what the computer is, how busy, its disks and programs; a folder watched, and a signal caught |
| 97 | `97-power-and-volume.cby` | the battery, the screen's brightness and the volume, as a status bar shows them |
| 98 | `98-users.cby` | `use users`: who's running it, their groups, and everyone else on the computer |
| 99 | `99-linux.cby` | `use linux`: temperatures, disks, USB devices, modules, a kernel setting, and services |
| 100 | `100-secrets.cby` | `use secrets`: random codes, a password's hash, a secret and a file locked, messages signed |

**Part XVIII: The desktop**

| | File | Covers |
|---|---|---|
| 101 | `101-desktop.cby` | `desktop.notify`, `notify_again`, `desktop.open`, and `mime`: each file's kind, by its name and by what's in it |
| 102 | `102-media.cby` | `use media`: what's playing, and next, back, pause and play |
| 103 | `103-printing.cby` | `use printing`: the printers, and a practice sheet printed twice, on both sides |
| 104 | `104-speech.cby` | `use speech`: English and Latin said aloud, saved, and heard back |
| 105 | `105-bluetooth.cby` | `use bluetooth`: the devices known, and a search |
| 106 | `106-dbus.cby` | `use dbus`: the services on the session bus, and who shows notifications |
| 107 | `107-screens.cby` | `use screens`, `use mouse`, `use keyboard`: the monitors, the mouse and touchpad, and the keyboard's layouts |

Every keyword in the language appears in at least one tutorial. The manual's
appendices and the main README (installed as
`/usr/local/share/doc/cabby/README.md`) are the full reference.
