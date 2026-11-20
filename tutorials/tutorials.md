# The Cabby Manual

*For Cabby 0.39.2 (2026-11-20).*

A book for learning Cabby one idea at a time, and a reference to come back
to afterwards.

Cabby is a small, natively compiled language with a quiet syntax: no
semicolons, no parentheses around conditions, tabs and braces for blocks.
Every value has an owner, so memory is freed for you, and most mistakes are
caught before a program ever runs. It has objects with multiple
inheritance, calls straight into the C library, and a `gui` library for
windows.

Each chapter goes with one of the tutorial programs in this folder
(`01-hello.cby` to `106-dbus.cby`). The chapter explains; the program
lets you see it all working together and change it. Read a chapter, run its
program, then try the exercises at the end.

## How to read this book

* **In order, the first time.** Each chapter uses only what came before it.
  Parts I to III are the core of the language; after [Chapter 16](#chapter-16-lookups)
  you know all of Cabby's central ideas.
* **Run the examples.** Every example that shows output was run to produce
  that output, and Cabby's test suite checks them all again (`make test`),
  so what you read is what you'll get. The quickest way to try one is the
  interactive session ([Appendix B](#appendix-b-the-interactive-session)):
  type `cabby` and paste the lines in. One thing looks different there:
  the session starts a new line before each prompt, so when a `print`
  leaves a line unfinished for the next input to carry on, the output
  comes out over more lines than shown. To see it exactly as shown, type
  the lines between `{` and `}`, which the session runs as one input, or
  put them inside `int main { ... }` in a file and run that.
* **Read the error messages.** Many exercises go wrong on purpose. Cabby's
  messages say what is wrong, where, and usually how to fix it; learning to
  read them is half of learning the language.
* **Code blocks use tabs.** Cabby requires a tab for each level of
  indentation ([Chapter 1](#chapter-1-hello-cabby)). If you copy an example
  from a web page that turned its tabs into spaces, Cabby will tell you.

Short examples are written as the inside of `main`, to keep them short. To
run one as a file, put it inside `int main { ... }`, one tab further in:

```cabby
int main {
	println "the example goes here"
}
```

## Contents

**[Part I: The basics](#part-i-the-basics)**

1. [Hello, Cabby](#chapter-1-hello-cabby): `main`, `print`, `println`, comments, the layout rules
2. [Variables and the basic types](#chapter-2-variables-and-the-basic-types): `int` `double` `char` `bool` `str`
3. [Integer types](#chapter-3-integer-types): sizes, `signed` and `unsigned`, overflow, bits
4. [Floating point](#chapter-4-floating-point): `float` and `double`, conversions and casts
5. [Constants](#chapter-5-constants): `const`

**[Part II: Control flow](#part-ii-control-flow)**

6. [If and else](#chapter-6-if-and-else): conditions, `==` `!=` `&&` `||` `!`, short-circuiting
7. [While, until and do](#chapter-7-while-until-and-do): loops that test a condition
8. [For loops, break and continue](#chapter-8-for-loops-break-and-continue): `for i in 1 to 10`, the three-part `for`
9. [Switch](#chapter-9-switch): `case`, `default`, fall-through

**[Part III: Functions and data](#part-iii-functions-and-data)**

10. [Functions](#chapter-10-functions): parameters, `return`, the two ways to call
11. [Arrays](#chapter-11-arrays): fixed-size arrays, `.length`, bounds checks
12. [Strings](#chapter-12-strings): `str`, `s[i]`, `+`, `.find`, `.replace`, slices, `.text`, `.to_int`, `input`, `read_line`
13. [Ownership: moves and drops](#chapter-13-ownership-moves-and-drops): who frees what, and when
14. [References: borrowing](#chapter-14-references-borrowing): `T &` and `const T &`
15. [Lists](#chapter-15-lists): `list`, adding and removing, `for X in L`, printing, `.sort`, `split` and `join`
16. [Lookups](#chapter-16-lookups): `lookup`, values by key, `for Key, Value in L`, counting
17. [Structs](#chapter-17-structs): plain data with named fields
18. [Unions](#chapter-18-unions): several views of the same bytes
19. [Enums](#chapter-19-enums): named integer constants
20. [aka: another name for a type](#chapter-20-aka-another-name-for-a-type): `double aka celsius`
21. [Static and global variables](#chapter-21-static-and-global-variables): values that outlive a call

**[Part IV: Objects](#part-iv-objects)**

22. [Objects and methods](#chapter-22-objects-and-methods): `object`, `self`, `const` and `static` methods, [what goes where](#what-goes-where)
23. [Made and dropped](#chapter-23-made-and-dropped): `name { }` when an object is made, `~name` when it's dropped
24. [Inheritance](#chapter-24-inheritance): `object dog : animal`, overriding, virtual calls
25. [Multiple inheritance](#chapter-25-multiple-inheritance): several parents, diamonds, method order
26. [Handlers in your own objects](#chapter-26-handlers-in-your-own-objects): `handler` fields, `.call`, what keeps them safe

**[Part V: Close to the machine](#part-v-close-to-the-machine)**

27. [Calling C](#chapter-27-calling-c): `extern`, `use`, the C library
28. [Addresses](#chapter-28-addresses): `use addresses`, where things are in memory, C pointers
29. [Register and volatile](#chapter-29-register-and-volatile): two storage hints
30. [Goto and labels](#chapter-30-goto-and-labels): forward jumps out of loops

**[Part VI: Programs in the world](#part-vi-programs-in-the-world)**

31. [Command-line arguments and exit status](#chapter-31-command-line-arguments-and-exit-status): `Argc`, `Argv`, [stderr](#errors-go-to-stderr)
32. [Files, directories and commands](#chapter-32-files-directories-and-commands): `use files`, `use directories`, `use shell`
33. [Splitting a program into files](#chapter-33-splitting-a-program-into-files): `use local`, headers
34. [Processes](#chapter-34-processes): `use processes`, programs your program starts and talks to
35. [Timers and the event loop](#chapter-35-timers-and-the-event-loop): `use events`, `timer`, `events.run`
36. [A whole program: reverse](#chapter-36-a-whole-program-reverse): putting it together

**[Part VII: When it fails](#part-vii-when-it-fails)**

37. [Panics](#chapter-37-panics): the errors only a running program can find
38. [A class's marks](#chapter-38-a-classs-marks): `else` after a statement: `else Value`, `else { }`, `else Why { }`

**[Part VIII: Every day](#part-viii-every-day)**

39. [Time and dates](#chapter-39-time-and-dates): `use time` and `use dates`, durations, calendars, patterns
40. [JSON](#chapter-40-json): `use json`, reading and writing the data web services and settings use, structs to JSON and back; tables in CSV
41. [Downloading](#chapter-41-downloading): `use networks`, fetching pages, files and web services' answers
42. [Web addresses](#chapter-42-web-addresses): `networks.read_url`, `networks.url`, encoding and decoding
43. [HTML](#chapter-43-html): `use html`, web pages: reading them with CSS selectors, and making them
44. [XML](#chapter-44-xml): `use xml`, documents with tags of your own: reading and writing them
45. [Bytes](#chapter-45-bytes): `use bytes`, files that aren't text: hexadecimal, numbers inside, byte order
46. [Changing files in place](#chapter-46-changing-files-in-place): `files.open_to_change`, moving around in a file, formats of your own
47. [Patterns](#chapter-47-patterns): `use patterns`, regular expressions: finding, checking, taking apart and replacing text
48. [Hashes](#chapter-48-hashes): `use hashes`, fingerprints of text and files: checking downloads, finding what changed
49. [Archives](#chapter-49-archives): `use archives`, zip files, tar files and gzipped files: making, listing, reading, unpacking
50. [Databases](#chapter-50-databases): `use databases`, SQLite: tables, `?`s, rows, joins and sums, changes kept together

**[Part IX: Windows](#part-ix-windows)**

51. [Your first window](#chapter-51-your-first-window): `gui.window`, [the build method](#the-build-method), buttons, text boxes, handlers
52. [A small app](#chapter-52-a-small-app): rows, several handlers, numbers as text
53. [More widgets](#chapter-53-more-widgets): check boxes, drop-downs, sliders, text areas, lists
54. [Timers](#chapter-54-timers): `every`, `after`, `stop_timers`
55. [Dialogs and several windows](#chapter-55-dialogs-and-several-windows): messages, questions, files, pictures
56. [Menus and shortcuts](#chapter-56-menus-and-shortcuts): `add_menu`, `add_item`, keyboard shortcuts, graying out
57. [Tabs and scrolling](#chapter-57-tabs-and-scrolling): `add_tabs`, `add_tab`, `add_scrolling_column`
58. [Tables](#chapter-58-tables): `add_table`, rows and cells, sorting, choosing a row
59. [Numbers, colors and fonts](#chapter-59-numbers-colors-and-fonts): `add_number`, `add_color_button`, `add_font_button`
60. [Drawing](#chapter-60-drawing): `add_canvas`, `on_draw` and `redraw`, shapes, colors, the mouse and keys
61. [A phrasebook](#chapter-61-a-phrasebook): text in any script, right to left and joined; emoji in color
62. [Windows in a terminal](#chapter-62-windows-in-a-terminal): `use tui`, the same windows drawn in text

**[Part X: Doing things at once](#part-x-doing-things-at-once)**

63. [Workers](#chapter-63-workers): `use threads`, `threads.worker`, `start`, `wait`, `is_done`, sharing out a job, what keeps it safe
64. [Done later](#chapter-64-done-later): `on_done`, a window that carries on while a worker works
65. [Progress](#chapter-65-progress): `tell`, `on_message`, `ask_to_stop`, `stopping`, a progress bar and a Cancel button

**[Part XI: Pictures and videos](#part-xi-pictures-and-videos)**

66. [Painting](#chapter-66-painting): `use images`, pictures in memory, `set_pixel_rgb`, `tell_picture`: a worker that paints, and a window that shows it
67. [A video player](#chapter-67-a-video-player): `use videos`, what's in a video, `add_video`, `show_controls`, `seek`, `on_end`, `picture_at`

**[Part XII: Games](#part-xii-games)**

68. [Sprites and frames](#chapter-68-sprites-and-frames): `game.window`, sprites, `on_frame`, `key_press`, `key_hold`, `key_release`
69. [A walking character](#chapter-69-a-walking-character): `animate`, a map of tiles, walking tile by tile
70. [Touching](#chapter-70-touching): `touches`, groups, `sprites_in`, `touching`, `on_stage`
71. [A bigger world](#chapter-71-a-bigger-world): `set_world_size`, `follow`, the camera
72. [Sound](#chapter-72-sound): `use sound`, sound files, notes, instruments, MIDI files
73. [A world in files](#chapter-73-a-world-in-files): maps read from files, areas, `set_layer`, `sort_by_depth`, people who talk

**[Part XIII: Sound and music](#part-xiii-sound-and-music)**

74. [Songs](#chapter-74-songs): `sound.part`, `sound.song`, notes in beats, saving and reading MIDI files, transposing
75. [Recording](#chapter-75-recording): `sound.record`, inputs, a meter, saving WAV files, recording in time with a click
76. [Keyboards](#chapter-76-keyboards): `sound.open_keyboard`, `play_through`, `on_note`, `on_control`, synthesizers, writing down what's played

**[Part XIV: Cabby Quest](#part-xiv-cabby-quest)**

77. [The map](#chapter-77-the-map): a game built a part at a time, sections of ten lines
78. [Walking, and signs](#chapter-78-walking-and-signs): an object for anyone who walks, what's in the way, words in a box
79. [Areas, fades, and going inside](#chapter-79-areas-fades-and-going-inside): areas joined edge to edge, `set_world_area`, a door, fading to black and back, an area's name
80. [Music and sounds](#chapter-80-music-and-sounds): MIDI music per area, fading it, sound effects, words a word at a time
81. [People who walk about](#chapter-81-people-who-walk-about): a list of walkers, wandering, turning to talk
82. [Conversations, and a quest](#chapter-82-conversations-and-a-quest): questions and answers, remembering them, a quest
83. [Things to carry, and saving](#chapter-83-things-to-carry-and-saving): picking up, giving, `directories.save_folder`, saving as JSON
84. [Menus](#chapter-84-menus): a title screen, a menu in the game, options
85. [Being noticed](#chapter-85-being-noticed): a watched tile, line of sight, a "!" bubble, coming over to talk, a rival
86. [The hills](#chapter-86-the-hills): ledges to hop down, high ground and stairs, a bridge to walk over and under
87. [A battle](#chapter-87-a-battle): tall grass, a creature met in it, a battle a turn at a time: moves, health bars, who goes first, how hard a hit is
88. [The rival's battle](#chapter-88-the-rivals-battle): a battle with someone, no running away, experience and levels, healing

**[Part XV: Three dimensions](#part-xv-three-dimensions)**

89. [Coins in a field](#chapter-89-coins-in-a-field): `use world`, shapes in 3D, metres and degrees, `move_forward`, `follow`, `touches`, `add_text`, lamps
90. [A robot from Blender](#chapter-90-a-robot-from-blender): `add_model`, glTF files, `play`, `play_once`, `stop_animation`

**[Part XVI: Talking over a network](#part-xvi-talking-over-a-network)**

91. [Servers and connections](#chapter-91-servers-and-connections): `networks.server`, `listen`, `on_connect`, `on_line`, `on_close`, `reply`, `send_line_to_all`; `networks.connect`
92. [A web server](#chapter-92-a-web-server): `networks.web_server`, `on_request`, `path`, `query_value`, `form_value`, `send_page`, `send_json`, `redirect`, cookies, files from a folder
93. [WebSockets](#chapter-93-websockets): `wants_socket`, `accept_socket`, `on_message`, `send_message_to_all`; `networks.open_socket`
94. [Encrypted](#chapter-94-encrypted): `make_certificate`, `set_certificate`, `trust_certificate`, `connect_encrypted`: HTTPS, wss and TLS
95. [Emails](#chapter-95-emails): `emails.message`, `attach`, `save`; `emails.account_at`, `send`; `emails.mailbox_at`, `unread`

**[Part XVII: The computer](#part-xvii-the-computer)**

96. [The computer](#chapter-96-the-computer): `use system` and `use os`: what it is, its disks, programs, watching files, signals
97. [Power, brightness and volume](#chapter-97-power-brightness-and-volume): `use power`, `use brightness`, `use volume`: the battery, the screen, the sound
98. [Users](#chapter-98-users): `use users`: who's here, their groups; making users, with the administrator
99. [Linux](#chapter-99-linux): `use linux`: sensors, disks and partitions, USB, the kernel's modules and settings, services
100. [Secrets](#chapter-100-secrets): `use secrets`: random codes, password hashes, locking, signatures, public keys

**[Part XVIII: The desktop](#part-xviii-the-desktop)**

101. [The desktop](#chapter-101-the-desktop): `use desktop` and `use mime`: notifications, opening files and web pages, what kind a file is
102. [What's playing](#chapter-102-whats-playing): `use media`: the music playing, and its controls
103. [Printing](#chapter-103-printing): `use printing`: printers, printing files and text, settings, jobs
104. [Speech](#chapter-104-speech): `use speech`: said aloud in many languages, and heard by Whisper
105. [Bluetooth](#chapter-105-bluetooth): `use bluetooth`: devices, searching, pairing, connecting
106. [D-Bus](#chapter-106-d-bus): `use dbus`: calling the desktop's programs, reading properties, being told
107. [Screens, the mouse and the keyboard](#chapter-107-screens-the-mouse-and-the-keyboard): `use screens`, `use mouse`, `use keyboard`: monitors' resolution and rotation, the cursor, layouts

**[Appendices](#appendices)**

* A. [The cabby command](#appendix-a-the-cabby-command)
* B. [The interactive session](#appendix-b-the-interactive-session)
* C. [Keywords](#appendix-c-keywords)
* D. [Types](#appendix-d-types)
* E. [Operators](#appendix-e-operators)
* F. [What is checked, and when](#appendix-f-what-is-checked-and-when)
* G. [The gui library](#appendix-g-the-gui-library)
* H. [Not in Cabby yet](#appendix-h-not-in-cabby-yet)
* I. [Library reference pages](#appendix-i-library-reference-pages)
* J. [Style guide](#appendix-j-style-guide)

---

## Part I: The basics

The first five chapters cover what every program is made of: how a program
is laid out, how it prints, and the kinds of values it can hold.

### Chapter 1: Hello, Cabby

*Program: `01-hello.cby`. Topics: `main`, `print`, `println`, comments, layout.*

#### The smallest program

<!-- run -->
```cabby
int main {
	println "Hello, Cabby!"
}
```

```text
Hello, Cabby!
```

Every program starts at `main`. The `int` in front of it is the type of
value it returns: the program's *exit status*, which tells the shell whether
it worked. 0 means "all went well". If `main` reaches its closing brace
without a `return`, it returns 0.

Save that as `hello.cby` and there are three ways to run it:

```sh
cabby hello.cby           # run it straight away (Cabby interprets it)
cabby -c hello.cby        # compile it to a native program called hello...
./hello                   # ...and run that
cabby                     # no file: the interactive session, a line at a time
```

Both ways of running a file behave exactly the same; compiling gives you a
fast program you can run without Cabby. [Appendix A](#appendix-a-the-cabby-command)
lists everything the `cabby` command can do.

#### print and println

`println` prints its arguments with one space between each, then ends the
line. `print` does the same without ending the line. On its own, `println`
just ends the line.

<!-- main -->
```cabby
println "The answer is", 42
print "one"
print " two"
println " three"
print "no newline yet..."
println
println "Numbers:", 1, 2.5, 'c', true
```

```text
The answer is 42
one two three
no newline yet...
Numbers: 1 2.5 c true
```

Typed into the interactive session a line at a time, `one`, ` two` and
` three` come out on three lines instead: each line runs on its own, and
the session starts a new line before its next prompt. Typed between `{`
and `}`, the lines run as one input and print as shown. So do they when
run as a file (`cabby print.cby`) or compiled (`cabby -c print.cby` and
`./print`), with the lines inside `int main { ... }`.

Notice there are no parentheses around the arguments. A call's arguments
are simply everything after its name, to the end of the line. Parentheses are only
needed to show where the arguments end, when something else follows them or
when they carry on over several lines.
[Chapter 10](#chapter-10-functions) has the full rules.

#### Lines are statements

A statement ends at the end of its line, so there are no semicolons. To
spread one statement over several lines, leave a parenthesis, square bracket
or brace open: the statement carries on until it closes.

<!-- main -->
```cabby
println "No semicolons", "needed"
println("this call",
	"wraps")
```

```text
No semicolons needed
this call wraps
```

A semicolon at the end of a line is an error, and the message says why:

<!-- error-main: statements end at the end of the line -->
```cabby
println "x";
```

```text
error: unexpected ';' after the statement; statements end at the end of the line
```

#### Comments

`//` starts a comment that runs to the end of the line. `/* ... */` can span
several lines.

```cabby
// A line comment.
/* A block comment,
   over two lines. */
```

#### The layout rules

Cabby's layout is part of the language, and it is checked:

1. A block opens with `{` at the end of a line and closes with `}`.
2. Every line inside a block is indented by exactly **one more tab** than
   the line that opened it. Spaces are not allowed for indentation.
3. The closing `}` lines up with the line that opened the block.

<!-- error: indentation must use tabs only -->
```cabby
int main {
    println "four spaces instead of a tab"
}
```

```text
error: indentation must use tabs only (found a space)
note: Cabby indents each block with one tab character; set your editor to
      insert tabs, not spaces, for .cby files
```

Because the layout is fixed, every Cabby program reads the same way, and
a wrongly placed brace can't silently change what a program means.

#### In short

* A program starts at `int main { ... }`; its return value is the exit status.
* `println a, b, c` prints with spaces between and a newline at the end;
  `print` leaves the line open.
* One statement per line, no semicolons; an open `(`, `[` or `{` continues a line.
* One tab per level of indentation, nothing else.

#### Try this

* Indent a line with spaces and read the error.
* Return 3 from `main`, run the program, then type `echo $?` in the shell.
* Compile `01-hello.cby` with `cabby -c` and run the result.

[Contents](#contents) · [Chapter 2 →](#chapter-2-variables-and-the-basic-types)

---

### Chapter 2: Variables and the basic types

*Program: `02-variables.cby`. Keywords: `int` `double` `char` `bool` `true` `false` `str`.*

#### Declaring a variable

A declaration is the type, the name, and optionally `=` and a starting
value:

<!-- main -->
```cabby
int Apples = 3              // a whole number
double Price = 0.45         // a number with a fraction
char Grade = 'A'            // one character, in single quotes
bool Ripe = true            // true or false
str Name = "Granny Smith"   // text, in double quotes
println Name, Apples, Price, Grade, Ripe
println "total:", Apples * Price
```

```text
Granny Smith 3 0.45 A true
total: 1.35
```

These five types are the everyday ones:

| Type | Holds | Example |
|---|---|---|
| `int` | whole numbers, about ±2 billion | `42`, `-7`, `0x2A` |
| `double` | numbers with fractions, about 16 digits | `0.45`, `1e6` |
| `char` | one character (one byte) | `'A'`, `'\n'` |
| `bool` | `true` or `false` | `true` |
| `str` | text of any length | `"hello"` |

[Chapter 3](#chapter-3-integer-types) and [Chapter 4](#chapter-4-floating-point)
cover the other number types, and [Chapter 12](#chapter-12-strings) covers
strings properly.

#### Naming

Variable names start each word with a capital: `Apples`, `DateOfBirth`,
`TotalPrice`. That isn't required by Cabby, but it's the house style
([Appendix J](#appendix-j-style-guide)), and it makes variables stand out
from functions, which are written in lower case with `_` between words
(`say_hello`). One lower-case letter is fine for a counter: `i`, `n`.

#### Everything starts at zero

A variable declared without a value starts at its type's zero: 0, 0.0,
`false`, or the empty string. Cabby has no uninitialized memory, so there is
no "garbage value" to trip over.

<!-- main -->
```cabby
int Count
double Weight
bool Done
str Empty
println Count, Weight, Done, "[" + Empty + "]"
```

```text
0 0.0 false []
```

#### Changing a variable

<!-- main -->
```cabby
int Apples = 3
Apples = Apples + 2
Apples += 1              // the same as Apples = Apples + 1
Apples++                 // add one; Apples-- takes one away
println "apples now:", Apples
```

```text
apples now: 7
```

Every operator has a compound assignment: `+=` `-=` `*=` `/=` `%=` `&=`
`|=` `^=` `<<=` `>>=`. Assignments and `++`/`--` are statements of their
own; they can't be hidden inside a bigger expression.

Several variables can be declared on one line:

<!-- main -->
```cabby
int x = 1, y = 2, z
println x, y, z
```

```text
1 2 0
```

#### Every variable says its type

C (since C23) and many other languages let `auto` stand in for a type,
and work it out from the starting value. Cabby doesn't: the type is
written, so anyone reading `long Big = 5000000000` sees at once what `Big`
is, without working it out. Writing `auto` says which type to write:

<!-- error: Cabby has no 'auto': write the type, so the reader sees it ('double Ratio = ...') -->
```cabby
int main {
	auto Ratio = 0.75
}
```

A long type can be given a short name with `aka`
([chapter 20](#chapter-20-aka-another-name-for-a-type)).

#### Values don't change type by accident

Cabby converts a value to another type for you only when nothing can be
lost: an `int` fits in a `long` or a `double`, so that's automatic. A
`double` doesn't fit in an `int` without losing its fraction, so Cabby asks
you to say so with a *cast*:

<!-- error-main: this would drop the fraction -->
```cabby
int n = 2.5
```

```text
error: expected 'int' for 'n', found 'double'
note: this would drop the fraction; write a cast if you mean it: (int)...
```

<!-- main -->
```cabby
int n = (int)2.5         // a cast: "I know, keep the whole part"
println n
```

```text
2
```

#### Booleans

Comparisons give a `bool`, which can be stored like any other value:

<!-- main -->
```cabby
double Price = 0.45
bool Cheap = Price < 1.0
println "cheap?", Cheap, "expensive?", !Cheap
```

```text
cheap? true expensive? false
```

#### In short

* `type Name = value`; without a value, a variable starts at zero.
* `int`, `double`, `char`, `bool` and `str` are the everyday types, and
  every variable says which it is (Cabby has no `auto`).
* Conversions that could lose information need a cast: `(int)2.5`.

#### Try this

* Print `Grade + 1` where `Grade` is `'A'`. A `char` is also a small number.
  Then print `(char)(Grade + 1)`.
* Declare `int n = 2.5` and read the error.

[← Chapter 1](#chapter-1-hello-cabby) · [Contents](#contents) · [Chapter 3 →](#chapter-3-integer-types)

---

### Chapter 3: Integer types

*Program: `03-integers.cby`. Keywords: `char` `short` `int` `long` `signed` `unsigned` `sizeof`.*

#### Sizes

Cabby has four sizes of integer:

| Type | Bytes | Range (signed) | Range (unsigned) |
|---|---|---|---|
| `char` | 1 | −128 to 127 | 0 to 255 |
| `short` | 2 | −32,768 to 32,767 | 0 to 65,535 |
| `int` | 4 | about ±2.1 billion | 0 to about 4.3 billion |
| `long` | 8 | about ±9.2 × 10¹⁸ | 0 to about 1.8 × 10¹⁹ |

Each comes `signed` (the default: it can be negative) or `unsigned` (zero and
up, with twice the range). `unsigned` on its own means `unsigned int`, and
`long long` is the same as `long`.

`sizeof` gives the size in bytes of a type or a variable:

<!-- main -->
```cabby
long l = 9000000000000000000
println sizeof(char), sizeof(short), sizeof(int), sizeof(long)
println sizeof l
unsigned char Byte = 255
println (int)Byte
```

```text
1 2 4 8
8
255
```

Every `char` type prints as a character, so to see a byte's number, cast it
to `int` as above.

#### Overflow wraps around

When a calculation goes past the end of its type's range, it wraps around to
the other end, like a car's odometer. This is defined behavior, for signed
and unsigned types alike:

<!-- main -->
```cabby
unsigned char Odometer = 255
Odometer++
println "255 + 1 in one byte:", (int)Odometer
int Top = 2147483647
Top++
println "biggest int + 1:", Top
```

```text
255 + 1 in one byte: 0
biggest int + 1: -2147483648
```

#### Division

Dividing two integers throws the remainder away; `%` gives the remainder.
Dividing by zero stops the program with a *panic*
([Chapter 37](#chapter-37-panics)).

<!-- main -->
```cabby
println 17 / 5, 17 % 5, -17 / 5, -17 % 5
```

```text
3 2 -3 -2
```

#### Mixing and narrowing

When two different integer types meet, the smaller is widened first. Going the other way, into a smaller type, needs a cast, because the value
might not fit; the cast keeps only the low bytes:

<!-- main -->
```cabby
long Wide = 300
unsigned char Narrow = (unsigned char)Wide
println "300 squeezed into a byte:", (int)Narrow
```

```text
300 squeezed into a byte: 44
```

A constant that fits needs no cast: `char c = 65` and `short s = 1000` are
fine. A negative value can't go into an unsigned type without one:

<!-- error-main: can't hold every 'int' value -->
```cabby
unsigned u = -1
```

#### Bases and bits

Integer literals can be written in hexadecimal (`0x`), binary (`0b`) or
octal (`0o`). A bare leading zero (`0755`) is an error, because it's too
easy to write by accident; `0o755` says octal clearly.

<!-- main -->
```cabby
int Flags = 0b1010
int Mask = 0x0F
int Perms = 0o755
println Flags, Mask, Perms
println Flags & 0b0010, Flags | 1, Flags ^ 0b1111, Flags << 2, Flags >> 1, ~0
```

```text
10 15 493
2 11 5 40 5 -1
```

| Operator | Meaning |
|---|---|
| `a & b` | bits set in both |
| `a \| b` | bits set in either |
| `a ^ b` | bits set in one but not the other |
| `~a` | every bit flipped |
| `a << n`, `a >> n` | shift left or right by n bits |

Shifting by a negative amount, or by the type's width or more (`1 << 40` on
an `int`), is a panic.

Literals can carry suffixes: `u` for unsigned and `l` for long (`10ul`).
A literal too big for an `int` is a `long` automatically, and one too big
for a `long` an `unsigned long` (`unsigned long Most =
18446744073709551615`).

#### In short

* `char` 1, `short` 2, `int` 4, `long` 8 bytes; `unsigned` doubles the
  positive range.
* Overflow wraps around; division by zero panics.
* Narrowing needs a cast; widening is automatic.
* `0x`, `0b`, `0o` literals; `& | ^ ~ << >>` work on bits.

#### Try this

* Print `1 << 40` with `int` values and read the panic.
* Find the largest `unsigned long` by starting at 0 and subtracting 1.

[← Chapter 2](#chapter-2-variables-and-the-basic-types) · [Contents](#contents) · [Chapter 4 →](#chapter-4-floating-point)

---

### Chapter 4: Floating point

*Program: `04-floating-point.cby`. Keywords: `float` `double`.*

#### double and float

`double` is a 64-bit floating-point number, good for about 16 significant
digits; a literal like `1.5` is a `double`. `float` is 32-bit, good for about
7 digits; its literals end in `f` (`1.5f`). Use `double` unless you have a
reason not to.

<!-- main -->
```cabby
double d = 1.0 / 3.0
float f = 1.0f / 3.0f
println d
println f
```

```text
0.3333333333333333
0.33333334
```

Cabby prints the shortest digits that read back as exactly the same number,
and always shows a decimal point, so you can tell a `double` from an `int`:

<!-- main -->
```cabby
println 0.1, 2.5, 100.0, 1e20, 1e-7
```

```text
0.1 2.5 100.0 1e+20 1e-07
```

#### The classic surprise

Most decimal fractions can't be stored exactly in binary, so arithmetic on
them is very slightly off:

<!-- warning-main: two that look the same may not be exactly equal -->
```cabby
println 0.1 + 0.2, 0.1 + 0.2 == 0.3
```

```text
0.30000000000000004 false
```

This is true in every language that uses the computer's floating point, and
Cabby warns about that `==` when it checks the program:

```text
warning: floating-point numbers are rounded, so two that look the same may not be exactly equal; use 'close_to A, B'
```

#### Close enough

Don't compare floating-point results with `==`; check that they're close
instead. `close_to A, B` is true when `A` and `B` are the same but for
rounding:

<!-- main -->
```cabby
double D = 0.1 + 0.2
float F = 1.0f / 3.0f
println close_to(D, 0.3), close_to(F * 3.0f, 1.0f), close_to(D, 0.31)
println close_to(1e20, 1e20 + 1e5), close_to(0.000000000001, 0.0)
println close_to(2.5, 2.6, 0.2), close_to(2.5, 2.8, 0.2)
```

```text
true true false
true true
true false
```

On its own, `close_to` asks for the two to be the same to about 9
significant digits (5 for two `float`s, which keep only about 7), so it
works for numbers of any size: `1e20` and `1e20 + 1e5` differ by a
hundred thousand, but only in the 16th digit. Near zero there are no
digits to compare, so anything within `0.000000000001` of zero (for
`float`s, `0.000001`) is close to zero. A third number sets the limit
instead: `close_to A, B, 0.2` is true when they're no more than 0.2 apart.

The warning comes when a side of `==` or `!=` is arithmetic or a decimal
with a fraction, like `0.3`, which binary can't hold exactly. Comparing
with a whole number (`D == 0.0`), or two variables (`Old == New`), is left
alone, since that's often meant exactly: a value that was set rather than
worked out.

`abs` gives a number without its sign (it's built in, and works on every
kind of number, so it's C's `fabs` and `fabsf` in one). `abs(A - B)` is
the gap between two numbers, for a test of your own:

<!-- main -->
```cabby
double D = 0.1 + 0.2
println abs(D - 0.3) < 0.000000001, abs(-2.5), abs(-2.5f)
```

```text
true 2.5 2.5
```

#### Integers and doubles together

An integer meeting a `double` becomes a `double`. But integer division
happens first if both sides are integers, even when the answer is going into
a `double`:

<!-- main -->
```cabby
println 7 / 2, 7 / 2.0
double Half = 7 / 2          // 7 / 2 is 3 before it becomes a double
println Half, (double)7 / 2
```

```text
3 3.5
3.0 3.5
```

#### Casts to integers

A `float` widens to a `double` automatically; the other way needs a cast.
Casting to an integer type cuts off the fraction (toward zero), and a
value too big for the type *saturates*: it becomes the type's largest or
smallest value.

<!-- main -->
```cabby
println (int)3.99, (int)-3.99
println (int)1e20, (int)(unsigned char)300.0, (int)(unsigned char)-5.0
```

```text
3 -3
2147483647 255 0
```

#### Infinity and NaN

Dividing a floating-point number by zero doesn't panic. It gives infinity,
or NaN ("not a number") for 0 / 0:

<!-- main -->
```cabby
double Zero = 0.0
println 1.0 / Zero, -1.0 / Zero, Zero / Zero
```

```text
inf -inf NaN
```

#### In short

* `double` (about 16 digits) is the default; `float` (about 7) uses an `f`
  suffix.
* Floating-point results are rarely exact; don't test them with `==`, but
  with `close_to A, B` (or `close_to A, B, Limit`). `abs` drops a sign.
* `7 / 2` is integer division even when stored in a `double`.
* Casts to integers truncate and saturate; float division by zero gives
  `inf` or `NaN`.

#### Try this

* Print a `float` stored in a `double`: `double x = 1.0f / 3.0f`. Why isn't
  it 0.3333333333333333?
* What do `(long)2.5e9` and `(int)2.5e9` print?

[← Chapter 3](#chapter-3-integer-types) · [Contents](#contents) · [Chapter 5 →](#chapter-5-constants)

---

### Chapter 5: Constants

*Program: `05-const.cby`. Keywords: `const`.*

`const` in front of a declaration means the value can't be changed after it
is set. Cabby checks this before the program runs, so a constant really is
constant.

<!-- run -->
```cabby
const int DAYS_PER_WEEK = 7
const double PI = 3.141592653589793
const str APP = "Cabby manual"

// A constant can give an array its size.
int Rainfall[DAYS_PER_WEEK] = {3, 0, 0, 12, 5, 0, 1}

int main {
	println APP
	const int WEEKS = 4
	println WEEKS * DAYS_PER_WEEK, "days in", WEEKS, "weeks"
	println "circle area:", PI * 2.0 * 2.0
	println "rain on day 4:", Rainfall[3]
}
```

```text
Cabby manual
28 days in 4 weeks
circle area: 12.566370614359172
rain on day 4: 12
```

Constants declared outside any function are *global*: every function can
use them. Writing them in capitals is a common habit, not a rule.

Changing a constant is an error, and the message points at both the
assignment and the declaration that made it constant:

<!-- error-main: can't assign to 'WEEKS' because it is const -->
```cabby
const int WEEKS = 4
WEEKS = 5
```

```text
error: can't assign to 'WEEKS' because it is const
note: 'WEEKS' is declared const
```

`const` turns up in two more places later on, with the same meaning of
"won't change":

* `const T &name`, a borrow that can only read ([Chapter 14](#chapter-14-references-borrowing));
* a `const` method, which promises not to change its object
  ([Chapter 22](#chapter-22-objects-and-methods)).

#### In short

* `const` values are fixed once set; the check happens before the program
  runs.
* Global constants can size arrays and be used anywhere.

#### Try this

* Make `Rainfall` constant, then try to change `Rainfall[0]`.

[← Chapter 4](#chapter-4-floating-point) · [Contents](#contents) · [Chapter 6 →](#chapter-6-if-and-else)

---

## Part II: Control flow

So far every program has run from top to bottom. These chapters make
programs choose between paths and repeat things.

### Chapter 6: If and else

*Program: `06-if-else.cby`. Keywords: `if` `else`.*

#### Choosing a path

A condition comes straight after `if`, with no parentheses around it, and is
always followed by a block in braces, even for a single statement:

<!-- run -->
```cabby
void judge int Score {
	if Score >= 90 {
		println Score, "excellent"
	} else if Score >= 50 {
		println Score, "pass"
	} else {
		println Score, "try again"
	}
}

int main {
	judge 95
	judge 70
	judge 12
}
```

```text
95 excellent
70 pass
12 try again
```

(`judge` is a function; [Chapter 10](#chapter-10-functions) explains them.
`judge 95` calls it with 95.)

`else` can go on the line after the closing brace, if you prefer that style:

```cabby
if Items > 5 {
	println "lots"
}
else {
	println "a few"
}
```

#### Comparisons and logic

Comparisons give `true` or `false`:

| Comparison | Meaning |
|---|---|
| `a == b` | equal |
| `a != b` | not equal |
| `a < b`, `a <= b` | less (or equal) |
| `a > b`, `a >= b` | greater (or equal) |

Logic joins them, or turns one around:

| Logic | Meaning |
|---|---|
| `a && b` | both true |
| `a \|\| b` | at least one true |
| `!a` | not |

<!-- main -->
```cabby
int Age = 34
bool Member = true
if Age >= 18 && Member {
	println "welcome back"
}
if Age < 13 || Age > 65 {
	println "discount"
} else {
	println "full price"
}
if !Member {
	println "please join"
}
```

```text
welcome back
full price
```

`&&` and `||` *short-circuit*: they stop as soon as the answer is known. That
makes it safe to guard a risky test with a safe one first:

<!-- main -->
```cabby
int Total = 10, Count = 0
if Count != 0 && Total / Count > 2 {    // the division never happens
	println "never printed"
}
println "no crash"
```

```text
no crash
```

#### `!` and a comparison

`!` turns round only what comes straight after it. To turn round a whole
comparison, put it in parentheses, or write the opposite comparison
instead, which usually reads better:

<!-- main -->
```cabby
int Tries = 2
bool Locked = false
if !Locked && !(Tries == 0) {
	println "tried before"
}
if !Locked && Tries != 0 {    // the same, and easier to read
	println "tried before"
}
```

```text
tried before
tried before
```

`!Tries == 0` would be `(!Tries) == 0`, which isn't what it looks like.
If you know Python, note that there are no words for these: Cabby
writes `==`, `!=`, `&&`, `||` and `!`, as C and most languages do.

#### Numbers as conditions

A number works as a condition too: zero is false and anything else is
true. `if items` means `if items != 0`.

#### Logic is not bits

`&&` `||` `!` work on true and false. `&` `|` `^` `~` are different operators
that work on the bits of numbers ([Chapter 3](#chapter-3-integer-types)).
Mixing them up is an easy mistake. A related trap is that `&` binds more
loosely than `==`, so `Bits & 4 != 0` means `Bits & (4 != 0)`. Cabby warns
about this and suggests the parentheses:

<!-- main -->
```cabby
int Bits = 0b0110
if (Bits & 0b0100) != 0 {
	println "bit 2 is set"
}
```

```text
bit 2 is set
```

#### In short

* `if cond { } else if cond { } else { }`: no parentheses around the
  condition, braces always.
* `&&` and `||` short-circuit; `!` negates what's straight after it, so a
  comparison it turns round goes in parentheses: `!(A == B)`, or `A != B`.
* Zero is false, everything else is true.

#### Try this

* Remove the parentheses in `(Bits & 0b0100) != 0` and read the warning.
* Write `void sign int n` that prints "negative", "zero" or "positive".

[← Chapter 5](#chapter-5-constants) · [Contents](#contents) · [Chapter 7 →](#chapter-7-while-until-and-do)

---

### Chapter 7: While, until and do

*Program: `07-while-do.cby`. Keywords: `while` `until` `do`.*

#### while

`while cond { ... }` checks the condition, runs the block if it's true, and
repeats. If the condition is false to begin with, the block never runs.

<!-- main -->
```cabby
int n = 3
while n > 0 {
	println n, "..."
	n--
}
println "lift off!"
```

```text
3 ...
2 ...
1 ...
lift off!
```

`while` suits loops where you don't know in advance how many times they'll
go round:

<!-- main -->
```cabby
int Halvings = 0
double x = 1000.0
while x >= 1.0 {
	x = x / 2
	Halvings++
}
println Halvings, "halvings"
```

```text
10 halvings
```

#### until

`until cond { ... }` is `while` turned round: it repeats *until* the
condition is true. `until Done` means `while !Done`; use whichever says
what you mean:

<!-- main -->
```cabby
int Rolls = 0
int Total = 0
until Total >= 10 {
	Rolls++
	Total += Rolls          // 1, then 2, then 3 ...
}
println Rolls, "rolls make", Total
```

```text
4 rolls make 10
```

#### do ... while

`do { ... } while cond` checks the condition *after* the block, so the block
always runs at least once. Here that makes 0 print one digit rather than
none:

<!-- main -->
```cabby
int Value = 4096
do {
	print Value % 10
	Value = Value / 10
} while Value > 0
println

int Zero = 0
do {
	print Zero % 10
	Zero = Zero / 10
} while Zero > 0
println
```

```text
6904
0
```

`do` can end with `until` instead, to repeat until something is true:

<!-- main -->
```cabby
int Guess = 0
do {
	Guess += 7
} until Guess % 5 == 0
println Guess
```

```text
35
```

#### Loops that never end

`while true { }` loops forever. In a program that is sometimes what you want
(with a `break` inside, see [Chapter 8](#chapter-8-for-loops-break-and-continue));
by mistake, press Ctrl-C to stop it. In the interactive session Ctrl-C only
stops the line you typed.

#### In short

* `while` tests first and may run zero times; `until` is the same, but
  repeats until its condition is true.
* `do ... while` (or `do ... until`) tests afterwards and runs at least
  once.

#### Try this

* The Collatz sequence: start at a number; halve it if it's even, otherwise
  triple it and add one; stop at 1. How many steps does 27 take?
  (`07-while-do.cby` does it for 6.)

[← Chapter 6](#chapter-6-if-and-else) · [Contents](#contents) · [Chapter 8 →](#chapter-8-for-loops-break-and-continue)

---

### Chapter 8: For loops, break and continue

*Program: `08-for-break-continue.cby`. Keywords: `for` `in` `to` `down` `by` `break` `continue`.*

#### Counting loops

Most loops count, and Cabby's counting loop says so directly:

| Loop | Counts |
|---|---|
| `for i in 5` | 0, 1, 2, 3, 4 (five numbers, from 0) |
| `for i in 1 to 5` | 1, 2, 3, 4, 5 (both ends included) |
| `for i in 5 down to 1` | 5, 4, 3, 2, 1 |
| `for i in 0 to 20 by 5` | 0, 5, 10, 15, 20 |
| `for i in 10 down to 1 by 3` | 10, 7, 4, 1 |

<!-- main -->
```cabby
print "in 5:"
for i in 5 {
	print "", i       // "" then i: a space, then the number
}
println
print "1 to 5:"
for i in 1 to 5 {
	print "", i
}
println
print "down:"
for i in 10 down to 1 by 3 {
	print "", i
}
println
```

```text
in 5: 0 1 2 3 4
1 to 5: 1 2 3 4 5
down: 10 7 4 1
```

The loop declares its counter for you; it exists only inside the loop. The
counter is read-only, because the loop does the counting. The range is
worked out once, before the loop starts, and the loop never steps past its
end, so even `for unsigned i in 3 down to 0` stops at 0 instead of wrapping
round to four billion.

`for i in n` is the natural loop over an array, whose positions run from 0 to
n − 1 ([Chapter 11](#chapter-11-arrays)):

<!-- main -->
```cabby
int Scores[4] = {7, 9, 4, 8}
int Total = 0
for i in Scores.length {
	Total += Scores[i]
}
println "total:", Total
```

```text
total: 28
```

#### The counter's type

The counter takes its type from the range: `1 to 5` counts in `int`, and
`'a' to 'e'` counts in `char`. To choose the type, name it before the
counter:

<!-- main -->
```cabby
for c in 'a' to 'e' {
	print c
}
println
print "longs:"
for long Big in 3 {
	print "", Big * 1000000000
}
println
```

```text
abcde
longs: 0 1000000000 2000000000
```

#### Loops inside loops

<!-- main -->
```cabby
for Row in 1 to 3 {
	print Row, "x:"
	for Col in 1 to 4 {
		print "", Row * Col
	}
	println
}
```

```text
1 x: 1 2 3 4
2 x: 2 4 6 8
3 x: 3 6 9 12
```

#### break and continue

`break` leaves a loop straight away. `continue` skips the rest of this turn
and goes on with the next. Both work in every kind of loop, and both act on
the innermost loop only.

<!-- main -->
```cabby
print "odd:"
for i in 1 to 9 {
	if i % 2 == 0 {
		continue
	}
	print "", i
}
println

int First = 0
for i in 2 to 1000 {
	if i * i > 200 {
		First = i
		break
	}
}
println "first square above 200:", First, "squared"
```

```text
odd: 1 3 5 7 9
first square above 200: 15 squared
```

To leave several loops at once, use `goto` ([Chapter 30](#chapter-30-goto-and-labels)).

#### The three-part for

For loops that don't just count (two counters, multiplying, a `double`),
there is the three-part `for`:

```cabby
for start; condition; step {
```

It is the only place Cabby uses semicolons. Any of the three parts may be
left out, and `for ;; { }` loops until a `break`.

<!-- main -->
```cabby
print "powers of 2:"
for int p = 1; p <= 100; p *= 2 {
	print "", p
}
println
```

```text
powers of 2: 1 2 4 8 16 32 64
```

#### In short

* `for i in n`, `for i in a to b`, `for i in a down to b`, each with an
  optional `by step`; both ends are included in `to`.
* The counter is declared by the loop, read-only, and typed by the range
  unless you name a type.
* `break` leaves the innermost loop; `continue` moves on to its next turn.
* `for init; cond; step { }` covers everything else.

#### Try this

* Assign to the counter of a `for i in` loop and read the error.
* FizzBuzz: print 1 to 30, but "fizz" for multiples of 3, "buzz" for
  multiples of 5 and "fizzbuzz" for both.
* `08-for-break-continue.cby` prints the primes below 30 with a loop inside
  a loop. Change it to print the primes below 100.

[← Chapter 7](#chapter-7-while-until-and-do) · [Contents](#contents) · [Chapter 9 →](#chapter-9-switch)

---

### Chapter 9: Switch

*Program: `09-switch.cby`. Keywords: `switch` `case` `default`.*

`switch` chooses where to go by an integer, `char` or enum value. Each
`case` label names one constant value; `default` catches every value that no
case names.

<!-- run -->
```cabby
void day_name int d {
	switch d {
		case 0:
			println "Sunday"
			break
		case 6:
			println "Saturday"
			break
		default:
			println "a weekday"
	}
}

int main {
	day_name 0
	day_name 3
	day_name 6
}
```

```text
Sunday
a weekday
Saturday
```

The layout: the `case` labels are one tab inside the `switch`, and the
statements under each label one tab further.

#### Fall-through

Running carries on from one case into the next unless a `break`
stops it. Usually that's a mistake to avoid, but it's also how several
values share the same code: stack their labels.

<!-- main -->
```cabby
for n in 1 to 4 {
	switch n {
		case 1:
		case 3:
			println n, "odd"
			break
		default:
			println n, "even"
	}
}
```

```text
1 odd
2 even
3 odd
4 even
```

And on purpose, to do "this, and everything below it":

<!-- main -->
```cabby
int Level = 2
switch Level {
	case 3:
		println "gold"
	case 2:
		println "silver"
	case 1:
		println "bronze"
}
```

```text
silver
bronze
```

Each case section is its own scope: a variable declared under one `case`
isn't visible under the next.

#### In short

* `switch value { case X: ... break ... default: ... }`.
* Without `break`, running falls through into the next case.
* Stacked labels share code.

#### Try this

* Remove the first `break` in `day_name` and call `day_name 0`. What prints,
  and why?

[← Chapter 8](#chapter-8-for-loops-break-and-continue) · [Contents](#contents) · [Chapter 10 →](#chapter-10-functions)

---

## Part III: Functions and data

This part is the core of the language: functions, the ways of grouping
data, and ownership and borrowing, the two ideas that let Cabby free memory
for you without a garbage collector.

### Chapter 10: Functions

*Program: `10-functions.cby`. Keywords: `void` `return`.*

#### Defining a function

A function is written as its return type, its name, and its parameters, with
no parentheses around them:

<!-- run -->
```cabby
int add int a, int b {
	return a + b
}

void greet {                 // void: returns nothing; no parameters
	println "hello!"
}

void repeat str Word, int Times {
	print Word
	for i in 2 to Times {
		print "", Word
	}
	println
}

int main {
	greet
	repeat "hip", 3
	println add(2, 3)
}
```

```text
hello!
hip hip hip
5
```

`return` leaves the function at once, from anywhere inside it, giving back
its value. A `void` function can use a bare `return` to leave early, or just
reach its closing brace.

Function names are lower case with `_` between words (`repeat`,
`say_hello`); parameters are variables, so they start each word with a
capital (`Word`, `Times`). The case tells you at a glance which is which
([Appendix J](#appendix-j-style-guide)).

Functions can be used before they're defined, so their order in the file
doesn't matter. You may still write a *prototype* first: the same
header with no body. If you do, it must match the definition.

```cabby
int later int x              // a prototype
```

#### Recursion

A function can call itself:

<!-- run -->
```cabby
long factorial int n {
	if n <= 1 {
		return 1
	}
	return n * factorial(n - 1)
}

int main {
	println "10! =", factorial(10)
}
```

```text
10! = 3628800
```

Each call uses some stack. Recursing too deeply (around 100,000 calls deep
when interpreted) stops the program with a panic instead of crashing.

#### Calling a function

A call is the function's name followed by its arguments, and the arguments
are everything after the name, up to the end of the line:

```cabby
greet                     // no arguments
repeat "hip", 2           // "hip" and 2
println add 2, 3          // add's answer, 5
```

The last line works because `add 2, 3` is itself a call taking the rest of
the line, and its answer is `println`'s argument.

Inside an expression the same rule applies: a call takes everything after
it, up to the end of the line *or the closing parenthesis around it*. So
when something follows a call, wrap it in parentheses to show where its
arguments end. The parentheses can go around the whole call or around just its
arguments; both mark the end in the same way:

<!-- run -->
```cabby
int add int a, int b {
	return a + b
}

int main {
	int Sum = (add 1, 2) + 3      // parentheses around the call
	int Same = add(1, 2) + 3      // parentheses around its arguments
	println "sum:", Sum, Same
}
```

```text
sum: 6 6
```

A call starts when the name is followed directly by a value: a name, a
number, a string, an opening parenthesis, `!` or `~`. A minus sign depends
on the name. After a function that takes arguments it starts the first
argument, since subtracting from such a function could never make sense:
`abs -7` is `abs` of -7, and `abs -7 + 3` is `abs` of -4. After anything
else (a variable, or a function with no parameters) it subtracts: `n - 1`,
`n -1` and `tick - 1` all subtract. Spaces make no difference either way.

<!-- run -->
```cabby
int tick {
	return 10
}

int main {
	int n = 5
	println abs -7 + 3        // abs of -4: the call takes the rest of the line
	println n -1, tick - 1    // both subtract
}
```

```text
4
4 9
```

#### The one subtle case

At the start of a line, a parenthesis right after the name holds the call's
arguments only if it holds *all* of them. Otherwise it's just the start of
the first argument:

<!-- main -->
```cabby
int a = 1, b = 2
println a + b * 2          // 5
println (a + b) * 2        // 6: prints (a + b) * 2
println(a + b) * 2         // 6 too
```

```text
5
6
6
```

Written alone, `println(a + b)` holds everything and prints 3. That works,
but the style is `println a + b`, without the parentheses (`cabby --style`
points it out).

That holds whatever follows the closing parenthesis, even `.text`:
`println (n + 1).text + " left"` prints the text of `n + 1`. So to use what
a call gives back at the start of a line, put the whole call in
parentheses: `(counter.make 3).report` calls `make`, then `report` on what
it returns.

One more way does the same, for the common case: with the parenthesis
written *against* the name, and the line going on with a method, the
parenthesis holds the call's arguments, and the method is called on what
the call gives back. So `json.read(Text).fill Player` reads the JSON, then
fills `Player` from it, and `Page.find("td").clear` clears the cell it
found. (With a space, `print (n + 1).text`, the parenthesis still starts an
argument.)

Because a call takes everything after it, a comparison
after it is part of its argument: `is_even 4 == true` passes `4 == true`.
To compare the call's result, put the call in parentheses: `(is_even 4) == true`.

#### Functions with no parameters

A function with no parameters is called by its name alone, anywhere:
`int t = tick + 1` calls `tick`. Cabby has no function values, so a
function's name can only mean calling it. (One exception, in
[Chapter 51](#chapter-51-your-first-window): naming a window's method as a
*handler* for a button.)

A prototype of a function with no parameters is written with `void`
(`int tick void`); otherwise it would read as a global variable.

#### Arguments are converted like assignments

An argument is converted to its parameter's type when nothing can be lost:
an `int` can be passed as a `long` or a `double`, but a `double` can't be
passed as an `int` without a cast.

#### In short

* `type name params { body }`; `void` for no result, nothing at all for no
  parameters.
* A call's arguments run to the end of the line, or to the closing parenthesis
  around the call.
* When something follows a call, wrap it: `(f a, b) + 1` or `f(a, b) + 1`.
* Functions may be used before they're defined, and may call themselves.

#### Try this

* Print `is_even 4 == true`, then `(is_even 4) == true`, with
  `bool is_even int n { return n % 2 == 0 }`. Explain the difference.
* Write `int fib int n` that returns the nth Fibonacci number recursively.
  Time `fib(32)` interpreted, then compiled with `cabby -c`.

[← Chapter 9](#chapter-9-switch) · [Contents](#contents) · [Chapter 11 →](#chapter-11-arrays)

---

### Chapter 11: Arrays

*Program: `11-arrays.cby`. Topics: fixed-size arrays, `.length`, array parameters.*

#### Declaring arrays

`int Scores[5]` is five `int`s, numbered 0 to 4. An array's size is fixed
when it's declared; for a collection that grows and shrinks, use a list
([Chapter 15](#chapter-15-lists)). Like every variable, an array's elements
start at zero unless you give values:

<!-- main -->
```cabby
int Scores[5] = {70, 85, 90, 65, 100}
println "first:", Scores[0], "last:", Scores[4]
Scores[3] = 75
int Partial[4] = {1, 2}           // missing values are zero
int Primes[] = {2, 3, 5, 7, 11}   // [] : the length comes from the values
println Partial[0], Partial[1], Partial[2], Partial[3]
println Primes.length, "primes"
```

```text
first: 70 last: 100
1 2 0 0
5 primes
```

`.length` is the number of elements. Together with `for i in`, it walks an
array without any chance of going past its end:

<!-- main -->
```cabby
int Scores[5] = {70, 85, 90, 65, 100}
int Best = Scores[0]
for i in 1 to Scores.length - 1 {
	if Scores[i] > Best {
		Best = Scores[i]
	}
}
println "best:", Best
```

```text
best: 100
```

#### Every index is checked

Reading or writing outside an array is one of the most common bugs in
programming. Cabby checks every index. A constant index that's out of range is an error
before the program runs; any other is checked as it runs, and stops the
program with a *panic* that says what happened:

<!-- error-main: index 3 is outside -->
```cabby
int Xs[3]
println Xs[3]
```

```text
error: index 3 is outside 'int[3]' (positions 0 to 2)
```

```text
panic at prog.cby:4:12: index 5 is outside an array of length 3 (positions 0 to 2)
```

#### Passing arrays to functions

A parameter written `T name[]` *borrows* an array of any length from the
caller (borrowing is [Chapter 14](#chapter-14-references-borrowing)'s
subject). With `const`, the function can only read it; without, it can
change the caller's array:

<!-- run -->
```cabby
int sum const int Xs[] {
	int Total = 0
	for i in Xs.length {
		Total += Xs[i]
	}
	return Total
}

void double_all int Xs[] {
	for i in Xs.length {
		Xs[i] *= 2
	}
}

int main {
	int Primes[] = {2, 3, 5, 7, 11}
	println "sum:", sum(Primes)
	double_all Primes
	println "after doubling:", sum(Primes)
}
```

```text
sum: 28
after doubling: 56
```

#### Copying, and two dimensions

An array of numbers is copied by assignment, so changing the copy leaves the
original alone. Both arrays must be the same size.

<!-- main -->
```cabby
int Scores[3] = {70, 85, 90}
int Copy[3] = Scores
Copy[0] = 0
println Scores[0], Copy[0]

int Grid[3][3]                    // an array of three arrays of three
for r in 3 {
	for c in 3 {
		Grid[r][c] = r * 3 + c
	}
}
println Grid[1][2], Grid[2][0], Grid.length, Grid[0].length
```

```text
70 0
5 6 3 3
```

Arrays of any type work the same way: `str names[3] = {"Ann", "Bo", "Cy"}`.
An array of strings or objects moves instead of copying, like the things in
it ([Chapter 13](#chapter-13-ownership-moves-and-drops)).

#### In short

* `T name[N]`, numbered 0 to N − 1; `T name[] = {...}` sizes itself.
* `.length` is the number of elements.
* Every index is checked: out of range is an error or a panic, never a
  silent bug.
* `T name[]` parameters borrow an array of any length; add `const` to only
  read it.

#### Try this

* Read `Scores[5]` with a variable index and read the panic.
* Write `int count_above const int Xs[], int Limit`.

[← Chapter 10](#chapter-10-functions) · [Contents](#contents) · [Chapter 12 →](#chapter-12-strings)

---

### Chapter 12: Strings

*Program: `12-strings.cby`. Keywords: `str`.*

#### What a str is

A `str` holds text of any length. It *owns* its text: the text lives on the
heap and is freed automatically when the `str` is finished with
([Chapter 13](#chapter-13-ownership-moves-and-drops) explains exactly when).
A literal like `"hi"` is fixed text built into the program; it becomes an
owned `str` wherever one is needed.

Every string has these:

| | |
|---|---|
| `s.length` | how many characters (bytes) it has, as a `long` |
| `s[i]` | one character, numbered from 0; you can assign to it too |
| `a + b` | a new string: a `str` joined with a `str` or a `char` |
| `s += b` | add a `str` or a `char` to the end of `s` |
| `s.copy` | a second, independent string with the same text |
| `a == b`, `a != b` | compare the text |
| `a < b`, `a >= b`, ... | dictionary order |
| `s.find t`, `s.contains t`, `s.replace a, b`, ... | searching and changing ([below](#searching-and-changing-text)) |
| `s[a to b]` | a new string of characters `a` to `b` ([below](#slices)) |
| `s.is_int`, `s.to_int` | is the text a whole number, and that number ([below](#text-to-numbers)) |

<!-- main -->
```cabby
str Greeting = "hello"
println Greeting, "has", Greeting.length, "characters"
println "first:", Greeting[0], "last:", Greeting[Greeting.length - 1]
Greeting[0] = 'j'
println Greeting
```

```text
hello has 5 characters
first: h last: o
jello
```

`s[i]` is checked like an array index: reading past the end is a panic.

#### Building strings

<!-- main -->
```cabby
str Name = "Ada"
str Line = "Hi, " + Name + '!'
println Line
Line += " Welcome back."
println Line

str Spaced                          // starts empty
for i in Name.length {
	if i > 0 {
		Spaced += ' '
	}
	Spaced += Name[i]
}
println Spaced
```

```text
Hi, Ada!
Hi, Ada! Welcome back.
A d a
```

#### Numbers as text: .text

`+` joins text with text, not with numbers:

<!-- error-main: use its .text -->
```cabby
int n = 5
str Bad = "n = " + n
```

```text
error: '+' joins a 'str' with a 'str' or a 'char', not 'int'
note: to put a number into text, use its .text: '"total: " + n.text'
```

Every number, `bool` and `char` has `.text`: the same text `println` would
show for it, as a new `str`.

<!-- main -->
```cabby
int n = 42
double Price = 2.5
str Label = "n = " + n.text + ", price = " + Price.text
println Label
println "double: " + (n * 2).text + ", " + true.text
```

```text
n = 42, price = 2.5
double: 84, true
```

To just print numbers next to text, `println "n =", n` is simpler; `.text`
is for when you need the text itself, like the label of a window in
[Chapter 52](#chapter-52-a-small-app).

Given a number of decimal places, `.text` rounds to them, and always
shows that many: what prices and averages want.

<!-- main -->
```cabby
double Price = 4.5
println "$" + Price.text(2), (2.0 / 3).text(3), 7.text(2)
```

```text
$4.50 0.667 7.00
```

**Columns.** `.pad_right n` adds spaces after text to make it `n`
characters wide, and `.pad_left n` adds them in front, which puts numbers
in a column on the right; `.centered n` puts the spaces either side. Text
that's as wide already is left as it is:

<!-- main -->
```cabby
list str Names = {"Tea", "Hot chocolate", "Cake"}
list double Prices = {3.5, 4.2, 12}
println "Menu".centered(22)
for I in Names.length {
	println Names[I].pad_right(15) + Prices[I].text(2).pad_left(7)
}
```

```text
         Menu         
Tea               3.50
Hot chocolate     4.20
Cake             12.00
```

#### Text to numbers

`.text` turns a number into text; these go the other way. Each "is"
checks the text, and each "to" gives the number:

| Check | Convert | For |
|---|---|---|
| `s.is_int` | `s.to_int` | a whole number that fits in an `int`: `"42"`, `"-7"` |
| `s.is_long` | `s.to_long` | a whole number that fits in a `long` |
| `s.is_number` | `s.to_double` | any number, with a fraction or exponent too: `"2.5"`, `"1e3"` |

The whole text must be the number, apart from spaces around it:
`" 42 "` is 42, but `"42 apples"` and `""` aren't numbers.

<!-- main -->
```cabby
str Answer = "42"
if Answer.is_int {
	println Answer.to_int + 1
}
println "2.5".to_double * 2, "x".is_number, " 7 ".is_int
```

```text
43
5.0 false true
```

Converting text that isn't a number panics, the way an index past the end
of an array does, so check first unless you're sure:

<!-- panic: "forty" is not a whole number -->
```cabby
int main {
	str Age = "forty"
	println "Age:", Age
	int Years = Age.to_int
	println "never printed"
}
```

```text
Age: forty
```

```text
panic at prog.cby:4:17: "forty" is not a whole number (check first with .is_int)
    	int Years = Age.to_int
    	               ^
```

#### Reading what's typed: input and read_line

`input "prompt"` shows the prompt, waits for a line to be typed, and gives
it back as a `str`, without the Enter. `read_line` does the same without a
prompt. Nothing is left behind for the next read.

<!-- input: Ada\nforty\n41\n -->
```cabby
int main {
	str Name = input "What's your name? "
	println "Hello,", Name + "!"

	str Age = input "How old are you? "
	while !Age.is_int {
		Age = input "A whole number, please: "
	}
	println "Next year you'll be", Age.to_int + 1
}
```

```text
What's your name? Ada
Hello, Ada!
How old are you? forty
A whole number, please: 41
Next year you'll be 42
```

The input can also come from a file or another program instead of the
keyboard: `cabby greet.cby < answers.txt`.

**The end of the input.** When there's nothing left to read (Ctrl-D at the
keyboard, or the end of a file), `read_line` gives `""` and `end_of_input`
becomes true. That's how an empty line (just Enter) is told apart from no
more input. This program counts the lines it's given:

<!-- input: one\n\nthree\n -->
```cabby
int main {
	int Lines = 0
	str Line = read_line
	while !end_of_input {
		Lines++
		Line = read_line
	}
	println Lines, "lines"
}
```

```text
one

three
3 lines
```

That shape (read one line, then loop while there's input, reading the next
at the bottom of the loop) is the way to go through every line of the
input. Reading again after the end has been reported is a panic, because it
could only ever give `""` again, and a loop that forgot to check
`end_of_input` would otherwise print empty lines forever:

<!-- panic: read_line after the end of the input -->
```cabby
int main {
	while true {                  // never checks end_of_input
		str Line = read_line
		println "[" + Line + "]"
	}
}
```

```text
[]
```

```text
panic at prog.cby:3:14: read_line after the end of the input (stop when end_of_input is true)
    		str Line = read_line
    		           ^
```

(Run with no input at all: the first read finds the end and gives `""`; the
second is the panic.)

#### One character, or one key: read_char and read_key

**`read_char`** gives the first character of the next line typed. The rest
of the line, and its Enter, are read too, so nothing is left over for the
next read (the trap C's `getchar` sets). An empty line gives `'\n'`.

<!-- input: yes please\n -->
```cabby
int main {
	print "Carry on? (y/n) "
	char Answer = read_char
	if Answer == 'y' || Answer == 'Y' {
		println "Carrying on"
	}
}
```

```text
Carry on? (y/n) yes please
Carrying on
```

**`read_key`** doesn't wait for Enter: it gives one key the moment it's
pressed, as a `str`. A letter or other character is itself (`"q"`, `"é"`);
other keys have names: `"enter"`, `"tab"`, `"backspace"`, `"escape"`,
`"up"`, `"down"`, `"left"`, `"right"`, `"home"`, `"end"`, `"insert"`,
`"delete"`, `"page_up"`, `"page_down"` and `"f1"` to `"f12"`. The key isn't
shown on the screen, and Ctrl-C still stops the program. For menus and
games:

```cabby
int main {
	println "Arrow keys to move, q to stop"
	int X = 0
	str Key = read_key
	until Key == "q" || end_of_input {
		if Key == "left" {
			X--
		} else if Key == "right" {
			X++
		}
		println "at", X
		Key = read_key
	}
}
```

When the input isn't a keyboard (a file, or another program), `read_key`
takes it a character at a time, and a line end is `"enter"`. At the end of
the input (or Ctrl-D) it gives `""`, and `end_of_input` becomes true.

#### Comparing

`==` and `!=` compare the text, character by character. `<`, `<=`, `>`
and `>=` put strings in dictionary order, comparing a character at a time.
A string that runs out first comes first (`"ab" < "abc"`), and capitals
come before small letters, because their codes are smaller
(`"Zebra" < "apple"`): use `.lower` on both to ignore case.

<!-- main -->
```cabby
str a = "jello"
println a == "jello", a != "hello"
println "apple" < "banana", "ab" < "abc", "Zebra" < "apple"
println "Zebra".lower < "apple".lower
```

```text
true true
true true true
false
```

#### Searching and changing text

| | gives |
|---|---|
| `s.contains t` | `true` if `t` appears anywhere in `s` |
| `s.starts_with t`, `s.ends_with t` | `true` if `s` begins or ends with `t` |
| `s.find t` | where `t` first starts in `s` (a `long`), or -1 if it isn't there |
| `s.replace a, b` | a new string with every `a` replaced by `b` |
| `s.trim` | a new string without the spaces, tabs and Enters at either end |
| `s.upper`, `s.lower` | a new string in capitals, or small letters |
| `s.repeat n` | a new string of `s`, `n` times over |
| `s.pad_left n`, `s.pad_right n`, `s.centered n` | a new string, with spaces in front, after, or either side, to make it `n` characters wide |

What they look for can be a `str` or a `char`: `Line.contains ','`. None
of them change `s`; the ones that make text give a new string, so to change
a variable, assign it back: `Name = Name.trim`.

<!-- main -->
```cabby
str Line = "the cat sat on the mat"
println Line.contains("cat"), Line.find("sat"), Line.find("dog")
println Line.starts_with("the"), Line.ends_with(".")
println Line.replace("cat", "dog")

str Typed = "   Ada Lovelace  "
str Name = Typed.trim
println "[" + Name + "]", Name.upper, Name.lower
println "=".repeat(Name.length)
```

```text
true 8 -1
true false
the dog sat on the mat
[Ada Lovelace] ADA LOVELACE ada lovelace
============
```

Each call there is in parentheses because it shares a line with others
([Chapter 10](#calling-a-function)). On a line of its own, none are needed:
`bool Found = Line.contains "cat"`.

`.upper` and `.lower` change the English letters A to Z only; letters like
é or Greek α are left as they are.

#### Slices

`s[a to b]` is a new string of the characters numbered `a` to `b`, both
included, just as `for i in a to b` visits both. Leave out the start to
begin at 0, or the end to go to the last character:

```text
c  a  b  b  a  g  e
0  1  2  3  4  5  6
```

<!-- main -->
```cabby
str Word = "cabbage"
println Word[0 to 2], Word[1 to 5]       // characters 0 to 2, and 1 to 5
println Word[4 to], Word[to 2]           // from 4 to the end; from the start to 2

long Space = "Ada Lovelace".find ' '
str First = "Ada Lovelace"[to Space - 1]
println First
```

```text
cab abbag
age cab
Ada
```

A slice is a copy: changing it doesn't change the original. A slice
outside the string panics, like an index does, with one exception: the end
can be one before the start, giving `""`. That makes "everything before
position `i`" (`Word[to i - 1]`) work even when `i` is 0.

<!-- panic: slice end 9 is outside a string of length 7 -->
```cabby
int main {
	str Word = "cabbage"
	println Word[4 to 6]
	println Word[5 to 9]
}
```

```text
age
```

```text
panic at prog.cby:4:14: slice end 9 is outside a string of length 7
    	println Word[5 to 9]
    	            ^
```

#### Strings move; .copy copies

Assigning a string to another variable *moves* it rather than copying the
text: the old variable gives it up. This is cheap, and it's what makes
automatic freeing work. The next chapter explains moves fully. When you want
a second string you can change while keeping the first, ask for a copy:

<!-- main -->
```cabby
str Original = "cat"
str Changed = Original.copy
Changed[0] = 'b'
println Original, Changed
```

```text
cat bat
```

To let a function read a string without taking it, *borrow* it with
`const str &`. Then the caller keeps it:

<!-- run -->
```cabby
int count const str &Text, char c {
	int n = 0
	for i in Text.length {
		if Text[i] == c {
			n++
		}
	}
	return n
}

int main {
	str Word = "hello"
	println "l's in", Word + ":", count(Word, 'l')
	println Word, "is still ours"
}
```

```text
l's in hello: 2
hello is still ours
```

#### Escapes and bytes

In literals, `\n` is a newline, `\t` a tab, `\"` a quote, `\\` a backslash,
`\0` a zero byte and `\x41` the byte 0x41. A character is one byte. Plain
English text is one byte per character; letters such as é or α take two or
more bytes in UTF-8, so they count as more than one in `.length` and `s[i]`.

#### In short

* `str` owns its text and frees it automatically.
* `.length`, `s[i]`, `+`, `+=`, `.copy`, `==`, `!=`, and `<` `>` for
  dictionary order.
* `.contains`, `.starts_with`, `.ends_with`, `.find`, `.replace`, `.trim`,
  `.upper`, `.lower`, `.repeat` and `.pad_left`; `s[a to b]` slices, and either end can
  be left out.
* `.text` turns a number, `bool` or `char` into a string; `.to_int`,
  `.to_long` and `.to_double` turn text into a number, after checking with
  `.is_int`, `.is_long` or `.is_number`.
* `input "prompt"` and `read_line` read a line; `end_of_input` says when
  there are no more.
* Assigning a string moves it; `.copy` makes an independent copy; `const
  str &` borrows one to read.

#### Try this

* Read `Greeting[10]` and read the panic.
* Write `str reversed const str &s` that returns `s` backwards
  ([Chapter 36](#chapter-36-a-whole-program-reverse) has one way).

[← Chapter 11](#chapter-11-arrays) · [Contents](#contents) · [Chapter 13 →](#chapter-13-ownership-moves-and-drops)

---

### Chapter 13: Ownership: moves and drops

*Program: `13-ownership.cby`.*

This is the heart of Cabby. There is no garbage collector and no `free()`.
Instead, four rules decide when memory is released:

1. **Every value has exactly one owner**: a variable, a field, an array
   element.
2. **Plain values are copied.** Numbers, `bool`, `char`, enums, and structs
   and arrays made only of those, are copied when assigned or passed.
3. **Everything else is moved.** A `str`, an object, or anything that
   contains one is *moved*: the old owner gives it up and can't use it any
   more, until it's given a new value.
4. **When an owner goes away, it drops what it owns.** At the end of its
   block, a variable *drops* whatever it still owns: frees it, after running
   its destructor if it has one.

All of this is checked before the program runs. Using a value after it has
moved is an error, not a crash.

#### Seeing it happen

To watch values being dropped, this chapter borrows something from
[Chapter 23](#chapter-23-made-and-dropped): an object with a *destructor*, a
method named `~ticket` that runs when a ticket is dropped. Read `ticket` as
"a value that announces when it's freed".

<!-- run -->
```cabby
object ticket {
	str Holder
	~ticket {
		println "  (dropped", Holder + "'s ticket)"
	}
}

void use_up ticket t {             // takes a ticket: it moves in
	println "using", t.Holder + "'s ticket"
}                                  // ...and is dropped here

ticket issue str Who {
	ticket t = {.Holder = Who}
	return t                       // returning moves it out to the caller
}

int main {
	println "1. moves"
	ticket First = issue "Ann"
	ticket Second = First          // moved: First is now empty
	println Second.Holder, "has the ticket"

	println "2. moving into a function"
	use_up Second
	println "back in main"

	println "3. dropped at the end of a block"
	if true {
		ticket Local = issue "Bo"
		println "inside the block"
	}
	println "after the block"

	println "4. a new value drops the old one"
	ticket t = issue "Cy"
	t = issue "Di"

	println "5. a moved-from variable can be filled again"
	First = issue "Ed"
	println "end of main"
}
```

```text
1. moves
Ann has the ticket
2. moving into a function
using Ann's ticket
  (dropped Ann's ticket)
back in main
3. dropped at the end of a block
inside the block
  (dropped Bo's ticket)
after the block
4. a new value drops the old one
  (dropped Cy's ticket)
5. a moved-from variable can be filled again
end of main
  (dropped Di's ticket)
  (dropped Ed's ticket)
```

Notice the last two lines. At the end of `main`, the variables still owning
something are dropped in reverse order of declaration: `t` was declared
after `First`, so Di's ticket goes before Ed's. (`Second` is empty by then:
its ticket moved into `use_up`.)

Each ticket is dropped exactly once. Ann's ticket moved from `First` to
`Second` to `use_up`'s `t`, and only its last owner dropped it.

#### Using a moved value

<!-- error-main: use of moved value 'a' -->
```cabby
str a = "x"
str b = a
println a
```

```text
error: use of moved value 'a'
note: 'a' was moved here; a 'str' is moved, not copied
```

When you need the value in two places, there are three choices:

* **Borrow it** instead of moving it ([Chapter 14](#chapter-14-references-borrowing)).
  This is usually the answer for functions that only look at something.
* **Copy it** explicitly: strings have `.copy`.
* **Give the old variable a new value** before using it again.

#### Copies

Plain values are copied, so both variables stay usable:

<!-- main -->
```cabby
int a = 5
int b = a
b++
println a, b
```

```text
5 6
```

#### When drops happen

* At the end of a block, for everything declared in it, in reverse order of
  declaration.
* When a block is left early by `return`, `break`, `continue` or `goto`.
* When a variable is given a new value: the old value is dropped first.
* At the end of the statement, for temporary values nobody kept.
* When a function that a value was moved into ends.

A panic ([Chapter 37](#chapter-37-panics)) stops the program at once, and
nothing is dropped after it.

#### In short

* One owner per value; plain values copy, everything else moves.
* Owners drop what they own at the end of their block, exactly once.
* A use after a move is caught before the program runs.

#### Try this

* In the example, add `println First.Holder` straight after the move in
  step 1 and read the error.
* Move `ticket Local` to just before the `if`. When is it dropped now?

[← Chapter 12](#chapter-12-strings) · [Contents](#contents) · [Chapter 14 →](#chapter-14-references-borrowing)

---

### Chapter 14: References: borrowing

*Program: `14-references.cby`. Topics: `T &`, `const T &`, the borrowing rules.*

Moving a value into a function every time it needs to look at it would be
tiresome. Instead, you can *borrow* it: a reference lets a function, or a
local name, use a value that someone else still owns.

| | |
|---|---|
| `const T &name` | a shared borrow: may read, may not change |
| `T &name` | a mutable borrow: may read and change |

The `&` goes in the declaration. At the call you just pass the
variable, and Cabby borrows it for you.

<!-- run -->
```cabby
struct account {
	str Owner
	int Balance
}

void show const account &a {
	println a.Owner, "has", a.Balance
}

void deposit account &a, int Amount {
	a.Balance += Amount
}

void swap int &x, int &y {
	int t = x
	x = y
	y = t
}

int main {
	account Acc = {"Ann", 100}
	show Acc                     // borrowed, not moved: Acc is still ours
	deposit Acc, 50              // a mutable borrow changes Acc itself
	show Acc

	int p = 1, q = 2
	swap p, q
	println "swapped:", p, q

	int Score = 10
	int &s = Score               // a local borrow: another name for Score
	s += 5
	println "score:", Score
}
```

```text
Ann has 100
Ann has 150
swapped: 2 1
score: 15
```

#### The rules

These are checked before the program runs:

1. **A borrowed value can't be moved away** while the borrow lasts.
2. **Many readers or one writer.** There can be any number of shared
   (`const`) borrows of a value, *or* one mutable borrow, at any one time.
   While a shared borrow lasts, not even the owner may change the value.
3. **A borrow ends at its last use**, not at the end of its block.
4. **References can't be returned, stored in fields or put in arrays**, so a
   borrow can never outlive the thing it points to.

Rule 2 in action: `View` reads `Acc`, so `Acc` can't change until `View` is
finished with:

<!-- error: can't lend 'Acc' as mutable while it is also borrowed read-only -->
```cabby
struct account {
	str Owner
	int Balance
}

void deposit account &a, int Amount {
	a.Balance += Amount
}

int main {
	account Acc = {"Ann", 100}
	const account &View = Acc
	deposit Acc, 1
	println View.Balance
}
```

```text
error: can't lend 'Acc' as mutable while it is also borrowed read-only
note: 'Acc' is borrowed here
note: the borrow is still in use here, through 'View'
```

Move the `println` above the `deposit` and it's fine: by rule 3, the borrow
ended at its last use.

Rule 4 is why this is refused:

<!-- error: a function can't return a reference -->
```cabby
int &pick int &a {
	return a
}

int main {
}
```

```text
error: a function can't return a reference: the borrow could outlive what
       it points to; return a value instead
```

These rules are what make Cabby safe without a garbage collector: a value
can't be freed while anything still points at it, and two parts of a
program can't change the same thing at the same time.

The rules apply to each field on its own. Lending one field of a struct or
object leaves its other fields free, so one can be read while another is
changed:

<!-- run -->
```cabby
struct account {
	str Name
	int Balance
}

void count_letters const str &Name, int &Count {
	Count = (int)Name.length
}

int main {
	account Acc = {"Ann", 100}
	count_letters Acc.Name, Acc.Balance   // one field read, another changed
	println Acc.Balance
}
```

```text
3
```

The same goes for an object's methods: a method can go through one of its
lists while changing another field. Calling one of its own methods that can
change the object counts as changing all of it, though, since that method
might change the very field being gone through.

#### Which to use

* Reading something big (a string, a struct, an object)? `const T &`.
* Changing the caller's value? `T &`.
* Taking ownership (storing it, or letting it be dropped)? Take it by value.
* Small plain values (numbers) are cheap to copy: take them by value.

#### In short

* `const T &`: borrow to read. `T &`: borrow to change.
* Many readers or one writer; nothing moves while borrowed.
* A borrow lasts until its last use and can never be returned or stored.

#### Try this

* Write `void rename account &a, str name` and use it.
* Try to call `deposit` on a `const account &`. Read the error.

[← Chapter 13](#chapter-13-ownership-moves-and-drops) · [Contents](#contents) · [Chapter 15 →](#chapter-15-lists)

---

### Chapter 15: Lists

*Program: `15-lists.cby`. Keywords: `list`.*

An array's size is fixed when it's declared. A *list* grows and shrinks as
the program runs: add to the end, put things in the middle, take them out.
`list int Scores` is a list of `int`s; the element type comes straight
after `list`, and it can be anything, including `str`, objects and other
lists.

#### Making a list

A list starts empty, or with values in braces:

<!-- main -->
```cabby
list int Scores = {90, 85, 77}
list str Names                    // empty
println Scores.length, Names.length
println Scores[0], Scores[2]
Scores[1] = 88                    // change an element in place
println Scores[1]
```

```text
3 0
90 77
88
```

As with arrays, the first element is number 0 and every index is checked:
reading past the end is a panic, not a wrong value.

`println` shows a whole list in square brackets, with commas between the
elements. Strings are shown as they are, without quotes, just as `println`
shows a single `str`:

<!-- main -->
```cabby
list int Scores = {90, 85, 77}
list str Names = {"Ada", "Alan"}
list list int Grid = {{1, 2}, {3}}
list int Empty
println Scores, Names
println Grid, Empty
```

```text
[90, 85, 77] [Ada, Alan]
[[1, 2], [3]] []
```

Lists of numbers, `bool`, `char` and `str` can be printed, and lists of
those. A list of objects can't: go through it and print what you want
from each.

#### Adding and removing

| | |
|---|---|
| `L.add X` | puts `X` on the end |
| `L.insert i, X` | puts `X` at position `i`, moving the rest along |
| `L.remove_at i` | takes out the element at `i`, closing the gap |
| `L.take_last` | takes out the last element and gives it to you |
| `L.clear` | empties the list |
| `L.length` | how many elements it has, as a `long` |

<!-- main -->
```cabby
list str Queue
Queue.add "Ada"
Queue.add "Grace"
Queue.insert 0, "Alan"            // to the front
println Queue.join(", ")
Queue.remove_at 1
str Last = Queue.take_last
println Queue.join(", "), "and", Last
```

```text
Alan, Ada, Grace
Alan and Grace
```

`.insert` takes any position from 0 to the length (the length itself means
the end). `.take_last` on an empty list is a panic, so check `.length` first
when the list might be empty.

#### Going through a list

`for X in L` runs once for each element, in order, with `X` holding it:

<!-- main -->
```cabby
list double Prices = {2.5, 1.25, 4.0}
double Total = 0.0
for Price in Prices {
	Total += Price
}
println "total:", Total
```

```text
total: 7.75
```

`X` is read-only. For numbers, `bool` and `char` it's a copy; for anything
else (strings, objects, lists) it's a read-only borrow of the element, so
nothing is copied. The same works for arrays, and for the characters of a
string: `for c in "hello"`.

To change elements as you go, count through the positions instead:

<!-- main -->
```cabby
list int Scores = {90, 85, 77}
for i in Scores.length {
	Scores[i] += 5
}
println Scores
```

```text
[95, 90, 82]
```

A list can't be changed (added to, removed from, cleared) inside a loop
that goes through it: the loop would lose its place. Cabby refuses it:

<!-- error: can't change 'Scores' inside a loop that goes through it -->
```cabby
int main {
	list int Scores = {90, 85}
	for Score in Scores {
		Scores.add Score / 2
	}
}
```

```text
error: can't change 'Scores' inside a loop that goes through it
note: the loop over 'Scores' starts here; collect the changes in another
      list and make them after the loop
```

#### Searching

`L.contains X` says whether `X` is in the list, and `L.find X` where it
first is, or -1 if it isn't there. They work on lists of numbers, `bool`,
`char` and `str`:

<!-- main -->
```cabby
list str Fruit = {"apple", "pear", "plum"}
println Fruit.contains("pear"), Fruit.find("plum"), Fruit.find("fig")
```

```text
true 2 -1
```

#### Sorting and reversing

`L.sort` puts the elements in order, smallest first, and `L.reverse` turns
the list around, last element first. Sorting the other way is the two
together:

<!-- main -->
```cabby
list int Scores = {85, 97, 72, 90}
Scores.sort
println Scores
Scores.reverse
println Scores
println "best:", Scores[0]

list str Names = {"Grace", "alan", "Ada", "Alan"}
Names.sort
println Names
```

```text
[72, 85, 90, 97]
[97, 90, 85, 72]
best: 97
[Ada, Alan, Grace, alan]
```

`.sort` works on lists of numbers, `bool` (`false` first), `char` and
`str`. Strings go in dictionary order, comparing a character at a time by
its code, the same order as `<`
([Chapter 12](#chapter-12-strings)): capitals come before
small letters, so `"alan"` sorts after `"Grace"`. `.reverse` works on a
list of anything. Both change the list in place, so neither can be used
inside a loop that goes through that list.

#### Copies and slices

Like strings, lists are moved, not copied ([Chapter 13](#chapter-13-ownership-moves-and-drops)):
`list int B = A` gives the list to `B`, and `A` can't be used afterwards.
`.copy` makes a second, independent list, and a slice copies part of one,
written as for strings: `L[a to b]`, `L[a to]`, `L[to b]`.

<!-- main -->
```cabby
list int A = {1, 2, 3, 4, 5}
list int B = A.copy
B[0] = 100
list int Middle = A[1 to 3]
println A[0], B[0], Middle.length, Middle[0]
```

```text
1 100 3 2
```

A list of objects can't be copied, because objects are never copied, only
moved.

#### Lists and strings: split and join

`S.split Sep` cuts a string into a `list str` at every `Sep`, and
`L.join Sep` puts a list of strings back together with `Sep` between them:

<!-- main -->
```cabby
list str Words = "the quick brown fox".split " "
println Words.length, Words[1]
println Words.join("-")

int Sum = 0
for Part in "4,8,15".split "," {
	Sum += Part.to_int
}
println "sum:", Sum
```

```text
4 quick
the-quick-brown-fox
sum: 27
```

Splitting `"a,,b"` at `","` gives three strings, the middle one empty;
splitting `""` gives one empty string.

#### Who owns the elements

A list owns its elements, and frees them when it's freed. Adding moves a
string (or object, or list) into the list, so the variable it came from
can't be used again. Keep your own with `.copy`:

<!-- main -->
```cabby
list str Names
str First = "Ada"
Names.add First.copy              // First is still yours
str Second = "Grace"
Names.add Second                  // Second now belongs to the list
println First, Names.length
```

```text
Ada 2
```

`Names[0]` can be read and borrowed, but not moved out of the list (that
would leave a hole): to take an element out, use `.take_last` or
`.remove_at`, or copy it with `Names[0].copy`.

Functions take lists like any other value: `const list int &Scores` to
read one, `list int &Scores` to change it, and a list as a return value is
given to the caller.

<!-- run -->
```cabby
list int evens int Limit {
	list int Found
	for i in Limit {
		if i % 2 == 0 {
			Found.add i
		}
	}
	return Found
}

void double_all list int &Values {
	for i in Values.length {
		Values[i] *= 2
	}
}

int total const list int &Values {
	int Sum = 0
	for v in Values {
		Sum += v
	}
	return Sum
}

int main {
	list int Numbers = evens 10
	double_all Numbers
	println Numbers.length, total(Numbers)
}
```

```text
5 40
```

#### Lists inside lists, and inside objects

Elements can be lists themselves, and objects can keep lists as fields:

<!-- run -->
```cabby
object classroom {
	list str Pupils
	list list int Marks

	void enroll const str &Name {
		Pupils.add Name.copy
		Marks.add {}
	}

	void mark int Pupil, int Score {
		Marks[Pupil].add Score
	}
}

int main {
	classroom Room
	Room.enroll "Ada"
	Room.enroll "Alan"
	Room.mark 0, 9
	Room.mark 0, 7
	Room.mark 1, 8
	for i in Room.Pupils.length {
		println Room.Pupils[i], "has", Room.Marks[i].length, "marks"
	}
}
```

```text
Ada has 2 marks
Alan has 1 marks
```

#### In short

* `list T Name`: a growable list of `T`, empty or `= {a, b, c}`.
* `.add`, `.insert`, `.remove_at`, `.take_last`, `.clear`, `.length`;
  `L[i]` reads and changes elements, and every index is checked.
* `for X in L` goes through it; count with `for i in L.length` to change
  elements; the list itself can't change inside its own loop.
* `println L` shows it as `[a, b, c]`.
* `.contains`, `.find`, `.sort`, `.reverse`, `.copy`, slices `L[a to b]`.
* `S.split Sep` and `L.join Sep` between strings and lists of strings.
* A list owns its elements: adding moves them in.

#### Try this

* Read words with `read_line` until `end_of_input`, keep them in a list,
  and print them backwards (first with a counting loop, then with
  `.reverse`).
* Read numbers the same way and print the middle one once they're sorted.
* Write `list str unique const list str &Words`, which gives each word once
  (use `.contains`).
* Try `Scores[Scores.length]` and read the panic.

[← Chapter 14](#chapter-14-references-borrowing) · [Contents](#contents) · [Chapter 16 →](#chapter-16-lookups)

---

### Chapter 16: Lookups

*Program: `16-lookups.cby`. Keywords: `lookup`.*

A list finds its elements by position: `Scores[0]`, `Scores[1]`. A
*lookup* finds its values by a *key* you choose, such as a name, a word or
an ID number. `lookup str int Ages` holds a whole number for each name: the
type of the keys comes first, then the type of the values.

#### Making a lookup

A lookup starts empty, or with `key: value` entries in braces. `println`
shows the whole thing in braces, in the same way as a list:

<!-- main -->
```cabby
lookup str int Ages = {"Ada": 36, "Alan": 41}
lookup int str Rooms                  // empty
println Ages, Rooms
println Ages["Ada"], Ages.length
```

```text
{Ada: 36, Alan: 41} {}
36 2
```

#### Reading and setting values

`Ages[Key]` reads the value for a key. Setting `Ages[Key] = Value` adds the
key if it's new, or replaces its value if it's already there. `.contains`
says whether a key is there:

<!-- main -->
```cabby
lookup str int Ages = {"Ada": 36}
Ages["Grace"] = 85                    // a new key: added
Ages["Ada"] = 37                      // an old key: its value is replaced
Ages["Ada"] += 1
println Ages
println Ages.contains("Grace"), Ages.contains("Bob")
```

```text
{Ada: 38, Grace: 85}
true false
```

Reading a key that isn't there is a panic, like reading past the end of a
list. So is `+=` on a missing key, since it reads the old value first:

<!-- panic: the key "Bob" isn't in the lookup -->
```cabby
int main {
	lookup str int Ages = {"Ada": 36}
	println "Ada:", Ages["Ada"]
	println "Bob:", Ages["Bob"]
}
```

```text
Ada: 36
```

```text
panic at prog.cby:4:22: the key "Bob" isn't in the lookup (check with .contains first)
    	println "Bob:", Ages["Bob"]
    	                    ^
```

When a key might be missing, check with `.contains` first.

#### Going through a lookup

`for Key, Value in L` runs once for each entry, in the order the keys were
first added. With one name, it goes through the keys alone:

<!-- main -->
```cabby
lookup str int Ages = {"Ada": 36, "Alan": 41, "Grace": 85}
for Name, Age in Ages {
	println Name, "is", Age
}
str Line
for Name in Ages {                    // one name: just the keys
	Line += Name + " "
}
println Line.trim
```

```text
Ada is 36
Alan is 41
Grace is 85
Ada Alan Grace
```

Both names are read-only, as with lists: copies for numbers, `bool` and
`char`, and read-only borrows for anything else. A lookup can't be changed
inside a loop that goes through it:

<!-- error: can't change 'Ages' inside a loop that goes through it -->
```cabby
int main {
	lookup str int Ages = {"Ada": 36, "Alan": 41}
	for Name, Age in Ages {
		Ages[Name + " Jr"] = 0
	}
}
```

```text
error: can't change 'Ages' inside a loop that goes through it
note: the loop over 'Ages' starts here; collect the changes in another
      lookup (or a list) and make them after the loop
```

#### Removing, copying, keys and values

| | |
|---|---|
| `L.remove Key` | takes out the key and its value (a panic if it isn't there) |
| `L.clear` | empties the lookup |
| `L.length` | how many keys it has, as a `long` |
| `L.copy` | a second, independent lookup |
| `L.keys` | a list of the keys, in order |
| `L.values` | a list of the values, in order |

<!-- main -->
```cabby
lookup str int Stock = {"apples": 12, "pears": 0, "plums": 7}
Stock.remove "pears"
println Stock, Stock.length
println Stock.keys, Stock.values
lookup str int Backup = Stock.copy
Stock.clear
println Stock, Backup
```

```text
{apples: 12, plums: 7} 2
[apples, plums] [12, 7]
{} {apples: 12, plums: 7}
```

`.keys` and `.values` give copies, so changing those lists leaves the
lookup alone. Like `.copy`, `.values` needs values that can be copied: not
objects.

#### Counting things

The commonest use of a lookup is counting how often each thing turns up:

<!-- main -->
```cabby
lookup str int Counts
for Word in "the cat sat on the mat by the cat".split " " {
	if Counts.contains Word {
		Counts[Word] += 1
	} else {
		Counts[Word] = 1
	}
}
println Counts
println "the:", Counts["the"]
```

```text
{the: 3, cat: 2, sat: 1, on: 1, mat: 1, by: 1}
the: 3
```

#### What can be a key

Keys can be whole numbers of any size, `char`, `bool` or `str`. Decimals
(`float` and `double`) can't be keys, because two calculations that should
give the same number often differ in the last digit. Values can be of any
type, including lists, objects and other lookups:

<!-- main -->
```cabby
lookup char int Scores = {'a': 1, 'b': 3, 'c': 3}
lookup int str Months = {1: "January", 12: "December"}
lookup bool str Answer = {true: "yes", false: "no"}
lookup str list str Friends
Friends["Ada"] = {"Alan", "Grace"}
Friends["Ada"].add "Charles"
println Scores['b'], Months[12], Answer[3 > 2]
println Friends
```

```text
3 December yes
{Ada: [Alan, Grace, Charles]}
```

The key type is a single word, or `signed` or `unsigned` and a word:
`lookup unsigned long str`. It never takes a second word the way
`long int` does, so `lookup long int Counts` has `long` keys and `int`
values.

#### Changing a list inside a lookup

A list (or lookup) kept as a value is changed through its key:
`Shopping["Fruit"]` *is* that list, so everything a list has works on it
directly. To use one list several times, borrow it with a name:

<!-- main -->
```cabby
lookup str list str Shopping
Shopping["Fruit"] = {"pears", "apples"}
Shopping["Dairy"] = {}
Shopping["Fruit"].add "plums"          // the list's own members, through [key]
Shopping["Fruit"][0] = "green pears"
Shopping["Fruit"].sort
Shopping["Dairy"].add "milk"
println Shopping

list str &Dairy = Shopping["Dairy"]    // or borrow it, and use it by name
Dairy.add "cheese"
Dairy.add "butter"
println Shopping["Dairy"]

for Aisle in Shopping.keys {           // a copy of the keys: the lookup can change
	Shopping[Aisle].add "bags"
}
println Shopping
```

```text
{Fruit: [apples, green pears, plums], Dairy: [milk]}
[milk, cheese, butter]
{Fruit: [apples, green pears, plums, bags], Dairy: [milk, cheese, butter, bags]}
```

A key that isn't there yet has no list to change, so `Shopping["Bakery"].add
"bread"` is a panic until `Shopping["Bakery"] = {}` has made one. And as
with any change to the lookup, its lists can't be changed inside
`for Aisle, Items in Shopping`. Going through `Shopping.keys` works,
because that is a separate list of copies.

#### Who owns the keys and values

A lookup owns its keys and values, and frees them when it's freed. A key is
copied in, so a `str` variable used as a key is still yours afterwards. A
value is moved in, as with `.add` on a list: a string, object or list given
as a value belongs to the lookup from then on. Setting a key that's already
there drops its old value (running its destructor, if it's an object).

`Ages[Key]` can be read and borrowed, but not moved out: copy it with
`.copy`, or take the entry out with `.remove` once you've copied what you
need.

Functions take lookups like lists: `const lookup str int &Ages` to read
one, `lookup str int &Ages` to change it, and a lookup returned from a
function goes to the caller:

<!-- run -->
```cabby
lookup str int count_words const str &Text {
	lookup str int Counts
	for Word in Text.split " " {
		if Counts.contains Word {
			Counts[Word] += 1
		} else {
			Counts[Word] = 1
		}
	}
	return Counts
}

void show_repeats const lookup str int &Counts {
	for Word, Times in Counts {
		if Times > 1 {
			println Word, "comes", Times, "times"
		}
	}
}

int main {
	lookup str int Counts = count_words "a rose is a rose is a rose"
	show_repeats Counts
}
```

```text
a comes 3 times
rose comes 3 times
is comes 2 times
```

#### In short

* `lookup K V Name`: values of type `V` found by keys of type `K`, empty or
  `= {k: v, k: v}`.
* `L[Key]` reads a value (a panic if the key is missing); `L[Key] = V` adds
  or replaces one.
* `.contains`, `.remove`, `.clear`, `.length`, `.copy`, `.keys`, `.values`.
* `for Key, Value in L` (or `for Key in L`) goes through it in the order
  the keys were added; the lookup can't change inside its own loop.
* Keys are whole numbers, `char`, `bool` or `str`; values are anything.
* A list kept as a value is changed through its key: `L[Key].add X`, or
  borrowed with `list T &Name = L[Key]`.
* `println L` shows it as `{k: v, k: v}`.

#### Try this

* Count the letters in a sentence with a `lookup char int`.
* Read lines of the form `name score` with `read_line` until
  `end_of_input`, and keep each person's best score.
* Make a phone book, `lookup str str`, and a loop that asks for a name with
  `input` and prints the number, or "not found".
* Read `Ages["Bob"]` from a lookup without Bob, and read the panic.

[← Chapter 15](#chapter-15-lists) · [Contents](#contents) · [Chapter 17 →](#chapter-17-structs)

---

### Chapter 17: Structs

*Program: `17-struct.cby`. Keywords: `struct`.*

A struct groups values under one name. It's plain data: fields only, with no
methods (for those, see objects in [Chapter 22](#chapter-22-objects-and-methods)).
Its memory layout is exactly C's, so a struct can be handed to C functions.

<!-- run -->
```cabby
struct point {
	int x
	int y
}

struct rect {
	point Corner                  // structs can contain structs
	int Width
	int Height
}

int area const rect &r {
	return r.Width * r.Height
}

int main {
	point a = {3, 4}              // positional, in field order
	point b = {.y = 10, .x = 1}   // by field name, in any order
	point Origin                  // all zero
	println a.x, a.y, b.x, b.y, Origin.x, Origin.y

	a.x = 30                      // fields are read and written with a dot
	rect r = {{0, 0}, 20, 5}      // nested initializers
	println "area:", area(r), "corner y:", r.Corner.y
	println sizeof(point), "bytes per point"
}
```

```text
3 4 1 10 0 0
area: 100 corner y: 0
8 bytes per point
```

Missing fields in an initializer are zero. Naming the fields keeps working
if the struct's fields are ever reordered, so prefer it for structs with
more than two or three fields.

#### Copies, and struct values in expressions

A struct of plain values is itself a plain value: assigning or passing it
copies it.

<!-- run -->
```cabby
struct point {
	int x
	int y
}

point moved_by point p, int Dx, int Dy {
	p.x += Dx                     // changes the copy, not the caller's point
	p.y += Dy
	return p
}

int main {
	point a = {30, 4}
	point d = moved_by a, 1, 1
	println "a:", a.x, a.y, "d:", d.x, d.y
	point e = moved_by point{5, 5}, -5, 0    // a struct value in an expression
	println e.x, e.y
}
```

```text
a: 30 4 d: 31 5
0 5
```

In an expression, put the type in front of the braces: `point{5, 5}`.

A struct that contains a `str` (or an object) isn't plain data any more: it
moves instead of copying, and drops its string when it is dropped.

#### In short

* `struct name { fields }`: plain data with C's layout.
* Initialize positionally `{3, 4}`, by name `{.x = 3}`, or not at all (zero).
* `type{...}` makes a struct value inside an expression.
* Plain structs copy; structs containing strings or objects move.

#### Try this

* Give `point` a third field `z`. Which initializers still work, and what
  value does `z` get?
* Write `bool same point p, point q`.

[← Chapter 16](#chapter-16-lookups) · [Contents](#contents) · [Chapter 18 →](#chapter-18-unions)

---

### Chapter 18: Unions

*Program: `18-union.cby`. Keywords: `union`.*

A union's fields all share the same memory, so it's as big as its largest
field. Writing one field and reading another reinterprets the same bytes.

<!-- run -->
```cabby
union number {
	float f
	unsigned int Bits
	unsigned char Bytes[4]
}

int main {
	number n
	n.f = 1.0f
	println "1.0 is stored as", n.Bits
	n.Bits = 0x40490fdb
	println "these bits as a float:", n.f
	println "top byte:", (int)n.Bytes[3]
	println sizeof(number), "bytes"
}
```

```text
1.0 is stored as 1065353216
these bits as a float: 3.1415927
top byte: 64
4 bytes
```

Reading the "wrong" field is a famous source of bugs, so Cabby allows only plain-data fields
in a union (numbers, chars, bools, and arrays and structs of them), so
reading the "wrong" field can give a strange number but can never cause a
crash or a memory error.

#### Tagged unions

The common use of a union is one value that can be one of several kinds,
together with a *tag* saying which kind it is:

<!-- run -->
```cabby
enum shape_kind {
	CIRCLE
	SQUARE
}

union size {
	double Radius
	int Side
}

struct shape {
	int Kind
	size s
}

double area shape Sh {
	if Sh.Kind == CIRCLE {
		return 3.14159 * Sh.s.Radius * Sh.s.Radius
	}
	return (double)(Sh.s.Side * Sh.s.Side)
}

int main {
	shape a = {CIRCLE, {.Radius = 2.0}}
	shape b = {SQUARE, {.Side = 3}}
	println "areas:", area(a), area(b)
}
```

```text
areas: 12.56636 9.0
```

#### In short

* All of a union's fields share one piece of memory.
* Only plain data is allowed, so a union can't break memory safety.
* Pair a union with a tag to hold one of several kinds of value.

#### Try this

* Set `n.f = -0.0f` and print `n.Bits`. Only one bit differs from `0.0f`.

[← Chapter 17](#chapter-17-structs) · [Contents](#contents) · [Chapter 19 →](#chapter-19-enums)

---

### Chapter 19: Enums

*Program: `19-enum.cby`. Keywords: `enum`.*

An enum gives names to `int` constants. Each name is one more than the one
before, starting at 0, unless you give it a value:

<!-- run -->
```cabby
enum color {
	RED                  // 0
	GREEN                // 1
	BLUE                 // 2
}

enum http_status {
	OK = 200
	CREATED              // 201
	NOT_FOUND = 404
}

str color_name color c {
	switch c {
		case RED:
			return "red"
		case GREEN:
			return "green"
		case BLUE:
			return "blue"
	}
	return "unknown"
}

int main {
	color c = GREEN
	println c, color_name(c)
	print "colors:"
	for i in RED to BLUE {
		print "", color_name(i)
	}
	println
	println OK, CREATED, NOT_FOUND
}
```

```text
1 green
colors: red green blue
200 201 404
```

An enum's name (`color`) can be used as a type for variables and
parameters, which says what a number means. Its values are `int`s
underneath: they print as numbers, and arithmetic and mixing with plain
`int`s work. That keeps enums simple rather than strict.

Enums make good `switch` values and good tags for unions
([Chapter 18](#chapter-18-unions)).

#### In short

* `enum name { A  B = 5  C }`: names for `int` constants, counting up.
* The enum's name is a type; its values are `int`s.

#### Try this

* Give the names the values 1, 2 and 4 and use them as bit flags with `|`
  and `&`.

[← Chapter 18](#chapter-18-unions) · [Contents](#contents) · [Chapter 20 →](#chapter-20-aka-another-name-for-a-type)

---

### Chapter 20: aka: another name for a type

*Program: `20-aka.cby`. Keywords: `aka`.*

`aka` gives an existing type a second name. Write the type, then `aka`,
then the new name, at the top level of a file; it reads as "double, also
known as celsius". It doesn't make a new type: the two names can be used
interchangeably. It's useful to say what a number *means*, or to give a
long type a short name.

<!-- run -->
```cabby
double aka celsius
unsigned char aka byte
int aka matrix[2][2]              // array types can be named too

celsius to_celsius double Fahrenheit {
	return (Fahrenheit - 32.0) * 5.0 / 9.0
}

int trace const matrix &m {
	return m[0][0] + m[1][1]
}

int main {
	celsius Body = to_celsius 98.6
	println "98.6F is", Body, "C"
	matrix m = {{1, 2}, {3, 4}}
	println "trace:", trace(m)
	byte b = 200
	double Plain = Body           // just another name for double
	println (int)b, Plain == Body
}
```

```text
98.6F is 37.0 C
trace: 5
200 true
```

C writes this `typedef double celsius`. Cabby keeps `typedef` reserved, so
writing it says what to write instead:

<!-- error: Cabby has no 'typedef': write the type, then 'aka', then the new name (e.g. 'double aka celsius') -->
```cabby
typedef double celsius

int main {
}
```

#### Kinds: numbers that don't mix

`aka` names a type, but a `celsius` is still a `double`, and nothing stops
a temperature being added to a price. A **kind** is a number of its own:
write the number it is kept as, then `kind`, then its name.

<!-- run -->
```cabby
long kind meters
long kind grams

// A kind's methods are functions named after it: kind.method, with the
// value first. M.text (and println M) then uses meters.text.
str meters.text meters M {
	long N = (long)M
	return N.text + " m"
}

int main {
	meters Walk = 400                      // a number written here is taken as meters
	meters There = Walk + 250
	There += 50
	println There, There * 2, There / Walk
	if There > 500 {
		println "a long way"
	}
	grams Flour = 500
	long Plain = (long)Flour               // a cast turns a kind into a number
	println Plain
}
```

```text
700 m 1400 m 1.75
a long way
500
```

A kind adds to and takes from its own kind, and is multiplied or divided
by plain numbers; one kind divided by the same kind is how many times one
goes into the other (a `double`).

**A number written in the program** (`400`, `-25`, `60 * 60`) is taken as
the kind wherever a kind is wanted: setting it, passing or returning it,
comparing with it, and adding or taking it away. The variable's type, and
a good name (`meters LapLength`), say what the number means. What a kind
never takes is a number from somewhere else:

<!-- error: '+' can't be used with 'meters' and 'long' -->
```cabby
long kind meters
long kind grams

int main {
	meters Walk = 400
	long Steps = 50
	meters Wrong = Walk + Steps            // a variable's number: which unit?
}
```

`Walk + Flour` (different kinds) is an error too, and `(meters)Steps` says
"I mean this" when a plain variable really is meters.

**`strict`** in front makes a kind that doesn't take written numbers
either, for one whose name doesn't say its unit. A `duration`
([chapter 39](#chapter-39-time-and-dates)) is one: it's kept in
nanoseconds, so `time.sleep 5` would wait five nanoseconds; it asks for
`time.seconds(5)` instead.

```cabby
strict long kind duration
```

Some things are measured from a starting point rather than added up: a
date, a moment in time, a position. Two of them can't be added, but the
difference between two is a different kind, and adding that difference
moves one along. `steps` says what that difference is:

<!-- run -->
```cabby
long kind meters
long kind marker, steps meters

int main {
	marker Start
	marker Shop = Start + (meters)400       // a marker and a step: a marker
	marker Park = Shop + (meters)250
	meters Apart = Park - Start             // two markers: the step between
	println (long)Apart, Park > Shop
}
```

```text
650 true
```

The [time and dates](#chapter-39-time-and-dates) libraries are built this
way: a `duration` is a kind, a `moment` steps by durations, and a `date`
steps by whole days.

#### In short

* `existing aka new_name`; both names mean the same type.
* `long kind name`: a number of its own, which doesn't mix with plain
  variables or other kinds; numbers written in the program are taken as
  it, and a cast turns one into the other.
* `strict long kind name`: one that takes no plain numbers at all.
* `long kind name, steps other`: one measured from a starting point.
* Functions named `name.method` are a kind's methods; `name.text` is how
  it is shown.

#### Try this

* Give a struct a second name: `point aka position`, after declaring
  `struct point`, and use both names.
* Make `celsius` and `fahrenheit` kinds of their own (`double kind
  celsius`), and see what Cabby says when they're added together.

[← Chapter 19](#chapter-19-enums) · [Contents](#contents) · [Chapter 21 →](#chapter-21-static-and-global-variables)

---

### Chapter 21: Static and global variables

*Program: `21-static-and-globals.cby`. Keywords: `static`.*

#### Globals

A variable declared outside any function is *global*: every function can
use it, and it lives as long as the program.

<!-- run -->
```cabby
int CallsMade                    // starts at 0, like every variable
const str VERSION = "1.0"

void work {
	CallsMade++
}

int main {
	work
	work
	println "version", VERSION, "calls:", CallsMade
}
```

```text
version 1.0 calls: 2
```

Globals can be plain data (numbers, `bool`, `char`, enums, and structs,
unions and arrays of those), and `const str` constants. A global that's a
changeable `str` or an object isn't allowed, because nothing would ever own
it to free it:

<!-- error: a global str must be const -->
```cabby
str Title = "x"

int main {
}
```

A global's starting value must be a constant, worked out before the program
runs.

#### static inside a function

`static` on a variable inside a function makes it keep its value between
calls. It's given its starting value only once and lives as long as the
program, but only its function can see it:

<!-- run -->
```cabby
int next_id {
	static int Id = 100
	Id++
	return Id
}

int main {
	println next_id, next_id, next_id
}
```

```text
101 102 103
```

#### static at the top level

On a function or global variable, `static` means *private to this file*:
other files that `use` this one can't see it.
[Chapter 33](#chapter-33-splitting-a-program-into-files) shows that in use.

#### In short

* Globals live for the whole program; they hold plain data or `const str`.
* `static` inside a function: one variable kept between calls.
* `static` at the top level: private to the file.

#### Try this

* Remove `static` from `static int Id = 100`. What does `next_id` return
  now, and why?

[← Chapter 20](#chapter-20-aka-another-name-for-a-type) · [Contents](#contents) · [Chapter 22 →](#chapter-22-objects-and-methods)

---

## Part IV: Objects

Objects bundle data with the functions that work on it. They're Cabby's
classes, with destructors, inheritance, and multiple inheritance done the
way Python does it. The part ends with *handlers*: a method that one object
calls on another when something happens, without knowing who is
listening.

### Chapter 22: Objects and methods

*Program: `22-objects.cby`. Keywords: `object` `self`, and `const` and `static` on methods.*

#### Fields and methods

An object is declared like a struct, but it can also have *methods*:
functions that belong to it. Inside a method, the object's own fields and
methods are used by their bare names.

<!-- run -->
```cabby
object counter {
	str Label
	int Count

	void bump {
		Count++                   // this counter's count
	}

	void add int n {
		Count += n
	}

	int value const {
		return Count
	}

	void report const {
		println Label, "=", value
	}
}

int main {
	counter Clicks = {.Label = "clicks"}
	Clicks.bump
	Clicks.bump
	Clicks.add 10
	Clicks.report
	println "doubled:", Clicks.value * 2
}
```

```text
clicks = 12
doubled: 24
```

Objects are made like structs: by field name, positionally, or with every
field zero. A method is called with a dot and its arguments, like any
call: `Clicks.add 10`. A method with no parameters is called by its name
alone, even inside an expression (`Clicks.value * 2`).

Unlike structs, objects are always *moved*, never copied, even when all
their fields are numbers ([Chapter 13](#chapter-13-ownership-moves-and-drops)).

#### const methods

A method marked `const` (after its parameters) promises not to change its
object. Cabby checks the promise, and only `const` methods can be called
through a read-only borrow:

<!-- error: can't call 'bump' through a read-only borrow -->
```cabby
object counter {
	int Count
	void bump {
		Count++
	}
}

int main {
	counter c
	const counter &View = c
	View.bump
}
```

```text
error: can't call 'bump' through a read-only borrow: it can change the object
note: 'View' is a read-only borrow ('const counter &'); drop the 'const' to
      allow changes
```

Mark every method that only reads as `const`. It documents the method, and
it lets the method be used in more places.

#### self

`self` is the object the method was called on. You need it in two cases:
when a parameter or local has the same name as a field, and to pass the
whole object to something else.

<!-- run -->
```cabby
object counter {
	str Label
	int Count

	void set_label str Label {
		self.Label = Label         // the field, not the parameter
	}

	void show const {
		print_counter self
	}
}

void print_counter const counter &c {
	println "[", c.Label, c.Count, "]"
}

int main {
	counter c = {"laps", 6}
	c.set_label "circuits"
	c.show
}
```

```text
[ circuits 6 ]
```

#### static methods

A `static` method has no object. It's called on the type's name, and is the
usual way to write a "constructor" that builds a ready-made object:

<!-- run -->
```cabby
object counter {
	str Label
	int Count

	static counter starting_at str Label, int n {
		return counter{.Label = Label, .Count = n}
	}
}

int main {
	counter Laps = counter.starting_at "laps", 5
	println Laps.Label, Laps.Count
}
```

```text
laps 5
```

#### How methods see their object

A method borrows its object: mutably for an ordinary method, read-only for a
`const` one ([Chapter 14](#chapter-14-references-borrowing)). So calling a
method never moves the object, and the borrowing rules apply as usual.

#### What goes where

A Cabby file has three levels, and each holds different things. Lines that
*do* something (statements: calls, assignments, `if`, loops, `println`)
only ever go inside a function or method.

| Where | What can go there | What can't |
|---|---|---|
| The top level of a file | `use` lines, constants, global variables, types (`struct`, `object`, `enum`, `aka`), `extern` declarations, functions, `main` | statements |
| Directly inside an object | fields, methods and the destructor | statements, and starting values for fields |
| Inside a function or method | statements and local variables | functions, methods and types |

```cabby
use standard                    // top level: use lines,
const int LIMIT = 3             // constants,

object counter {                // types,
	int Count               // in an object: fields,

	void bump {             // and methods.
		Count++         // In a method: statements.
		if Count > LIMIT {
			println "too many"
		}
	}
}

int main {                      // functions, and main.
	counter c               // In a function: locals
	c.bump                  // and statements.
}
```

A statement straight inside an object is an error, because the object
doesn't run: its methods do, when they are called.

<!-- error: an object holds only fields and methods -->
```cabby
object counter {
	int Count
	Count = 5
}

int main {
}
```

```text
error: an object holds only fields and methods; a line that does something,
       like this one, goes inside a method
```

The same goes for the top level ("statements must be inside a function"):
a program starts in `main`, so that's where its first statements go.

**Fields start at zero.** A field can't have a starting value where it's
declared (`int Count = 5` inside an object is an error). Give the starting
values when making the object (`counter c = {.Count = 5}`), in a `static`
method that makes one (below), or, for a window, in its `build` method
([Chapter 51](#the-build-method)).

#### Struct or object?

Both group fields under one name, so which should you use? Think of it
this way: **a struct is a value, like a number; an object is a thing, like
a window.**

| Use a struct for | Use an object for |
|---|---|
| plain data: a point, a color, a date and time, a size | something with a job and a life of its own: a window, a counter, a player in a running game |
| lots of the same: a list of a million points, a picture's dots, records in a file | something there are only a few of, each one its own |
| values you copy and compare freely (`point b = a`) | things that shouldn't be quietly duplicated (two copies of one open file would be trouble) |
| data saved and loaded: `json.from` and `.fill` ([Chapter 40](#chapter-40-json)), records in a binary file ([Chapter 46](#chapter-46-changing-files-in-place)) | anything with methods, inheritance, a destructor, or handlers |
| data handed to C functions ([Chapter 27](#chapter-27-calling-c)) | |

They also behave differently when they're stored or given away:

<!-- run -->
```cabby
struct point {
	int x
	int y
}

object counter {
	int Count

	void bump {
		Count++
	}
}

int main {
	point a = {1, 2}
	point b = a                   // a copy: both are usable
	b.x = 10
	println a.x, b.x

	counter First
	First.bump
	counter Second = First        // moved: First is gone now
	Second.bump
	println Second.Count
}
```

```text
1 10
2
```

A struct sits right inside whatever holds it: a variable, a field, or a
list, with its fields side by side, exactly as C lays them out. That makes
structs as small and fast as data can be. Each object, on the other hand,
lives in its own piece of memory, with a little bookkeeping its methods
need, and its variable holds where that is. That's nothing for a few
objects, but for a million values a list of structs is several times
smaller and faster to go through.

A good habit: start with a struct for data, and make it an object when it
needs to *do* things: methods, a destructor, handlers.

#### In short

* `object name { fields  methods }`; fields and methods by bare name inside.
* Statements go only inside functions and methods; an object's body holds
  fields, methods and its destructor, and fields start at zero.
* `obj.method args` or `obj.method(args)`; no-parameter methods by name alone.
* `const` methods only read, and can be called through `const` borrows.
* `self` is the object itself; `static` methods have none and are called on
  the type.
* Objects always move.
* A struct is a value (plain data, copied, small and fast); an object is a
  thing (methods, a life of its own, moved).

#### Try this

* Add `void reset` that sets `Count` back to 0. Should it be `const`? Try it.

[← Chapter 21](#chapter-21-static-and-global-variables) · [Contents](#contents) · [Chapter 23 →](#chapter-23-made-and-dropped)

---

### Chapter 23: Made and dropped

*Program: `23-destructors.cby`. Topics: `Name { }` and `~Name { }`, when things are made and dropped, and in what order.*

A *destructor* is a method named `~` plus the object's name. It runs
automatically, exactly once, when the object is dropped
([Chapter 13](#chapter-13-ownership-moves-and-drops)). You never call it
yourself. Typical uses are closing a file, releasing something borrowed from
C, or logging.

<!-- run -->
```cabby
object room {
	str Name
	~room {
		println "  leaving", Name
	}
}

void visit str Where {
	room r = {.Name = Where}
	println "  in", r.Name
	if r.Name == "attic" {
		println "  too dusty"
		return                     // r is still dropped on the way out
	}
	println "  staying a while"
}

int main {
	println "end of a block drops in reverse order:"
	if true {
		room a = {.Name = "first"}
		room b = {.Name = "second"}
	}
	println "early return still drops:"
	visit "attic"
	println "a loop drops at the end of each turn:"
	for i in 1 to 2 {
		room r = {.Name = "loop room"}
		println "  turn", i
	}
	println "end of main"
}
```

```text
end of a block drops in reverse order:
  leaving second
  leaving first
early return still drops:
  in attic
  too dusty
  leaving attic
a loop drops at the end of each turn:
  turn 1
  leaving loop room
  turn 2
  leaving loop room
end of main
```

#### Drop order

* **Variables** in a block are dropped in reverse order of declaration, so
  something declared later (which may depend on something earlier) goes
  first.
* **An object's own destructor runs first, then its fields are dropped**, in
  the order they're declared.
* **With inheritance**, the child's destructor runs, then its parent's, up
  to the base-most ([Chapter 24](#chapter-24-inheritance)).
* **Array elements** are dropped from first to last.

<!-- run -->
```cabby
object room {
	str Name
	~room {
		println "  leaving", Name
	}
}

object house {
	str Address
	room Kitchen
	room Hall
	~house {
		println "  closing up", Address
	}
}

int main {
	house h = {.Address = "1 Main St", .Kitchen = {"kitchen"}, .Hall = {"hall"}}
	println "house built"
}
```

```text
house built
  closing up 1 Main St
  leaving kitchen
  leaving hall
```

A moved object is dropped by its last owner only, so its destructor still
runs exactly once. After a panic nothing is dropped: the program just stops.

#### What runs when an object is made

The opposite of a destructor: a part named just the object's name runs
when an object is made, and sets it up. It's for objects what `main` is for
a program, and it's optional, as a destructor is:

<!-- run -->
```cabby
object counter {
	str Label
	int Count

	counter {
		Count = 10
		println "  made", Label
	}

	~counter {
		println "  dropped", Label
	}
}

object panel {
	counter Clicks
	panel {
		println "  made the panel"
	}
}

int main {
	counter c = {.Label = "visits"}
	println "  starts at", c.Count
	panel p
	println "end of main"
}
```

```text
  made visits
  starts at 10
  made 
  made the panel
end of main
  dropped 
  dropped visits
```

It runs whenever an object comes into being: declared, made from
`{...}` (after those fields are set, so it can use them), or made as part
of another object or array. Its parts are made first, as `Clicks` was
before the panel, the reverse of dropping. With inheritance, the parent's
runs first, then the child's ([Chapter 24](#chapter-24-inheritance)). It
takes no parameters, and like a destructor it can't be called by hand.

It's the natural place to give an object's parts its own methods as
handlers ([Chapter 26](#chapter-26-handlers-in-your-own-objects)), since
it runs inside the object.

#### In short

* `Name { ... }` runs when the object is made; `~Name { ... }` when it is
  dropped. Both are optional, take no parameters, and are never called by
  hand.
* Made: parts first, parent before child. Dropped: the other way round.
* Locals: dropped in reverse order. Fields: after the object's destructor,
  in order.
* Early `return`, `break`, `continue` and `goto` still drop.

#### Try this

* Add a `break` inside the loop. Is the room still dropped?
* Make an array `room rooms[2] = {room{"a"}, room{"b"}}`. In what order are
  they dropped?

[← Chapter 22](#chapter-22-objects-and-methods) · [Contents](#contents) · [Chapter 24 →](#chapter-24-inheritance)

---

### Chapter 24: Inheritance

*Program: `24-inheritance.cby`. Topics: `object child : parent`, overriding, virtual calls.*

`object dog : animal` makes a dog a kind of animal: it has every field and
method an animal has, and can add its own or replace (*override*) some.

<!-- run -->
```cabby
object animal {
	str Name
	int Legs

	void speak const {
		println Name, "makes a sound"
	}

	void describe const {
		print Name, "has", Legs, "legs and says: "
		speak                     // the child's speak, if it has one
	}
}

object dog : animal {
	str Breed                     // a field of its own

	void speak const {            // overrides animal's speak
		println "Woof!"
	}

	void fetch const {            // a method of its own
		println Name, "the", Breed, "fetches"
	}
}

object snake : animal {
	// no speak: uses animal's
}

void introduce const animal &a {
	a.describe
}

int main {
	dog Rex = {.Name = "Rex", .Legs = 4, .Breed = "collie"}
	snake Sid = {.Name = "Sid"}
	introduce Rex
	introduce Sid
	Rex.fetch
}
```

```text
Rex has 4 legs and says: Woof!
Sid has 0 legs and says: Sid makes a sound
Rex the collie fetches
```

#### Virtual calls

`introduce` takes any animal, borrowed as a `const animal &`. When it calls
`describe`, and `describe` calls `speak`, the speak that runs is the one for
what the object *really* is: a dog barks even when it's being treated as an
animal. This is called *virtual dispatch*, and it's what lets one function
work with every kind of animal.

A child can be borrowed as its parent, but through that borrow only the
parent's fields and methods can be used, because the borrow might be any
animal:

<!-- error: 'animal' has no method 'fetch' -->
```cabby
object animal {
	str Name
}

object dog : animal {
	void fetch const {
		println Name, "fetches"
	}
}

int main {
	dog Rex = {.Name = "Rex"}
	const animal &a = Rex
	a.fetch
}
```

```text
error: 'animal' has no method 'fetch'
```

#### A list of animals

What's made from a parent goes wherever the parent goes, not only through
a borrow. A `list animal` holds dogs and cats together, and each still
answers as what it is:

<!-- run -->
```cabby
object animal {
	str Name
	str sound const {
		return "..."
	}
}

object dog : animal {
	str sound const {
		return "Woof!"
	}
}

object cat : animal {
	str sound const {
		return "Meow."
	}
}

int main {
	list animal Pets
	Pets.add dog{.Name = "Rex"}
	Pets.add cat{.Name = "Tom"}
	for Pet in Pets {
		println Pet.Name, "says", Pet.sound
	}
}
```

```text
Rex says Woof!
Tom says Meow.
```

As through a borrow, only what every animal has can be used through the
list; and when the list is dropped, each is dropped as what it is, its
own destructor first.

#### Overriding

An override has the same name and the same signature (parameters, return
type and `const`) as the parent's method. A different signature is an error,
not a new method:

<!-- error: with a different signature -->
```cabby
object animal {
	void speak const {
	}
}

object dog : animal {
	int speak const {
		return 1
	}
}

int main {
}
```

```text
error: 'dog.speak' overrides 'animal.speak' with a different signature
note: the overridden method is declared here
```

#### Fields and initializers

A child's fields come after its parent's, so positionally a dog is
`{Name, Legs, Breed}`. Naming the fields, as above, is clearer and keeps
working if the parent changes.

#### Inner parts

`inner` in front of a field or method keeps it for the object's own use:
only the object's methods, and those of objects made from it, can use it.
Everything else is open to all, as before.

<!-- error: 'Balance' is an inner field of 'account' -->
```cabby
object account {
	inner int Balance

	void pay_in int Amount {
		Balance += Amount
	}

	int balance const {
		return Balance
	}
}

object savings : account {
	void add_interest {
		Balance += Balance / 50      // a child can use it
	}
}

int main {
	savings s
	s.pay_in 100
	s.add_interest
	println s.balance
	s.Balance = 1000000              // but nothing outside can
}
```

```text
error: 'Balance' is an inner field of 'account': only account's own methods (and those of objects made from it) can use it
```

So the account decides how its balance changes: through `pay_in`, never
by someone setting it directly. A method that only makes sense from
inside, such as a helper or a handler for one of its parts, can be inner
too. An override must be inner exactly when the method it overrides is.

#### In short

* `object child : parent` inherits every field and method.
* Overrides keep the parent's exact signature.
* `inner` fields and methods are for the object and its children only.
* Calls are virtual: the object's real type decides which method runs.
* A borrow of the parent type only sees what every parent has.
* A parent's list (or variable) holds its children too, each still itself.

#### Try this

* Give `snake` its own `speak` ("Hiss!").
* Add `object puppy : dog` and override `speak` again.

[← Chapter 23](#chapter-23-made-and-dropped) · [Contents](#contents) · [Chapter 25 →](#chapter-25-multiple-inheritance)

---

### Chapter 25: Multiple inheritance

*Program: `25-multiple-inheritance.cby`. Topics: several parents, diamonds, method order.*

An object may have several parents. It gets the fields and methods of all of
them:

```cabby
object catbot : pet, robot {
```

#### The diamond

The famous difficulty is the *diamond*: `pet` and `robot` both inherit from
`thing`. Does a `catbot` contain one `thing` or two? In Cabby, as in Python,
**one**: a shared base appears once, so there's one `Name`.

```text
        thing
       /     \
     pet     robot
       \     /
       catbot
```

#### Which method wins

When two parents both have a method, Cabby uses the same rule as Python, the
*C3 linearization*: the child first, then its parents from left to right,
and a shared base only after everything that inherits from it. For
`catbot : pet, robot` that order is catbot, pet, robot, thing.

<!-- run -->
```cabby
object thing {
	str Name
	void hello const {
		println "I am", Name
	}
	void kind const {
		println Name, "is a thing"
	}
	~thing {
		println "(", Name, "dismantled )"
	}
}

object pet : thing {
	str Owner
	void kind const {
		println Name, "is a pet of", Owner
	}
}

object robot : thing {
	int Battery
	void kind const {
		println Name, "is a robot"
	}
	void charge {
		Battery = 100
		println Name, "charged to", Battery
	}
}

object catbot : pet, robot {
	int Model
}

int main {
	catbot Tom = {.Name = "Tom", .Owner = "Ann", .Battery = 20, .Model = 9000}
	Tom.hello            // from thing
	Tom.charge           // from robot
	Tom.kind             // pet and robot both have kind: pet comes first
	const robot &r = Tom
	r.kind               // virtual: still catbot's choice, pet's kind
}                        // one thing inside, so dismantled once
```

```text
I am Tom
Tom charged to 100
Tom is a pet of Ann
Tom is a pet of Ann
( Tom dismantled )
```

Fields are laid out base-most first, then parent by parent from left to
right: `Name`, `Owner`, `Battery`, `Model`. That's the order for positional
initializers, which is another reason to name the fields.

#### What's refused

Cabby rejects hierarchies that would be ambiguous:

* two parents that each declare a field with the same name;
* parent orders with no consistent C3 order;
* inheritance cycles, and objects that contain themselves.

<!-- error: inherits two fields named 'Owner' -->
```cabby
object pet {
	str Owner
}

object robot {
	str Owner
}

object catbot : pet, robot {
}

int main {
}
```

```text
error: 'catbot' inherits two fields named 'Owner', from 'pet' and 'robot'
```

#### In short

* `object c : a, b` inherits from both; a shared base appears once.
* Method lookup follows the C3 order: child, parents left to right, shared
  bases last.
* Ambiguous field names and inconsistent orders are errors.

#### Try this

* Swap the parents: `object catbot : robot, pet`. Which `kind` wins now?

[← Chapter 24](#chapter-24-inheritance) · [Contents](#contents) · [Chapter 26 →](#chapter-26-handlers-in-your-own-objects)

---

### Chapter 26: Handlers in your own objects

*Program: `26-your-own-handlers.cby`. Topics: `handler` fields, `.call`, `.is_set`, `.clear`.*

#### An object that calls back

A handler field holds someone else's method, to call when the moment comes.
The object gives it a method for setting it (`on_done`, by Cabby's
custom: the windows of Part IX have `on_click` for their buttons), and
calls it with `.call`:

<!-- run -->
```cabby
object countdown {
	int Left
	handler Done                     // whose method to call at zero

	void on_done handler h {
		Done = h
	}

	void tick {
		Left--
		if Left == 0 {
			Done.call
		}
	}
}

object rocket {
	countdown Count
	bool Launched

	rocket {                         // runs when a rocket is made
		Count.Left = 3
		Count.on_done launch         // one of rocket's own methods
	}

	inner void launch {              // for the countdown, not for anyone
		Launched = true
		println "lift off!"
	}
}

int main {
	rocket R
	until R.Launched {
		println R.Count.Left
		R.Count.tick
	}
}
```

```text
3
2
1
lift off!
```

The countdown doesn't know about rockets: it only knows that someone wants
to hear when it reaches zero. The rocket decides what that means.

* **`handler Done`** is a field holding a method to call. To begin with it
  holds nothing.
* **`Done = h`** keeps a handler that was passed in. A handler field can
  only be set this way, from a `handler` parameter.
* **`Done.call`** calls it. If nothing was set, nothing happens.
* **`Done.is_set`** says whether something was set, and **`Done.clear`**
  forgets it.
* **`Count.on_done launch`** gives the countdown one of the rocket's own
  methods, named without parentheses. A handler's method takes no
  parameters and returns nothing.
* The rocket does this in **`rocket { }`**, which runs when a rocket is
  made ([Chapter 23](#chapter-23-made-and-dropped)), so every rocket's
  countdown is ready from the start. **`inner`** keeps `launch` for the
  countdown: nothing outside the rocket can launch it by hand
  ([Chapter 24](#chapter-24-inheritance)).

#### What keeps it safe

A handler is a promise to call an object later, so Cabby makes sure that
object is still there when it happens. Three rules, all checked before the
program runs, make sure of it.

**A handler can only be given to a part of its object.** `Count` is a field
of the rocket, so it can't outlive the rocket. Giving `launch` to a
countdown that isn't part of the rocket could leave the countdown calling a
rocket that's gone, so it's an error:

<!-- error: 'on_done' keeps this handler, so it can only be given to this object or a part of it -->
```cabby
object countdown {
	handler Done
	void on_done handler h {
		Done = h
	}
}

object rocket {
	void prepare {
		countdown Spare
		Spare.on_done launch         // Spare isn't part of the rocket
	}
	void launch {
	}
}

int main {
}
```

Cabby works out which methods keep their handler (`on_done` stores it in a
field) from what their bodies do, including methods that pass it on to a
part of their own. (A window's button, in Part IX, doesn't keep its
`on_click` handler, but gives it to the window, so it takes the window's
methods.)

**Moving is fine; taking apart isn't.** An object's handlers move with it:
put the rocket in a list and its countdown still calls the right rocket. A
field can never be moved out of its object (Cabby refuses that anyway), so
the countdown can't be separated from its rocket.

**Calling something that might call back counts as a change.** While the
rocket goes through one of its lists, `Count.tick` might call `launch`,
which might change that very list. So Cabby treats a call to a part that
has handlers as a change to the whole object, and refuses it inside a loop
over the object's lists:

<!-- error: can't change 'self' inside a loop that goes through it -->
```cabby
object countdown {
	handler Done
	void tick {
		Done.call
	}
}

object rocket {
	countdown Count
	list str Crew

	void roll_call {
		for Name in Crew {
			Count.tick               // might change Crew
		}
	}
}

int main {
}
```

A destructor can't call a handler either, as the object it would call may
be going at the same moment.

#### In short

* `handler Name` in an object; a method `void on_something handler h {
  Name = h }` to set it; `Name.call` to call it.
* `.is_set` and `.clear`; a handler that isn't set does nothing when
  called.
* Give an object one of your methods by name: `Part.on_something method`.
  Only parts of your object (its fields) can be given your methods.

#### Try this

* In `26-your-own-handlers.cby`, give `countdown` a third handler,
  `Halfway`, and have the rocket print "halfway there".
* Make a `bank_account` object with an `on_low` handler, called when the
  balance drops below 10, and a `customer` object that owns one.

[← Chapter 25](#chapter-25-multiple-inheritance) · [Contents](#contents) · [Chapter 27 →](#chapter-27-calling-c)

---

## Part V: Close to the machine

Cabby aims to go as low as anyone needs and as high as anyone wants. This
part is the low end: the C library, called straight from Cabby; memory
itself, through addresses; and the C-like corners of the language.

### Chapter 27: Calling C

*Program: `27-c-functions.cby`. Keywords: `extern` `use`.*

Cabby can call any function in the C library. Its types match C's on 64-bit
Linux and macOS, so C sees exactly what it expects.

#### The standard headers

The C functions Cabby doesn't have its own way to do are already declared
in three headers. `use` brings one in:

<!-- run -->
```cabby
use standard
use math
use strings

int main {
	printf "%d apples at $%.2f each\n", 3, 0.45
	println "sqrt(2) =", sqrt(2.0)
	println "2^10 =", pow(2.0, 10.0)
	println "floor/ceil of 2.5:", floor(2.5), ceil(2.5)
	println "abs(-7) =", abs(-7)
	println "strlen:", strlen("Cabby")
	println "apple before banana?", strcmp("apple", "banana") < 0
}
```

```text
3 apples at $0.45 each
sqrt(2) = 1.4142135623730951
2^10 = 1024.0
floor/ceil of 2.5: 2.0 3.0
abs(-7) = 7
strlen: 5
apple before banana? true
```

| Header | Declares |
|---|---|
| `use standard` | `printf` `abs` `labs` `rand` `srand` `exit` `system` |
| `use math` | `sqrt` `cbrt` `pow` `exp` `log` `log10` `log2` `sin` `cos` `tan` `asin` `acos` `atan` `atan2` `hypot` `floor` `ceil` `round` `trunc` `fabs` `fmod` |
| `use strings` | `strlen` `strcmp` `strncmp` |

C's other everyday functions aren't declared, because Cabby has its own:
`println` and `print` for `puts` and `putchar`, `read_char` and `read_key`
for `getchar`, and `.to_int`, `.to_long` and `.to_double` for `atoi`, `atol`
and `atof`. Using one of C's names says which to use instead.

The headers are ordinary `.hby` files in Cabby's library directory
(`/usr/local/lib/cabby` after `sudo make install`). Open them to see exactly
what they declare.

#### Declaring a C function yourself

`extern` declares a C function the headers don't cover, using C's types and
Cabby's way of writing a function header:

<!-- run -->
```cabby
extern double fmin double a, double b
extern double modf double x, double &whole

int main {
	println "smaller:", fmin(2.5, 1.5)
	double Whole
	double Fraction = modf 4096.25, Whole
	println "4096.25 is", Whole, "and", Fraction
}
```

```text
smaller: 1.5
4096.25 is 4096.0 and 0.25
```

How Cabby's types reach C:

| In the declaration | C receives |
|---|---|
| `int`, `double`, ... | the same type |
| `str` | a `const char *`, NUL-terminated. The string stays yours. |
| `T &` | a pointer to the variable, so C can write into it |
| `...` | any number of extra arguments, as in `printf` |

A C function that takes `...` (`printf`, `sscanf`) must be declared with it,
even when you'll always give it the same arguments. On some processors
(Apple's, in every Mac since 2020) the extra arguments are passed another
way from the others: declared without `...`, they'd go where C doesn't look.

Cabby checks the arguments against the `extern` declaration, but it can't
check what the C function does with them: that a `printf` format matches its
arguments, for instance. Calling C is only as safe as C. Prefer Cabby's own
`println`, and keep to C functions you understand.

In the interpreter, `extern` functions are found in the C library and the
math library while the program runs. Compiled programs are linked with them.

#### In short

* `use standard`, `use math`, `use strings` for the C functions Cabby
  doesn't have its own way to do.
* `extern type name params` for any other C function.
* `str` goes to C as a C string; `T &` as a pointer.

#### Try this

* Use `rand` and `srand` to roll ten dice.
* Pass a `double` to `%d` in `printf` and see what C does with a mismatched
  format.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**printf's long and unsigned numbers.** `%d` is for an `int`; a `long`
needs `%ld`, and an unsigned one `%u` (`unsigned int`) or `%lu` (`unsigned
long`). The wrong one prints nonsense, as in C:

<!-- run -->
```cabby
use standard

int main {
	long Big = 5000000000
	unsigned long Huge = 18000000000000000000
	unsigned int Small = 4000000000
	printf "%ld, %lu and %u\n", Big, Huge, Small
}
```

```text
5000000000, 18000000000000000000 and 4000000000
```

[← Chapter 26](#chapter-26-handlers-in-your-own-objects) · [Contents](#contents) · [Chapter 28 →](#chapter-28-addresses)

---

### Chapter 28: Addresses

*Program: `28-addresses.cby`. Library: `use addresses`.*

#### Where a value is

An *address* says where a value is in memory: C's pointer. Everything so
far has done without one. Borrows let a function change your variable,
and lists, lookups and objects hold data of any shape, all checked before
the program runs. Addresses are for the layer underneath: C functions
that take or give pointers, and memory at fixed places, such as a
microcontroller's device registers.

<!-- run -->
```cabby
use addresses

int main {
	int Count = 5
	address int A = addresses.of Count    // where Count is
	println A.value                       // what's there
	A.value = 6                           // change it
	println Count

	int Scores[4] = {10, 20, 30, 40}
	address int S = addresses.of Scores   // an array: its first element
	println (S + 1).value, S[2]           // one along, two along
	address int Last = addresses.of Scores[3]
	println Last - S, Last > S
}
```

```text
5
6
20 30
3 true
```

* **`address int`** is "the address of an int". `address` on its own
  points at nothing in particular (C's `void *`).
* **`addresses.of X`** gives where `X` is; **`.value`** reads or changes
  what's there; **`A[i]`** is what's `i` values along.
* **`+` and `-`** step by whole values, not bytes. `B - A` says how many
  values apart two addresses are, and addresses compare with `==`, `<`
  and the rest.
* **`addresses.none`** is no address (C's `NULL`); **`.is_none`** checks
  for it. **`addresses.at 0x0200`** is an address given as a number, for
  devices at fixed places.

An address can only point at plain data: numbers, `bool`, `char`, other
addresses, and structs and arrays of those. That's also all C understands.

#### Why it needs `use addresses`

Everything Cabby checks before a program runs depends on knowing what each
name refers to. An address is a number that could point anywhere, so
Cabby can't check it. The `use` line confines that: a file without it
can't touch an address, and anyone reading the file can see that this
part needs care.

When **interpreted**, Cabby checks what it can as the program runs.
Stepping past the end of an array, reading `addresses.none`, or reading an
address as a different kind from the one it was made for each stop the
program with a clear message:

<!-- panic: this address is past the end of an array of 4 (at 4) -->
```cabby
use addresses

int main {
	int Scores[4] = {10, 20, 30, 40}
	address int S = addresses.of Scores
	println "first:", S[0]
	println S[4]
}
```

```text
first: 10
```

```text
panic at prog.cby:6:10: this address is past the end of an array of 4 (at 4)
```

When **compiled**, nothing is checked, as in C. So try such a program with
`cabby prog.cby` first, and compile it once it runs cleanly. Or compile it
with `cabby -c --checked prog.cby`: the program is built with clang's
AddressSanitizer, which stops it at reading or writing past the end, at
memory used after it's freed, and (on Linux) at memory never freed, with
a report naming the function it happened in. It runs about half as fast,
so it's for testing, not for the program you give people.

An address is also only good while its value is there. A local variable
is gone when its function returns, and its block ends; a list's items move
when it changes length. Using an address after that is a panic when interpreted,
saying which value it was and why it went ("this address is of 'Count' in
make_counter, which is gone: 'make_counter' returned"). Returning the
address of a function's own variable is a warning in both modes.

#### Memory by hand

Every value so far has belonged to a variable, and gone when the variable
did. `addresses.allocate` gives memory that belongs to nobody: it stays
until you give it back with `addresses.free`. This is C's `malloc` and
`free`, for the times a C library wants memory you've allocated, or you
want to manage a block of memory yourself.

<!-- run -->
```cabby
use addresses

int main {
	address int Squares = addresses.allocate 5      // room for 5 ints, all 0
	for i in 5 {
		Squares[i] = i * i
	}
	Squares = addresses.reallocate Squares, 10      // room for 10 now
	for i in 5 to 9 {
		Squares[i] = i * i
	}
	println Squares[4], Squares[9]
	addresses.free Squares                          // given back
}
```

```text
16 81
```

* **`addresses.allocate N`** gives room for `N` values of the kind the
  variable says (`address int` here), all set to 0.
* **`addresses.reallocate A, N`** makes it room for `N`. The values already
  there are kept; new room isn't set to anything. The memory may move, so
  use the address it gives, never the old one.
* **`addresses.free A`** gives it back. Freeing `addresses.none` does
  nothing.

Nothing frees this memory for you, which is where C programs go wrong: they
forget to free it (a *leak*), use it after freeing it, free it twice, or
step past its end. The interpreter catches all four. It keeps track of
every block, so using one after it's been given back stops the program and
says where it was freed:

<!-- panic: this memory was given back by addresses.free at -->
```cabby
use addresses

int main {
	address int Scores = addresses.allocate 3
	Scores[0] = 10
	println "first:", Scores[0]
	addresses.free Scores
	println Scores[0]
}
```

```text
first: 10
```

```text
panic at prog.cby:8:16: this memory was given back by addresses.free at prog.cby:7 (allocated at line 4)
```

And when the program ends, the interpreter lists any blocks never given
back, with where each was allocated:

```text
note: 1 block from addresses.allocate never given back (12 bytes):
  12 bytes allocated at prog.cby:4:23
```

It also notices memory being lost *as* it happens: when a variable holding
the only address of a block is changed, or a line allocates again while
nothing holds its last block any more (a leak in a loop), it says where,
once for each line.

So running with `cabby prog.cby` is a memory debugger: try a program that
manages memory by hand there first. Compiled, none of this is checked, as
in C.

#### C functions

A C function that takes or gives a pointer is declared with addresses, and
works as it does in C:

<!-- run -->
```cabby
use addresses

extern address char strchr address char Text, int Letter

int main {
	char Word[6] = {'h', 'e', 'l', 'l', 'o', '\0'}
	address char Start = addresses.of Word
	address char L = strchr Start, 'l'
	println "found at", L - Start
	L.value = 'L'
	println Word[2]
}
```

```text
found at 2
L
```

#### Strings and lists for C

`addresses.of_text Name` gives C a string as it expects one: an `address
char` of its characters, with a `'\0'` after them. `addresses.of_items
Scores` gives the first item of a list of plain data, with the rest after
it, as an array:

<!-- run -->
```cabby
use addresses

extern long strlen address char Text

int main {
	str Name = "cabbage"
	println strlen addresses.of_text Name
	list int Scores = {10, 20, 30}
	address int S = addresses.of_items Scores
	println S[0] + S[1] + S[2]
}
```

```text
7
60
```

These are the string's and list's own memory, so a change through the
address changes them. They're good until the string gets a new value or
grows, or the list changes length; after that the interpreter panics,
saying what changed.

#### C calling back: callbacks

Some C functions call a function of yours: `qsort` asks one which of two
items comes first, as it sorts; a C library tells one of each event.
C takes a *function pointer*. `callback` names its shape, as a function
header without a body, and any function of the program's with that shape
can be given where it's wanted:

<!-- run -->
```cabby
use addresses

callback int comparer address A, address B
extern void qsort address Base, long Count, long Size, comparer Compare

// which comes first: less than 0 for A, more than 0 for B
int by_number address A, address B {
	long X = ((address long)A).value
	long Y = ((address long)B).value
	if X < Y {
		return -1
	}
	if X > Y {
		return 1
	}
	return 0
}

int main {
	list long Numbers = {42, 7, 19, 3, 88, 1}
	qsort addresses.of_items(Numbers), Numbers.length, sizeof(long), by_number
	println Numbers
}
```

```text
[1, 3, 7, 19, 42, 88]
```

A callback takes and gives what C passes: numbers of 32 bits or more
(`int`, `long`, `unsigned int`...), `double`, and addresses; C's `void *`
is a plain `address`, read as what it holds with a cast (`(address
long)A`). Cabby checks that the function given has the callback's shape,
and says what's different when it hasn't. A compiled program gives C the
function itself; the interpreter gives C one that runs it, in the
interpreter, even when C calls it from a thread of its own. A callback
can only be a parameter of an `extern` function: it's how C asks for one,
and Cabby's own code passes handlers instead.

#### A linked list, two ways

A linked list is a chain of nodes, each holding a value and the way to the
next. Cabby can build one without addresses: an object can hold a `list`
of its own kind, and a list of at most one node is "the next one, or
none". The chain owns itself, so dropping the first node drops the rest,
and nothing can be lost or freed twice:

<!-- run -->
```cabby
object node {
	str Name
	list node Next      // empty at the end, or the one node after this
}

void add_at_end node &N, str Name {
	if N.Next.length == 0 {
		node New
		New.Name = Name
		N.Next.add New
		return
	}
	add_at_end N.Next[0], Name
}

void print_all node &N {
	println N.Name
	if N.Next.length > 0 {
		print_all N.Next[0]
	}
}

// takes out the node after N, joining the rest of the chain back on
void remove_after node &N {
	node Gone = N.Next.take_last
	if Gone.Next.length > 0 {
		N.Next.add Gone.Next.take_last
	}
}

int main {
	node Head
	Head.Name = "apple"
	add_at_end Head, "banana"
	add_at_end Head, "cherry"
	remove_after Head
	node New
	New.Name = "fig"
	New.Next.add Head        // the whole chain moves into the new first node
	Head = New
	print_all Head
}
```

```text
fig
apple
cherry
```

The same list as C would write it uses a struct with an address of the
next node, memory allocated for each node, and a loop to free them:

<!-- run -->
```cabby
use addresses

struct node
{
	int Value
	address node Next
}

int main {
	address node Head = addresses.none
	for i in 1 to 3 {
		address node N = addresses.allocate 1
		N.value.Value = i * 10
		N.value.Next = Head      // the new node goes in front
		Head = N
	}
	for address node P = Head; !P.is_none; P = P.value.Next {
		println P.value.Value
	}
	until Head.is_none {        // free each node, keeping the next one's address first
		address node Next = Head.value.Next
		addresses.free Head
		Head = Next
	}
}
```

```text
30
20
10
```

The first is the Cabby way, and a chain of any length is dropped safely
when its first node is. The second is for data that C code builds or
reads.

#### In short

* `use addresses`; `address int A = addresses.of X`; `A.value`, `A[i]`.
* `A + n` steps `n` values; `B - A`; compare; `addresses.none`,
  `.is_none`; `addresses.at N`.
* Plain data only. Checked when interpreted, and compiled with `--checked`.
* `addresses.allocate N`, `addresses.reallocate A, N`, `addresses.free A`:
  memory by hand, which the interpreter tracks.
* `callback int comparer address A, address B` is a C function pointer's
  shape; a function of that shape is given to C by its name.

#### Try this

* In `28-addresses.cby`, write `void swap address int A, address int B`.
* Read one past the end of an array with `cabby`, then compiled, and
  compare.
* Remove the `addresses.free` line from the squares example and run it
  with `cabby` to see the leak note.

[← Chapter 27](#chapter-27-calling-c) · [Contents](#contents) · [Chapter 29 →](#chapter-29-register-and-volatile)

---

### Chapter 29: Register and volatile

*Program: `29-register-volatile.cby`. Keywords: `register` `volatile`.*

Two storage hints. Neither changes what a correct program
prints.

#### register

`register` says "keep this in a CPU register if you can". Modern compilers
decide that for themselves, so in Cabby the useful part is the rule that
comes with it: a `register` variable can never be **borrowed**. It promises
that nothing else will ever see or change the variable.

<!-- error: can't borrow 'Total' because it is declared 'register' -->
```cabby
void bump int &n {
	n++
}

int main {
	register int Total = 0
	for register int i = 1; i <= 100; i++ {
		Total += i
	}
	bump Total
}
```

```text
error: can't borrow 'Total' because it is declared 'register'
note: declared here
```

#### volatile

`volatile` says "every read and write of this must really happen". Compiled
code normally keeps a value in a register and skips reads and writes it can
prove are pointless. That's wrong when something outside the program's own
logic can see or change the memory: a hardware register, a signal handler, a
debugger, or a timing loop you don't want optimized away.

<!-- main -->
```cabby
volatile long Spins = 0
for i in 1000000 {
	Spins++                  // kept, even though nothing reads Spins
}
println "spun", Spins, "times"
```

```text
spun 1000000 times
```

The interpreter performs every access anyway; `volatile` matters in programs
compiled with `cabby -c`. `cabby -c prog.cby --emit-llvm` writes the
compiled code as LLVM IR, where you can see `load volatile` and
`store volatile`.

#### In short

* `register`: the variable can't be borrowed.
* `volatile`: every access really happens in compiled code.

#### Try this

* In `29-register-volatile.cby`, uncomment `bump Total` and read the
  error.
* Compile it with `cabby -c 29-register-volatile.cby --emit-llvm`, and
  look for `load volatile` and `store volatile` in the `.ll` file.

[← Chapter 28](#chapter-28-addresses) · [Contents](#contents) · [Chapter 30 →](#chapter-30-goto-and-labels)

---

### Chapter 30: Goto and labels

*Program: `30-goto.cby`. Keywords: `goto`.*

`goto name` jumps to the line labeled `name:`. A label is indented like any
other line of its block.

Cabby keeps `goto` safe by allowing only **forward** jumps, to a label in
the same block or an enclosing one, and never past a declaration. Its
classic use is getting out of several loops at once:

<!-- main -->
```cabby
int FoundA = 0, FoundB = 0
for a in 1 to 19 {
	for b in 1 to 19 {
		if a * b == 42 && a + b == 13 {
			FoundA = a
			FoundB = b
			goto found          // leaves both loops at once
		}
	}
}
println "no pair"
found:
println "pair:", FoundA, FoundB
```

```text
pair: 6 7
```

Why only forward? A backward `goto` is a loop in disguise, and Cabby
already has three kinds of loop that say what they mean. Jumping backwards
is an error:

<!-- error-main: 'goto' can only jump forward -->
```cabby
int n = 0
again:
n++
goto again
```

```text
error: 'goto' can only jump forward; use a loop to repeat code
note: the label is here
```

When a `goto` leaves a block, everything declared in that block is cleaned
up on the way out, just as at the end of the block
([Chapter 13](#chapter-13-ownership-moves-and-drops)).

#### In short

* `goto label` jumps forward to `label:` in the same or an enclosing block.
* Use it to leave nested loops; use loops for everything else.

#### Try this

* Rewrite the search with a `bool` and two `break`s instead. Which version
  is easier to read?

[← Chapter 29](#chapter-29-register-and-volatile) · [Contents](#contents) · [Chapter 31 →](#chapter-31-command-line-arguments-and-exit-status)

---

## Part VI: Programs in the world

Real programs take arguments from the command line, read and change
files, are split across files of their own, and start other programs.
This part covers all of these, and finishes with a whole program.

### Chapter 31: Command-line arguments and exit status

*Program: `31-main-arguments.cby`.*

`main` can take the program's command-line arguments:

```cabby
int main int Argc, str Argv[] {
```

* `Argc` is how many there are, counting the program's own name.
* `Argv[0]` is the program's name; `Argv[1]` onwards are the arguments.

Running `cabby prog.cby apple banana 42` (or `./prog apple banana 42` when
compiled):

<!-- run: apple banana 42 -->
```cabby
int main int Argc, str Argv[] {
	println "got", Argc - 1, "arguments"
	for i in 1 to Argc - 1 {
		println " ", i, Argv[i]
	}
	if Argc < 2 {
		stderr println "usage: give me some words"   // an error: to stderr
		return 1
	}
	int Total = 0
	for i in 1 to Argc - 1 {
		if Argv[i].is_int {             // skip the words that aren't numbers
			Total += Argv[i].to_int
		}
	}
	println "numbers add up to", Total
	return 0
}
```

```text
got 3 arguments
  1 apple
  2 banana
  3 42
numbers add up to 42
```

`Argv` is borrowed from the system: read its strings, or `.copy` one to get
a string of your own.

#### Exit status

`main`'s return value is the exit status. 0 means success; anything else (up
to 255) is an error code the shell can test:

```sh
cabby prog.cby
echo $?                         # prints the exit status
if cabby prog.cby; then echo ok; else echo failed; fi
```

A panic exits with status 101.

To end the program early, from anywhere (inside a function, deep in a
loop), `exit` takes the status:

<!-- check -->
```cabby
int main int Argc, str Argv[] {
	if Argc < 2 {
		println "say which file"
		exit 1
	}
	println "reading", Argv[1]
}
```

`exit` writes out what was printed, and what open files hold, then ends
there: destructors don't run.

#### Errors go to stderr

A program has two places to print: *stdout*, its ordinary output, and
*stderr*, for errors and warnings. On a terminal both appear on the screen,
but they can be separated: `cabby prog.cby > results.txt` puts stdout in
the file and still shows stderr on the screen, so an error isn't hidden in
the results, and a program reading your output doesn't take an error
message for data.

`print` and `println` write to stdout. Put `stderr` in front of a call to
send everything that call prints to stderr instead:

<!-- both -->
```cabby
void report const str &What, int Code {
	println "problem:", What, "(code " + Code.text + ")"
}

int main {
	println "starting"
	stderr println "warning: nothing to do"
	stderr report "disk full", 28        // everything report prints: stderr
	report "just testing", 0             // stdout, as usual
}
```

```text
starting
warning: nothing to do
problem: disk full (code 28)
problem: just testing (code 0)
```

(Here both streams are shown together, as on a terminal.)

So a function is written once and prints with plain `println`, and the
caller decides where it goes. The rule:

* **Everything the call prints goes to that stream**, including what the
  functions it calls print, until it returns. Then printing goes back to
  where it was, even if the program panics in between.
* **The call still gives its value back:** `int Count = stderr check_files`
  prints to stderr and keeps `check_files`'s answer.
* **`stdout` works the same way**, to put one call back on stdout inside a
  call that was sent to stderr.
* **Only Cabby's own printing follows it:** `print`, `println`, and
  `input`'s prompt. C functions, such as `printf` from `use standard`,
  always write to stdout.

#### In short

* `int main int Argc, str Argv[]`: `Argv[0]` is the program's name, and
  `Argv[1]` on are its arguments.
* `main`'s `return` is the exit status: 0 for all went well, anything else
  for what went wrong; `exit N` ends the program at once with one.
* `stderr println ...` for errors, so they reach the person even when the
  output goes into a file.

#### Try this

* Run `31-main-arguments.cby` with no arguments, then type `echo $?`.
* Run it again with `2> /dev/null` on the end: the usage message goes,
  because it went to stderr.
* Return the number of arguments as the exit status.

[← Chapter 30](#chapter-30-goto-and-labels) · [Contents](#contents) · [Chapter 32 →](#chapter-32-files-directories-and-commands)

---

### Chapter 32: Files, directories and commands

*Program: `32-files.cby`. Libraries: `files`, `directories`.*

So far a program's data has lasted only as long as the program. Files keep
it: a list of scores, a program's settings, notes to read next time. Two
libraries handle them: `use files` for reading and writing files, and
`use directories` for what's in a directory (a folder).

Everything here is a built-in function with the library's name in front,
like `files.read`. The `use` line at the top turns them on.

#### Writing and reading a file

`files.write` makes a file (or replaces one) with the text you give it, and
`files.append` adds to the end. `files.read` gives back the whole file as
one `str`, and `files.lines` gives its lines as a `list str`, without the
line ends:

<!-- run -->
```cabby
use files

int main {
	files.write "shopping.txt", "bread\nmilk\n"
	files.append "shopping.txt", "eggs\n"
	str All = files.read "shopping.txt"
	print All
	list str Items = files.lines "shopping.txt"
	println Items, Items.length
	files.delete "shopping.txt"
}
```

```text
bread
milk
eggs
[bread, milk, eggs] 3
```

A file name on its own, like `"shopping.txt"`, means a file in the
directory the program was started from. A path with directories
(`"notes/today.txt"`) or one starting with `/` works too.

`files.lines` goes well with `for` and `.split`:

<!-- run -->
```cabby
use files

int main {
	files.write "scores.txt", "Ada 9\nAlan 7\nGrace 10\n"
	int Total = 0
	for Line in files.lines "scores.txt" {
		list str Parts = Line.split " "
		Total += Parts[1].to_int
	}
	println "total:", Total
	files.delete "scores.txt"
}
```

```text
total: 26
```

#### When a file isn't there

Reading a file that isn't there is a panic, and the message says which
file and why:

<!-- panic: can't read 'settings.txt': no such file or directory -->
```cabby
use files

int main {
	println "looking for settings.txt"
	str Settings = files.read "settings.txt"
	println "never printed"
}
```

```text
looking for settings.txt
```

```text
panic at prog.cby:5:17: can't read 'settings.txt': no such file or directory
    	str Settings = files.read "settings.txt"
    	               ^
```

The same goes for everything in both libraries: a file that can't be
written, a directory that isn't empty, a disk that's full. When a file
might not be there, check with `files.exists` first:

<!-- run -->
```cabby
use files

int main {
	str Settings = "size 12"
	if files.exists "settings.txt" {
		Settings = files.read "settings.txt"
	}
	println Settings
}
```

```text
size 12
```

#### Copying, renaming and deleting

<!-- run -->
```cabby
use files

int main {
	files.write "draft.txt", "Dear Ada,"
	files.copy "draft.txt", "backup.txt"
	files.rename "draft.txt", "letter.txt"
	println files.exists("draft.txt"), files.exists("letter.txt"), files.size "backup.txt"
	files.delete "letter.txt"
	files.delete "backup.txt"
}
```

```text
false true 9
```

`files.rename` also moves a file to another directory
(`files.rename "letter.txt", "sent/letter.txt"`). `files.size` is in
bytes. Deleting is for good: there is no bin to get it back from.

#### A file as a place to print to

For a file written a piece at a time, such as a log, *open* it. Opening
gives a `file`, and a file goes in front of a call, as `stderr` does
([Chapter 31](#chapter-31-command-line-arguments-and-exit-status)):
everything the call prints goes into the file, and the call still gives
back its value:

<!-- run -->
```cabby
use files

int add int A, int B {
	println "adding", A, "and", B
	return A + B
}

int main {
	file Log = files.open_to_write "cabby-log.txt"
	Log println "started"
	int Sum = Log add 2, 3          // add's printing goes into the file
	println "the sum is", Sum       // this goes to the screen
	Log println "finished with", Sum
	for Line in files.lines "cabby-log.txt" {
		println "  log:", Line
	}
	files.delete "cabby-log.txt"
}
```

```text
the sum is 5
  log: started
  log: adding 2 and 3
  log: finished with 5
```

`files.open_to_write` starts the file afresh, and `files.open_to_add`
adds to the end of what's there. A file is closed when it's dropped, at
the end of its block, like any other value, so it can't be left open by
mistake. It is moved, not copied: pass it to a function as `file &Log`.
What's printed into a file is written out in large pieces, which is fast;
it's all there once the file closes, and whenever the program stops to
wait (for input, or between a window's handlers), so `cat log.txt` in
another terminal shows it.

To read a file a line at a time, without reading it all first, open it
with `files.open` and use `.read_line` and `.at_end`:

<!-- run -->
```cabby
use files

int main {
	files.write "cabby-log.txt", "started\nadding 2 and 3\nfinished with 5\n"
	file In = files.open "cabby-log.txt"
	while !In.at_end {
		str Line = In.read_line
		println Line.length, Line
	}
	files.delete "cabby-log.txt"
}
```

```text
7 started
14 adding 2 and 3
15 finished with 5
```

#### Directories

`directories.make` makes a directory, along with any missing directories
above it. `directories.list` gives the names of what's in one, and
`directories.all_files` every file below it, in the directories inside it
too:

<!-- run -->
```cabby
use files
use directories

int main {
	directories.make "trip/photos"
	files.write "trip/plan.txt", "pack"
	files.write "trip/photos/beach.jpg", ""
	files.write "trip/photos/hills.jpg", ""
	println directories.list "trip"
	println directories.all_files "trip"
	println directories.exists("trip/photos"), files.exists("trip/photos")

	for Name in directories.all_files "trip" {
		files.delete "trip/" + Name
	}
	directories.remove "trip/photos"
	directories.remove "trip"
	println directories.exists "trip"
}
```

```text
[photos, plan.txt]
[photos/beach.jpg, photos/hills.jpg, plan.txt]
true false
false
```

Both lists are sorted. `directories.list` gives bare names (`"plan.txt"`),
so put the directory in front to use one (`"trip/" + Name`).
`directories.all_files` gives paths from the directory you asked about.
`files.exists` and `directories.exists` tell files and directories apart.
`directories.remove` only removes an empty directory, so delete what's in
it first.

#### Where the program is

`directories.current` is the directory the program is working in: where a
plain file name like `"notes.txt"` is found. `directories.change_to` moves
there, and `directories.home` is your home directory:

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

#### Running commands

`use shell` runs other programs, the way you would type them in a
terminal. `shell.run` gives what the command printed, as a `str`:

<!-- run -->
```cabby
use shell

int main {
	str Greeting = shell.run "echo hello from the shell"
	println "it said:", Greeting
	str Count = shell.run "printf 'one\ntwo\nthree\n' | wc -l"
	println Count.trim, "lines"
}
```

```text
it said: hello from the shell
3 lines
```

The command goes to the system's shell, `/bin/sh`, so pipes (`|`) and the
rest work in it. The line end at the very end of the output is left off,
as a terminal's shell does with `$(...)`.

A command that fails (an exit status other than 0, as in
[Chapter 31](#chapter-31-command-line-arguments-and-exit-status)) makes
`shell.run` panic. When a command is allowed to fail, `shell.status` runs
it and gives its exit status instead; its output goes straight to the
screen:

<!-- run -->
```cabby
use shell

int main {
	if shell.status("test -d /tmp") == 0 {
		println "there is a /tmp"
	}
	int Status = shell.status "exit 3"
	println "the status was", Status
}
```

```text
there is a /tmp
the status was 3
```

`shell` also has the program's environment variables: `shell.get "HOME"`
(a panic if it isn't set, so check with `shell.has` when unsure),
`shell.set Name, Value` and `shell.unset Name`. What you set is seen by the
commands you run afterwards.

#### In short

* `use files`: `files.read`, `files.lines`, `files.write`, `files.append`,
  `files.exists`, `files.size`, `files.copy`, `files.rename`,
  `files.delete`.
* `file Log = files.open_to_write Path` (or `open_to_add`), then
  `Log println ...` or `Log some_call`; `files.open` with `.read_line` and
  `.at_end` to read. A file closes when it's dropped.
* `use directories`: `directories.list`, `directories.all_files`,
  `directories.exists`, `directories.make`, `directories.remove`,
  `directories.rename`, `directories.current`, `directories.change_to`,
  `directories.home`.
* A plain file name is found in `directories.current`.
* `use shell`: `shell.run Command` gives what it printed (a panic if it
  fails); `shell.status Command` gives its exit status. `shell.get`,
  `shell.has`, `shell.set` and `shell.unset` for environment variables.
* Anything that goes wrong is a panic that says why; check first with
  `files.exists` or `directories.exists` when you aren't sure.
* The full lists are on the libraries' pages, `files.md` and
  `directories.md` ([Appendix I](#appendix-i-library-reference-pages)).

#### Try this

* Keep a to-do list in a file: read it with `files.lines` if it exists,
  add what's typed with `input`, and save it with `files.write` and
  `.join "\n"`.
* Count the words in a text file with a lookup
  ([Chapter 16](#chapter-16-lookups)).
* List every file below your home directory that ends with `.txt`, with
  its size.
* Read a file that isn't there, and read the panic.
* Print how many files `ls` finds in your home directory, with
  `shell.run` and `.split "\n"`.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**What a file is told to print.** An open file can go in front of a call,
as `stderr` can: everything the call prints goes into the file instead.

<!-- run -->
```cabby
use files

void report int Total {
	println "Total:", Total
	println "All done."
}

void write_report {
	file Log = files.open_to_write "cabby-report.txt"
	Log report 42
	Log println "(written by Log println)"
}                                    // (closed here, as Log is dropped)

int main {
	write_report
	print files.read("cabby-report.txt")
	files.delete "cabby-report.txt"
}
```

```text
Total: 42
All done.
(written by Log println)
```

**A directory held open.** `directories.open` keeps hold of a directory,
so what's inside it is named from there, wherever the program is:

<!-- run -->
```cabby
use files
use directories

int main {
	directories.make "cabby-trip/photos"
	directory Trip = directories.open "cabby-trip"
	Trip.write "plan.txt", "pack\n"
	Trip.add_to "plan.txt", "go\n"
	print Trip.read("plan.txt")
	println Trip.has("plan.txt"), Trip.is_file("plan.txt"), Trip.is_directory("photos")
	println Trip.path_of("plan.txt").ends_with("cabby-trip/plan.txt")
	directory Photos = Trip.open_inside "photos"
	Photos.write "beach.txt", "sand"
	println Photos.names
	files.delete "cabby-trip/photos/beach.txt"
	files.delete "cabby-trip/plan.txt"
	directories.remove "cabby-trip/photos"
	directories.remove "cabby-trip"
}
```

```text
pack
go
true true true
true
[beach.txt]
```

**Programs without a shell.** `shell.run_program` runs a program with its
arguments as a list, so spaces and quotes in them need no care at all;
`shell.run_program_with` gives it something to read too, and
`shell.program_status` gives its exit status. `shell.unset` removes an
environment variable:

<!-- run -->
```cabby
use shell

int main {
	str Odd = "it's \"quoted\" and spaced"
	println shell.run_program({"printf", "[%s]", Odd})
	println shell.run_program_with({"sort"}, "pear\napple\n")
	println shell.program_status({"grep", "-q", "x", "/dev/null"})
	shell.set "CABBY_COLOR", "green"
	shell.unset "CABBY_COLOR"
	println shell.has("CABBY_COLOR")
}
```

```text
[it's "quoted" and spaced]
apple
pear
1
false
```

[← Chapter 31](#chapter-31-command-line-arguments-and-exit-status) · [Contents](#contents) · [Chapter 33 →](#chapter-33-splitting-a-program-into-files)

---

### Chapter 33: Splitting a program into files

*Program: `33-your-own-headers.cby`, with the folder `geometry/`. Topics: `use local`, headers.*

#### Headers and implementation files

A program can be split into several files:

* a **header** (`.hby`) holds the declarations other files need: types, and
  *prototypes* of functions (their headers without bodies);
* an **implementation file** (`.cby`) of the same name, next to it, holds
  the function bodies.

`use local path` reads `path.hby`, relative to the file that says it (leave
out the `.hby`). If a `.cby` of the same name sits next to the header, it is
compiled into the program too. So one line brings in a whole module.

The tutorial's `geometry/geometry.hby`:

```cabby
struct vec2 {
	double x
	double y
}

object polygon {
	str Name
	vec2 Points[4]
	int Count

	// A method body can live in the header; that's fine for small ones.
	void add double x, double y {
		Points[Count] = vec2{x, y}
		Count++
	}
}

double distance vec2 a, vec2 b
double perimeter const polygon &p
```

And `geometry/geometry.cby`, which has the bodies:

```cabby
use math
use local geometry

// static: private to this file. Programs that use geometry can't
// call it.
static double square double v {
	return v * v
}

double distance vec2 a, vec2 b {
	return sqrt square(b.x - a.x) + square(b.y - a.y)
}

double perimeter const polygon &p {
	double Total = 0.0
	for int i = 0; i < p.Count; i++ {
		Total += distance p.Points[i], p.Points[(i + 1) % p.Count]
	}
	return Total
}
```

A program using it:

<!-- run -->
```cabby
use local geometry/geometry

int main {
	println "distance:", distance(vec2{0.0, 0.0}, vec2{3.0, 4.0})
	polygon Tri = {.Name = "triangle"}
	Tri.add 0.0, 0.0
	Tri.add 3.0, 0.0
	Tri.add 0.0, 4.0
	println Tri.Name, "perimeter:", perimeter(Tri)
}
```

```text
distance: 5.0
triangle perimeter: 12.0
```

#### The rules

* Each file is read once per program, however many files `use` it.
* `static` functions and globals are private to their file. The program
  above can't call `square`.
* `use local` paths are relative to the file containing the `use` line.
  `use name` (without `local`) looks in Cabby's library directory instead
  ([Appendix A](#appendix-a-the-cabby-command) says where it looks).
* A `use` line goes at the top level, on its own line.

#### A library of plain functions: no header needed

A header is only needed to share types, or to keep the bodies out of
sight. A file of plain functions can be used on its own: when there's no
`numbers.hby`, `use local numbers` reads `numbers.cby` instead.

```cabby
// numbers.cby
int triple int N {
	return N * 3
}
```

```cabby
// main.cby
use local numbers

int main {
	println triple(14)
}
```

Its `static` functions stay private to it, as in any file, and anything
else it declares can be used by the program.

#### Namespaces

A library can keep its names apart from yours by giving them a prefix:
`object gui.window`, `struct geo.size`. After `use`, the names are written
in full (`gui.window w`), so two libraries, or a library and your program,
can both have a `window`. The `gui` library in Part IX works this way.

A file's own name is a prefix it can use, as a library's is (`use files`
gives `files.read`). So a file of functions names them all with its own
name in front, and `use local` brings them in with it:

```cabby
// kit/tools.cby
double tools.twice double V {
	return V * 2
}

// static: private to this file, so it needs no prefix
static double half double V {
	return V / 2
}

double tools.half_of_twice double V {
	return half tools.twice(V)
}
```

```cabby
// main.cby
use local kit/tools

int main {
	println tools.twice(2.5), tools.half_of_twice(7.0)
}
```

```text
5.0 7.0
```

A type a file declares can start with any prefix (`struct geo.size` makes
`geo` one), and its functions can then use that one too:
`double geo.area const geo.size &S`. This is how Cabby's own libraries
are written: open `lib/brightness.hby` and you'll find
`int brightness.level`, with the C it calls underneath.

#### In short

* `.hby` for declarations, `.cby` beside it for bodies; `use local path`
  brings in both. Without a `.hby`, it brings in the `.cby` alone.
* `static` keeps a top-level name private to its file.
* A file's name is a prefix for its functions: `tools.twice` in
  `tools.cby`, brought in by `use local tools`.

#### Try this

* Add `double area const polygon &p` to the header and write it in
  `geometry.cby`.
* Make a file `words.cby` of your own with `str words.shout const str &S`,
  which gives `S` in capitals with "!" after it, and use it.

[← Chapter 32](#chapter-32-files-directories-and-commands) · [Contents](#contents) · [Chapter 34 →](#chapter-34-processes)

---

### Chapter 34: Processes

*Program: `34-processes.cby`. Library: `use processes`.*

#### A program you talk to

`shell.run` ([Chapter 32](#chapter-32-files-directories-and-commands)) runs
a command and gives back everything it printed once it's finished. A
`process` is for programs that keep going: you can send them lines, read
the lines they print as they come, and wait for them or stop them.

<!-- run -->
```cabby
use processes

int main {
	process Sorter
	Sorter.start "sort"
	Sorter.send "pear"
	Sorter.send "apple"
	Sorter.send "fig"
	Sorter.end_input               // sort waits for the end of its input
	until Sorter.at_end {
		println Sorter.read_line
	}
	println "exit status:", Sorter.wait
}
```

```text
apple
fig
pear
exit status: 0
```

* **`start`** runs a command, by `/bin/sh` as in a terminal.
* **`send`** sends a line to its input; **`end_input`** says there's no
  more.
* **`read_line`** gives the next line it printed, waiting if need be;
  **`at_end`** is `true` once it has ended and every line has been read.
* **`wait`** waits for it to end and gives its exit status.
  **`is_running`**, **`status`** and **`stop`** do what they say.

#### A Cabby program on the other end

A program of your own can be the process. What `send` sends is its
*input*, read with `read_line` until `end_of_input`, as if it were typed;
its *arguments* are the words after its name in `start`, in `Argv`
([Chapter 31](#chapter-31-command-line-arguments-and-exit-status)). A program that only
looks at `Argv` never sees what's sent:

<!-- check -->
```cabby
// count.cby: says what it was started with, and counts the lines it's sent
int main int Argc, str Argv[] {
	println "started with", Argc - 1, "arguments"
	int Lines = 0
	str Line = read_line
	while !end_of_input {
		Lines++
		Line = read_line
	}
	println Lines, "lines"
}
```

<!-- check -->
```cabby
// talk.cby
use processes

int main {
	process Counter
	Counter.start "cabby count.cby one two"    // arguments, after its name
	Counter.send "pear"                          // lines, to its input
	Counter.send "apple"
	Counter.send "fig"
	Counter.end_input                            // now it can count them
	until Counter.at_end {
		println Counter.read_line
	}
}
```

```text
started with 2 arguments
3 lines
```

`count.cby` reads until the end of its input, so it only prints its count
once `end_input` says there's no more. A program that never reads its
input (one that only prints its `Argv`, say) may have ended before the
lines are sent, and sending to it then is a panic: "can't send to 'cabby
args.cby': it has stopped reading (it has ended)". `is_running` says
whether it's still there to send to.

#### A process belongs to its variable

When a `process` variable is dropped, its input is ended and the program
waits for the process to finish. So a process can't be forgotten and left
running in the background. Call `stop` to end one sooner.

For a program that *should* keep running after yours ends, such as a
server, call `detach` before `start`:

<!-- check -->
```cabby
use processes

int main {
	process Server
	Server.detach                  // before start: a daemon
	Server.start "my-server >> server.log 2>&1"
}
```

Cabby then starts it the way Unix daemons have always been started, with
a *double fork*: it runs in a session of its own, so closing the terminal
or pressing Ctrl-C doesn't end it, with no input or output of its own, and
the system adopts it so nothing waits for it. In return, nothing connects
it to the `process` variable any more: it can't be sent lines, waited for
or stopped through it. `processes.md` (Appendix I) explains each step, and
how to write down a daemon's number so it can be stopped later.

#### Handlers for its lines

A process in a field of one of your objects can be given two of the
object's methods ([Chapter 26](#chapter-26-handlers-in-your-own-objects)):
one called for each line it prints, which reads the line with `.line`, and
one called when it ends:

<!-- run -->
```cabby
use processes

object watcher {
	process Job
	int Count

	void run_command const str &Command {
		Job.on_line got_line
		Job.on_end finished
		Job.start Command
		Job.wait                   // the handlers are called meanwhile
	}

	void got_line {
		Count++
		println "line", Count, "says", Job.line
	}

	void finished {
		println "it ended, with exit status", Job.status
	}
}

int main {
	watcher W
	W.run_command "echo first; echo second; exit 3"
}
```

```text
line 1 says first
line 2 says second
it ended, with exit status 3
```

The handlers are called while the program waits: in `wait`, or, in a
window (Part IX), between clicks and key presses. So a window can show a
program's output as it runs; `processes.md` (Appendix I) has a whole
one.

#### In short

* `use processes`; `process P`; `P.start Command`.
* `send`, `end_input`, `read_line`, `at_end`, `wait`, `stop`.
* `on_line` (read `P.line`) and `on_end`, called while waiting.
* Dropping a process waits for it; `detach` before `start` for a daemon.

#### Try this

* In `34-processes.cby`, start `bc` (a calculator), send it `6 * 7`, and
  read its answer.
* Run `ls -l` with `on_line`, and print only the lines that have `.cby`
  in them.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Its errors apart.** What a process prints to stderr goes to your
program's stderr. `keep_errors_apart`, before `start`, gives it to you
instead, as lines of their own:

<!-- run -->
```cabby
use processes

int main {
	process Lister
	Lister.keep_errors_apart
	Lister.start "ls -d / /no/such/folder"
	until Lister.at_end {
		println "output:", Lister.read_line
	}
	until Lister.errors_at_end {
		str Error = Lister.read_error_line
		println "error:", Error.contains("No such file")
	}
	println "exit status:", Lister.wait > 0
}
```

```text
output: /
error: true
exit status: true
```

`on_error_line` calls a handler for each of them, as `on_line` does for
the output, with the line in `error_line`.

[← Chapter 33](#chapter-33-splitting-a-program-into-files) · [Contents](#contents) · [Chapter 35 →](#chapter-35-timers-and-the-event-loop)

---

### Chapter 35: Timers and the event loop

*Program: `35-events.cby`. Library: `use events`.*

#### Waiting for things to happen

Handlers are called when something happens: a click, a line from a
process, a timer going off. Something has to wait for those moments. In a
window that's its `run`; for everything else, it's `events.run`:

<!-- run -->
```cabby
use events

object kettle {
	timer Heat
	int Degrees

	kettle {
		Degrees = 20
		Heat.on_tick warm_up
		Heat.every time.milliseconds 100    // ten times a second
	}

	inner void warm_up {
		Degrees += 20
		println Degrees, "degrees"
		if Degrees >= 100 {
			Heat.stop
			println "boiled!"
		}
	}
}

int main {
	kettle K
	events.run                      // waits, calling handlers
	println "nothing left to wait for"
}
```

```text
40 degrees
60 degrees
80 degrees
100 degrees
boiled!
nothing left to wait for
```

* **`timer`** is an object, so it goes in one of yours, and its handler is
  one of your methods ([Chapter 26](#chapter-26-handlers-in-your-own-objects)).
* **`T.every D`** goes off every so often until **`T.stop`**;
  **`T.after D`** goes off once. `D` is a duration, such as
  `time.milliseconds(100)` or `time.seconds(2)` ([chapter 39](#chapter-39-time-and-dates)
  has more; `use events` brings `time` with it). **`T.is_running`** says whether it's still
  waiting to.
* **`events.run`** waits and calls handlers as things happen, and returns
  once nothing is left to wait for: no timer running, and no process with
  handlers still going. **`events.stop`**, in a handler, ends it sooner.
* A dropped timer stops: ownership tidies up timers too.

#### One loop for everything

There's only one event loop, and everything that waits uses it: timers,
processes' handlers ([Chapter 34](#chapter-34-processes)), and windows. A
gui or tui window's `run` waits in the same loop, so a timer object or a
process carries on while a window is open, and one program can watch a
process, keep time and show a window at once:

<!-- run -->
```cabby
use events
use processes

object watcher {
	process Job
	timer Beat
	int Beats

	watcher {
		Job.on_line got_line
		Job.on_end finished
		Beat.on_tick beat
	}

	void go {
		Job.start "for i in 1 2 3; do echo line $i; sleep 0.1; done"
		Beat.every time.milliseconds 70
	}

	inner void got_line {
		println "process says:", Job.line
	}

	inner void beat {
		Beats++
	}

	inner void finished {
		Beat.stop
		println "it ended; the timer kept going:", Beats > 2
	}
}

int main {
	watcher W
	W.go
	events.run
}
```

```text
process says: line 1
process says: line 2
process says: line 3
it ended; the timer kept going: true
```

#### Waiting is always written down

A program never waits by itself: if `main` ends while a timer is still
running, the program simply ends. The wait is the `events.run` line, so
you can always see where a program waits, and why.

#### In short

* `use events`; a `timer` field; `T.on_tick method`,
  `T.every time.seconds(1)` or `T.after time.milliseconds(250)`, `T.stop`.
* `events.run` waits until nothing is left; `events.stop` ends it.
* Windows, processes and timers share the one loop.

#### Try this

* In `35-events.cby`, add a second timer that prints "still waiting"
  every 300 ms, and stops when the kettle boils.
* Make a timer that prints the time once a second, and stops itself after
  five. (Windows have timers of their own: [Chapter 54](#chapter-54-timers).)

[← Chapter 34](#chapter-34-processes) · [Contents](#contents) · [Chapter 36 →](#chapter-36-a-whole-program-reverse)

---

### Chapter 36: A whole program: reverse

*Program: `36-reverse.cby`, with the folder `reverse/`.*

This program puts several chapters together: command-line arguments
([31](#chapter-31-command-line-arguments-and-exit-status)), a header and its
implementation ([33](#chapter-33-splitting-a-program-into-files)), borrowing
([14](#chapter-14-references-borrowing)) and the string tools
([12](#chapter-12-strings)).

The header, `reverse/reverse.hby`, declares one function:

```cabby
// Returns `Text` backwards. It borrows Text (const str &), so the caller
// keeps its string, and returns a new one.
str reverse_str const str &Text
```

The implementation, `reverse/reverse.cby`:

```cabby
use local reverse

str reverse_str const str &Text {
	str Out = Text.copy             // our own string to change; Text stays as it was
	long n = Text.length
	// Swap the first and last characters, then the second and the
	// second-to-last, and so on. Stop halfway: going all the way would swap
	// every pair back again.
	for i in n / 2 {
		char c = Out[i]
		Out[i] = Out[n - 1 - i]         // the last character is at n - 1, not n
		Out[n - 1 - i] = c
	}
	return Out                       // moved out to the caller: no copy
}
```

And the program, run as `cabby 36-reverse.cby stressed`:

<!-- run: stressed -->
```cabby
use local reverse/reverse

int main int Argc, str Argv[] {
	if Argc != 2 {
		println "Error! One argument needed..."
		return 1
	}

	println reverse_str Argv[1]

	str Word = Argv[1].copy
	str Back = reverse_str Word
	println Word, "<->", Back
	println "palindrome?", Word == Back

	str Racecar = "racecar"
	println Racecar, "palindrome?", Racecar == (reverse_str Racecar)
	return 0
}
```

```text
desserts
stressed <-> desserts
palindrome? false
racecar palindrome? true
```

Things to notice:

* `println reverse_str Argv[1]`: a call can be the argument of another;
  its arguments run to the end of the line. When more follows it, it gets
  parentheses of its own, to show where its arguments end:
  `Racecar == (reverse_str Racecar)`.
* `reverse_str` borrows its argument (`const str &`), so `Word` is still
  usable after `reverse_str Word`.
* `Out` is returned by moving it to the caller; nothing is copied.
* The loop stops at `n / 2`. Going all the way would swap every pair back
  again.

#### Try this

* Run it with no argument, then two. What exit status do you get?
* Add `bool is_palindrome const str &s` that compares `s[i]` with
  `s[s.length - 1 - i]` without building a reversed copy.

#### In short

* A program of its own header (`reverse.hby`), its implementation, and a
  `main` that uses them: `use local reverse`.
* Arguments checked, errors to stderr, and an exit status for each way it
  can end.

[← Chapter 35](#chapter-35-timers-and-the-event-loop) · [Contents](#contents) · [Chapter 37 →](#chapter-37-panics)

---

## Part VII: When it fails

Some mistakes can only be found while a program runs, and a panic ends it
there: that's right for a mistake, which is found and fixed. But some
failures are part of the job, and an `else` deals with them where they
happen.

### Chapter 37: Panics

*Program: `37-panics.cby`.*

Most mistakes are caught before a Cabby program runs: type errors, using a
moved value, conflicting borrows. A few can only be noticed while it runs.
Then the program *panics*: it stops at once, says where and why, and exits
with status 101. It never carries on with corrupted memory.

<!-- panic: index 4 is outside an array of length 4 (positions 0 to 3) -->
```cabby
int main {
	int Squares[4] = {0, 1, 4, 9}
	for i in 0 to 4 {        // `to` includes 4: one too far
		println i, "squared is", Squares[i]
	}
	println "never printed"
}
```

```text
0 squared is 0
1 squared is 1
2 squared is 4
3 squared is 9
```

```text
panic at prog.cby:4:35: index 4 is outside an array of length 4 (positions 0 to 3)
    		println i, "squared is", Squares[i]
    		                                 ^
```

#### What panics, and what doesn't

| Panics | Doesn't panic (well-defined instead) |
|---|---|
| an array or string index out of bounds | integer overflow: wraps around |
| integer division or `%` by zero, and `INT_MIN / -1` | floating-point division by zero: `inf` or `NaN` |
| shifting by a negative amount, or by the type's width or more | a float-to-integer cast out of range: saturates |
| recursing too deeply (stack overflow) | |

The messages:

```text
panic at prog.cby:3:13: division by zero
panic at prog.cby:3:12: can't shift by 40: it must be 0 to 31
panic at prog.cby:2:9: stack overflow: more than 100000 nested calls (calling 'depth')
```

No destructors run after a panic: the program just stops (unless an
`else` catches it, chapter 38). In the
interactive session, a panic stops only the input you typed; the session
carries on.

[Appendix F](#appendix-f-what-is-checked-and-when) lists everything Cabby
checks, before and while a program runs.

#### In short

* A panic stops the program at once, says where and why, and exits with
  status 101: a position past the end, a number that isn't one, dividing
  by zero, a file that isn't there.
* It never carries on with memory gone wrong.
* Check first when failing is expected (`.is_int`, `files.exists`,
  `Names.length`), or catch it with `else` (the next chapter).

#### Try this

* In `37-panics.cby`, fix the loop so it stops at the right place.
* Uncomment each panic in turn, and read its message.

[← Chapter 36](#chapter-36-a-whole-program-reverse) · [Contents](#contents) · [Chapter 38 →](#chapter-38-a-classs-marks)

---

### Chapter 38: A class's marks

*Program: `38-when-it-fails.cby`, a teacher's file of marks, read even when lines in it are wrong. Library: `use files`.*

A file that might not be there is read with a value to use if it isn't:

```cabby
str Settings = files.read "settings.txt" else "out of: 20"
```

`files.read` panics when there's no such file. With `else` after it,
nothing is shown: `Settings` is `"out of: 20"`, and the program carries
on. The `else` catches a panic anywhere in working out the statement,
however deep: in a function it calls, or a library that function calls.

Each line of marks is read by a function of the program's own, which
panics on a line that isn't `"Name: number"` (`.to_int` does, on text
that isn't a number). The `else` here has a block, and a name for why:

```cabby
int Mark = mark_of Line else Why {
	println "skipped '" + Line + "':", Why
	continue
}
```

`Why` is a `str`: what the panic would have said. After a declaration, a
block has to leave (here, `continue` on to the next line), since `Mark`
has no value otherwise; `else 0` would give it one instead.

`else` also goes after an assignment, giving the value assigned if
working it out fails:

```cabby
Fourth = Names[3].copy else "nobody yet"
```

and after a call on its own line, where a block says what to do (or, if
it's empty, to carry on):

```cabby
files.delete "settings.txt" else { }
```

What the failed call had made is dropped, as if it had returned: its
destructors run, last made first, so a file it had opened is closed.
Where failing is expected every time, checking first is clearer:
`files.exists`, `.is_int`, `Names.length`. Ctrl-C, running out of stack,
and `exit` aren't panics, and aren't caught.

#### In short

* `else Value` after a declaration or an assignment: the value to use
  when working it out panics.
* `else { }` after a call on its own: carry on regardless.
* `else Why { }`: a block told what the panic would have said; after a
  declaration, it has to leave (`continue`, `break` or `return`).

#### Try this

* In `38-when-it-fails.cby`, print "no marks" when every line is bad,
  rather than dividing by zero.
* Put the settings into `settings.txt`, and read "out of" from it.
* Read a number typed in with `input`, and ask again when it isn't one,
  using `else`.

[← Chapter 37](#chapter-37-panics) · [Contents](#contents) · [Chapter 39 →](#chapter-39-time-and-dates)

---

## Part VIII: Every day

Libraries for the things most programs need sooner or later: dates and
times, JSON, the web, documents, bytes, patterns in text, hashes,
archives and databases.

### Chapter 39: Time and dates

*Program: `39-dates.cby`. Libraries: `use time`, `use dates`.*

#### Two kinds of time

The computer keeps time as exact **moments**, the same everywhere in the
world; people keep it as **dates** on a calendar and **times** on a
clock, in a time zone. Cabby has a library for each: `use time` for
moments and how long things take, and `use dates` for calendars and
clocks. `use dates` brings `use time` with it.

<!-- run -->
```cabby
use time

int main {
	duration Break = time.minutes(5) + time.seconds(30)
	println Break
	println Break.seconds, "seconds"
	moment Start = time.now
	time.sleep time.milliseconds 10
	println time.since(Start) >= time.milliseconds(10)
}
```

```text
5 minutes 30 seconds
330.0 seconds
true
```

* **`moment`** is one exact moment; `time.now` is now.
* **`duration`** is a length of time. `time.seconds 2`, `time.minutes
  1.5` and the like make one; `D.seconds` and the rest give it back as a
  number.
* **`time.since M`** is how long ago a moment was, and **`time.sleep D`**
  waits.

Both are *kinds* ([chapter 20](#chapter-20-aka-another-name-for-a-type)):
they don't mix with plain numbers, so seconds and milliseconds can't be
confused. `time.sleep 5` is an error that asks for a duration, and a
moment and a duration give a moment, two moments the duration between.

When a call is part of a sum, give it parentheses: `time.minutes(5) +
time.seconds(30)`. Without them, the call would take everything after it.

#### Dates

<!-- run -->
```cabby
use dates

int main {
	date Today = dates.make 2026, 11, 3
	println Today, "is a", Today.weekday
	date Christmas = dates.make Today.year, 12, 25
	println Christmas - Today, "days until Christmas"
	println Today + 14, Today.add_months(3)
	println Today.weekday == TUESDAY, Today.month
}
```

```text
2026-11-03 is a Tuesday
52 days until Christmas
2026-11-17 2027-02-03
true November
```

* **`date`** is a day on the calendar. `dates.today` is today;
  `dates.make Year, Month, Day` makes one (a date that doesn't exist, like
  31 February, is a panic).
* A date **steps by days**: `Today + 14` is two weeks on, and two dates
  give the days between them.
* **`.add_months`** and **`.add_years`** go by the calendar: 31 January
  and a month is the last day of February.
* **`.weekday`** and **`.month`** are enums (`TUESDAY`, `NOVEMBER`), named in capitals as every enum value is, so
  they compare with `==`, and show as their names.

#### Patterns: written as in a spreadsheet

Dates are shown and read with patterns, written the way spreadsheets write
them, so if you've formatted a date in a spreadsheet, you already know
them:

| Letters | Mean | For Tuesday 3 November 2026 |
|---|---|---|
| `d`, `dd` | the day | `3`, `03` |
| `ddd`, `dddd` | the weekday | `Tue`, `Tuesday` |
| `m`, `mm` | the month | `11`, `11` |
| `mmm`, `mmmm` | the month's name | `Nov`, `November` |
| `yy`, `yyyy` | the year | `26`, `2026` |
| `h`, `hh` | the hour | `16`, or `4` with `am/pm` |
| `mm` after an hour | the minutes | `05` |
| `ss` | the seconds | `09` |
| `am/pm`, `AM/PM` | morning or afternoon | `pm`, `PM` |

The rule to remember: `m` is the month, except straight after an hour,
where it's the minutes (`h:mm`). Everything else in the pattern is
written as it is.

<!-- run -->
```cabby
use dates

int main {
	date D = dates.make 2026, 11, 3
	println D.format "d mmmm yyyy"
	println D.format "dddd, dd/mm/yy"
	time_of_day Afternoon = dates.time 16, 5
	date_time Talk = D.at Afternoon
	println Talk.format "ddd d mmm, h:mm am/pm"
	date Read = dates.read "25/12/2026", "dd/mm/yyyy"
	println Read, dates.can_read("31/02/2026", "dd/mm/yyyy")
}
```

```text
3 November 2026
Tuesday, 03/11/26
Tue 3 Nov, 4:05 pm
2026-12-25 false
```

**`dates.read Text, Pattern`** reads a date with the same patterns. Text
that doesn't fit, or isn't a real date, is a panic that says why; check
first with **`dates.can_read`** when it might not be (something a person
typed, say).

#### Clock times and time zones

* **`time_of_day`** is a time on a clock: `dates.time 7, 30`, or
  `dates.time_now`. Add a duration to move it on.
* **`date_time`** is a date and a time together: `dates.now`, or
  `Day.at(Time)`.
* A moment becomes a date and time in a time zone: **`M.local`** here (the
  computer's time zone, with daylight saving), **`M.utc`** in UTC; and
  **`DT.moment`** goes back the other way.

<!-- run -->
```cabby
use dates

int main {
	time_of_day Opens = dates.time 9, 0
	time_of_day Closes = Opens + time.hours(8) + time.minutes(30)
	println Opens.format("h:mm am/pm"), "to", Closes.format("h:mm am/pm")
	println "open for", Closes - Opens
}
```

```text
9:00 am to 5:30 pm
open for 8 hours 30 minutes
```

#### In short

* `use time`: `moment`, `duration`, `time.now`, `time.since`,
  `time.sleep`, `time.seconds N` and the rest.
* `use dates`: `date`, `time_of_day`, `date_time`, `weekday`, `month`;
  `dates.today`, `dates.make`, `dates.time`, `dates.now`.
* A date steps by days; `.add_months`; `.weekday == SATURDAY`.
* `X.format "d mmmm yyyy"`, `dates.read Text, Pattern`: spreadsheet
  patterns, with `m` meaning minutes after an hour.

#### Try this

* In `39-dates.cby`, work out your age in days.
* Print a calendar for this month: start at its first day, and step
  along a day at a time, starting a new line each `SUNDAY`.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Reading times, and about months and years.** (`dates.time_now` is the
time of day here, as `dates.today` is the date.)

<!-- run -->
```cabby
use dates

int main {
	time_of_day Bell = dates.read_time "15:20", "hh:mm"
	date_time Concert = dates.read_date_time "19/12/2026 19:30", "dd/mm/yyyy hh:mm"
	println Bell, Concert
	date Day = dates.make 2028, 1, 31
	println Day.days_in_month, Day.is_leap_year
	println Day.add_months(1), Day.add_years(1), Day.add_years(-1)
}
```

```text
15:20:00 2026-12-19 19:30:00
31 true
2028-02-29 2029-01-31 2027-01-31
```

**Durations, counted.** `time.nanoseconds` to `time.days` make a
duration; `.whole_seconds` and the others count it, as a `long`:

<!-- run -->
```cabby
use time

int main {
	duration Film = time.hours(1) + time.minutes(52) + time.seconds(30)
	println Film.whole_hours, Film.whole_minutes, Film.whole_seconds
	println time.days(2).whole_hours, time.milliseconds(1500).whole_seconds
	println time.nanoseconds(2500000).whole_milliseconds, time.seconds(1).nanoseconds
}
```

```text
1 112 6750
48 1
2 1000000000
```

**Time zones.** A moment is the same everywhere; `in_zone` gives the date
and time somewhere, and `moment_in` the moment a date and time there is.
(`dates.zone_here` is this computer's own zone's name.)

<!-- run -->
```cabby
use dates
use time

int main {
	date_time Lesson = dates.make(2026, 11, 3).at dates.time(19, 0)
	moment When = Lesson.moment_in "Australia/Sydney"
	println When.text_in("Australia/Sydney")
	println When.in_zone("Europe/Berlin"), When.abbreviation_in("Europe/Berlin")
	println When.utc, When.offset_in("Asia/Kolkata")
	println When.utc.utc_moment == When
	println dates.is_zone("Europe/Berlin"), dates.is_zone("Middle/Earth")
}
```

```text
2026-11-03 19:00:00 +11:00
2026-11-03 09:00:00 CET
2026-11-03 08:00:00 5 hours 30 minutes
true
true false
```

**Other languages.** Month and weekday names in a pattern can be in
another language, by its code or its English name:

<!-- run -->
```cabby
use dates

int main {
	date Day = dates.make 2026, 11, 3
	println Day.format_in("dddd d mmmm yyyy", "fr")
	println Day.format_in("dddd d mmmm yyyy", "la")
	println Day.format_in("ddd d mmm", "German")
	println dates.read_in("25 dicembre 2026", "d mmmm yyyy", "it")
	println JULY.text_in("es"), FRIDAY.text_in("Old English")
	println dates.knows_language("el"), dates.knows_language("Klingon")
}
```

```text
mardi 3 novembre 2026
dies Martis 3 November 2026
Di 3 Nov
2026-12-25
julio Frīġedæġ
true false
```

**Loose text.** People write dates every which way; `read_loosely` reads
them in English, from today, and `read_loosely_from` from another day:

<!-- run -->
```cabby
use dates

int main {
	date_time Now = dates.make(2026, 11, 3).at dates.time(9, 15)    // a Tuesday
	list str Said = {"tomorrow", "next friday", "in 2 weeks", "25 Dec", "3 days ago"}
	for Each in Said {
		println Each, "->", dates.read_loosely_from(Each, Now)
	}
	println dates.read_date_time_loosely_from("friday at 5pm", Now)
	println dates.can_read_loosely("tomorrow"), dates.can_read_loosely("the twelfth of never")
}
```

```text
tomorrow -> 2026-11-04
next friday -> 2026-11-06
in 2 weeks -> 2026-11-17
25 Dec -> 2026-12-25
3 days ago -> 2026-10-31
2026-11-06 17:00:00
true false
```

**Seconds, and am or pm.** In a pattern, `s` and `ss` are the seconds,
and `AM/PM` (or `am/pm`) makes the hours run 1 to 12:

<!-- run -->
```cabby
use dates
use time

int main {
	date_time Start = dates.make(2026, 11, 3).at dates.time(16, 5) + time.seconds(9)
	println Start.format("h:mm:ss AM/PM"), Start.format("hh:mm:s am/pm"), Start.format("hh:mm:ss")
}
```

```text
4:05:09 PM 04:05:9 pm 16:05:09
```

**The rest of reading.** `read_date_time_in` reads a date and time in
another language, and `can_read_in` checks first; `read_loosely` and
`read_date_time_loosely` read loose text from today, as it really is:

<!-- run -->
```cabby
use dates
use time

int main {
	println dates.read_date_time_in("mardi 3 novembre 2026 19:30", "dddd d mmmm yyyy hh:mm", "fr")
	println dates.can_read_in("25 dicembre", "d mmmm", "it"), dates.can_read_in("25 dezember", "d mmmm", "it")
	println dates.read_loosely("tomorrow") == dates.today + 1
	println dates.read_date_time_loosely("today at noon").time_of_day
	println time.hours(72).whole_days
}
```

```text
2026-11-03 19:30:00
true false
true
12:00:00
3
```

[← Chapter 38](#chapter-38-a-classs-marks) · [Contents](#contents) · [Chapter 40 →](#chapter-40-json)

---

### Chapter 40: JSON

*Program: `40-json.cby`. Libraries: `use json`, `use csv`.*

#### What JSON is

JSON is the most common way programs pass data to each other as text. Ask
a weather service for today's weather and it answers with something like:

```json
{
  "city": "Sydney",
  "now": { "temperature": 23.5, "raining": false },
  "days": [
    { "day": "Tuesday", "high": 26 },
    { "day": "Wednesday", "high": 24 }
  ]
}
```

There are only six kinds of value: text in quotes, numbers, `true` and
`false`, `null` (nothing here), lists in `[ ]`, and names with values in
`{ }`. They nest inside each other to any depth.

JSON calls the `{ }` kind an *object*, but in Cabby an object is
something else ([chapter 22](#chapter-22-objects-and-methods)), and names
with values are what Cabby calls a **lookup**. So that's what the json
library calls it too: `json.lookup` and `.is_lookup`. When another
language's documentation says "JSON object", read "lookup".

#### Reading it

Cabby checks every type before a program runs, but JSON's types aren't
known until the text is read. So one type, `json`, holds any JSON value,
and you ask it for what you expect:

<!-- run -->
```cabby
use json

int main {
	str Text = "{\"city\": \"Sydney\", \"now\": {\"temperature\": 23.5}, "
	Text += "\"days\": [{\"day\": \"Tuesday\", \"high\": 26}, {\"day\": \"Wednesday\"}]}"
	json Weather = json.read Text
	println Weather["city"].str, Weather["now"]["temperature"].double
	for Day in Weather["days"] {
		println Day["day"].str, Day["high"].long_or(0)
	}
}
```

```text
Sydney 23.5
Tuesday 26
Wednesday 0
```

* **`json.read Text`** reads it (usually from `files.read`). Text that
  isn't JSON is a panic that says where: "line 2, column 4: ...".
* **`J["name"]`** goes into a lookup and **`J[2]`** into a list; they
  chain: `Weather["days"][0]["high"]`. **`for X in J`** goes through a
  list.
* **`.str`, `.double`, `.long`, `.bool`** give real Cabby values, named
  after the types you get.

Asking for the wrong kind, or a name that isn't there, is a panic that
says what it found: `'days[0].high' is text ("26"), not a number`. A
program never carries on with a wrong value without knowing. When
something might really be missing, say so: **`.long_or 0`** (and
`.str_or`, `.double_or`, `.bool_or`) gives a default instead, and
**`.has "name"`**, **`.is_number`** and the like check first.

#### Writing it

<!-- run -->
```cabby
use json

int main {
	json Settings = json.lookup
	Settings["name"] = "Ada"
	Settings["volume"] = 7
	json Recent = json.list
	Recent.add "notes.txt"
	Settings["recent"] = Recent
	println Settings
	println Settings.pretty
}
```

```text
{"name":"Ada","volume":7,"recent":["notes.txt"]}
{
  "name": "Ada",
  "volume": 7,
  "recent": [
    "notes.txt"
  ]
}
```

* **`json.lookup`** and **`json.list`** start empty; `J["name"] = V` and
  `J.add V` fill them. Text, numbers and `true`/`false` become JSON by
  themselves.
* **`.text`** is the JSON on one line, for sending (and what `println`
  shows); **`.pretty`** is indented, for files people read.
* A value taken out of another (`json Day = Weather["days"][0]`) is part
  of it: changing it changes the whole. **`.copy`** makes one of its own.

#### Structs, to JSON and back

Building JSON a value at a time is fine for a few values. For a struct,
**`json.from`** does it all at once, by the fields' names, and
**`.fill`** goes the other way:

<!-- run -->
```cabby
use json

struct settings {
	str Theme
	int Volume
	bool Sounds
	list str RecentFiles
}

int main {
	settings Mine = {"dark", 7, true}
	Mine.RecentFiles.add "notes.txt"
	println json.from(Mine).pretty

	settings Loaded = {"light", 5, true}       // the defaults
	json Old = json.read "{\"theme\": \"blue\", \"volume\": 9}"
	Old.fill Loaded
	println Loaded.Theme, Loaded.Volume, Loaded.Sounds, Loaded.RecentFiles.length
}
```

```text
{
  "theme": "dark",
  "volume": 7,
  "sounds": true,
  "recent_files": [
    "notes.txt"
  ]
}
blue 9 true 0
```

* In JSON, a field's name is written in small letters with `_` between
  words (`RecentFiles` is `"recent_files"`), as most JSON is. `.fill`
  also takes `"recentFiles"` and `"RecentFiles"`.
* `.fill` changes only the fields the JSON has, so a settings file from
  an older version of a program, without the newer settings, still
  loads, and the struct's own values are the defaults. Names the struct
  doesn't have are passed over.
* Fields can be text, numbers, `bool`, `char`, enums (written by name),
  other structs, `json`, and lists, `lookup str ...`s and arrays of
  those. A wrong kind is a panic that says which field.

So saving and loading a program's data is one line each:

```cabby
files.write "settings.json", json.from(Mine).pretty
json.read(files.read("settings.json")).fill Mine
```

Why not save a struct's bytes as they are in memory (chapter 45)? Those
depend on the computer and on exactly how the struct is declared, so
adding a field, or opening the file on another computer, would break it.
JSON keeps the names, so it doesn't.

#### How the library is made

`json` is an ordinary Cabby object, written in `json.hby` with its work
done in C. Three things let it feel built in, and any library's object can
use them:

* An object with **`item`** and **`item_at`** methods can be read with
  `[ ]` (by a name or a number), and with **`set_item`** and
  **`set_item_at`**, written to with `[ ] =`. With `length` and `item_at`,
  `for X in J` goes through it.
* Where an object is wanted and a `str`, number or `bool` is given, a
  function named **`json.from_str`** (`from_long`, `from_double`,
  `from_bool`) turns it into one.
* A method can be named after a type with a `_` after it (`long long_`),
  and is called without it: `J.long`. An object with a **`text`** method
  is shown by it in `println`.

#### Tables: CSV

A spreadsheet saves a table as **CSV**: a row a line, the fields between
commas, and a field in double quotes when it holds a comma, a quote (as
`""`) or a line end. `use csv` reads one into rows, each a `list str` of
its fields, or into records, a lookup each, by the names the first row
gives the columns:

<!-- run -->
```cabby
use csv
use files

int main {
	files.write "people.csv", "Name,Age,Town\nAda,36,London\n\"Lovelace, Ada\",36,\"Ockham \"\"Park\"\"\"\n"
	for Row in csv.read_file "people.csv" {
		println Row.length, Row
	}
	for P in csv.read_records_file "people.csv" {
		println P["Name"], "is", P["Age"].to_long + 1, "next year, in", P["Town"]
	}
	list list str Rows = {{"Item", "Price"}, {"tea, black", "3.50"}}
	csv.write_file "prices.csv", Rows
	print files.read("prices.csv")
	files.delete "people.csv"
	files.delete "prices.csv"
}
```

```text
3 [Name, Age, Town]
3 [Ada, 36, London]
3 [Lovelace, Ada, 36, Ockham "Park"]
Ada is 37 next year, in London
Lovelace, Ada is 37 next year, in Ockham "Park"
Item,Price
"tea, black",3.50
```

Every field is text: `.to_long` and `.to_double` make numbers of them.
`csv.read` and `csv.read_records` take the text itself, `csv.text` gives rows
as text, and `csv.read_separated Text, ';'` reads fields between
semicolons (as some countries' spreadsheets save them) or tabs.
`csv.md` has the rest.

#### In short

* `use json`; `json J = json.read Text`; `J["name"]`, `J[i]`, `for X in J`.
* `.str`, `.double`, `.long`, `.bool`: a panic if it's not that, unless
  `_or Default`.
* `json.lookup` (JSON's "object"), `json.list`; `J["name"] = V`, `J.add V`;
  `.text`, `.pretty`.
* `json.from S` makes JSON from a struct; `J.fill S` fills one in from
  JSON, by the fields' names.
* `use csv`: `csv.read_file Path` (rows), `csv.read_records_file Path`
  (lookups, by the first row's names), `csv.write_file Path, Rows`.

#### Try this

* In `40-json.cby`, add a song with no title, and make the program show
  "(no title)" for it instead of panicking.
* Save some settings of your own to a file, with a struct and
  `json.from(...).pretty`, and read them back with `.fill` the next time
  the program runs.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Defaults, and asking what kind.** `.long_or` has brothers for each
kind, and `.is_str` and the rest say what a value is before it's asked
for:

<!-- run -->
```cabby
use json

int main {
	json Settings = json.read "{\"name\": \"Ada\", \"volume\": 0.8, \"muted\": null, \"tags\": [], \"window\": {}}"
	println Settings["name"].str_or("nobody"), Settings["title"].str_or("no title")
	println Settings["volume"].double_or(1.0), Settings["muted"].bool_or(false)
	list str Names = {"name", "volume", "muted", "tags", "window"}
	for Name in Names {
		json Value = Settings[Name]
		println Name + ":", Value.is_str, Value.is_number, Value.is_bool, Value.is_null, Value.is_list, Value.is_lookup
	}
}
```

```text
Ada no title
0.8 false
name: true false false false false false
volume: false true false false false false
muted: false false false true false false
tags: false false false false true false
window: false false false false false true
```

[← Chapter 39](#chapter-39-time-and-dates) · [Contents](#contents) · [Chapter 41 →](#chapter-41-downloading)

---

### Chapter 41: Downloading

*Program: `41-downloads.cby`. Library: `use networks`.*

`networks.download` fetches what's at an address on the internet, as a
`str`: a web page, a web service's JSON, or any other bytes.

```cabby
use networks
use json

int main {
	// a web page, as text
	str Page = networks.download "https://example.com"
	println Page.length, "characters"

	// a web service's answer: Sydney's weather now, from Open-Meteo (free,
	// no sign-up), as JSON
	str Url = "https://api.open-meteo.com/v1/forecast?latitude=-33.87&longitude=151.21&current_weather=true"
	json Weather = json.read networks.download Url
	println "Sydney:", Weather["current_weather"]["temperature"].double, "degrees"

	// saved as a file
	networks.download_to "https://example.com", "example.html"
}
```

These are real addresses, so the program runs as it is.

* **`networks.download Url`** gives what's there. With `json.read` in
  front, a web service's answer is ready to use in one line.
* **`networks.download_to Url, Path`** saves it as a file, without it
  ever having to fit in memory. A failed download leaves nothing behind.
* **`networks.can_reach Url`** says whether something is there and
  answering, and **`networks.status Url`** gives the number it answers
  with: `200` for fine, `404` for nothing there, `0` if nothing answered at
  all.

A download that fails is a panic that says why ("404 Not Found: nothing is
at that address", "Could not resolve host"), at the line that asked. When
it might not work (the internet may be down, or a person typed the
address), check first with `can_reach`.

`file://` addresses fetch files on this computer, which is handy for
trying a program out without the internet; the tutorial program does that,
so it gives the same output every time:

<!-- run -->
```cabby
use networks
use files
use directories

int main {
	files.write "note.txt", "hello from a file"
	str Url = "file://" + directories.current + "/note.txt"
	println networks.download Url
	println networks.can_reach(Url + ".old"), networks.status(Url + ".old")
	files.delete "note.txt"
}
```

```text
hello from a file
false 0
```

Downloads use libcurl, the library behind the `curl` command, so
`https://`, redirects and proxies all work as they do for `curl`.

#### Who's on the network

The computers on your own network can be asked whether something's
listening on a port, all of them at once, as `nmap` does. A computer
that's on and serving that port answers within a moment; one that isn't
there doesn't, and isn't waited for longer than the time given:

<!-- check -->
```cabby
use networks
use time

int main {
	list str Near = networks.nearby_addresses     // 192.168.1.1 to .254, but this one
	println "asking", Near.length, "computers about port 22 (ssh)..."
	for Host in networks.answering Near, 22, time.milliseconds(500) {
		println " ", Host, "has ssh"
	}
	if networks.pings "192.168.1.1", time.seconds(1) {
		println "the router answers a ping, in", networks.ping_time("192.168.1.1", time.seconds(1)).milliseconds, "ms"
	}
}
```

```text
asking 253 computers about port 22 (ssh)...
  192.168.1.12 has ssh
  192.168.1.30 has ssh
the router answers a ping, in 1.84 ms
```

`networks.answering Hosts, Port, Wait` gives those of `Hosts` with a
program listening on `Port`, and takes `Wait` however many are asked.
`networks.pings` and `networks.ping_time` send a ping, as the `ping`
command does; many computers are set not to answer one, though, so a
port is the surer way to find them. Ask only the computers on a network
that's yours: on anyone else's, it's what an attacker does first.

#### In short

* `use networks`; `networks.download Url`, `networks.download_to Url,
  Path`.
* `networks.nearby_addresses`, `networks.answering Hosts, Port, Wait`,
  `networks.pings Host, Wait` and `ping_time`: who's on the network.
* `networks.can_reach Url` and `networks.status Url` check first; a failed
  download is a panic that says why.

#### Try this

* Download a page you know, and count how many times a word appears in it
  (`.split`, or `.find` in a loop).
* Find a free weather service that answers in JSON, and print tomorrow's
  forecast.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it. These need the
internet, so they're checked here rather than run.*

**Bytes, and sending.** `download_bytes` gives what's at an address as
[bytes](#chapter-45-bytes), for a picture or a zip file. `post` sends text
and gives back the answer; `post_json` sends JSON and reads the answer as
JSON:

<!-- check -->
```cabby
use networks
use json
use bytes

int main {
	bytes Logo = networks.download_bytes "https://example.com/logo.png"
	println Logo.length, "bytes"
	str Said = networks.post "https://httpbin.org/post", "Hello", "text/plain"
	json Item = json.read "{\"name\": \"Ada\", \"age\": 36}"
	json Made = networks.post_json "https://httpbin.org/post", Item
	println Made["json"]["name"].str
}
```

**A request of your own.** `request_to` makes a request to set up first:
any method, headers, a body, a time limit; then a look at the whole
answer, where a status of 404 isn't a panic:

<!-- check -->
```cabby
use networks
use json

int main {
	networks.request R = networks.request_to "https://api.example.com/items"
	R.set_method "PUT"
	R.set_header "Accept", "application/json"
	R.set_json json.read("{\"name\": \"tea\"}")
	R.set_timeout 10
	if R.send == 0 {
		println "no answer:", R.error
		return 1
	}
	println R.status, R.answer_header("Content-Type")
	if R.worked {
		println R.answer_json["id"].long_or(0), R.answer_bytes.length
	}
	return 0
}
```

`set_body Text, Kind`, `set_form Fields` and `set_bytes Data, Kind` send
other kinds of body; `save_to Path` sends it and saves the answer as a
file as it comes.

**Progress.** `on_progress` calls a method as a big transfer goes, with
`received`, `total` and `fraction` (and `sent` and `to_send` for what's
going the other way):

<!-- check -->
```cabby
use networks

object fetcher {
	networks.request R

	void get const str &Url, const str &Path {
		R = networks.request_to Url
		R.on_progress went
		if R.save_to(Path) != 200 {
			println "\nit didn't work:", R.status, R.error
		}
	}

	void went {
		print "\r" + R.received.text + " of " + R.total.text + " bytes"
	}
}

int main {
	fetcher F
	F.get "https://example.com/big.zip", "big.zip"
	return 0
}
```

**Programs talking to each other.** A server listens on a port; a
connection talks to one, a line at a time. Here both are in one program,
on this computer:

<!-- run -->
```cabby
use networks
use events

object shouter {
	networks.server S
	networks.connection C

	void begin {
		S.on_connect came
		S.on_line heard
		S.listen_here 0                 // 0: any free port
		C = networks.connect "127.0.0.1", S.port      // (this computer, by its IPv4 address)
		C.on_line answered
		C.send_line "hello"
		C.send_line "goodbye"
		events.run
	}

	void came {
		println "connections:", S.connections.length, "from", S.address_of(S.from)
	}

	void heard {
		S.reply S.line.upper
	}

	void answered {
		println "back came", C.line
		if C.line == "GOODBYE" {
			S.close_connection S.connections[0]
			S.close
			C.close
			println "open:", C.is_open
		}
	}
}

int main {
	shouter Sh
	Sh.begin
}
```

```text
connections: 1 from 127.0.0.1
back came HELLO
back came GOODBYE
open: false
```

`listen Port` listens for other computers too; `send_line_to Who, Text`
and `send_line_to_all Text` send without being asked; `on_close` is
called when the other end closes. `networks.web_server` answers browsers
(pages, files, forms, cookies), `networks.open_socket` and a web server's
`accept_socket` talk through WebSockets, and `connect_encrypted`,
`make_certificate` and `set_certificate` make it all encrypted. Each has
its own part of `networks.md` (Appendix I); they'll have a chapter of
their own.

**Where this computer is on its network.** `networks.local_address` is
its address there, for another computer to connect to, and
`networks.wifi_name` the Wi-Fi network it's on (each `""` when there's
none):

<!-- check -->
```cabby
use networks

int main {
	println "This computer is", networks.local_address, "on", networks.wifi_name
}
```

[← Chapter 40](#chapter-40-json) · [Contents](#contents) · [Chapter 42 →](#chapter-42-web-addresses)

---

### Chapter 42: Web addresses

*Program: `42-web-addresses.cby`. Library: `use networks`.*

A web address has parts:

```text
https://example.com/search?q=fish+%26+chips&page=2#top
kind    computer   page    query                  part of the page
```

`networks.read_url` takes one apart into a `networks.url`: `Scheme`,
`Host`, `Port`, `Path`, `Query` (a lookup of the names and values after
`?`, in their order) and `Fragment`. `text` puts it back together.

<!-- run -->
```cabby
use networks

int main {
	networks.url Page = networks.read_url "https://example.com/search?q=fish+%26+chips&page=2"
	println Page.Host, Page.Path, Page.Query["q"]
	Page.Query["page"] = "3"
	println Page.text
}
```

```text
example.com /search fish & chips
https://example.com/search?q=fish%20%26%20chips&page=3
```

#### Encoding

Some letters can't go in an address as they are: a space is written
`%20`, an `&` `%26`, and `é` (two bytes in UTF-8) `%C3%A9`. Taking an
address apart decodes them, so the parts are plain text; putting it back
together encodes what needs it. `networks.url_encode` and `networks.url_decode`
do it to any text, and `+` in a query is a space too, as forms send it.

#### Making an address

An empty `networks.url` with its parts filled in is an address: the
tutorial makes the weather service's question from chapter 41, with its
latitude and longitude in `Query`, and a `file://` address for a file on
this computer, which `networks.download` reads.

An address without its kind first (`https://`) can't be read: the
tutorial ends with one, and its panic.

This part of networks is written in Cabby itself, in `networks.hby`: it's
all text.

#### In short

* `networks.read_url Address` gives its parts (`Scheme`, `Host`, `Port`,
  `Path`, `Query`, `Fragment`), and `.text` puts them together again.
* `networks.url_encode` and `url_decode` make text safe for an address, and back.

#### Try this

* Download the weather address in `42-web-addresses.cby` with
  `networks.download` (it's a real one), and read it with `json.read`.
* Take apart an address from your browser's address bar, and print every
  name and value in its query.
* Add a port: `Weather.Port = 8080`, and see where it goes.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**A query on its own.** `networks.read_query` reads the part after the
`?` into its names and values, and `networks.query_text` writes one:

<!-- run -->
```cabby
use networks

int main {
	lookup str str Query = networks.read_query "q=fish+%26+chips&page=2"
	println Query["q"], Query["page"]
	Query["page"] = "3"
	println networks.query_text(Query)
}
```

```text
fish & chips 2
q=fish%20%26%20chips&page=3
```

[← Chapter 41](#chapter-41-downloading) · [Contents](#contents) · [Chapter 43 →](#chapter-43-html)

---

### Chapter 43: HTML

*Program: `43-html.cby`. Library: `use html`.*

#### HTML in five minutes

A web page is written in **HTML**: text, with **tags** around parts of it
saying what each part is.

```html
<h1>Corner Café</h1>
<p>Open every day. <a href="https://example.com/map">Find us</a></p>
```

* `<h1>` starts a heading and `</h1>` ends it. A tag, its end, and what's
  between them make an **element**: an `h1` element holding the text
  "Corner Café".
* Elements go inside elements: the `p` (a paragraph) holds some text and an
  `a` (a link).
* A tag can carry **attributes**, `name="value"` pairs inside its `<...>`.
  The `a` has `href`: the address it links to.
* A few elements have no end, because nothing goes inside them: `<br>` (a
  line break), `<img src="photo.jpg">` (a picture), `<hr>` (a line across),
  `<input>`.

Two attributes are on almost every page, because they let parts be picked
out (by the page's own colors and programs, and by yours):

* **`class`** says what kind of thing an element is: `<li class="item">`.
  Many elements share a class, and one element can have several, with
  spaces between: `class="item sold-out"`.
* **`id`** names one particular element: `<ul id="drinks">`. Only one
  element on a page has a given id.

A whole page:

```html
<!DOCTYPE html>
<html>
  <head><title>Corner Café</title></head>
  <body>
    <h1>Today's menu</h1>
    <ul id="drinks">
      <li class="item" data-price="3.50">Tea</li>
      <li class="item sold-out" data-price="4.20">Hot chocolate</li>
    </ul>
  </body>
</html>
```

`head` holds what isn't shown (the `title`, on the browser's tab), and
`body` what is. `ul` is a list and `li` an item in it. Attributes starting
`data-` are a page's own extra information, here each item's price. The
[html reference](../lib/html.md) has a table of the tags you'll meet most.

The tree of elements is what Cabby reads:

```text
html
├── head
│   └── title: "Corner Café"
└── body
    ├── h1: "Today's menu"
    └── ul  id="drinks"
        ├── li  class="item"           "Tea"
        └── li  class="item sold-out"  "Hot chocolate"
```

#### Reading a page

<!-- run -->
```cabby
use html

int main {
	str Text = "<html><head><title>Corner Café</title></head><body><h1>Today's menu</h1>"
	Text += "<ul id='drinks'><li class='item' data-price='3.50'>Tea</li>"
	Text += "<li class='item sold-out' data-price='4.20'>Hot chocolate</li></ul></body></html>"
	html Menu = html.read Text
	println Menu.find "title"
	for Item in Menu.find_all "li" {
		println Item.text, Item.attribute "data-price"
	}
}
```

```text
Corner Café
Tea 3.50
Hot chocolate 4.20
```

* **`html.read Text`** reads a page. A real one comes from
  `networks.download` ([chapter 41](#chapter-41-downloading)) or
  `files.read`.
* **`find`** gives the first element inside that matches; **`find_all`**
  gives every one, as a list to go through with `for`.
* **`.text`** is the text inside, as a person reads it (what `println`
  shows), and **`.attribute "name"`** an attribute's value. `.tag` is the
  tag's name, `.children` the elements directly inside, and `.parent` the
  element it's inside.

A page is an `html`, and what's found in it are `element`s: an `html` is a
kind of element (the whole page), so everything an element can do, a page
can too.

Pages on the web often have mistakes: a `<p>` never closed, `<DIV>` in
capitals, an attribute without quotes. Browsers forgive them, and so does
`html.read`, the same way; it never panics.

#### Finding things: CSS selectors

What goes in `find` and `find_all` is a **CSS selector**. Web pages use the
same little language to say which parts get which colors, so it's worth
learning: it works everywhere on the web. A selector is built from a few
pieces:

| Selector | Finds |
|---|---|
| `"li"` | elements by tag: every `li` |
| `".item"` | by class: elements whose `class` includes `item` |
| `"#drinks"` | by id: the element whose `id` is `drinks` |
| `"li.item"`, `".item.sold-out"` | written together, all must be true: `li`s with class `item`; elements with both classes |
| `"[data-price]"`, `"a[href='/map']"` | by attribute: having it, or having that value |
| `"ul li"` | a space means *somewhere inside*: `li`s anywhere inside a `ul` |
| `"ul > li"` | `>` means *directly inside*: `li`s whose parent is a `ul` |
| `"h1, h2"` | a comma means *either*: every `h1` and every `h2` |

They combine: `"#drinks > li.item"` is the class-`item` `li`s directly
inside the element with id `drinks`.

<!-- run -->
```cabby
use html

int main {
	str Text = "<ul id='drinks'><li class='item' data-price='3.50'>Tea</li>"
	Text += "<li class='item sold-out' data-price='4.20'>Hot chocolate</li></ul>"
	Text += "<ul id='food'><li class='item' data-price='9.50'>Toastie <em>(vegetarian)</em></li></ul>"
	html Menu = html.read Text
	println Menu.find_all("li").length, Menu.find_all("#drinks li").length
	println Menu.find ".sold-out"
	println Menu.find("li em").parent
	println Menu.find("#food [data-price]").attribute "data-price"
}
```

```text
3 2
Hot chocolate
Toastie (vegetarian)
9.50
```

#### When it isn't there

As with JSON ([chapter 40](#chapter-40-json)), what isn't there is a panic
that says what was looked for, unless you check or give a default:

* **`find`** that finds nothing gives a missing element: using it is a
  panic (`nothing in the page matches "#breakfast"`). **`.is_missing`**
  checks first.
* **`find_all`** that finds nothing gives an empty list, and the `for` loop
  just doesn't run.
* **`.attribute`** that isn't there is a panic; **`.attribute_or "title",
  ""`** gives a default, and **`.has_attribute`** checks.

#### Making a page

<!-- run -->
```cabby
use html

int main {
	element List = html.element "ul"
	list str Names = {"Tea", "Hot chocolate & cream"}
	for Name in Names {
		element Item = html.element "li"
		Item.set_attribute "class", "item"
		Item.add_text Name
		List.add Item
	}
	println List.markup
	println List.pretty
}
```

```text
<ul><li class="item">Tea</li><li class="item">Hot chocolate &amp; cream</li></ul>
<ul>
  <li class="item">Tea</li>
  <li class="item">Hot chocolate &amp; cream</li>
</ul>
```

* **`html.element "ul"`** makes an empty element.
* **`.add_text`** puts text inside it, **`.add`** another element, and
  **`.set_attribute`** sets an attribute. `.remove` takes an element out of
  where it is, and `.clear` empties one.
* **`.markup`** writes it out as HTML; **`.pretty`** puts each element on
  its own line, indented, for people.

Text you add is always written safely: the `&` became `&amp;`, and `<`
becomes `&lt;`. Joining strings (`"<li>" + Name + "</li>"`) goes wrong the
day a name has a `<` or `&` in it; building with elements can't. To make a
page to open in a browser, build an `html` element holding a `head` and a
`body`, and `files.write "page.html", Page.pretty`.

#### In short

* `use html`; `html Page = html.read Text`.
* `Page.find "selector"`, `Page.find_all "selector"`: `tag`, `.class`,
  `#id`, `[attribute]`, a space (inside), `>` (directly inside), `,`.
* `.text`, `.attribute "name"` (or `_or`, `has_attribute`), `.tag`,
  `.children`, `.parent`, `.is_missing`.
* `html.element "tag"`, `.add_text`, `.add`, `.set_attribute`, `.markup`,
  `.pretty`.

#### Try this

* In `43-html.cby`, print the cheapest item on the menu, and how many are
  sold out.
* Download a page you like, and print every link on it with its text.
* Make a page listing your music students (or anything you keep a list
  of), and open it in a browser.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**More of an element.** `raw_text` is the text exactly as it is; an
attribute can have a default, be asked about, and be taken out:

<!-- run -->
```cabby
use html

int main {
	html Page = html.read "<p class='note' title='A tip'>Mind   the <strong>gap</strong>.</p>"
	element Note = Page.find "p"
	println Note.text
	println "[" + Note.raw_text + "]"
	println Note.attribute_or("title", "none"), Note.attribute_or("lang", "none")
	println Note.has_attribute("class"), Note.has_attribute("id")
	Note.remove_attribute "class"
	println Note.markup
}
```

```text
Mind the gap.
[Mind   the gap.]
A tip none
true false
<p title="A tip">Mind   the <strong>gap</strong>.</p>
```

**More selectors.** An id, two classes at once, the parts combined, the
outermost element, and form parts:

<!-- run -->
```cabby
use html

int main {
	str Text = "<html><body><ul id='prices'><li class='price sale'>3.50</li><li class='price'>4.20</li></ul>"
	Text += "<form><input type='checkbox' name='milk' checked><input name='cup' disabled></form></body></html>"
	html Page = html.read Text
	println Page.find("#prices").tag, Page.find(".price.sale")
	println Page.find_all("#prices li.price").length, Page.find(":root").tag
	println Page.find(":checked").attribute("name"), Page.find(":disabled").attribute("name")
	println Page.find_all("input:enabled").length
}
```

```text
ul 3.50
2 html
milk cup
1
```

**Tags for making pages.** `h1` to `h6` are headings, `ul`, `ol` and `li`
lists, `table`, `tr`, `th` and `td` tables, `div` and `span` boxes and
stretches of text, `strong` and `em` bold and italic, and `img` a picture:

<!-- run -->
```cabby
use html

element cell const str &Tag, const str &Text {
	element E = html.element Tag
	E.add_text Text
	return E
}

int main {
	element Box = html.element "div"
	Box.add cell("h2", "Prices")
	element Table = html.element "table"
	element Row = html.element "tr"
	Row.add cell("th", "Tea")
	Row.add cell("td", "3.50")
	Table.add Row
	Box.add Table
	element Picture = html.element "img"
	Picture.set_attribute "src", "tea.png"
	Picture.set_attribute "alt", "a cup of tea"
	Box.add Picture
	println Box.pretty
}
```

```text
<div>
  <h2>Prices</h2>
  <table>
    <tr>
      <th>Tea</th>
      <td>3.50</td>
    </tr>
  </table>
  <img src="tea.png" alt="a cup of tea">
</div>
```

[← Chapter 42](#chapter-42-web-addresses) · [Contents](#contents) · [Chapter 44 →](#chapter-44-xml)

---

### Chapter 44: XML

*Program: `44-xml.cby`. Library: `use xml`.*

#### XML: HTML's strict cousin

XML is written the same way as HTML, with elements, attributes and text,
but **you choose the tag names**, to describe your own data:

```xml
<library owner="Sam">
  <book lang="en">
    <title>The Hobbit</title>
    <year>1937</year>
  </book>
</library>
```

It's how news feeds (RSS), SVG pictures, the insides of office documents,
and many programs' settings are written. Where HTML is forgiving, XML is
strict: every element is closed (`<empty/>` for one with nothing inside),
attribute values are in quotes, capitals matter, and there's exactly one
top element holding everything else.

#### Reading it

<!-- run -->
```cabby
use xml

int main {
	str Text = "<library owner='Sam'><book lang='en'><title>The Hobbit</title><year>1937</year></book>"
	Text += "<book lang='de'><title>Momo</title><year>1973</year></book></library>"
	xml Library = xml.read Text
	println Library.tag, "of", Library.attribute "owner"
	for Book in Library.find_all "book" {
		println Book.find("title"), Book.find("year").text.to_int, Book.attribute "lang"
	}
	println Library.find "book[lang=de] > title"
}
```

```text
library of Sam
The Hobbit 1937 en
Momo 1973 de
Momo
```

`xml.read` gives the document's top element, and everything from
[chapter 43](#chapter-43-html) works the same: `find` and `find_all` with
CSS selectors, `.text`, `.attribute`, `.children`, `.is_missing`. Names
with a prefix, such as `dc:creator` in news feeds, are found as they're
written.

XML with a mistake is a panic that says exactly where:

```text
panic at prog.cby:4:12: this isn't XML: line 3, column 12: expected </item> (opened at line 3), found </itm>
```

**`xml.can_read Text`** checks first, for XML from somewhere you don't
trust.

#### Writing it

<!-- run -->
```cabby
use xml

int main {
	element Note = xml.element "note"
	Note.set_attribute "id", "7"
	element To = xml.element "to"
	To.add_text "Ada & Alan"
	Note.add To
	Note.add xml.element "read"
	println Note.pretty
}
```

```text
<note id="7">
  <to>Ada &amp; Alan</to>
  <read/>
</note>
```

`xml.element` makes an element, and the rest is as in HTML. An element
with nothing inside is written `<read/>`.

#### In short

* `use xml`; `xml Doc = xml.read Text` (a panic that says where, if it
  isn't XML); `xml.can_read Text`.
* Finding, reading and building are as in HTML: `find`, `find_all`,
  `.text`, `.attribute`, `xml.element`, `.add`, `.pretty`.

#### Try this

* In `44-xml.cby`, count the stories in each category.
* Download a news feed from a site you read (its "RSS" link), and print its
  headlines.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Changing a document.** XML's elements change as HTML's do: attributes
taken out, elements removed or emptied, and copies made:

<!-- run -->
```cabby
use xml

int main {
	xml Library = xml.read "<library><book lang='en' id='1'><title>The Hobbit</title></book><book id='2'><title>Momo</title></book></library>"
	element First = Library.find "book"
	First.remove_attribute "id"
	element Spare = First.copy
	Library.find("book[id=2]").remove
	First.find("title").clear
	First.find("title").add_text "There and Back Again"
	println Library.markup
	println Spare.markup
}
```

```text
<library><book lang="en"><title>There and Back Again</title></book></library>
<book lang="en"><title>The Hobbit</title></book>
```

[← Chapter 43](#chapter-43-html) · [Contents](#contents) · [Chapter 45 →](#chapter-45-bytes)

---

### Chapter 45: Bytes

*Program: `45-picture-size.cby`. Library: `use bytes` (`use files` brings
it too).*

#### Text, and files that aren't text

Everything a computer keeps is **bytes**: numbers from 0 to 255. In a text
file, each byte is a letter: `A` is 65, `a` is 97, a space is 32 and a
line end is 10. Every file so far has been text, so `files.read` and
`read_line` were all that was needed.

Most files aren't text, though. A picture, a sound, a zip file or a game's
save file is bytes that aren't letters, and reading one as lines gives
nonsense. For these, Cabby has **`bytes`**: a run of bytes, each one a
number.

<!-- run -->
```cabby
use bytes

int main {
	bytes Data = "Hi!"           // a str's bytes: its letters' numbers
	Data.add 0
	Data.add 200
	println Data.length, "bytes:", Data
	println Data[0], Data[4]
	for Byte in Data {
		print Byte.text + " "
	}
	println ""
}
```

```text
5 bytes: 48 69 21 00 c8
72 200
72 105 33 0 200 
```

`Data[i]` is one byte, as an ordinary `int`; `.add` puts one more on the
end, and `.add_bytes` puts several (`Data.add_bytes "more text"`, or
another `bytes`); `for` goes through them. A byte outside 0 to 255 is a panic. But
`println Data` shows `48 69 21 00 c8`, not `72 105 33 0 200`: bytes are
printed in hexadecimal.

#### Hexadecimal

**Hexadecimal** ("hex") writes numbers with 16 digits instead of 10: `0`
to `9`, then `a` for 10, `b` 11, `c` 12, `d` 13, `e` 14 and `f` 15. In
ordinary numbers each place is worth ten times the one to its right; in
hex, sixteen times. So in a two-digit hex number the first digit counts
16s and the second counts 1s:

* `48` is 4 × 16 + 8 = 72 (the letter `H`)
* `c8` is 12 × 16 + 8 = 200
* `ff` is 15 × 16 + 15 = 255, the biggest byte
* `0a` is 10, a line end

Two hex digits are exactly one byte, from `00` to `ff`, so bytes in hex
always line up, and every file format's description, every hex viewer,
and Cabby use it. `bytes.from_hex "48 69 21"` goes the other way, making
bytes from hex as they print.

For a longer look, `.dump` gives a **hex dump**, 16 bytes a line:

<!-- run -->
```cabby
use bytes

int main {
	bytes Data = "Cabby's bytes: hex, at last!"
	Data.add 0
	Data.add 255
	println Data.dump
}
```

```text
00000000  43 61 62 62 79 27 73 20  62 79 74 65 73 3a 20 68  |Cabby's bytes: h|
00000010  65 78 2c 20 61 74 20 6c  61 73 74 21 00 ff        |ex, at last!..|
```

On the left is where each line starts, in hex (`10` is byte 16). On the
right are the same bytes as letters, with a `.` for each byte that isn't
one: in a picture or a save file, that's how to spot the words a format
puts among its numbers.

#### Numbers made of several bytes

A byte holds only up to 255, so a file keeps bigger numbers in several
bytes in a row, the same way C and Cabby keep them in memory (chapter 3):
a `short` in 2 bytes, an `int` in 4, a `long` or a `double` in 8.

How does 640 fit in bytes? 640 is 2 × 256 + 128. That's like a two-digit
number in base 256: a byte holding **2** (counting 256s) and a byte
holding **128** (counting 1s), just as 47 is a 4 counting tens and a 7
counting ones. As an `int`, with two bytes of 0 in front: `00 00 02 80`
(128 is `80` in hex).

#### Byte order

Here's the catch. Those four bytes can be stored in either order, and
both are used:

* **Smallest byte first**, called **little-endian**: `80 02 00 00`. The
  byte counting ones comes first. Every ordinary PC and phone keeps numbers
  this way in memory, so most file formats do too: BMP pictures, WAV
  sounds, zip files.
* **Biggest byte first**, called **big-endian**: `00 00 02 80`. The way we
  write numbers, biggest place first. It's what networks send, and what
  some formats use: PNG and JPEG pictures, MIDI music.

(The names come from *Gulliver's Travels*, whose Lilliputians go to war
over which end of a boiled egg to break first: the big end or the little
end. Programmers have been arguing about bytes in the same spirit since.)

Neither order is better. What matters is reading a number in the order it
was written, and every format's description says which it uses. In Cabby,
the plain methods use smallest byte first, as the computer does; the same
names ending in `_big` use biggest byte first:

<!-- run -->
```cabby
use bytes

int main {
	bytes Little
	Little.add_int 640           // 4 bytes, smallest first
	bytes Big
	Big.add_int_big 640          // 4 bytes, biggest first
	println Little, "  and  ", Big
	println Little.int_at(0), Big.int_at_big(0)
	println Little.int_at_big 0  // read the wrong way round
}
```

```text
80 02 00 00   and   00 00 02 80
640 640
-2147352576
```

The last line is the mistake to look out for: reading with the wrong
order doesn't fail, it just gives a different number. When a number from
a file comes out huge, or below 0, check the byte order first.

| Write | Reads or writes |
|---|---|
| `B.short_at At`, `int_at`, `long_at`, `double_at` | the number whose bytes start at byte `At` |
| `B.set_int_at At, N` (and `short`, `long`, `double`) | writes over those bytes |
| `B.add_int N` (and `short`, `long`, `double`) | adds the number's bytes on the end |
| any of these with `_big` on the end | the same, biggest byte first |
| `B.unsigned_short_at At`, `unsigned_int_at` | the bytes counted from 0 up |

The last row is for formats that say a number is **unsigned**. The bytes
`ff ff` are -1 as a `short` (which holds -32768 to 32767), but 65535 when
counted from 0 up (as an `unsigned short`, chapter 3). The format's
description says which it means.

#### Reading a picture's size

Now everything together. A PNG picture's first 8 bytes are always
`89 50 4e 47 0d 0a 1a 0a` (the `50 4e 47` is `PNG` in letters), so a
program can tell a PNG by them. Then come its width, in bytes 16 to 19,
and its height, in 20 to 23, both biggest byte first. Here is a whole PNG
picture, 3 dots by 1, written in hex:

<!-- run -->
```cabby
use bytes

int main {
	str Hex = "89 50 4e 47 0d 0a 1a 0a 00 00 00 0d 49 48 44 52 00 00 00 03 00 00 00 01 08 02 00 00 00 94 82 83 e3 "
	Hex += "00 00 00 12 49 44 41 54 78 9c 63 f8 cf c0 c0 c0 f0 ff ff ff ff 00 12 f7 04 fc 57 7c 0d db "
	Hex += "00 00 00 00 49 45 4e 44 ae 42 60 82"
	bytes Picture = bytes.from_hex Hex
	bytes Png = bytes.from_hex "89 50 4e 47 0d 0a 1a 0a"
	if Picture.starts_with Png {
		println "a PNG,", Picture.int_at_big(16), "x", Picture.int_at_big(20)
	}
	bytes Size = Picture.part 16, 8
	println Size
}
```

```text
a PNG, 3 x 1
00 00 00 03 00 00 00 01
```

`Picture.part 16, 8` gives 8 bytes from byte 16, as bytes of their own: the
width `00 00 00 03` and the height `00 00 00 01`. `.starts_with` compares
the first bytes; a `str` works there too (`Picture.starts_with "BM"`),
since text becomes bytes wherever bytes are wanted.

With a real file, it's `bytes Picture = files.read_bytes "photo.png"`, and
`files.write_bytes "copy.png", Picture` writes bytes into a file. The
tutorial program reads PNG, BMP and GIF pictures (each keeps its size in
a different place and order), and makes a BMP picture of its own, a little
flag, byte by byte: open `flag.bmp` in any picture viewer.

#### In short

* `bytes` holds numbers from 0 to 255; it prints in hex, two digits a
  byte; `.dump` shows a hex dump.
* `B[i]`, `.add`, `.add_bytes`, `.part`, `.length`, `.starts_with`,
  `.find`, `.str`; `bytes B = "text"`, `bytes.from_hex "..."`.
* Numbers inside: `int_at`, `set_int_at`, `add_int` (and `short`, `long`,
  `double`), smallest byte first; with `_big`, biggest byte first. A
  format's description says which.
* `files.read_bytes Path` and `files.write_bytes Path, B` for whole files.
* The reference page is `bytes.md`.

#### Try this

* Run `45-picture-size.cby` with the pictures in your own Pictures folder
  after it (`cabby 45-picture-size.cby ~/Pictures/*.png`).
* Change the flag's colors and size (keep `WIDTH` a multiple of 3), and
  look at it in a picture viewer.
* A WAV sound file keeps its number of channels at byte 22 (a short), and
  its samples a second at byte 24 (an int), smallest byte first. Print
  them for a `.wav` file you have.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Every size of number.** `short` (2 bytes), `int` (4), `long` and
`double` (8) are added, read and written over the same way; the
`unsigned_` ones read the same bytes counted from 0 up:

<!-- run -->
```cabby
use bytes

int main {
	bytes Data
	Data.add_short -2
	Data.add_int 70000
	Data.add_long 5000000000
	Data.add_double 2.5
	println Data.length, "bytes"
	println Data.short_at(0), Data.unsigned_short_at(0)
	println Data.int_at(2), Data.long_at(6), Data.double_at(14)
	Data.set_short_at 0, 7
	Data.set_int_at 2, -1
	Data.set_long_at 6, 42
	Data.set_double_at 14, 0.5
	println Data.short_at(0), Data.int_at(2), Data.unsigned_int_at(2), Data.long_at(6), Data.double_at(14)
}
```

```text
22 bytes
-2 65534
70000 5000000000 2.5
7 -1 4294967295 42 0.5
```

**Looking further on, and files.** `find_from` looks from a byte onwards;
`files.append_bytes` adds bytes to the end of a file, as `files.append`
adds text (`networks.download_bytes` is in [Chapter 41](#chapter-41-downloading)):

<!-- run -->
```cabby
use bytes
use files

int main {
	bytes Data = "abcabc"
	println Data.find("c"), Data.find_from("c", 3)
	files.write_bytes "cabby-data.bin", Data
	files.append_bytes "cabby-data.bin", "!!"
	println files.read("cabby-data.bin")
	files.delete "cabby-data.bin"
}
```

```text
2 5
abcabc!!
```

[← Chapter 44](#chapter-44-xml) · [Contents](#contents) · [Chapter 46 →](#chapter-46-changing-files-in-place)

---

### Chapter 46: Changing files in place

*Program: `46-save-game.cby`. Library: `use files`.*

#### Going straight to the part you want

`files.read_bytes` and `files.write_bytes` read and write a whole file.
That's simplest, and right for most files. But a big file, a database or
a game's save file with hundreds of players, would have to be read in
full and written back in full to change one number in it.

Instead, a format can keep its parts at fixed places. If every player's
slot in a save file is 32 bytes long, then player 5's slot starts at byte
5 × 32 (after any header), and a program can go straight there, read or
change just those bytes, and leave the rest of the file alone.

#### Opening a file to change it

`files.open_to_change Path` opens a file that's already there to both
read and write. Nothing in it is lost: bytes only change where they're
written over. An open file has a **position**, where the next read or
write happens, counted in bytes from the start:

| Write | What it does |
|---|---|
| `F.read_bytes Count` | the next `Count` bytes; fewer at the end, none after it |
| `F.write_bytes B` | writes them at the position, over what's there |
| `F.position` | where the position is |
| `F.move_to Position` | moves it: 0 is the start, `F.size` the end |
| `F.move_to_end` | moves it to the end, to add to the file |
| `F.size` | how many bytes the file has |

<!-- run -->
```cabby
use files

int main {
	bytes Scores
	Scores.add_int 120
	Scores.add_int 340
	Scores.add_int 75
	files.write_bytes "scores.dat", Scores         // three ints: 12 bytes

	file F = files.open_to_change "scores.dat"
	F.move_to 4                                    // the second number
	int Second = F.read_bytes(4).int_at 0
	println "second:", Second, "and now the position is", F.position
	bytes New
	New.add_int Second + 1000
	F.move_to 4                                    // back, to write over it
	F.write_bytes New
	F.move_to_end
	F.write_bytes New                              // and once more, on the end
	println F.size, "bytes"

	bytes All = files.read_bytes "scores.dat"
	for i in All.length / 4 {
		println All.int_at i * 4
	}
	files.delete "scores.dat"
}
```

```text
second: 340 and now the position is 8
16 bytes
120
1340
75
1340
```

Reading and writing both move the position on past the bytes they read or
wrote. That's handy for reading one thing after another, but it means
that to change something just read, the program first has to move back.

`read_line`, `at_end` and `F println` work in the same file, at the same
position, for formats that mix lines and bytes. `read_bytes` and
`write_bytes` also work in files opened with `files.open` (reading),
`files.open_to_write` and `files.open_to_add` (writing; the last always at
the end).

#### A format of your own

The tutorial program saves a game: a small header, then a slot of 32
bytes for each player. Writing a format down is the first step, the way
the people behind PNG and BMP did:

```text
bytes 0-3   "CSAV": what kind of file this is
bytes 4-5   the format's version (a short)
bytes 6-7   how many slots (a short)
then 32 bytes for each slot:
  0-15    the name, then 0 bytes to fill the 16
  16-17   level (a short)       18-19  lives (a short)
  20-23   score (an int)        24-31  how far along the map (a double)
every number smallest byte first
```

The first 4 bytes let a program check it was given the right kind of
file. The version lets a later version of the game, with more in each
slot, still read old files (and tell them apart). Every slot being the
same size is what lets the program go straight to one, so the program
changes one player's score by moving to `8 + slot × 32 + 20` and writing
4 bytes, and adds a player by writing a slot at the end and adding 1 to
the count in the header.

When should you use a format of your own like this, and when JSON, the
text format of [Chapter 40](#chapter-40-json)? JSON is easier: anyone can read it and fix it by hand, a
missing field can have a default, and it doesn't care what order things
are in. Binary formats are for when the file has to be small or fast,
when it's huge and changed a little at a time, or when it's a format that
already exists (a picture, a sound) and has to be read or written exactly.

#### In short

* `files.open_to_change Path`: read and write anywhere in a file.
* `F.move_to`, `F.move_to_end`, `F.position` and `F.size` move around in
  it; `F.read_bytes N` and `F.write_bytes B` read and write at the
  position, and move it on.
* Parts of a fixed size let a program go straight to the part it wants.

#### Try this

* In `46-save-game.cby`, give everyone an extra life: for each slot, move
  to its lives, read the short, and write it back one bigger.
* Write a function `remove_player`, which moves the last slot into the
  removed one's place, then makes the count one smaller. (The file stays
  the same length, with the old last slot left on the end; write the file
  again with `files.write_bytes` to make it shorter.)

[← Chapter 45](#chapter-45-bytes) · [Contents](#contents) · [Chapter 47 →](#chapter-47-patterns)

---

### Chapter 47: Patterns

*Program: `47-patterns.cby`, a music teacher's notes. Library: `use
patterns`.*

A **pattern** describes what some text looks like. `\d{1,2}:\d{2}` is
"one or two digits, a colon, two digits": a time. These are *regular
expressions* ("regex"), as text editors and other languages have them.
`patterns.make` makes one, and checks it: one that can't be used is a
panic that says why.

<!-- run -->
```cabby
use patterns

int main {
	patterns.pattern Time = patterns.make "\\d{1,2}:\\d{2}"
	str Notes = "Ada at 16:30, Grace at 17:00, Alan at 9:15."
	println Time.find_all(Notes)
	println Time.matches("9:15"), Time.matches("noon")
}
```

```text
[16:30, 17:00, 9:15]
true false
```

In a Cabby string, `\\` is one `\`, so the pattern `\d` is written
`"\\d"`.

#### What goes in a pattern

| Write | Matches |
|---|---|
| `abc` | those letters |
| `.` | any one letter |
| `\d`, `\w`, `\s` | a digit; a letter, digit or `_`; a space |
| `[aeiou]`, `[a-z]`, `[^0-9]` | one of those; from a to z; anything but a digit |
| `x*`, `x+`, `x?` | none or more; one or more; none or one |
| `x{2}`, `x{1,2}` | two; one or two |
| `cat\|dog` | either |
| `( )` | a part, kept for `parts` and `\1` |
| `^`, `$` | the start and the end of the text |
| `\.` | a dot itself (any special character, with a `\` before it) |

`lib/patterns.md` has the rest.

#### What a pattern does

* **Finding.** `find` gives the first piece of the text like it, and
  `find_all` every piece. The tutorial finds the email addresses and the
  times in a week's notes.
* **Checking.** `matches` asks whether the *whole* text is like it, as a
  form checks what's typed; `found_in` only whether it's somewhere in it.
  The tutorial's clock pattern, `([01]?\d|2[0-3]):[0-5]\d`, takes 9:15
  and 16:30, but not 24:00 or 7:5.
* **Parts.** The parts of a pattern in `( )` are kept: `parts` gives what
  each matched, the first time it's found. A date's year, month and day.
* **Replacing.** `replace_all` puts something else in each piece's place,
  and in what it puts, `\1`, `\2`, `\3` are the parts:
  `Date.replace_all Notes, "\\3/\\2/\\1"` turns 2026-11-10 into 10/11/2026.
* **Cutting up.** `split` cuts the text wherever the pattern is: `[,;\s]+`
  cuts at commas, semicolons and spaces, however many there are together.

`patterns.make_ignoring_case "guitar"` finds GUITAR and Guitar too.

#### Letters in any alphabet

Text is matched a letter at a time, not a byte at a time, so `.` is one
letter whether it's `e`, `é` or `φ`, and `\w` is a letter in Greek too:
`\w+` finds the three words of "Χαῖρε, ὦ φίλε!".

#### Taking as little as it can, and the edges of words

A repeat takes as much as it can, so `<.+>` in `"<b>bold</b>"` finds all
of it. With a `?` after it, a repeat takes as little as it can instead:

<!-- run -->
```cabby
use patterns

int main {
	println patterns.make("<.+?>").find_all("<b>bold</b>")
	println patterns.make("\\bcat\\b").find_all("cat concat cat's bobcat")
	println patterns.make("\\b(\\w+) \\1\\b").find("it was the the best")
}
```

```text
[<b>, </b>]
[cat, cat]
the the
```

`\b` is the edge of a word, so `\bcat\b` finds "cat" on its own, but not
inside "concat" or "bobcat". `\1` is what the first part matched, again:
a word written twice. patterns.md has the rest, looking ahead with `(?=`
among it.

#### In short

* `patterns.make` makes a pattern, and checks it; `matches` for all of a
  text, `find` and `find_all` for parts of it, `parts` for what its
  `( )` matched.
* `replace_all` and `split`; `(?i)` ignores capitals; `[[:alpha:]]` and
  the rest work in any alphabet.

#### Try this

* Find the students' names in `47-patterns.cby`: a capital letter, then
  small ones, before a comma (`"[[:upper:]][[:lower:]]+,"`).
* Change every time to the 12-hour clock, with `replace_all` and two
  patterns.
* Read a file with `files.read`, and count the words in it.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Sets of letters, and asking a pattern.** `[aeiou]` is one of those
letters, `[[:alpha:]]` a letter of any alphabet; `found_in` asks whether
the pattern's anywhere in a text, and `has_part` and `part_names` what
named parts it has:

<!-- run -->
```cabby
use patterns

int main {
	println patterns.make("[aeiou]").find_all("Cabby")
	println patterns.make("[[:alpha:]]+").find_all("Ελλάδα, 2026, Köln")
	println patterns.make("[[:upper:]]").find_all("Ada and Grace")
	patterns.pattern Date = patterns.make "(?<year>\\d{4})-(?<month>\\d\\d)"
	println Date.found_in("Due 2026-11."), Date.matches("Due 2026-11.")
	println Date.has_part("year"), Date.has_part("day"), Date.part_names
}
```

```text
[a]
[Ελλάδα, Köln]
[A, G]
true false
true false [year, month]
```

[← Chapter 46](#chapter-46-changing-files-in-place) · [Contents](#contents) · [Chapter 48 →](#chapter-48-hashes)

---

### Chapter 48: Hashes

*Program: `48-hashes.cby`. Libraries: `use hashes`, with `use files` and
`use directories`.*

A **hash** is a fingerprint. The same text, or file, always gives the
same one, and any change, even one letter, gives a quite different one:

<!-- run -->
```cabby
use hashes

int main {
	println hashes.sha256("Salve!")
	println hashes.sha256("Salve?")
}
```

```text
61ee6cad977244872566817a3499af9bd18548798437dd11ab239863bcaa705f
f5f55d612bea42dac1bc5bee1b903eff58f729564e39836eae07cf8187c57969
```

So comparing two hashes says whether two things are the same, without
comparing them byte by byte.

#### Checking a download

Websites that offer big files (a Linux ISO, say) show each file's SHA-256
beside it. `hashes.sha256_of_file` works out the file's own; if the two
are the same, the file arrived whole and unchanged. A file can be any
size: it's read a piece at a time.

#### What changed

`fingerprints` keeps a lookup of a folder's files and their hashes. Later,
`compare` works out the hashes again, and a file whose hash is different
has changed; one that's only in the new lookup is new, and one only in
the old is gone. Backup programs and git notice changes this way.

#### Which hash

SHA-256 (`hashes.sha256`, 64 hexadecimal digits) is the one to use. SHA-1
and MD5 are older, and someone who wanted to could make two different
files with the same one: they're there for websites that only show
those. None of them is for keeping passwords.

#### In short

* `hashes.sha256 Text` and `sha256_of_file Path`: a fingerprint that
  changes completely when anything in it does.
* Checking a download against the sum its site shows; finding which files
  changed since last time.
* `md5` and `sha1` for older tools; `secrets` for passwords.

#### Try this

* Find the SHA-256 of a real download (a Linux ISO's site shows it), and
  check it with `hashes.sha256_of_file`.
* Save Monday's fingerprints in a file, so a program run on Tuesday can
  compare against them.
* Find the files in a folder that are the same as each other.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**SHA-1 and MD5.** Older hashes, still asked for by older sites and
tools. They're fine for spotting a changed file, but not for anything
someone might try to fake:

<!-- run -->
```cabby
use hashes
use files

int main {
	println hashes.sha1("Salve!")
	println hashes.md5("Salve!")
	files.write "cabby-hash.txt", "Salve!"
	println hashes.sha1_of_file("cabby-hash.txt") == hashes.sha1("Salve!")
	println hashes.md5_of_file("cabby-hash.txt") == hashes.md5("Salve!")
	files.delete "cabby-hash.txt"
}
```

```text
d8b3a37261ba013ab0db4a5d5738a541d093a2eb
ff940ba204d233c254bab9de1e0f0618
true
true
```

[← Chapter 47](#chapter-47-patterns) · [Contents](#contents) · [Chapter 49 →](#chapter-49-archives)

---

### Chapter 49: Archives

*Program: `49-archives.cby`, a term's lesson notes. Library: `use
archives`.*

An **archive** is many files and folders in one file, squeezed smaller:
to send, to back up, or to unpack something downloaded.

```cabby
archives.make "lessons.zip", {"lessons", "notes.txt"}    // a folder, and a file
println archives.list("lessons.zip")                     // what's in it
str Notes = archives.read "lessons.zip", "notes.txt"     // one file, without the rest
archives.unpack "lessons.zip", "unpacked"                // everything, into a folder
```

The kind of archive made is by its name: `.zip`, which every computer
opens, or `.tar.gz`, the files one after another and squeezed together,
as Linux backups are; `.tar` is the same, not squeezed. Reading, it's
known by what's in it, whatever it's called.

#### Names inside

A folder given to `make` goes in with everything in it, each named as it
would be from here: `lessons/`, `lessons/ada/`, `lessons/ada/scales.txt`.
The tutorial lists them, reads `lessons/grace.txt` straight from inside,
and unpacks the whole zip into another folder; `hashes.sha256_of_file`
(chapter 48) shows a file unpacked is the same as the one that went in.

**Unpacking is safe.** An archive can be made to do harm: a name inside
like `../../.bashrc` would land outside the folder it's unpacked into. A
name like that is a panic, and nothing of it is written.

#### One file

`archives.compress "practice.log"` squeezes one file, as
`practice.log.gz` beside it, and `archives.decompress` puts it back. A
log of the same few lines, over and over, squeezes to a tenth of its
size, or less; a photo or a song, already squeezed, hardly at all.

Squeezing is zlib's, which every Linux system and Mac has; the zip and
tar formats around it are Cabby's own, and what they make opens with
`unzip`, `tar`, a file manager, or any other program.

#### In short

* `archives.make Archive, Paths` makes a zip or a tar file (by its name);
  `list`, `read` and `unpack` take one apart, never outside its folder.
* `compress` and `decompress` squeeze one file (`.gz`, `.xz`).

#### Try this

* Zip a folder of your own, and open the zip with your file manager.
* Unpack a `.tar.gz` you've downloaded, listing what's inside first.
* Make a backup with the date in its name (Chapter 39 has the date).

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**xz, and other names.** `compress_xz` squeezes a file smaller than gzip,
more slowly (as `.tar.xz` does a folder); `compress_to` and
`decompress_to` choose the name:

<!-- run -->
```cabby
use archives
use files

int main {
	str Text
	for I in 1 to 20000 {
		Text += (I * I).text + "\n"
	}
	files.write "cabby-big.txt", Text
	archives.compress "cabby-big.txt"
	archives.compress_xz "cabby-big.txt"
	long Plain = files.size "cabby-big.txt"
	long Gzipped = files.size "cabby-big.txt.gz"
	long Xzed = files.size "cabby-big.txt.xz"
	println "gzip:", Gzipped * 100 / Plain, "% of it; xz:", Xzed * 100 / Plain, "%"
	archives.compress_to "cabby-big.txt", "cabby-small.xz"
	archives.decompress_to "cabby-small.xz", "cabby-back.txt"
	println files.read("cabby-back.txt") == files.read("cabby-big.txt")
	files.delete "cabby-big.txt"
	files.delete "cabby-big.txt.gz"
	files.delete "cabby-big.txt.xz"
	files.delete "cabby-small.xz"
	files.delete "cabby-back.txt"
}
```

```text
gzip: 42 % of it; xz: 21 %
true
```

**A zip with a password.** Its files are encrypted; without the password
they can't be read:

<!-- run -->
```cabby
use archives
use files
use directories

int main {
	files.write "cabby-secret.txt", "the cake is in the cupboard"
	list str Paths = {"cabby-secret.txt"}
	archives.make_with_password "cabby-secret.zip", Paths, "tea4two"
	println archives.needs_password("cabby-secret.zip")
	println archives.read_with_password("cabby-secret.zip", "cabby-secret.txt", "tea4two")
	archives.unpack_with_password "cabby-secret.zip", "cabby-opened", "tea4two"
	println files.read("cabby-opened/cabby-secret.txt")
	files.delete "cabby-opened/cabby-secret.txt"
	directories.remove "cabby-opened"
	files.delete "cabby-secret.zip"
	files.delete "cabby-secret.txt"
}
```

```text
true
the cake is in the cupboard
the cake is in the cupboard
```

[← Chapter 48](#chapter-48-hashes) · [Contents](#contents) · [Chapter 50 →](#chapter-50-databases)

---

### Chapter 50: Databases

*Program: `50-databases.cby`, a recipe book. Library: `use databases`.*

A **database** keeps records in **tables**: rows of the same columns, as
a spreadsheet's. Then it answers questions about them, however many rows
there are: what does each recipe cost, what do I need to buy, which
foods cost more than this. `use databases` is SQLite, where each
database is one file, with no server to run.

<!-- run -->
```cabby
use databases
use files

int main {
	databases.database Shop = databases.open "shop.db"
	Shop.run "CREATE TABLE IF NOT EXISTS items (name TEXT, price REAL)"
	Shop.run_with "INSERT INTO items VALUES (?, ?)", {"bread", "4.50"}
	Shop.run_with "INSERT INTO items VALUES (?, ?)", {"milk", "2.10"}
	for Row in Shop.rows "SELECT name, price FROM items ORDER BY price" {
		println Row["name"], Row["price"]
	}
	files.delete "shop.db"
}
```

```text
milk 2.1
bread 4.5
```

What's asked of a database is **SQL**, the language every database
speaks: `CREATE TABLE` to make a table, `INSERT` to put a row in,
`SELECT` to ask, `UPDATE` and `DELETE` to change. Its words are in
capitals in the tutorial only to stand out from the names.

#### Values, through ?s

`run_with` and `rows_with` take the values separately, one for each `?`
in the SQL. Never paste a value into SQL with `+`: "Grandma's scones",
with its `'`, would break the statement, and text someone typed could
change what it does. With `?`s, a value is only ever a value.

Rows come back as lookups, by column: `Row["price"]`. Every value is text,
as SQLite writes it; `.to_double` makes a number of one. `value` is the
first column of the first row, for a question with one answer: `SELECT
count(*) FROM foods`.

#### The recipe book

Three tables: `foods`, with a price for each; `recipes`; and `needs`, a
row for each food a recipe needs, and how much. `last_id` is the number
the last row put in was given, so the `needs` rows can say which recipe.

What each recipe costs is a **join**, `needs` matched up with `foods` and
`recipes`, and a `sum` of amount times price, for each recipe (`GROUP
BY`). The shopping list is the same join, for one recipe. When butter
goes up, an `UPDATE` changes one row (`changes` is 1), and asking again
gives the new cost.

#### Together, or not at all

Between `begin` and `commit`, changes are kept only when `commit` comes:
if the program stops part way, none of them are, and `rollback` forgets
them on purpose. The tutorial puts all its foods and recipes in between
one `begin` and `commit` (many inserts together are much faster, too),
and deletes every food by mistake, then rolls it back.

SQL that can't be done, a table that isn't there say, is a panic that
says what SQLite said. The `sqlite3` command opens the same files, to
look inside.

#### In short

* `databases.open Path`; `run` and `run_with` (with `?` for each value);
  `rows`, `value`; `begin` and `commit` for changes that go together.
* SQL does the asking: tables, joins and sums.

#### Try this

* Keep the database: take out `50-databases.cby`'s `files.delete` lines,
  run it twice, and see what `CREATE TABLE IF NOT EXISTS` and `UNIQUE` do
  the second time.
* Ask for every recipe that needs milk.
* Open `recipes.db` with the `sqlite3` command, and look round: `.tables`,
  then `SELECT * FROM foods;`.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Rows as JSON, whole numbers, nothing, and bytes.** `records` gives the
rows as JSON, with numbers as numbers and `NULL` as `null`; a statement
made once takes its values one at a time, `add_whole` for a whole number
kept exactly and `add_nothing` for `NULL`; `bytes_value` gives a column
kept as bytes:

<!-- run -->
```cabby
use databases
use bytes
use json
use files

int main {
	databases.database Album = databases.open "cabby-album.db"
	Album.run "CREATE TABLE photos (name TEXT, year INTEGER, note TEXT, picture BLOB)"
	databases.statement Add = Album.prepare "INSERT INTO photos VALUES (?, ?, ?, ?)"
	Add.add_text "cat"
	Add.add_whole 2026
	Add.add_nothing
	Add.add_bytes bytes.from_hex("89 50 4e 47")
	Add.run
	json Rows = Album.records_with "SELECT name, year, note FROM photos WHERE year > ?", {"2000"}
	println Rows
	println Rows[0]["note"].is_null
	bytes Picture = Album.bytes_value_with "SELECT picture FROM photos WHERE name = ?", {"cat"}
	println Picture.length, Picture.starts_with(bytes.from_hex("89 50"))
	println Album.bytes_value("SELECT picture FROM photos").length
	files.delete "cabby-album.db"
}
```

```text
[{"name":"cat","year":2026,"note":null}]
true
4 true
4
```

[← Chapter 49](#chapter-49-archives) · [Contents](#contents) · [Chapter 51 →](#chapter-51-your-first-window)

---

## Part IX: Windows

`use gui` gives Cabby programs windows and everything in them: buttons, text
boxes, check boxes, lists, sliders, pictures, timers, dialogs, menus,
tabs, tables and a canvas to draw on, with text in any language. Cabby
draws them itself, with SDL 3 underneath: you describe the window, and say
what should happen when things are clicked or typed.
The library's complete reference is `gui.md`, installed next to its header
(Appendix I). The same windows can be drawn in a terminal, with `tui`:
the part's last chapter.

### Chapter 51: Your first window

*Program: `51-first-window.cby`. Library: `use gui`.*

#### A window is an object

You make a window by making your own object from `gui.window`
([Chapter 24](#chapter-24-inheritance)). Its `build` method says what's in
the window; `run` shows it:

```text
your window object  ->  build: add things, attach handlers  ->  run
```

<!-- gui: click Say hello; type 1 Ada; click Say hello; type 1 Grace; enter 1; close -->
```cabby
use gui

object greeter : gui.window {
	gui.label Message                // what the handlers need later
	gui.entry Name

	void build {
		set_title "Greeter"
		set_size 360, 180
		Message = add_label "What's your name?"
		Name = add_entry
		gui.button Hello = add_button "Say hello"
		Hello.on_click say_hello     // a handler: runs on each click
		Name.on_enter say_hello      // and when Enter is pressed in the box
	}

	void say_hello {
		str Who = Name.text
		if Who.length == 0 {
			Message.set_text "Type your name first!"
			return
		}
		Message.set_text "Hello, " + Who + "!"
		println "greeted", Who
	}
}

int main {
	greeter g
	g.run                            // returns when the window is closed
	println "goodbye"
}
```

```text
greeted Ada
greeted Grace
goodbye
```

(That output is from a test run that clicked the button with the box empty,
typed "Ada" and clicked, then typed "Grace" and pressed Enter, then closed
the window.)

#### What's in it

* **Things stack top to bottom** in the order they're added. `add_label`,
  `add_entry` and `add_button` each add one and give it back to you as a
  `gui.label`, `gui.entry` or `gui.button`.
* **Keep what you'll need later as fields.** `Message` and `Name` are fields
  because `say_hello` uses them. The button is only needed inside `build`, so
  it's a local.
* **Every widget has `set_text` and `text`**: change what a label or button
  says, or read what's typed in a box.
* **Windows have `set_title`, `set_size width, height` and `close`.**

#### The build method

`build` is the method where a window sets itself up. You write it; Cabby
calls it. `run` calls it once, just before the window first appears, so
by the time anyone can click anything, everything `build` added is there.

**What goes in `build`:**

* the window's settings: `set_title`, `set_size`;
* the widgets: every `add_label`, `add_button`, `add_row` and so on;
* attaching handlers: `Hello.on_click say_hello`;
* starting timers that should run from the start: `every time.seconds(1), tick`
  ([Chapter 54](#chapter-54-timers));
* starting values for the window's fields, since fields start at zero.

**What doesn't go in `build`:**

* **Anything slow.** The window only appears when `build` returns, so a long
  calculation here means a long wait with nothing on screen. (The same is
  true of handlers: while one runs, the window can't redraw or react. Keep
  them quick.)
* **Waiting for the user.** A loop such as `while Name.text == ""` never
  ends: nothing typed reaches the window while your code is running. Attach
  a handler instead, and let the window call you when something happens.
* **Reading what the user typed or chose.** At this point the boxes are
  empty and nothing is selected; read them in handlers.
* **`run`, or `build` itself.** `run` calls `build`; your code only needs to
  call `run`, once, from outside the window.

**Inside the window object, but outside `build`:**

* **Fields** for the widgets a handler needs later (`gui.label Message`),
  and for the window's own data (`int Clicks`). A widget only `build` uses
  is a local in `build` instead.
* **Handlers**: methods with no parameters, named in `on_click` and the
  others.
* **Helper methods** that `build` and the handlers share.
* **Not statements.** `every time.seconds(1), tick` or `add_label "Hi"` straight
  inside the object is an error: they go in `build`, or in a handler
  ([Chapter 22](#what-goes-where)).

**Outside the window object,** at the top level: `use gui`, constants,
plain functions that don't need the window, and `main`, which makes the
window and calls `run`.

Here are all the places in one program:

<!-- gui: click Add one; click Add one; wait 3500; close -->
```cabby
use gui

const int START = 10                       // top level: constants...

// ...and plain functions, which need nothing from the window.
str plural int n, const str &Word {
	if n == 1 {
		return Word.copy
	}
	return Word + "s"
}

object clicker : gui.window {
	gui.label Shown                    // fields: widgets the handlers need,
	int Clicks                         // and the window's own data

	void build {                       // runs once, before the window appears
		set_title "Clicker"
		Clicks = START                 // starting values
		Shown = add_label ""
		show_count                     // build can call the window's methods
		gui.button Add = add_button "Add one"   // only needed here: a local
		Add.on_click add_one           // attach handlers
		after time.seconds(3), remind  // start timers
	}

	void add_one {                     // a handler: no parameters
		Clicks++
		show_count
	}

	void remind {
		println "still here, with", Clicks, plural(Clicks, "click")
	}

	// A helper that build and add_one share.
	void show_count {
		Shown.set_text Clicks.text + " " + plural(Clicks, "click")
	}
}

int main {                                 // makes the window and runs it
	clicker c
	c.run
	println "final count:", c.Clicks
}
```

```text
still here, with 12 clicks
final count: 12
```

(From a test run that clicked "Add one" twice, waited for the timer, then
closed the window.)

**Widgets after `build`.** A handler can add widgets too: they appear at
the bottom of the window. But a field for a widget must be filled in (by
`build` or a handler) before it's used. Using one that never was, such as
`Message.set_text` when nothing ever did `Message = add_label ...`, stops
the program with `gui: a widget used before it was added to a window`.

#### Handlers

`Hello.on_click say_hello` doesn't call `say_hello`. It names one of the
window's methods, to be called later, every time the button is clicked. The
method is a *handler*: it takes no parameters and returns nothing.

While the window is open, the program sleeps, using no processor time, until
something happens. Then it calls the handler, like an interrupt calling its
service routine, and goes back to sleep. `run` returns when the window is
closed.

| Event | Handler is called |
|---|---|
| `button.on_click Name` | each time the button is clicked |
| `entry.on_change Name` | each time the box's text changes |
| `entry.on_enter Name` | when Enter is pressed in the box |

A handler must be one of this window's own methods, named without parentheses:

<!-- error: 'pressed' takes arguments; a handler takes none -->
```cabby
use gui

object w : gui.window {
	void build {
		gui.button b = add_button "x"
		b.on_click pressed
	}
	void pressed int n {
	}
}

int main {
}
```

```text
error: 'pressed' takes arguments; a handler takes none
```

Because handlers belong to the window, they always live exactly as long as
the buttons that call them. A handler can never run after its window is
gone.

#### A window without handlers

For a window with nothing to react to, use a `gui.window` directly:

<!-- gui: close -->
```cabby
use gui

int main {
	gui.window w
	w.set_title "Note"
	w.add_label "Nothing to click here."
	w.run
	println "closed"
}
```

```text
closed
```

#### Running window programs

They run both ways, like every Cabby program: `cabby greeter.cby`, or
`cabby -c greeter.cby` for a native program. Cabby's gui library is built
when SDL 3 is installed (`sdl3` on Arch, `libsdl3-dev` on Debian, `brew
install sdl3` on a Mac); without it, `use gui` says what to install.

A window follows the desktop: dark when the desktop is dark, light when
it's light (`CABBY_THEME=dark` or `light` chooses). Text boxes do what you'd
expect of them: Ctrl+Z undoes (Cmd+Z on a Mac), a double click picks a
word, and accents and other alphabets type as they do anywhere else, Greek
with its breathings included.

#### The same window, drawn by GTK

Change `use gui` to `use gtk`, and `gui.` to `gtk.`, and the same program
is drawn by GTK 4, with GTK's own widgets (`gtk4` on Arch, `libgtk-4-dev`
on Debian). Everything in these chapters works the same with it. Choose it
if you want GTK's look. Both can be used with a screen reader on Linux
(Orca): gui's windows tell it what's in them, with nothing to do but
`set_description` on a picture or canvas ([Chapter 53](#chapter-53-more-widgets),
and `lib/gui.md`). On a Mac, gui's can't be read aloud by VoiceOver yet.

#### In short

* `object mine : gui.window { void build { ... } }`, then `mine m` and `m.run`.
* `build` sets the window up, once, before it appears: settings, widgets,
  handlers, timers and starting values. Keep it quick, and do the rest in
  handlers.
* `add_label`, `add_entry`, `add_button` stack things top to bottom.
* `on_click`, `on_change`, `on_enter` take the name of a method with no
  parameters.

#### Try this

* Add a "Clear" button whose handler empties the box with `Name.set_text ""`.
* Count the greetings and show the count in another label, with
  `count.text` ([Chapter 12](#chapter-12-strings)).

[← Chapter 50](#chapter-50-databases) · [Contents](#contents) · [Chapter 52 →](#chapter-52-a-small-app)

---

### Chapter 52: A small app

*Program: `52-small-app.cby`. Topics: rows, several handlers, `on_change`, numbers as text.*

A tip calculator puts together what a typical small app needs: a box to type
into, buttons side by side, and results that update as you type.

<!-- gui: type 1 50; click 20%; close -->
```cabby
use gui

object tipper : gui.window {
	gui.entry Bill
	gui.label TipLine
	gui.label TotalLine
	int Percent

	void build {
		set_title "Tip calculator"
		set_size 320, 220
		Percent = 10

		add_label "Bill:"
		Bill = add_entry
		Bill.on_change recalculate       // every time the text changes

		add_label "Tip:"
		gui.row Choices = add_row        // a row: things side by side
		gui.button Ten = Choices.add_button "10%"
		gui.button Twenty = Choices.add_button "20%"
		Ten.on_click tip_10
		Twenty.on_click tip_20

		TipLine = add_label ""
		TotalLine = add_label ""
		recalculate
	}

	void tip_10 {
		choose 10
	}
	void tip_20 {
		choose 20
	}

	void choose int p {                  // not a handler: it has a parameter
		Percent = p
		recalculate
	}

	void recalculate {
		double Amount = 0.0
		str Typed = Bill.text
		if Typed.is_number {             // "" or text that isn't a number counts as 0
			Amount = Typed.to_double
		}
		double Tip = Amount * Percent / 100.0
		TipLine.set_text "Tip (" + Percent.text + "%): " + Tip.text
		TotalLine.set_text "Total: " + (Amount + Tip).text
		println Percent.text + "%:", Amount, "->", Amount + Tip
	}
}

int main {
	tipper t
	t.run
}
```

```text
10%: 0.0 -> 0.0
10%: 5.0 -> 5.5
10%: 50.0 -> 55.0
20%: 50.0 -> 60.0
```

(The test typed "50" one character at a time, so `recalculate` ran for "5"
and then "50", and then clicked "20%".)

#### Rows

`add_row` adds a `gui.row` and gives it back. A row is a container like the
window itself, so it has the same `add_label`, `add_button`, `add_entry` and
`add_row`; what's added to it goes side by side. Rows inside rows, and a
window full of rows, make grids and forms.

#### One handler per button, sharing the work

A handler takes no arguments, so it can't be told *which* button was
clicked. The pattern is a small handler per button that calls one shared
method with the difference: `tip_10` and `tip_20` both call `choose`.

#### Numbers as text

Labels show text, so numbers are turned into text with `.text`
([Chapter 12](#chapter-12-strings)): `"Total: " + (Amount + Tip).text`.
Going the other way, `.to_double` turns the box's text into a `double`. The
box can be empty, or hold text that isn't a number, and `.to_double` would
panic on those, so check with `.is_number` first.

#### In short

* `add_row` for side-by-side layout; rows have everything a window has for
  adding things.
* One small handler per button, calling a shared method.
* `.text` for numbers into labels; `.is_number` then `.to_double` for text
  from boxes.

#### Try this

* Add a "Split between" box and show what each person pays.
* Round the amounts to cents: `use math`, then
  `floor(x * 100.0 + 0.5) / 100.0`.

[← Chapter 51](#chapter-51-your-first-window) · [Contents](#contents) · [Chapter 53 →](#chapter-53-more-widgets)

---

### Chapter 53: More widgets

*Program: `53-more-widgets.cby`. Topics: check boxes, drop-down lists, sliders, text areas, lists, columns.*

Labels, buttons and text boxes get a long way, but forms usually need
more. Every widget below works the same way as the ones you know: add it,
keep it in a field if a handler needs it later, and attach handlers.

| Add | You get | Read it with | Hear about changes with |
|---|---|---|---|
| `add_check "Text"` | a check box | `.checked` | `on_toggle` |
| `add_choice` | a drop-down list | `.selected`, `.text` | `on_change` |
| `add_slider 1, 10` | a slider | `.value` | `on_change` |
| `add_text_area` | a box for several lines | `.text` | `on_change` |
| `add_list` | a list to pick from | `.selected`, `.text` | `on_select` |
| `add_progress` | a progress bar | | |
| `add_image "file.png"` | a picture | | |

#### Choices

A check box is on or off. A drop-down list holds options, added with
`add_option`; the first is chosen to begin with. A slider chooses a whole
number between two limits.

<!-- gui: choose 1 Large; check Extra cheese; slide 1 3; click Order; close -->
```cabby
use gui

object order_form : gui.window {
	gui.choice Size
	gui.check Cheese
	gui.slider HowMany

	void build {
		set_title "Pizza"
		Size = add_choice
		Size.add_option "Small"
		Size.add_option "Medium"
		Size.add_option "Large"
		Cheese = add_check "Extra cheese"
		HowMany = add_slider 1, 10
		gui.button Order = add_button "Order"
		Order.on_click place_order
	}

	void place_order {
		println HowMany.value, "x", Size.text, "(option", Size.selected, ")"
		if Cheese.checked {
			println "with extra cheese"
		}
	}
}

int main {
	order_form f
	f.run
}
```

```text
3 x Large (option 2 )
with extra cheese
```

`selected` counts from 0, as array indexes do. For a drop-down or a list,
`text` is the chosen entry itself, which is usually what you want.

#### Text areas and lists

A text area is a text box for several lines, with a scroll bar when the text
grows. Its `text` is all of it, with `"\n"` between the lines. A list shows
items to pick from; `add_item` adds one at the end, and `clear` empties it.

<!-- gui: write 1 Milk\nEggs\nBread; click Add; select 1 Eggs; close -->
```cabby
use gui

object shopping : gui.window {
	gui.text_area Typed
	gui.list Items

	void build {
		set_title "Shopping"
		add_label "One item per line:"
		Typed = add_text_area
		gui.button Add = add_button "Add"
		Add.on_click add_items
		Items = add_list
		Items.on_select picked
	}

	void add_items {
		str All = Typed.text
		str One
		for i in All.length {
			if All[i] == '\n' {
				Items.add_item One
				One = ""
			} else {
				One += All[i]
			}
		}
		if One.length > 0 {
			Items.add_item One
		}
		println Items.count, "items"
	}

	void picked {
		if Items.selected >= 0 {
			println "picked", Items.text
		}
	}
}

int main {
	shopping s
	s.run
}
```

```text
3 items
picked Eggs
```

The list's handler also runs when the selection becomes "nothing", for
example when the list is cleared, so `picked` checks `selected >= 0` first.

#### Columns

A window stacks things top to bottom, and a row puts them side by side. A
*column* stacks them again, inside a row: a row holding two columns makes a
form with labels on the left and boxes on the right.

```cabby
gui.row Form = add_row
gui.column Labels = Form.add_column
gui.column Boxes = Form.add_column
Labels.add_label "Name:"
Name = Boxes.add_entry
```

`add_separator` draws a dividing line between groups of things.

#### Graying out and hiding

Every widget has `set_enabled` and `set_visible`. A button that shouldn't be
pressed yet can be grayed out until it should:

```cabby
void check_ready {                       // Name.on_change check_ready
	Order.set_enabled Name.text.length > 0
}
```

`set_visible false` hides a widget completely, and the rest of the window
closes up around it.

#### In short

* Check boxes, drop-downs, sliders, text areas and lists all follow the
  same pattern: add, keep, attach handlers.
* `selected` counts from 0 and is -1 for nothing; `text` is the chosen
  entry.
* Rows and columns nest to make any layout.
* `set_enabled` and `set_visible` work on every widget.

#### Try this

* In `53-more-widgets.cby`, add a "Delivery" check box that shows an
  address box only while it is ticked.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Colored text, and words for a screen reader.** Anything in a window
can have its text colored, and a description for a screen reader when
its text doesn't say what it is (a picture, a canvas):

<!-- gui: click Warn; close -->
```cabby
use gui

object notice : gui.window {
	gui.label Status

	void build {
		set_title "Notice"
		Status = add_label "All's well."
		Status.set_description "The status of the backup"
		gui.button Warn = add_button "Warn"
		Warn.on_click warn
	}

	void warn {
		Status.set_text "The disk is nearly full!"
		Status.set_text_color "darkred"
		println Status.text
	}
}

int main {
	notice N
	N.run
}
```

```text
The disk is nearly full!
```

**A progress bar, and a picture.** `add_progress` gives a bar that shows
how much is done, from 0.0 to 1.0; `add_image` shows a picture from a
file, scaled to fit, and `set_file` changes it:

<!-- gui: click Step; click Step; close -->
```cabby
use gui

object loader : gui.window {
	gui.progress Bar
	gui.image Picture
	double Done

	void build {
		set_title "Loading"
		Picture = add_image "pictures/cabby.png"
		Bar = add_progress
		gui.button Step = add_button "Step"
		Step.on_click step
	}

	void step {
		Done += 0.5
		Bar.set_fraction Done
		println "done:", Done
		if Done >= 1.0 {
			Picture.set_file ""
		}
	}
}

int main {
	loader L
	L.run
}
```

```text
done: 0.5
done: 1.0
```

[← Chapter 52](#chapter-52-a-small-app) · [Contents](#contents) · [Chapter 54 →](#chapter-54-timers)

---

### Chapter 54: Timers

*Program: `54-timers.cby`. Topics: `every`, `after`, `stop_timers`, progress bars.*

A timer is a handler that the clock calls instead of a click:

| | |
|---|---|
| `every time.milliseconds(100), tick` | calls `tick` every 100 milliseconds (ten times a second) |
| `after time.seconds(2), reset` | calls `reset` once, two seconds from now |
| `stop_timers` | stops all of this window's timers |

Each takes a duration ([chapter 39](#chapter-39-time-and-dates)):
`time.milliseconds(100)`, `time.seconds(2)` and the like; `use gui` brings
them with it. Timers follow
the same rules as every handler: they name one of the window's methods
with no parameters. They run only while their window is
open, and they stop when it closes. Between ticks the program sleeps.

<!-- gui: click Start; wait 1500; close -->
```cabby
use gui

object countdown : gui.window {
	gui.label Shown
	gui.progress Bar
	int Left

	void build {
		set_title "Countdown"
		Shown = add_label "Ready"
		Bar = add_progress
		gui.button Start = add_button "Start"
		Start.on_click begin
	}

	void begin {
		Left = 5
		every time.milliseconds(100), tick
	}

	void tick {
		Left--
		Shown.set_text Left.text
		Bar.set_fraction (5 - Left) / 5.0
		println Left
		if Left == 0 {
			stop_timers
			after time.milliseconds(200), finished
		}
	}

	void finished {
		Shown.set_text "Done!"
		println "done"
	}
}

int main {
	countdown c
	c.run
}
```

```text
4
3
2
1
0
done
```

#### Keeping time

A computer that is busy may call a timer a little late, so ticks aren't
perfectly regular. Count ticks, as `Left` does here, rather than adding up
how much time you hoped had passed. For a clock that must be exact over
hours, read the real time from the system on each tick instead.

#### In short

* `every time.seconds(1), handler` repeats; `after time.seconds(2), handler` happens once;
  `stop_timers` stops them all.
* Timers belong to their window and stop when it closes.
* Count ticks; don't assume each one is exactly on time.

#### Try this

* Add a "Pause" button to `54-timers.cby` that stops the timer without
  resetting, so "Start" carries on from where it was.

[← Chapter 53](#chapter-53-more-widgets) · [Contents](#contents) · [Chapter 55 →](#chapter-55-dialogs-and-several-windows)

---

### Chapter 55: Dialogs and several windows

*Program: `55-dialogs.cby`. Topics: `show_message`, `ask`, `ask_open_file`, `ask_save_file`, images, more windows.*

#### Dialogs

A dialog is a small window that asks something and then gets out of the way.
In `gui`, dialogs don't stop the program to wait for the answer: you give a
handler, and the handler is called with the answer.

| | |
|---|---|
| `show_message "Saved."` | a message with an OK button |
| `ask "Delete it?", delete_it` | calls `delete_it` only if the answer is yes |
| `ask_open_file opened` | calls `opened` once a file has been chosen |
| `ask_save_file "notes.txt", save_to` | calls `save_to` once a place to save has been chosen |
| `chosen_file` | the full path of the file the last file dialog chose |

If the person cancels, the handler is not called at all, so a handler never
has to check whether there really was an answer.

<!-- gui: click Open...; pick pictures/cabby.png; click Clear; answer no; click Clear; answer yes; close -->
```cabby
use gui

object viewer : gui.window {
	gui.image Picture
	gui.label Where

	void build {
		set_title "Viewer"
		gui.row Buttons = add_row
		gui.button OpenButton = Buttons.add_button "Open..."
		gui.button ClearButton = Buttons.add_button "Clear"
		OpenButton.on_click choose
		ClearButton.on_click confirm_clear
		Where = add_label "No picture"
		Picture = add_image ""
	}

	void choose {
		ask_open_file opened
	}

	void opened {
		Picture.set_file chosen_file
		Where.set_text chosen_file
		println "showing", chosen_file
	}

	void confirm_clear {
		ask "Clear the picture?", clear
	}

	void clear {
		Picture.set_file ""
		Where.set_text "No picture"
		println "cleared"
	}
}

int main {
	viewer v
	v.run
}
```

```text
showing pictures/cabby.png
cleared
```

The first "Clear" was answered no, so `clear` ran only once. `add_image`
shows a picture from a file (PNG, JPEG, SVG and more), scaled to fit; an
empty name shows nothing.

Saving works the same way: `ask_save_file "notes.txt", save_to`, and
`save_to` reads `chosen_file` to know where. (Writing the file itself waits
for the `files` library; until then, C's functions can do it.)

#### Several windows

A handler can make another window and run it. That `run` returns when the
second window is closed, and the first window carries on:

<!-- gui: click Settings; close; close -->
```cabby
use gui

object settings_window : gui.window {
	void build {
		set_title "Settings"
		add_check "Dark mode"
	}
}

object main_window : gui.window {
	gui.label Status

	void build {
		set_title "Main"
		Status = add_label "Hello"
		gui.button b = add_button "Settings"
		b.on_click show_settings
	}

	void show_settings {
		settings_window s
		s.run                            // waits here until Settings is closed
		Status.set_text "settings closed"
		println Status.text
	}
}

int main {
	main_window m
	m.run
	println "all done"
}
```

```text
settings closed
all done
```

`s` is a local variable of the handler, so when the handler ends, the
settings window is dropped and destroyed with it: ownership tidies up
windows too.

#### Testing with dialogs

When a test script drives a window (Appendix G), dialogs aren't shown: the
script answers them, with `answer yes`, `answer no`, `pick <path>` or
`cancel`, as in the example above.

#### In short

* Dialogs take a handler and call it with the answer; canceling calls
  nothing.
* `chosen_file` is the path a file dialog chose.
* A second window's `run` waits until it closes; dropping a window object
  destroys its window.

#### Try this

* In `55-dialogs.cby`, add a "Quit" button that asks first, and calls
  `close` if the answer is yes.

[← Chapter 54](#chapter-54-timers) · [Contents](#contents) · [Chapter 56 →](#chapter-56-menus-and-shortcuts)

---

### Chapter 56: Menus and shortcuts

*Program: `56-menus.cby`. Library: `use gui` (or `use tui`).*

#### A menu bar

Most desktop programs have a **menu bar** along the top: File, Edit, Help.
It holds what the program can do, without filling the window with buttons.
In Cabby, a window's `add_menu` adds a menu to its bar (the bar appears
with the first one), and the menu's `add_item` adds the things to choose
in it. Each item has a handler, called when it's chosen, just as a button
calls `on_click`'s:

<!-- gui: menu Greetings > Hello; menu Greetings > Goodbye; close -->
```cabby
use gui

object greeter : gui.window {
	gui.label Message

	void build {
		set_title "Greeter"
		gui.menu Greetings = add_menu "Greetings"
		Greetings.add_item "Hello", "Ctrl+H", hello
		Greetings.add_separator
		Greetings.add_item "Goodbye", "", goodbye
		Message = add_label "Choose from the menu."
	}

	void hello {
		Message.set_text "Hello!"
		println Message.text
	}

	void goodbye {
		println "Goodbye!"
		close
	}
}

int main {
	greeter g
	g.run
}
```

```text
Hello!
Goodbye!
```

`add_separator` draws a line between items, to group them. The middle
argument of `add_item` is the item's **shortcut**.

#### Keyboard shortcuts

A shortcut is a key, or a key held with Ctrl, Shift or Alt, that chooses
an item without opening its menu. It works from anywhere in the window,
and the menu shows it beside the item, so people learn it as they use the
menus. They're written as menus show them:

| Write | Means |
|---|---|
| `"Ctrl+S"` | hold Ctrl, press S |
| `"Ctrl+Shift+Z"` | hold Ctrl and Shift, press Z |
| `"Alt+X"` | hold Alt, press X |
| `"F5"` | the F5 key, on its own |
| `""` | no shortcut |

Most programs agree on a few, and people expect them: `Ctrl+N` new,
`Ctrl+O` open, `Ctrl+S` save, `Ctrl+Q` quit, `Ctrl+Z` undo, `Ctrl+F`
find, `F1` help. Use those for those, and choose freely for the rest.

A window's `add_shortcut "F5", refresh` makes a shortcut with no menu item.

#### Graying out

`add_item` gives back the item, a `gui.menu_item`. Keep it in a field, and
`set_enabled false` grays it out: it can't be chosen, and its shortcut
does nothing, until `set_enabled true`. Tutorial 56 grays out *Undo* until
there's something to undo, which tells the person what they can do at a
glance.

#### In a terminal

With `use tui`, the menu bar is the first row inside the window. F10 opens
the menus (or a click on a menu's name, or Alt and its first letter); the
arrow keys move, Enter chooses, and Esc closes them. Shortcuts with Ctrl,
Alt and the F keys work as in a window.

#### In short

* `gui.menu File = add_menu "File"`; `File.add_item "Save", "Ctrl+S", save`;
  `File.add_separator`.
* Shortcuts: `"Ctrl+S"`, `"Ctrl+Shift+Z"`, `"Alt+X"`, `"F5"`, or `""` for
  none; `add_shortcut Keys, h` for one without an item.
* `gui.menu_item` from `add_item`: `set_enabled false` grays it out.

#### Try this

* In `56-menus.cby`, add a "Join lines" item, with `Ctrl+J`.
* Add a "View" menu with "Bigger" and "Smaller" items that change the
  window's size with `set_size`.

[← Chapter 55](#chapter-55-dialogs-and-several-windows) · [Contents](#contents) · [Chapter 57 →](#chapter-57-tabs-and-scrolling)

---

### Chapter 57: Tabs and scrolling

*Program: `57-tabs.cby`. Library: `use gui` (or `use tui`).*

When a window has more to show than fits, there are two ways to share its
space: **tabs**, which show one page at a time, and a **scrolling
column**, which shows part of something long and lets the person scroll
through the rest.

#### Tabs

`add_tabs` adds a set of tabs, a `gui.tabs`. Each `add_tab "Title"` adds a
page, with its tab along the top, and gives back the page's column, to
fill as you'd fill a window:

<!-- gui: tab 1 Sound; close -->
```cabby
use gui

object settings : gui.window {
	gui.tabs Pages

	void build {
		set_title "Settings"
		Pages = add_tabs
		gui.column General = Pages.add_tab "General"
		General.add_label "Your name:"
		General.add_entry
		gui.column Sound = Pages.add_tab "Sound"
		Sound.add_check "Sounds on"
		Sound.add_slider 0, 10
		Pages.on_change changed
	}

	void changed {
		println "showing", Pages.text + ": page", Pages.selected, "of", Pages.count
	}
}

int main {
	settings s
	s.run
}
```

```text
showing Sound: page 1 of 2
```

The first page shows to begin with. Clicking a tab shows its page; the
program can too, with `set_selected` (pages count from 0). `selected` is
the page showing, `text` its tab's title, and `on_change` calls a handler
whenever a different page is shown.

Things on pages that aren't showing still exist: a handler can read and
change them as usual. Only their drawing waits until their page shows.

#### Scrolling

`add_scrolling_column` adds a column that shows as much as fits, with a
scroll bar for the rest. Fill it like any column: a long list of check
boxes, a form with many questions. It takes any spare height the window
has, so a bigger window shows more of it.

```cabby
gui.column Long = add_scrolling_column
for i in 1 to 100 {
	Long.add_check "Option " + i.text
}
```

(Lists and text areas scroll by themselves; a scrolling column is for
everything else.)

#### In a terminal

With `use tui`, the tabs are a line of titles, the showing one
highlighted. Tab moves to them, and Left and Right change the page. A
scrolling column has a scroll bar at its right, and moving to something
out of sight (with Tab) scrolls to it; the mouse wheel scrolls too.

#### In short

* `gui.tabs Pages = add_tabs`; `gui.column Page = Pages.add_tab "Title"`.
* `selected`, `set_selected`, `count`, `text`, `on_change`.
* `add_scrolling_column`: a column that scrolls when what's in it doesn't
  fit.

#### Try this

* In `57-tabs.cby`, add a fourth page, "Notes", with a text area.
* Put the "For more" page's amounts for every ingredient in a scrolling
  column.

[← Chapter 56](#chapter-56-menus-and-shortcuts) · [Contents](#contents) · [Chapter 58 →](#chapter-58-tables)

---

### Chapter 58: Tables

*Program: `58-tables.cby`. Library: `use gui` (or `use tui`).*

A **table** shows rows and columns of text, with a heading over each
column: scores, a list of files, the planets. `add_table` takes the
headings, as a list, and `add_row` adds a row, one text for each column:

<!-- gui: row 1 Tom; sort 1 Score; close -->
```cabby
use gui

object scores : gui.window {
	gui.table Table

	void build {
		set_title "Scores"
		Table = add_table {"Name", "Score"}
		Table.add_row {"Ada", "1200"}
		Table.add_row {"Tom", "95"}
		Table.add_row {"Grace", "640"}
		Table.on_select picked
	}

	void picked {
		if Table.selected >= 0 {
			println "row", Table.selected, "is", Table.text, "with", Table.cell(Table.selected, 1)
		}
	}
}

int main {
	scores s
	s.run
}
```

```text
row 1 is Tom with 95
```

The cells are text, so a number goes in as text: `Score.text`, or
`"1200"`.

#### Sorting

Clicking a heading sorts the rows by that column; clicking it again
sorts them the other way. Numbers are sorted as numbers, so 95 comes
before 640 and 1200, and text in alphabetical order, ignoring capitals.

Sorting only changes the order the rows are **shown** in. To the
program, the rows keep the numbers they had when they were added: `row 0`
is Ada, whichever row she's shown in. That way `selected` and `cell`
always mean the same row, and the program needn't care how the person has
sorted the table. (Above, sorting by Score after Tom was chosen changed
nothing the program sees: he's still row 1.)

#### Reading and changing it

| Write | What it does |
|---|---|
| `cell Row, Column` | one cell's text (rows and columns count from 0) |
| `set_cell Row, Column, Text` | changes it |
| `selected`, `set_selected` | the row chosen (-1: none) |
| `text` | the chosen row's first cell |
| `count`, `columns` | how many rows and columns |
| `remove_row Row`, `clear` | takes one row out (the rest move up), or all of them |
| `on_select h` | calls `h` when the choice changes |

`on_select` is also called when the chosen row is taken out, with nothing
chosen, so check `selected >= 0` first, as above.

#### In a terminal

With `use tui`, the headings are on a gray line with a `▲` or `▼` by the
column the rows are sorted by. Up and Down choose a row, Left and Right
sort by the column before or after, and a click on a heading sorts by it.

#### In short

* `gui.table T = add_table {"Name", "Score"}`; `T.add_row {"Ada", "1200"}`.
* `cell Row, Column`, `set_cell`, `remove_row`, `clear`, `count`.
* `selected`, `text`, `on_select`; rows keep their numbers however the
  table is sorted.

#### Try this

* In `58-tables.cby`, add text boxes and an "Add" button for a planet of
  your own.
* Read a table's rows from a file of comma-separated values: `files.lines`,
  then `.split ","` for each line.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Changing a table.** Cells change one at a time; rows are selected by
number, taken out, or all cleared away (the headings stay):

<!-- gui: click Change; close -->
```cabby
use gui

object scores : gui.window {
	gui.table Table

	void build {
		set_title "Scores"
		Table = add_table {"Name", "Score"}
		Table.add_row {"Ada", "1200"}
		Table.add_row {"Tom", "95"}
		Table.add_row {"Grace", "640"}
		gui.button Change = add_button "Change"
		Change.on_click change
	}

	void change {
		Table.set_cell 1, 1, "96"
		Table.set_selected 2
		println Table.cell(1, 1), Table.text, Table.count
		Table.remove_row 0
		println Table.count, Table.cell(0, 0)
		Table.clear
		println Table.count, Table.columns
	}
}

int main {
	scores S
	S.run
}
```

```text
96 Grace 3
2 Tom
0 2
```

[← Chapter 57](#chapter-57-tabs-and-scrolling) · [Contents](#contents) · [Chapter 59 →](#chapter-59-numbers-colors-and-fonts)

---

### Chapter 59: Numbers, colors and fonts

*Program: `59-choosers.cby`. Library: `use gui` (or `use tui`).*

Some choices are easier to make than to type. Three widgets make them:

| Add | Gives | Its choice |
|---|---|---|
| `add_number Min, Max` | `gui.number`: a box with buttons for one more or one less | `value`, an `int` |
| `add_color_button` | `gui.color_button`: opens a color chooser | `color`, as `"#4682b4"` |
| `add_font_button` | `gui.font_button`: opens a font chooser | `font`, as `"Serif Bold 16"` |

Each has a way to set its choice (`set_value`, `set_color`, `set_font`)
and an `on_change` handler, called whenever the choice changes:

<!-- gui: number 1 3; color 1 gold; close -->
```cabby
use gui

object party : gui.window {
	gui.number Guests
	gui.color_button Balloons

	void build {
		set_title "Party"
		Guests = add_number 1, 20
		Guests.on_change changed
		Balloons = add_color_button
		Balloons.set_color "red"
		Balloons.on_change changed
	}

	void changed {
		println Guests.value, "guests,", Guests.value * 2, "balloons colored", Balloons.color
	}
}

int main {
	party p
	p.run
}
```

```text
3 guests, 6 balloons colored #ff0000
3 guests, 6 balloons colored #ffd700
```

A number box keeps its value between its limits: `set_value 50` on this
one gives 20. It's the slider's cousin: a slider is quicker for "about
this much", a number box better for an exact number.

#### Choices a canvas understands

The color comes back as `"#rrggbb"`, however it was set or chosen, and
the font as its family, its style and its size. Both are just what a
canvas takes, so a program can draw with whatever the person chose:

```cabby
Badge.set_color Color.color
Badge.set_font Font.font          // "Serif Bold 28": the family, bold, 28 pixels high
Badge.write 24, 60, Name.text
```

`59-choosers.cby` draws a name badge that way, and redraws it each time a
choice changes.

#### In a terminal

With `use tui`, a number box is `‹ 7 ›`: Left and Right (or a click on
either end) change it by one, Page Up and Page Down by ten, and digits can
be typed. Enter on a color or font button opens a list to choose from.
A terminal has one font, so a font button there only keeps the font's
name, for the program to use; a canvas uses its size.

#### In short

* `add_number Min, Max`, then `value`, `set_value`.
* `add_color_button`, then `color` (`"#rrggbb"`), `set_color`.
* `add_font_button`, then `font` (`"Serif Bold 16"`), `set_font`.
* Each has `on_change`; a canvas's `set_color` and `set_font` take
  their choices as they come.

#### Try this

* In `59-choosers.cby`, add a second color button for the badge's
  background.
* Add a color button to `60-drawing.cby` for the color of the bars.

[← Chapter 58](#chapter-58-tables) · [Contents](#contents) · [Chapter 60 →](#chapter-60-drawing)

---

### Chapter 60: Drawing

*Program: `60-drawing.cby`. Library: `use gui` (or `use tui`).*

#### A canvas

A **canvas** is a part of a window to draw on: lines, shapes, text and
pictures, in any color. Charts, diagrams, clocks and games are all drawn
on one. `add_canvas 360, 240` adds one, 360 pixels wide and 240 high.

You don't draw on a canvas straight away. You give it a handler that
draws it, with **`on_draw`**, and the canvas calls that handler whenever
it needs drawing: when the window first shows, when it's uncovered, and
after **`redraw`**. So the handler draws the whole picture, every time,
from what the program knows:

<!-- check -->
```cabby
use gui

object house : gui.window {
	gui.canvas Picture

	void build {
		set_title "A house"
		Picture = add_canvas 200, 160
		Picture.on_draw draw_house
	}

	void draw_house {
		Picture.fill_with "lightskyblue"              // the sky: all of it
		Picture.set_color "forestgreen"
		Picture.fill_rectangle 0, 130, 200, 30         // grass along the bottom
		Picture.set_color "wheat"
		Picture.fill_rectangle 60, 80, 80, 50          // the walls
		Picture.set_color "firebrick"
		Picture.fill_polygon {50, 80, 150, 80, 100, 40}   // the roof: a triangle
		Picture.set_color "saddlebrown"
		Picture.fill_rectangle 90, 100, 20, 30         // the door
		Picture.set_color "gold"
		Picture.fill_circle 170, 30, 16                // the sun
		Picture.set_color "black"
		Picture.write 8, 8, "home"
	}
}

int main {
	house h
	h.run
}
```

The shapes are drawn in order, each on top of what's already there, so
the sky comes first and the door after the walls.

#### Where things go: x and y

Positions are in **pixels**, counted from the canvas's **top left
corner**. `x` counts to the right, as you'd expect, but **`y` counts
down**: `0, 0` is the top left corner, and the bottom of a 160-pixel
canvas is at `y = 160`. Every screen and picture format counts this way,
since screens are drawn from the top line down. It takes getting used to:
to draw something higher, make its `y` smaller.

```text
0,0 ──────────► x
 │
 │     60,80 ┌──────┐
 │           │      │  fill_rectangle 60, 80, 80, 50:
 │           └──────┘  80 across, 50 down from 60, 80
 ▼
 y
```

So a bar in a chart, which grows *up* from a line at `y = 200`, has its
top at `200 - Height`: tutorial 60 does just that.

#### Shapes and colors

| Write | Draws |
|---|---|
| `line X1, Y1, X2, Y2` | a line from one point to another |
| `rectangle X, Y, Width, Height` | a rectangle's outline; `X, Y` is its top left corner |
| `circle X, Y, Radius` | a circle's outline; `X, Y` is its center |
| `ellipse X, Y, Across, Down` | an oval: `Across` and `Down` from its center to its edges |
| `arc X, Y, Radius, From, To` | part of a circle's outline, between two angles |
| `polygon Corners` | a shape through its corners: `{x, y, x, y, ...}` |
| `write X, Y, Text` | text, its top left corner at `X, Y` |
| `image X, Y, Path` | a picture from a file |
| `picture X, Y, P` | a picture in memory (chapter 66) |
| `fill_rectangle`, `fill_circle`, `fill_ellipse`, `fill_polygon` | the same shapes, filled in |
| `fill_with Color` | the whole canvas |

The pen has a color, a width for lines and a size for text, which stay
until they're changed: `set_color "red"`, `set_line_width 3`,
`set_font_size 20`. Each drawing starts on white, in black, 1 pixel wide
and 14 high.

**Colors** are written as web pages write them, so there's nothing new to
learn and you can look any of them up:

* a name: `"red"`, `"orange"`, `"steelblue"`, `"lightyellow"`, `"dimgray"`
  (there are 148 of them, the CSS color names);
* `"#3366cc"`: red, green and blue in hexadecimal (chapter 45), from `00`
  (none) to `ff` (all), or `"#36c"` for short;
* `"rgb(51, 102, 204)"`: the same in ordinary numbers, from 0 to 255;
* with an amount of see-through on the end: `"rgba(0, 0, 0, 0.5)"` is
  black at half strength, so what's underneath shows through.

**Angles** for `arc` are in degrees *as on a clock*: 0 at the top, 90 at
the right, 180 at the bottom, going clockwise. `arc 100, 100, 50, 0, 90`
is the quarter from 12 o'clock to 3.

#### Changing the picture: redraw

To change what's on a canvas, change the fields its handler draws from,
and call **`redraw`**: the canvas calls the handler again. The drawing
methods only work inside that handler; calling one from a button's handler
is a panic that says to use `redraw`. That's what keeps a canvas right
when a window is covered and uncovered: it can always be drawn again.

<!-- gui: click Bigger; click Bigger; close -->
```cabby
use gui

object balloon : gui.window {
	gui.canvas Sky
	int Size

	void build {
		set_title "Balloon"
		Size = 20
		Sky = add_canvas 200, 200
		Sky.on_draw draw_balloon
		gui.button Grow = add_button "Bigger"
		Grow.on_click bigger
	}

	void draw_balloon {
		Sky.set_color "crimson"
		Sky.fill_circle 100, 100, Size
		Sky.set_color "gray"
		Sky.line 100, 100 + Size, 100, 190
	}

	void bigger {
		Size += 10
		Sky.redraw                     // draw it again, bigger
		println "size", Size
	}
}

int main {
	balloon b
	b.run
}
```

```text
size 30
size 40
```

A **timer** (chapter 54) that changes a field and calls `redraw` makes a
canvas move: a clock's hands, a bouncing ball.

#### The mouse and keys

* **`on_click h`** calls `h` when the canvas is clicked;
  **`on_mouse_move h`** each time the mouse moves over it. Inside, `mouse_x`
  and `mouse_y` say where, in the canvas's pixels.
* **`on_key h`** calls `h` when a key is pressed while the canvas has the
  keys: after it's been clicked, or Tab has moved to it. `key` says which,
  named as `read_key` names keys (chapter 12): `"a"`, `" "`, `"up"`,
  `"left"`, `"enter"`, `"escape"`.

Tutorial 60 uses both: clicking a bar of its chart says which day it is,
and the arrow keys move from bar to bar.

#### Saving a drawing

`save "chart.png"` draws the canvas once more, into a PNG picture file of
its size, which any program can open. It's how a program makes charts and
pictures for other uses.

#### In a terminal

The same canvas works in `tui`, drawn roughly with blocks: each character
stands for 8 by 16 pixels, as two blocks one above the other, in color.
Text is shown as characters, and a picture's name stands in for it. So
`use tui` and `tui.canvas` instead, and the same program runs in a
terminal (see `tui.md`).

#### In short

* `add_canvas Width, Height`; `on_draw h` draws it, whenever needed;
  `redraw` asks for it again.
* x to the right, y **down**, from the top left corner, in pixels.
* `line`, `rectangle`, `circle`, `ellipse`, `arc`, `polygon`, `write`,
  `image`; `fill_` versions; `fill_with`.
* `set_color "steelblue"` (or `"#3366cc"`), `set_line_width`,
  `set_font_size`.
* `on_click`, `on_mouse_move`, `mouse_x`, `mouse_y`; `on_key`, `key`;
  `save "picture.png"`.

#### Try this

* In `60-drawing.cby`, draw each bar's millimeters just above it.
* Draw a clock face: twelve short lines around a circle (for each hour,
  work out the ends with `sin` and `cos`, from `use math`), and the hands from
  `dates.time_now`, with a timer calling `redraw` every second.
* Make a tiny painting program: `on_mouse_move` adds the mouse's position
  to two lists, and `on_draw` draws a small circle at each one.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Ovals and arcs, and following the mouse.** `ellipse` and `fill_ellipse`
draw an oval; `arc` part of a circle, its angles as on a clock (0 at the
top, going clockwise); `on_mouse_move` is called as the mouse moves over
the canvas, with `mouse_x` and `mouse_y`:

<!-- gui: point 1 50 40; point 1 150 120; close -->
```cabby
use gui

object face : gui.window {
	gui.canvas Board

	void build {
		set_title "A face"
		Board = add_canvas 200, 160
		Board.on_draw draw_face
		Board.on_mouse_move moved
	}

	void draw_face {
		Board.set_color "gold"
		Board.fill_ellipse 100, 80, 70, 55
		Board.set_color "black"
		Board.ellipse 100, 80, 70, 55
		Board.fill_circle 75, 65, 6
		Board.fill_circle 125, 65, 6
		Board.arc 100, 80, 35, 120, 240       // a smile: from 4 o'clock to 8
	}

	void moved {
		println "the mouse is at", Board.mouse_x, Board.mouse_y
	}
}

int main {
	face F
	F.run
}
```

```text
the mouse is at 50 40
the mouse is at 150 120
```

**Rectangles, any shape, and pictures.** `rectangle` draws a rectangle's
edge (as `fill_rectangle` fills one); `polygon` and `fill_polygon` a
shape through its corners, `x, y, x, y ...`; `image` a picture from a
file:

<!-- gui: close -->
```cabby
use gui

object house : gui.window {
	gui.canvas Board
	list double Roof

	void build {
		set_title "A house"
		Board = add_canvas 200, 200
		Board.on_draw draw_house
		Roof = {40, 90, 100, 40, 160, 90}
		println "a roof with", Roof.length / 2, "corners"
	}

	void draw_house {
		Board.set_color "tan"
		Board.fill_rectangle 50, 90, 100, 90
		Board.set_color "brown"
		Board.rectangle 50, 90, 100, 90
		Board.fill_polygon Roof
		Board.set_color "black"
		Board.polygon Roof
		Board.image 85, 120, "pictures/cabby.png"
	}
}

int main {
	house H
	H.run
}
```

```text
a roof with 3 corners
```

[← Chapter 59](#chapter-59-numbers-colors-and-fonts) · [Contents](#contents) · [Chapter 61 →](#chapter-61-a-phrasebook)

---

### Chapter 61: A phrasebook

*Program: `61-phrasebook.cby`, greetings in seven languages. Library: `use gui`.*

Choose a language from the drop-down, and the window greets you in it:
English, German, Latin, Greek, Old English, Hebrew and Arabic, each with
an emoji. There's nothing in the program about scripts or directions:

```cabby
} else if Which == 5 {
	Hello = "שלום! 👋"
	Thank = "תודה רבה 🙏"
} else {
	Hello = "مرحبا! 👋"
	Thank = "شكرا جزيلا 🙏"
}
Greeting.set_text Hello
```

The label does the rest. Hebrew and Arabic are written right to left, so
`"שלום! 👋"` is shown with שלום on the right and the hand on the left, the
exclamation mark after the word as a reader of Hebrew expects. A number
or an English word in a Hebrew sentence stays left to right, in its place:
`"המחיר הוא 25 shekel"` reads as it should either way. These are Unicode's
bidirectional rules, which every program that shows text follows; Cabby
follows the usual part of them.

Arabic's letters join up as they're written, each taking the shape its
place in the word gives it (at the start, in the middle, at the end, or
on its own), and an emoji is drawn in color from the computer's emoji
font, an emoji of several (a family, a flag) as one picture.

The text box takes any language too. Typed in Hebrew, the cursor follows
the text leftwards; `Yours.text` is the text in the order it was typed,
the first letter first, as any `str` is. What's stored is always in the
order it's read: only what's shown is turned round.

Two libraries help, found by themselves: HarfBuzz (the same one browsers
and GTK use) shapes the text, and the computer's color emoji font gives
the pictures. Linux has HarfBuzz; on a Mac it comes with Homebrew's
`harfbuzz`. Without it, Cabby still joins Arabic's letters and puts the
text in order itself.

#### In short

* A `str` holds text in any language, and a window shows it as it's
  written: right to left for Hebrew and Arabic, Arabic's letters joined,
  emoji in color.
* Nothing in the program says which: it's the text that knows.

#### Try this

* Add a language of your own: Japanese (こんにちは), Hindi (नमस्ते), Russian
  (Здравствуйте). Each is shown in its own script.
* Type Hebrew or Arabic into the box (with the desktop's keyboard for
  it): the cursor moves as the text goes, right to left.
* Put an English word and a number into a Hebrew sentence: "המחיר הוא 25
  shekel". The number and the word stay as they're read.

[← Chapter 60](#chapter-60-drawing) · [Contents](#contents) · [Chapter 62 →](#chapter-62-windows-in-a-terminal)

---

### Chapter 62: Windows in a terminal

*Program: `62-terminal-window.cby`. Library: `use tui`.*

#### The same window, drawn in text

`use tui` gives the same windows as `use gui`, drawn in the terminal with
text and lines instead of on the desktop. It needs no desktop at all, so it
works over SSH, on a server, or in a terminal that fills the screen.

Everything from Chapters 51 to 55 carries over unchanged, names included:
`tui.window`, `tui.label`, `tui.button`, `add_row`, `on_click`, `every`,
`ask`. Here is the greeter from Chapter 51, with `gui` changed to `tui`:

<!-- tui: click Say hello; type 1 Ada; click Say hello; screen; type 1 Grace; enter 1; close -->
```cabby
use tui

object greeter : tui.window {
	tui.label Message
	tui.entry Name

	void build {
		set_title "Greeter"
		set_size 360, 180
		Message = add_label "What's your name?"
		Name = add_entry
		tui.button Hello = add_button "Say hello"
		Hello.on_click say_hello
		Name.on_enter say_hello
	}

	void say_hello {
		str Who = Name.text
		if Who.length == 0 {
			Message.set_text "Type your name first!"
			return
		}
		Message.set_text "Hello, " + Who + "!"
		println "greeted", Who
	}
}

int main {
	greeter g
	g.run
	println "goodbye"
}
```

```text
greeted Ada
┌───────────────── Greeter ─────────────────┐
│ Hello, Ada!                               │
│ Ada                                       │
│ [ Say hello ]                             │
│                                           │
│                                           │
│                                           │
│                                           │
│                                           │
│                                           │
└───────────────────────────────────────────┘
greeted Grace
goodbye
```

The box in the middle of that screen is the window, as the test script
printed it after "Ada" was greeted. Change `tui` back to `gui` and the same
program opens a desktop window instead.

#### Keys

There's no mouse needed (though a click works, in terminals that pass
clicks on):

| Key | Does |
|---|---|
| Tab, Shift-Tab | moves to the next or previous thing |
| Enter, Space | presses a button, ticks a check box |
| Up, Down | picks in a list; otherwise moves between things |
| Left, Right | changes a drop-down or slider; moves in text |
| Esc | closes the window |

#### Sizes and printing

* **Sizes stay in pixels**, so a program can switch libraries: a character
  counts as 8 pixels wide and 16 high. `set_size 360, 180` is 45 by 11
  characters. Without `set_size`, the window fills the terminal.
* **Printing waits.** The window covers the terminal, so what handlers
  print is kept and shown when the window closes. (In the test output
  above, everything is in the order it happened, since the screen was
  printed too.)
* **Pictures can't be shown**: an image appears as `[picture: name]`.

#### Testing a terminal window

As with `CABBY_GUI_SCRIPT` (Appendix G), `CABBY_TUI_SCRIPT` runs a window
from a script instead of the keyboard. It takes the same commands, plus
`key Name` to press a key and `screen` to print the window as text:

```sh
CABBY_TUI_SCRIPT='type 1 Ada; key tab; key enter; screen' cabby greet.cby
```

`tui.md` (Appendix I) has the full list.

#### In short

* `use tui` is `use gui` in a terminal: same objects, same methods.
* Tab and the arrow keys move around, Enter presses, Esc closes.
* Sizes are in pixels (8 by 16 to a character); printing waits until the
  window closes.

#### Try this

* In `62-terminal-window.cby`, change `tui` to `gui` everywhere and run it
  again.
* Take `55-dialogs.cby` and change it to `tui`. What happens to the
  picture?

[← Chapter 61](#chapter-61-a-phrasebook) · [Contents](#contents) · [Chapter 63 →](#chapter-63-workers)

---

## Part X: Doing things at once

A program does one thing after another. A **thread** is another line of
work in the same program, going on at the same time: a long job done in
the background, or one job shared out over every core of the computer.
Threads are where programs most often go wrong in other languages, with
two of them changing the same thing at once; Cabby's ownership, and a
few rules of its own, keep that from happening.

### Chapter 63: Workers

*Program: `63-workers.cby`. Library: `use threads`; reference:
`lib/threads.md`.*

A **worker** is an object of yours, made from `threads.worker` as a window
is made from `gui.window`. Its fields hold what it's given and what it
works out, and its `work` method is its job:

```cabby
object prime_counter : threads.worker {
	int From                        // what it's given
	int Upto
	int Primes                      // what it works out

	void work {
		Primes = 0
		for int N = From; N < Upto; N++ {
			if is_prime N {
				Primes++
			}
		}
	}
	...
}
```

Fill in its fields, then `start` it: `work` runs on a thread of its own,
and the program carries on at once. `wait` waits until it's done, and then
its fields are yours again: read what it worked out.

```cabby
prime_counter Counters[4]
for I in 4 {
	Counters[I].From = I * 25000
	Counters[I].Upto = (I + 1) * 25000
	Counters[I].start               // off it goes; the loop carries on
}
for I in 4 {
	Counters[I].wait
	println Counters[I].Primes
}
```

| Call | What it does |
|---|---|
| `W.start` | runs `work` on a thread of its own; the program carries on |
| `W.wait` | waits until it's done |
| `W.is_done` | `true` once it's done (or before it's started), without waiting |
| `threads.cores` | how many cores the computer has: how many workers can work at the same moment |

A worker that's done can be started again, with new values in its
fields. Dropping one that's still working waits for it first.

**What keeps it safe.** In most languages, two threads can change the
same thing at once, and the program goes wrong only now and then, which
makes it hard to find. In Cabby they can't:

* **While it works, a worker is its own.** Using its fields or its
  methods from outside, between `start` and `wait`, is a panic: "that
  worker is still working...". Only `start`, `wait` and `is_done` can be
  used meanwhile.
* **No globals that can change.** Whatever `work` reaches, its own
  methods and the functions they call, can't use a global variable that
  can change; the checker says so, and where. Constants are fine. Give the
  worker what it needs in fields.
* **No handlers, and no destructor of its own**, as either would let a
  worker's thread reach something that isn't the worker's.

**Compiled and interpreted.** Compiled, workers run at the same moment,
one on each core: four can do a job in a quarter of the time. Interpreted,
they take turns, as Python's threads do (one thread at a time runs Cabby
code, and each lets the others in every so often, and whenever it waits):
the job takes as long, but the rest of the program carries on meanwhile.

**The tutorial** shares out counting the primes below 100,000 over four
workers, and has a fifth, of another kind, look for the longest chain in
the "3n + 1" game alongside them.

#### In short

* An object made from `threads.worker`: its fields are what it's given
  and what it works out, its `work` method is its job.
* `start` sets it going on a thread of its own, `wait` waits for it, and
  `is_done` asks; while it works, its fields are its own.
* Share a big job out among several, one for each of `threads.cores`.

#### Try this

* `println threads.cores`: how many can work at once here? Make `PARTS`
  that many, and compile it: is it quicker?
* Time it, with `time.now` before and after (Chapter 39).
* Read `Finder.Steps` before `Finder.wait`, and see what Cabby says.

[← Chapter 62](#chapter-62-windows-in-a-terminal) · [Contents](#contents) · [Chapter 64 →](#chapter-64-done-later)

---

### Chapter 64: Done later

*Program: `64-done-later.cby`, a window that counts primes. Libraries:
`use gui`, `use threads`.*

A window's handlers must be quick: while one runs, the window can't
redraw, or react to a click, or even be moved. A long job in a handler
freezes it. So the job goes to a worker, and the handler only starts it;
`on_done` names the window's method to call once it's done, as `on_click`
names one for a button:

```cabby
object counter_window : gui.window {
	gui.button Count
	gui.label Answer
	prime_counter Counter           // the worker belongs to the window

	void build {
		...
		Count.on_click start_counting
		Counter.on_done show_answer
	}

	void start_counting {
		Counter.Below = Limit.text.to_int
		Counter.start               // straight back to the window
		Count.set_enabled false     // (one count at a time)
		Answer.set_text "Counting..."
	}

	void show_answer {
		Answer.set_text Counter.Primes.text + " primes below " + Counter.Below.text
		Count.set_enabled true
	}
}
```

The handler is called on the main thread, from the window's own loop,
between its other events: so it can change the window, as any handler
can, and the worker's fields are the window's again. A game's loop calls
it between frames, and `events.run` waits for a worker with a handler as
it waits for a timer. `wait` calls it too, if the loop hasn't yet.

Note where the `println` is in the tutorial's `start_counting`: before
`start`. After it, until the worker's done, `Counter.Below` is the
worker's, and reading it would be a panic.

The tutorial's test script uses `processes`, which waits until what the
window's loop is watching (the worker, here) is done.

#### In short

* `on_done` calls a method when the worker's finished, from the event
  loop, so a window carries on while it works.
* A worker that's a window's field belongs to the window; `set_enabled
  false` keeps a button from being clicked twice.

#### Try this

* Type 5000000, click Count, and move the window about, or type in it,
  while it counts.
* Show how long it took, with `time.now` (Chapter 39).

[← Chapter 63](#chapter-63-workers) · [Contents](#contents) · [Chapter 65 →](#chapter-65-progress)

---

### Chapter 65: Progress

*Program: `65-progress.cby`, tutorial 64's window with a progress bar and
a Cancel button. Libraries: `use gui`, `use threads`.*

A worker can say how it's going while it works, and be asked to stop.
Neither goes through its fields, which are its own while it works:

* **`tell Text`**, inside `work`, sends a line to the main program: "37"
  for 37%, or a result as it's found. It's copied, so once it's sent it's
  the main program's. `W.on_message h` names the method that hears each
  line, in order, from the window's loop, as `on_done` names the one told
  it's done (after all its messages); in it, `W.message` is the line.
* **`W.ask_to_stop`** sets a flag that `work` looks at with `stopping`,
  every so often, and finishes early when it's set. Nothing is stopped by
  force, half way through something. Dropping a worker that's still
  working asks it to stop, too, before waiting for it.

```cabby
// in work: every thousand numbers
if N % 1000 == 0 {
	if stopping {
		Canceled = true
		return
	}
	tell ((long)N * 100 / Below).text
}

// in the window
void show_progress {
	Bar.set_fraction Counter.message.to_int / 100.0
}

void cancel {
	Counter.ask_to_stop
}
```

The window tells the worker how often to look by how it's written: every
thousand numbers here, which is often enough for Cancel to feel at once,
and not so often that looking slows the count.

#### In short

* `tell` sends the window a message as the worker goes, and `on_message`
  hears it, with `message`.
* `ask_to_stop`, and the worker's `stopping`, for a Cancel button.

#### Try this

* Type 20000000 and watch the bar; click Cancel part way.
* Tell more than the percent: "37% (4000 primes so far)".
* Close the window while it counts: dropping a worker asks it to stop
  too, and waits for it.

[← Chapter 64](#chapter-64-done-later) · [Contents](#contents) · [Chapter 66 →](#chapter-66-painting)

---

## Part XI: Pictures and videos

A canvas draws what it's told each time it's drawn. A picture keeps what
was drawn on it: its pixels, in memory, to draw on a few at a time, save,
show, and hand from a worker to a window. And the `videos` library opens
video files, as `sound` opens sound files, and `gui` shows them.

### Chapter 66: Painting

*Program: `66-painting.cby`, the Mandelbrot set, painted by a worker.
Libraries: `use gui`, `use threads`, `use images`.*

`images.new 360, 20` makes a picture of 360 by 20 pixels, transparent to
begin with, and `set_pixel_rgb X, Y, Red, Green, Blue` colors one pixel.
It's the quickest way to fill a picture a pixel at a time, as the
Mandelbrot set is: each pixel is a sum, done over and over until it runs
off past 2 (up to 200 times), and how many times it took is the pixel's
color. Shapes and colors by name are there too, as a canvas has them:

<!-- run -->
```cabby
use images

int main {
	images.picture Flag = images.new 90, 60
	Flag.fill_with "white"
	Flag.set_color "crimson"
	Flag.fill_circle 45, 30, 18
	Flag.save "flag.png"
	println Flag.red(45, 30), Flag.green(45, 30), Flag.red(0, 0)
}
```

```text
220 20 255
```

A worker can make pictures, as it can make anything of its own, and
`tell_picture` hands one to the main program's `on_message` handler, as
`tell` hands a line (chapter 65). The picture is moved, not copied: its
pixels stay where they are, and only its number goes across, so a
picture of any size is handed on at once. The worker's picture is empty
afterwards. In the handler, `has_picture` says it's a picture rather than
a line, and `picture` takes it:

```cabby
void strip_done {
	if !Painter.has_picture {
		return
	}
	images.picture Strip = Painter.picture
	Whole.draw Strip, 0, Strips * STRIP
	Strips++
	Canvas.redraw
}
```

`Whole.draw Strip, X, Y` puts the strip onto the picture of the whole, at
its place, and the canvas's `on_draw` handler shows the whole with
`Canvas.picture 0, 0, Whole`. A window keeps what it's shown of each
picture, and sends its pixels to the screen again only when they've
changed: showing a picture on every redraw costs no more than showing a
picture from a file.

Why not draw on the window from the worker? A window is the main
program's: on a Mac, only the program's first thread may touch one, and
in Cabby, a worker's own fields are all it may use while it works
(chapter 63). A picture is the worker's while it draws on it, and the
window's once it's handed on: never both at once, so neither needs to
wait for the other.

#### In short

* `images.new Width, Height`, `set_pixel_rgb`, and the shapes and colors
  of a canvas, on a picture in memory; `save` writes it as a PNG.
* A worker paints, and `tell_picture` hands the window each strip as it's
  done, for a canvas to `picture`.

#### Try this

* Paint it with more workers: give each a part of the rows, and the
  window puts each strip where its Top says (tell it in a line first).
* Zoom in: change Re and Im's ranges to a corner of the set, and MOST to
  1000.
* Show it while it's being painted a row at a time: tell_picture each
  row instead of each strip.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**One pixel, and changed copies.** `set_pixel` paints one in the color
for drawing; `scaled_to_fit`, `scaled_by`, `cropped`, `mirrored`,
`flipped` and `blurred` each give a new picture, changed, and leave the
first as it was:

<!-- run -->
```cabby
use images

int main {
	images.picture Photo = images.new 400, 300
	Photo.fill_with "white"
	Photo.set_color "navy"
	Photo.set_pixel 0, 0
	println Photo.red(0, 0), Photo.green(0, 0), Photo.blue(0, 0)
	images.picture Small = Photo.scaled_to_fit 100, 100
	println Small.width, Small.height
	println Photo.scaled_by(0.5).width, Photo.cropped(10, 10, 50, 20).width
	println Photo.mirrored.blue(399, 0), Photo.flipped.blue(0, 299)
	println Photo.blurred(2).width, Photo.is_empty
	images.picture Nothing
	println Nothing.is_empty
}
```

```text
0 0 128
100 75
200 50
128 128
400 false
true
```

[← Chapter 65](#chapter-65-progress) · [Contents](#contents) · [Chapter 67 →](#chapter-67-a-video-player)

---

### Chapter 67: A video player

*Program: `67-video-player.cby`, a window that plays a video, given one or not. Library: `use videos`, with `use gui`.*

A video is opened, and asked what's in it:

```cabby
Film = videos.open Path
println Film.width, "by", Film.height, "for", Film.length, "seconds"
```

It's read with FFmpeg, which every video player on Linux is built on, so
it can be nearly anything: an MP4 from a phone, an MKV, a WebM from the
web. `picture_codec` and `sound_codec` say how its picture and sound are
kept (`"h264"` and `"aac"` for most MP4s), `title` gives its title, if
the file has one, and `has_sound` whether it has any.

A window shows it with `add_video`, given the video's `source`:

```cabby
Screen = add_video Film.source
Screen.show_controls
Screen.on_end finished
```

The widget is only what shows it. The video is what plays, pauses and
moves (`Film.play`, `Film.pause`, `Film.seek 30`), and `Film.position`
says where it's got to. The controls are a play button, a track to click
anywhere along, and the time; clicking the picture plays or pauses it,
and space and the arrow keys work too. `on_end` runs a method when it
comes to its end; `Film.set_looping true` would play it round again
instead.

The picture showing now is saved with `picture_at`, which gives an
`images.picture` ([Chapter 66](#chapter-66-painting)):

```cabby
images.picture Picture = Film.picture_at Film.position
Picture.save "frame.png"
```

The same makes thumbnails, without a window at all: `picture_at` for a
few times through it, each saved.

The program plays its own short test video (`videos/colors.mkv`, three
seconds of colors, with a beep as each one starts), or the file named
after it:

```text
cabby 67-video-player.cby ~/Videos/lesson.mp4
```

The picture and sound are kept together by the clock. The sound plays as
the computer's sound asks for more, and the picture shown is always the
one for now, so a slow computer drops pictures rather than falling
behind.

#### In short

* `videos.open Path`: a video's size, length and codecs; `picture_at`
  for one of its pictures.
* `add_video` shows it in a window, with `show_controls`; `play`,
  `pause`, `seek` and `on_end`.

#### Try this

* A button that plays it at the start again: Film.rewind.
* Film.set_looping true, for a video that plays round and round.
* A slider for its volume: Film.set_volume, from 0 to 100.
* Thumbnails: a picture from every 10 seconds of it, with picture_at,
  saved as "thumb-1.png", "thumb-2.png" ...

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Its frame rate, playing, looping, and its sound.** A video plays by its
own clock even without a window; `play_pause` plays it if it's paused and
pauses it if it's playing, and `save_sound` saves its sound as a WAV file:

<!-- run -->
```cabby
use videos
use files

int main {
	videos.video Film = videos.open "videos/colors.mkv"
	println Film.frame_rate, "pictures a second"
	Film.set_looping true
	println Film.looping
	Film.play
	Film.play_pause
	println Film.playing
	Film.play_pause
	println Film.playing
	println Film.save_sound("cabby-sound.wav"), files.size("cabby-sound.wav") > 0
	files.delete "cabby-sound.wav"
}
```

```text
10.0 pictures a second
true
false
true
true true
```

A `gui.video`'s controls are hidden to begin with; `show_controls` shows
them and `hide_controls` hides them again.

[← Chapter 66](#chapter-66-painting) · [Contents](#contents) · [Chapter 68 →](#chapter-68-sprites-and-frames)

---

## Part XII: Games

The `game` library: a window with a stage, sprites that move on it, and
the keys read sixty times a second.

Every program in this part also runs in a terminal: change `use game` to
`use tgame`, and `game.` to `tgame.`, and the stage is shown in the
terminal with half-block characters, two pixels to each, with what's
written on it as real text. It's the same library, shown another way, as
`tui` is for `gui`; `tgame.md` says what's different (mostly, which
terminals say when a key is let go). Ctrl+C or Ctrl+Q ends the game.

### Chapter 68: Sprites and frames

*Program: `68-games.cby`. Library: `use game`.*

A game is a window with a **stage**, and **sprites** on it: pictures or
shapes that move. A `game.window` is built the same way as a
`gui.window`, in `build`:

<!-- gui: frames 30; close -->
```cabby
use game

object sky : game.window {
	game.sprite Sun
	game.sprite Plane

	void build {
		set_title "Sky"
		set_size 400, 240
		set_background "skyblue"
		Sun = add_circle 30, "gold", 340, 50
		Plane = add_rectangle 50, 12, "white", 0, 120
		on_frame fly
	}

	void fly {
		Plane.move 4, 0
		if frame % 10 == 0 {
			println "frame", frame, "the plane is at", Plane.x
		}
	}
}

int main {
	sky s
	s.run
}
```

```text
frame 10 the plane is at 40.0
frame 20 the plane is at 80.0
frame 30 the plane is at 120.0
```

`set_size` is the stage's size, and the window fits it. `add_circle` and
`add_rectangle` make shape sprites; `add_sprite "plane.png", X, Y` makes
one from a picture. A sprite's position is its **middle**, in pixels from
the stage's top left corner, with y counting down.

#### Frames

There's no loop in the program. The game runs in **frames**, 60 a
second: for each one it calls `on_frame`'s handler, which moves things a
little, and then draws the stage again. Moving 4 pixels a frame is 240
pixels a second. `frame` counts the frames so far.

A sprite can `move` (by so much across and down), `move_to` a place,
`turn` and `set_angle` (degrees clockwise), `move_forward` the way it's
facing, `set_scale`, `set_visible`, and `remove` itself from the stage
for good. `x`, `y`, `angle`, `width` and `height` say where and how big
it is.

#### Keys

A game reads the keys each frame, three ways:

| Write | True |
|---|---|
| `key_press "space"` | in the frame the key went down: jump, fire, choose |
| `key_hold "left"` | in every frame it's held down: walk, steer |
| `key_release "space"` | in the frame it was let go |

and `key_held_for "space"` says for how many milliseconds it has been
held (in the frame it's let go: how long it was held), for a power-up that
grows the longer it's held:

<!-- gui: frames 5; hold space; frames 45; release space; frames 2; close -->
```cabby
use game

object charge : game.window {
	void build {
		set_size 200, 100
		on_frame step
	}

	void step {
		if key_press "space" {
			println "charging..."
		}
		if key_release "space" {
			int Power = key_held_for("space") / 100
			println "fire! power", Power
		}
	}
}

int main {
	charge c
	c.run
}
```

```text
charging...
fire! power 7
```

The keys belong to the game wherever the window's focus is, but Tab, keys
with Ctrl or Alt, and the window's own shortcuts still do what they do in
any window.

#### Timers, pausing and chance

`every` and `after` work as in a gui window, but they count frames, so
they keep in step with the game, and they wait while it's paused. `pause`
stops the frames and timers (the stage is still drawn) until `resume`;
`paused` says which. A shortcut still works while a game is paused, so
the tutorial's P key, made with `add_shortcut "P", pause_or_resume`,
pauses and resumes it.

`random 1, 6` gives a whole number from 1 to 6, both included, for
where an apple falls or what a dice shows.

#### Drawing over the stage

`stage` is the canvas the game is drawn on. The background and the
sprites are drawn first, then the stage's own `on_draw` handler, so a
score drawn there is on top of everything:

```cabby
stage.on_draw draw_score            // in build

void draw_score {
	stage.set_color "black"
	stage.write 10, 8, "Caught: " + Caught.text
}
```

#### Testing a game

A test script can play a game. `frames 60` runs 60 frames; `hold left`
and `release left` press and let go of a key; `tap space` is a key down
for one frame. While a script runs, frames only run when it says, and
`random` gives the same numbers every time, so the game goes the same way
every time. `68-games.cby` is tested like that.

#### In short

* `object g : game.window`; in `build`: `set_size`, `set_background`,
  `add_sprite`, `add_rectangle`, `add_circle`, `on_frame`.
* Each frame: `key_press`, `key_hold`, `key_release`, `key_held_for`;
  move the sprites.
* `every`, `after`, `pause`, `resume` and `random`; `stage.on_draw` draws on
  top.

#### Try this

* In `68-games.cby`, make the apples fall faster as more are caught.
* Make a sprite that turns with the left and right keys and moves forward
  with the up key, as a spaceship does.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Bigger, hidden, gone, and in front.** A sprite can be drawn bigger,
hidden and shown, taken off the stage for good, and put in front of the
others or behind them:

<!-- gui: frames 3; close -->
```cabby
use game

object stage_show : game.window {
	game.sprite Sun
	game.sprite Cloud

	void build {
		set_size 300, 200
		Sun = add_circle 30, "gold", 100, 80
		Cloud = add_rectangle 80, 30, "white", 90, 90
		Sun.to_front                    // the sun over the cloud
		Cloud.set_scale 2
		println Cloud.scale
		Cloud.set_visible false
		println Cloud.visible
		Cloud.set_visible true
		Sun.to_back
		Sun.remove
		println Sun.removed, Cloud.removed
		close
	}
}

int main {
	stage_show S
	S.run
}
```

```text
2.0
false
true false
```

[← Chapter 67](#chapter-67-a-video-player) · [Contents](#contents) · [Chapter 69 →](#chapter-69-a-walking-character)

---

### Chapter 69: A walking character

*Program: `69-walking.cby`, with its pictures in `pictures/`. Library: `use game`.*

In the old handheld adventure games, the world is a grid of **tiles** and
the hero walks from one to the next, legs moving as they go. All it takes
is three things from the last chapter, used together: sprites made from
pictures, `animate`, and counting frames.

#### Walking pictures

A walk is a few pictures shown in turn. The hero in `69-walking.cby` has
three for each way they can face: standing, left foot forward, and right
foot forward.

| | 1 | 2 | 3 |
|---|---|---|---|
| down | `hero-down-1.png` | `hero-down-2.png` | `hero-down-3.png` |
| up, left, right | likewise | | |

`animate` shows a list of pictures in turn, each for so many
milliseconds, round and round, until `set_picture` stops it. Standing
between the steps makes the walk: standing, one foot, standing, the
other foot.

```cabby
str Name = "pictures/hero-" + Facing
Hero.animate {Name + "-1.png", Name + "-2.png", Name + "-1.png", Name + "-3.png"}, 130
```

and to stand still, facing the same way:

```cabby
Hero.set_picture "pictures/hero-" + Facing + "-1.png"
```

`animate` starts its pictures from the beginning each time it's called,
so it's called once when the hero sets off or turns, not every frame.
Water and flowers use it too, more slowly, so the town moves a little
while nobody walks.

#### A map of tiles

The town is a list of strings, a character for each tile:

```cabby
Map = {
	"TTTTTTTTTTTTT",
	"T...f....~~~T",
	"T.=====..~~~T",
	"T.=S..=f....T",
	...
}
```

`Map[Row][Col]` is one tile, a `char`. At the start, `build` goes through
them all and adds a sprite for each (a tree, water, path, grass); the
hero is added last, so it's drawn over them. Asking the map is also how
the game knows what's in the way:

```cabby
bool is_free int C, int R const {
	char Tile = Map[R][C]
	return Tile != 'T' && Tile != '~' && Tile != 'S'
}
```

#### Tile by tile

A step goes from the middle of one tile to the middle of the next: 48
pixels, 3 a frame, so 16 frames. `StepsLeft` counts them down. While it's
above 0, each frame moves the hero on, whatever the keys say; so a step,
once started, always ends on a tile. Only on a tile does the game look
at the arrow keys, with `key_hold`, to start the next step: holding a key
walks on, tile after tile, and letting go stops on the next tile.

```cabby
void step {
	if StepsLeft > 0 {
		Hero.move Dx * SPEED, Dy * SPEED
		StepsLeft--
		if StepsLeft > 0 {
			return
		}
		Col += Dx                   // arrived
		Row += Dy
	}
	// on a tile: key_hold "up", "down" ... starts the next step
}
```

A key toward something in the way turns the hero to face it, without a
step. Facing the sign and pressing Space (`key_press`) shows its message,
drawn over everything in the stage's `on_draw`, until Space is pressed
again.

#### Pictures of your own

`tools/make-town-pictures.py` drew the pictures: 16 by 16 pixel art, made
three times bigger. Any drawing program that saves PNG files will do.
Make every picture of a walk the same size, with the character standing
in the same place in each, and transparent round the edges.

#### In short

* `animate {...}, Ms` shows pictures in turn; `set_picture` stops it.
  Call it when a walk starts or turns, not every frame.
* A list of strings makes a map, a character for each tile; asking it says
  what's in the way.
* Count a step's frames down, and only look at the keys between steps, to
  walk tile by tile.

#### Try this

* Hold B to run: twice the speed, and a shorter time for each picture.
* Add a second sign with its own message.
* Make a door: a tile that, walked onto, moves the hero somewhere else on
  the map.

[← Chapter 68](#chapter-68-sprites-and-frames) · [Contents](#contents) · [Chapter 70 →](#chapter-70-touching)

---

### Chapter 70: Touching

*Program: `70-touching.cby`. Library: `use game`.*

Games are full of things meeting: a bullet and a rock, a basket and an
apple, a hero and a wall. `A.touches B` says whether two sprites touch:

<!-- gui: frames 40; close -->
```cabby
use game

object meeting : game.window {
	game.sprite Left
	game.sprite Right

	void build {
		set_size 300, 100
		Left = add_circle 20, "tomato", 40, 50
		Right = add_rectangle 40, 40, "steelblue", 260, 50
		on_frame step
	}

	void step {
		if Left.touches Right {
			println "they touch at frame", frame
			pause
			return
		}
		Left.move 3, 0
		Right.move -3, 0
	}
}

int main {
	meeting m
	m.run
}
```

```text
they touch at frame 32
```

A circle counts as its circle, a rectangle as its box, and a picture as
the box round its pixels that show (so a character's see-through edges
don't count). `left`, `top`, `right` and `bottom` give that box's edges.

#### Groups

Keeping a list of every rock, and taking each out of the list when it's
shot, is a chore (chapter 68's apples needed one). A **group** does it
instead. `Rock.set_group "rock"` puts a sprite in one; then

* `sprites_in "rock"` is every rock on the stage, and
* `Bullet.touching "rock"` every rock that bullet touches.

So the apples of `68-games.cby` need no list at all:

```cabby
void drop_apple {
	game.sprite Apple = add_circle 10, "red", random(20, WIDTH - 20), -10
	Apple.set_group "apple"
}

void step {
	for Apple in sprites_in "apple" {
		Apple.move 0, APPLE_SPEED
		if Apple.touches Basket {
			Caught++
			Apple.remove
		} else if Apple.top > HEIGHT - 30 {
			Missed++
			Apple.remove
		}
	}
}
```

Each is a new list, made when it's asked for, so removing sprites while
going through it is safe. And though `for` gives read-only elements, a
sprite is a handle to what's on the stage, so a read-only one can still
move, turn and be removed.

#### Off the stage

`on_stage` says whether any of a sprite can be seen. A bullet that has
flown off the edge is done with:

```cabby
if !Bullet.on_stage {
	Bullet.remove
}
```

#### Turning and moving forward

`70-touching.cby` steers a ship: `turn` turns it (degrees, clockwise),
and `move_forward` moves it the way it faces. A bullet starts where the
ship is, facing the same way, and flies on with `move_forward` too.

#### In short

* `A.touches B`; `left`, `top`, `right`, `bottom`; `on_stage`.
* `set_group "rock"`, then `sprites_in "rock"` and
  `Sprite.touching "rock"`.
* Removing sprites while going through those lists is safe.

#### Try this

* In `70-touching.cby`, make big rocks break into two small ones.
* Rewrite `68-games.cby` with a group, as above, and take out its list.

[← Chapter 69](#chapter-69-a-walking-character) · [Contents](#contents) · [Chapter 71 →](#chapter-71-a-bigger-world)

---

### Chapter 71: A bigger world

*Program: `71-exploring.cby`. Library: `use game`.*

Chapter 69's town fits on the stage. A world to explore is bigger than
the screen, and scrolls as the hero walks. Two lines do it:

```cabby
set_world_size COLUMNS * TILE, ROWS * TILE      // the whole world: 32 tiles by 22
follow Hero                                     // the camera keeps the hero in the middle
```

`set_size` is still the stage's size, what's seen at once. The world is
everything, and the **camera** decides which part the stage shows.
Following the hero, it keeps them in the middle; near an edge of the
world it stops, so it never shows past it, and the hero walks on toward
the edge instead.

Sprites are always placed in the world: a tree at tile 30 is at x = 30 ×
48 + 24, wherever the camera is, and `x`, `y`, `touches` and the rest all
use world positions. Nothing in the game needs to know about scrolling.

#### What stays put

What the stage's `on_draw` draws isn't moved by the camera: a score, a
message box or a "where am I" stays in its place on the stage while the
world goes by. `71-exploring.cby` writes the hero's tile in the corner
that way. To draw at a sprite's place instead, take the camera away:
`Hero.x - camera_x`.

#### Moving the camera yourself

`move_camera_to X, Y` shows the world from there (the point at the
stage's top left corner), and stops following; `follow` starts again.
`camera_x` and `camera_y` say where it is.

#### Many signs

The bigger world has three signs, each with its own message. A lookup
holds them, by where each sign is:

```cabby
Signs = {
	"3,3": "Cabby Town: the lake is to the east.",
	"26,7": "The lake. No swimming!",
	"21,15": "You found the last sign. Well explored!"
}
...
Message = Signs[C.text + "," + R.text].copy
```

#### In short

* `set_world_size` for a world bigger than the stage; `follow Hero`.
* Sprites live in the world; `stage.on_draw` draws on the stage.
* `move_camera_to`, `camera_x`, `camera_y`.

#### Try this

* In `71-exploring.cby`, add a fourth sign somewhere hard to reach.
* Draw a small map in a corner: a dot for each tree, and one for the hero.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**The world's size, and a camera put somewhere.** `world_width` and
`world_height` say how big the world is, `world_left` and `world_top`
where it starts; `move_camera_to` shows it from a place of your choosing:

<!-- gui: frames 3; close -->
```cabby
use game

object map : game.window {
	void build {
		set_size 320, 240
		set_world_area -100, -50, 1000, 600
		println world_left, world_top, world_width, world_height
		move_camera_to 400, 200
		println camera_x, camera_y
		close
	}
}

int main {
	map M
	M.run
}
```

```text
-100 -50 1000 600
400.0 200.0
```

[← Chapter 70](#chapter-70-touching) · [Contents](#contents) · [Chapter 72 →](#chapter-72-sound)

---

### Chapter 72: Sound

*Program: `72-sound.cby`, a keyboard to play. Library: `use sound`.*

The sound library plays sound files, music, notes and MIDI files. Each
starts playing and the program goes straight on, so everything plays at
once, mixed together:

<!-- run -->
```cabby
use sound

int main {
	sound.play_note "C4", 1000                  // middle C, for a second
	sound.play_notes "E4 G4 C5", 300            // and a tune over it
	println "playing:", sound.busy
	sound.wait                                  // until they've finished
	println "playing:", sound.busy
}
```

```text
playing: true
playing: false
```

A program that only plays sounds needs `sound.wait` at the end: when the
program ends, so does its sound. A window's program goes on until the
window closes, so it doesn't.

#### Sound files

```cabby
sound.play "sounds/laser.wav"              // once: a sound effect
sound.play_music "theme.mp3"               // round and round, until sound.stop_music
```

WAV, MP3 and FLAC files all work. `sound.play` can play the same sound
again before it has finished, over itself, as a game's effects do:
`70-touching.cby` now plays a laser, a bang and a crash. For a sound to
pause, loop, make quieter or skip through, load it:

```cabby
sound.track Song = sound.load "song.mp3"
Song.set_volume 0.5
Song.play
...
println Song.position, "of", Song.seconds, "seconds"
Song.pause
```

#### Notes

Notes are named as on a stave: the letter, `#` for a sharp or `b` for a
flat, and the octave. `"C4"` is middle C, and `"A4"` the A above it, at
440 Hz.

| Write | Plays |
|---|---|
| `sound.play_note "A4", 500` | a note, for half a second |
| `sound.play_chord {"C4", "E4", "G4"}, 1000` | the notes together |
| `sound.play_notes "E4 D4 C4 - E4 D4 C4", 400` | one after another (`-` is a rest) |
| `sound.start_note "C4"` ... `sound.stop_note "C4"` | a note for as long as the program likes |

`72-sound.cby` is an octave of a keyboard on the computer's keys. Each
note starts when its key goes down (`key_press` in a game) and stops when
the key comes up (`key_release`), as on a real keyboard.

#### Instruments

`sound.set_instrument "violin"` changes what the notes are played with:
`"piano"`, `"guitar"`, `"violin"`, `"cello"`, `"flute"`, `"trumpet"`,
`"choir"`, `"drums"` and more (`lib/sound.md` has them all), or a General
MIDI number from 1 to 128.

The instruments are recordings in a **SoundFont**, a `.sf2` file. Linux
usually has one installed, or can (`soundfont-fluid` on Arch,
`fluid-soundfont-gm` on Debian), and sound finds it by itself;
`sound.set_soundfont "my.sf2"` uses another. Without one, Cabby's own
simple synthesizer plays the notes instead, every instrument alike.

#### MIDI files

```cabby
sound.play_midi "song.mid"
```

plays a MIDI file with the SoundFont's instruments, each part on the
instrument the file asks for. `sound.stop_midi` stops it.

#### Everything at once

`sound.set_volume 0.5` makes everything half as loud, `sound.stop` stops
everything, `sound.busy` says whether anything is playing, and
`sound.wait` waits for it to finish.

#### In short

* `use sound`; playing never waits; end with `sound.wait` if nothing else
  keeps the program going.
* `sound.play` for effects, `sound.play_music` for music, `sound.load`
  for a `sound.track` to control.
* `play_note`, `play_chord`, `play_notes`, `start_note` and `stop_note`,
  with `set_instrument`.
* `sound.play_midi` for MIDI files.

#### Try this

* In `72-sound.cby`, add a second octave below, on the keys Z X C V ...
* Play the first line of a tune you know with `play_notes`, then its
  harmony with `play_chord`.
* Add sounds to `68-games.cby`: `sounds/catch.wav` and `sounds/miss.wav`
  are there for it.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it. Sound can't be heard
in a book, so these are checked rather than run.*

**How loud, and how notes sound.** Effects, music and notes each have a
volume of their own; a SoundFont gives notes and MIDI files the sound of
real instruments; `crossfade_music` brings in other music while the old
fades:

<!-- check -->
```cabby
use sound
use time

int main {
	sound.set_effects_volume 0.5
	sound.set_music_volume 0.8
	sound.set_note_volume 0.6
	println sound.effects_volume, sound.music_volume
	sound.set_soundfont "/usr/share/soundfonts/FluidR3_GM.sf2"
	sound.play_music "music/woods.mid"
	time.sleep time.seconds(3)
	sound.crossfade_music "music/town.mid", 2000
	time.sleep time.seconds(3)
}
```

**Another kind of file.** `convert_to_wav` saves an MP3, FLAC or Ogg
file again as a WAV, at the rate and channels asked for:

<!-- check -->
```cabby
use sound

int main {
	sound.convert_to_wav "song.mp3", "song.wav", 44100, 2
}
```

[← Chapter 71](#chapter-71-a-bigger-world) · [Contents](#contents) · [Chapter 73 →](#chapter-73-a-world-in-files)

---

### Chapter 73: A world in files

*Program: `73-world-in-files.cby`, with `maps/woods.txt` and
`maps/town.txt`. Libraries: `use game`, `use files`.*

Chapter 71's world was written into the program, as a list of strings.
A world worth exploring has many areas, and they're easier to make, and
change, as files of their own: here, one for the woods and one for the
town, in `maps/`. The program reads whichever area the hero is in.

#### A map file

Each file is the map, a character for each tile, then a line of `---`,
then a line for each thing in the area:

```
TTTTTTTT=TTTTTTTTTTTTTTT
T.......=..............T
T..HHH..=......HHH.....T
T..HDH..=......HDH.....T
...
---
sign 16,8 Cabby Town. The woods are to the north.
way 8,0 woods 8,12
person 11,3 elder down Salve, amice! Welcome to Cabby Town.
```

A `sign` says what it says; a `way` out is a tile that takes the hero to
another area (step on 8,0, and you're in the woods, on tile 8,12); a
`person` has a picture, a way they face, and something to say. Lines
starting `//` are notes for whoever edits the map, and are skipped.

Reading it is a loop over the file's lines, from chapter 32's files
library:

```cabby
for Line in files.lines "maps/" + Area + ".txt" {
	if Line.starts_with("//") || Line.trim == "" {
		continue
	}
	if Line == "---" {
		InMap = false
	} else if InMap {
		Map.add Line.copy
	} else {
		add_thing Line
	}
}
```

and each thing is its words: `Line.split " "` gives `{"sign", "16,8",
"Cabby", "Town." ...}`, and `Words[2 to Words.length - 1].join " "` puts
what the sign says back together.

#### Going from one area to the next

When a step ends on a way out, `go_to` takes everything off the stage
(`for Old in sprites_in "" { Old.remove }`), reads the next area, makes
the world its size, and puts the hero on the tile the way leads to. The
camera follows the new hero; nothing else needs to know.

#### In front, and behind

Here a tree is taller than a tile: its trunk stands on its own tile, and
its leaves reach up over the tile above. A house is three tiles square:
its walls are on the bottom two rows, and its roof rises over the row
behind them. So a hero standing just north of a tree should be behind its
leaves, and one walking behind a house hidden by its roof; south of them,
in front.

Drawing in the order the sprites were added can't do that, since the hero
walks around them. Two things can:

```cabby
sort_by_depth STANDING          // in that layer, lower on the stage is in front
Tree.set_layer STANDING         // and the same for the hero, houses and people
```

* **Layers.** Every sprite is in a layer, 0 to begin with, and lower
  layers are drawn first. The ground (grass, paths, water) stays in layer
  0, so it's always under everyone; the things that stand on it go in
  layer 1. A layer above that would suit what's over everything: clouds,
  birds, a roof you walk under.
* **Sorting by depth.** In a layer sorted by depth, whatever is lower on
  the stage (its bottom edge, where it touches the ground) is drawn in
  front. It's worked out every frame, so as the hero walks up past a
  tree, the hero goes behind it at just the right moment.

`to_front` and `to_back` still work, within a sprite's layer.

#### People who turn to you

Facing someone and pressing Space finds them by where they stand, turns
them to face the hero (the opposite way to the hero's), and shows what
they say:

```cabby
People[Who].Sprite.set_picture "pictures/" + People[Who].Picture + "-" + opposite(Facing) + ".png"
Message = People[Who].Words.copy
```

Each person is a small object, `townsperson`, with their sprite, their
picture's name, where they stand and what they say, kept in a `list
townsperson`. Where people stand can't be walked through.

#### In short

* A map in a file: read its lines with `files.lines`, split each with
  `split`, and put words back together with `join`.
* An area at a time: remove every sprite, read the next one, build it.
* `Sprite.set_layer N` and `sort_by_depth N`: the ground under everything,
  and what stands on it lower in front.

#### Try this

* Add a person of your own to `maps/town.txt`, and a sign to the woods.
* Make a third area, a beach, with a way to it from the town.
* Let people say more than one thing: a list of what they say, and the
  next one each time Space is pressed.

[← Chapter 72](#chapter-72-sound) · [Contents](#contents) · [Chapter 74 →](#chapter-74-songs)

---

## Part XIII: Sound and music

Chapter 72 played notes as a game went along. This part goes further:
songs written down and saved as MIDI files, which other music programs
can open; recordings from a microphone or an audio interface, saved as WAV
files; and a MIDI keyboard heard through the computer as it's played.

### Chapter 74: Songs

*Program: `74-songs.cby`, a twelve bar blues. Library: `use sound`.*

A **part** is one instrument's notes. Each note goes after the last, and
how long it lasts is in **beats**, not milliseconds; the **song** the
parts are put in has a tempo, which says how fast the beats go.

<!-- run -->
```cabby
use sound

int main {
	sound.part Tune
	Tune.set_instrument "flute"
	Tune.notes "E4 D4 C4:2 - E4 D4 C4:2", 1     // a beat each, C4:2 two; - is a rest
	Tune.chord {"C4", "E4", "G4"}, 4

	sound.song Song
	Song.set_tempo 100                          // beats a minute
	Song.add Tune
	println Song.beats, "beats,", Song.seconds, "seconds"
	println Song.Parts[0].Notes[0].name, Song.Parts[0].Notes.length
}
```

```text
13.0 beats, 7.8 seconds
E4 9
```

`Song.save "tune.mid"` saves it, and `Song.play` (or
`sound.play_song Song`) plays it, as `sound.play_midi` plays a file.

#### Writing a part

| Write | Adds |
|---|---|
| `Part.note "C4", 2` | a note, two beats long, after the last one |
| `Part.notes "E4 D4 C4:2", 1` | notes one after another, a beat each, unless one says otherwise after a colon; `-` is a rest |
| `Part.chord {"C4", "E4", "G4"}, 4` | notes together |
| `Part.rest 1` | a beat of silence |
| `Part.note_at "C2", 8, 0.5` | a note at beat 8 (0 is the start), whatever has gone before |

A note is named as everywhere in sound (`"C4"`, `"F#3"`, `"Bb5"`), or
given by its MIDI number as text: `"60"` is middle C, so
`(Root + 7).text` is the note seven half steps above `Root`. The blues
works out its bass and chords that way, from the chords' roots.

`set_instrument` takes the instruments tutorial 72 did; with `"drums"`,
each note is a drum (`"C2"` the bass drum, `"D2"` the snare, `"F#2"` a
hi-hat). Drums play on top of each other, so the blues puts them in with
`note_at`. `set_volume` (0 to 1) sets how loud the notes after it are,
and `set_held` how much of their time they're held: 1, to begin with,
joins each note to the next, and 0.9 leaves a little gap, as a player
does.

#### The song

`set_tempo` (120 to begin with) and `set_time_signature 3, 4` (4/4 to
begin with) say how it's played and how a music program shows its bars.
`add` moves a part into the song: afterwards, it's `Song.Parts[0]`,
`Song.Parts[1]` and so on, in the order they were added, each with its
`Notes`. A note is a `sound.note`: its `Key` (the MIDI number), `Beat`,
`Beats` and `Volume`, and `name`.

#### Reading a MIDI file

`sound.read_song "song.mid"` reads a MIDI file, from Cabby or anywhere
else, into a song: a part for each instrument in it, with its notes and
the file's tempo. Change it and save it again: `transpose 7` puts every
part but the drums up seven half steps, from A into E, and `set_tempo 70`
slows it down.

#### Music for the Quest

`tools/make-music.cby` writes Cabby Quest's three tunes this way: a
melody and a bass each, as notes and beats. Change a tune and run it
again (`cabby tools/make-music.cby`, from Cabby's own folder), and the
game plays the new one.

#### In short

* A `sound.part` is one instrument's notes, in beats; a `sound.song` puts
  parts together, with a tempo.
* `save` writes a MIDI file; `sound.read_song` reads one back; `transpose`
  moves it up or down.

#### Try this

* Open `blues.mid` in a music program, and see the score.
* Make it a slow blues: `Blues.set_tempo 70`.
* Write a tune of your own: `"E4 D4 C4:2 - G4:0.5 G4:0.5"`, where `:2` is
  two beats and `-` a rest.

[← Chapter 73](#chapter-73-a-world-in-files) · [Contents](#contents) · [Chapter 75 →](#chapter-75-recording)

---

### Chapter 75: Recording

*Program: `75-recording.cby`, a recorder. Libraries: `use sound`, `use gui`.*

`sound.record` starts recording, from the computer's usual input, and
gives a `sound.recording`. It records until `stop`, into memory; then
`save` keeps it as a WAV file, and `play` plays it:

```cabby
sound.recording Take = sound.record
time.sleep time.seconds(5)
Take.stop
Take.save "take-1.wav"
println Take.seconds, "seconds"
```

The recorder is a window, so it can't sleep while it records: Record
starts a recording, and Stop stops it, whenever it's pressed. Meanwhile a
timer, every 50 milliseconds, shows `Take.level` in a progress bar. The
level is how loud the input is, from 0 to 1; a meter near 1 means it's
too loud, and the take will be distorted, so turn the input down.

#### Inputs

`sound.inputs` lists what can be recorded from by name: a built-in
microphone, a headset, an audio interface. The recorder puts them in a
drop-down, and `sound.record_from Inputs.text` records from the chosen
one. A part of the name is enough: `sound.record_from "Scarlett"`.

A recording is mono to begin with, which is right for one microphone or
one guitar; `sound.set_recording_channels 2` makes the next ones stereo.
It's saved with 24 bits, as an audio interface records;
`sound.set_recording_bits 16` saves them as a CD is, in half the space.

#### In time with a click

With Click ticked, the recorder plays a metronome as it records: a song,
as in chapter 74, of a wood block a beat, made at the tempo in the box.
`Metronome.play` starts it, and `sound.record` straight afterwards. What
comes out of the speakers is a moment behind what the program plays, and
what goes into the microphone is a moment late coming in; sound moves the
recording earlier by both, so it lines up with the click, or any backing
played with it, from the start. A take dropped into a music program
beside the click is in time. (If it's still a little early or late on a
computer, `sound.set_recording_delay` sets the delay in milliseconds.)

#### Tidying a take

`trim_silence` cuts off the quiet before the first note and after the
last, as the recorder does with its check box ticked; the take in the
tests goes from 2.4 seconds to 1.8. The recorder says how much it cut
off, by asking `seconds` before and after: "take 2: 1.8 seconds (0.6 cut
off)".

#### Hearing it as it comes

An audio interface usually lets its player hear themselves as they record
(its "direct monitoring" knob). A computer's own microphone socket
doesn't, so the singer hears nothing but the click. `sound.set_monitoring
true` sends what's coming in to the speakers or headphones, mixed with
what's playing, a few milliseconds behind:

```cabby
sound.set_monitoring true
sound.set_monitor_volume 0.7       // a little quieter than the click
sound.recording Take = sound.record
```

Turn it on before `sound.record`: the input is then read in smaller
pieces, sooner, so the wait is shorter. Use headphones, not speakers,
with a microphone, or the microphone hears itself and howls.

On a Mac, the first recording asks whether the terminal may use the
microphone. Cabby's tests can't play a guitar, so they record from a file
instead: with `CABBY_SOUND_IN=file.wav` set, the file comes in, all at
once, as if it had been played (and, monitored, is heard as it would be).

#### In short

* `sound.record` records from the input chosen (`sound.inputs`), and its
  `level` shows how loud as it goes.
* `trim_silence`, `play` and `save`; recording in time with a song
  that's playing.

#### Try this

* Record in stereo: `sound.set_recording_channels 2`, before recording.
* Count in: a bar of clicks before the recording starts.
* Play Chapter 74's blues while recording, and play along with it.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it. These need a
microphone, so they're checked rather than run.*

**Hearing what's recorded, and in time.** Monitoring plays what's coming
in through the speakers or headphones as it's recorded; the recording
delay moves a recording earlier, to make up for the time sound takes to
come in:

<!-- check -->
```cabby
use sound
use time

int main {
	sound.set_monitoring true
	sound.set_monitor_volume 0.7
	println sound.monitoring, sound.monitor_volume
	sound.set_recording_delay 12
	sound.recording Take = sound.record
	time.sleep time.seconds(5)
	Take.stop
	Take.save "take.wav"
}
```

[← Chapter 74](#chapter-74-songs) · [Contents](#contents) · [Chapter 76 →](#chapter-76-keyboards)

---

### Chapter 76: Keyboards

*Program: `76-keyboard.cby`, a piano that writes down what's played.
Libraries: `use sound`, `use events`.*

A MIDI keyboard sends a message for each key pressed and let go, and
for each pedal, wheel and knob that moves: no sound, just what was
played. `sound.open_keyboard` listens to the first one connected (by
USB, or through an audio interface's MIDI socket), and `play_through`
plays what comes in through one of sound's instruments, heard at once:

<!-- check -->
```cabby
use sound
use events

int main {
	sound.keyboard Keys = sound.open_keyboard
	Keys.play_through "piano"
	println "playing: press Enter to stop"
	read_line
	return 0
}
```

`play_through` plays the notes from a thread of the sound library's own,
straight into what's mixed, so they're not late however busy the program
is: a keyboard that's heard a moment after it's played is hard to play.
The sustain pedal and the pitch-bend wheel work too. `sound.midi_inputs`
lists the keyboards by name, and `sound.open_keyboard_named "Keystation"`
listens to one by its name, or part of it. None connected is a panic that
says so, so the tutorial looks first.

#### Being told what's played

A keyboard's handlers are told of each message, from the event loop, as
a process's lines are (chapter 34):

| Handler | Called for | In it |
|---|---|---|
| `on_note h` | a key pressed or let go | `pressed`, `key` (60 is middle C), `note` (`"C4"`), `velocity` (how hard: 1 to 127) |
| `on_control h` | a pedal, wheel, slider or knob moving | `control` (64 the sustain pedal, 1 the modulation wheel, 7 volume), `value` (0 to 127) |
| `on_bend h` | the pitch-bend wheel | `bend`, from -8192 to 8191 |

`channel` says which of MIDI's sixteen channels a message came on (most
keyboards send on 1). `events.run` waits while a keyboard has handlers,
until it's closed.

The tutorial writes each note down: when a key is pressed, the beat it is
now goes in a lookup by its key; when it's let go, the note goes in a
list, with the beat it started on and how long it was held. A timer, set
again with each note, goes off after three seconds without one; then the
notes become a `sound.part`, saved as `played.mid` (chapter 74), and the
keyboard is closed, which leaves nothing to wait for, so `events.run`
ends.

#### Synthesizers

A MIDI synthesizer goes the other way: it plays what it's sent. It might
be a hardware synthesizer, a keyboard's own sounds (a keyboard with
speakers is a synthesizer too), or a program such as FluidSynth.
`sound.open_synthesizer` gives one, and the notes are the same as
`sound.play_note`'s:

<!-- check -->
```cabby
use sound

int main {
	sound.synthesizer Synth = sound.open_synthesizer
	Synth.set_instrument "strings"
	Synth.play_note "C4", 1000          // held for a second, as the program goes on
	Synth.start_note "E4"
	read_line
	Synth.stop_note "E4"
	return 0
}
```

`set_channel` (1 to 16), `set_velocity`, `set_control` and `bend` are
there too, and `start_key` and `stop_key` take a key's number, as a
keyboard gives it: `Synth.start_key Keys.key + 12` plays it an octave up.
`Keys.send_to Synth` sends everything a keyboard plays straight on to a
synthesizer, from the keyboard's thread, as `play_through` plays it.

On Linux, keyboards and synthesizers are found through ALSA's MIDI (the
"sequencer", which PipeWire's MIDI goes through too); on a Mac, through
Core MIDI. The tests have neither keyboard nor synthesizer, so
`CABBY_MIDI_IN` is a MIDI file played as the keyboard, as fast as it can
be read, and `CABBY_MIDI_OUT` a file that what's sent to the synthesizer
is saved in.

#### In short

* `sound.open_keyboard`, and `play_through` to hear it through one of
  sound's instruments.
* `on_note` and `on_control`, to be told what's played; a synthesizer
  played by the program itself.

#### Try this

* Play it through another instrument: "strings", "organ", "electric
  piano", or the drums, whose keys are each a drum.
* Print Keys.channel too: a keyboard split in two may send each half on
  a channel of its own.
* Play every note an octave up, or as a chord: with a sound.synthesizer
  (sound.md), S.start_key Keys.key + 12.
* Show the notes on a gui window's canvas, as a piano roll.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it. These need a MIDI
keyboard or synthesizer, so they're checked rather than run.*

**The pitch-bend wheel, and stopping.** `on_bend` is called as the wheel
moves, with `bend` from -8192 to 8191; `stop_playing_through` stops what
`play_through` started:

<!-- check -->
```cabby
use sound
use events

object bender {
	sound.keyboard Keys

	void begin {
		Keys = sound.open_keyboard
		Keys.play_through "strings"
		Keys.on_bend bent
		events.run
	}

	void bent {
		println "bend:", Keys.bend
		if Keys.bend == 8191 {
			Keys.stop_playing_through
			events.stop
		}
	}
}

int main {
	bender B
	B.begin
}
```

**A synthesizer.** `sound.midi_outputs` names the synthesizers connected;
a keyboard can send what's played straight on to one, and a program can
play one itself, on any channel, as hard as it likes, with controls and
pitch bend:

<!-- check -->
```cabby
use sound
use time

int main {
	println sound.midi_outputs
	sound.synthesizer Synth = sound.open_synthesizer
	sound.keyboard Keys = sound.open_keyboard
	Keys.send_to Synth
	time.sleep time.seconds(10)
	Keys.stop_sending
	Synth.set_channel 2
	Synth.set_velocity 80
	Synth.set_control 64, 127          // the sustain pedal down
	Synth.start_key 60
	Synth.bend 4096
	time.sleep time.seconds(1)
	Synth.stop_key 60
	Synth.set_control 64, 0
}
```

[← Chapter 75](#chapter-75-recording) · [Contents](#contents) · [Chapter 77 →](#chapter-77-the-map)

---

## Part XIV: Cabby Quest

Tutorials 77 to 88 build one game, a little at a time: Cabby Quest, a
small adventure with a woods, a town, a meadow, a bakery, two people who
live there and a rival, music, a quest, saving, menus, and battles in the
tall grass. Each tutorial is the whole game so far; each adds one part of
it. Together they show the order a game like this is
built in: the map first, then walking, then going from place to place,
then sound, people, talking, things to carry, menus, people who notice
the hero, ground with two heights, and the battles last.

**How the code is laid out.** Every tutorial in this part is in numbered
sections of no more than ten lines of code, each with a heading:

```cabby
// ==== 78.21  Walking ======================================================
```

The number is the tutorial that wrote the section: 78.21 is the twenty-first
section part 2 added. Later tutorials keep it, under the same number and
title, so what's new in a tutorial is the sections with its own number.
When a later part changes a section, its heading says so:

```cabby
// ==== 78.21  Walking  (changed in 80) =====================================
```

Each chapter below goes through its tutorial's new sections in order, and
says what changed in the old ones. Read the tutorial beside it: the code is
there, and the chapter says why it's written that way.

**What's in the folder.** The maps are in `tutorials/quest/` (`woods.txt`,
`town.txt`, `meadow.txt` and `bakery.txt`), the pictures in `tutorials/pictures/`, the
sounds in `tutorials/sounds/`, and the music in `tutorials/music/`. The
tools that made the pictures, sounds and music are in `tools/`.

### Chapter 77: The map

*Program: `77-quest-map.cby`. Libraries: `use game`, `use files`.*

The woods, read from `quest/woods.txt`, and the hero standing in them.

* **77.1 Sizes.** A tile is 48 pixels; the stage shows 13 by 9 of them. The
  two layers are named: `GROUND` (0) and `STANDING` (1).
* **77.2 The world.** The game is a `game.window`. It keeps the area's map
  (a `list str`, one string a row), which picture each kind of ground has
  (a `lookup char str`), and the hero.
* **77.3 Building the world.** The window's size, a black background (for
  the edges of a small area), and `sort_by_depth STANDING`, so what stands
  lower on the stage is drawn in front (chapter 73). Then to the woods.
* **77.4 Going to an area.** Read the map, make the world its size, a
  sprite for every tile, the hero, and the camera following the hero.
* **77.5 Reading an area's map.** A map file starts with notes (lines
  starting `//`), then the map. A line of `---` ends it; what comes after
  waits for part 2.
* **77.6 A sprite for every tile.** Two loops, rows and columns: each tile
  gets its ground, and maybe something standing on it.
* **77.7 and 77.8 The ground.** Grass, unless `Grounds` names something
  else. Water and flowers have two pictures each, shown in turn by
  `animate`, so they move a little.
* **77.9 What stands on the ground.** Trees and houses are taller than a
  tile. Each is placed so its bottom sits on its tile, and put in the
  `STANDING` layer.
* **77.10 Where a tile is.** The middle of a tile, in pixels: `Tile * TILE
  + TILE / 2`.
* **77.11 Starting.** `main` makes the world and runs it.

Nothing moves yet. That's next.

#### In short

* An area read from a text file: its tiles, a letter each, and its
  settings after the `---`.
* The world a game window, the tiles sprites in layers, and the hero
  standing in it.

#### Try this

* Start the hero somewhere else: `go_to "woods", 10, 8`.
* Draw a new tree into `quest/woods.txt`, and run it again.
* Take out `Tree.set_layer STANDING`, and see the grass drawn over the
  tree's leaves.

[← Chapter 76](#chapter-76-keyboards) · [Contents](#contents) · [Chapter 78 →](#chapter-78-walking-and-signs)

---

### Chapter 78: Walking, and signs

*Program: `78-quest-walking.cby`.*

The hero walks, tile by tile; walks into trees; and reads the sign.

**Someone who walks (78.1 to 78.9).** A `walker` is an object for anyone
who walks: the hero now, the townspeople in part 5. It has a sprite, a
picture's name (`"hero"`), the tile it's on, the way it faces, and its
step: which way (`Dx`, `Dy`), and how many frames are left (`StepsLeft`).
`step` starts one: to the next tile, or, when the way is blocked, on the
spot (its legs move, but it doesn't). Each frame of a step,
`keep_stepping` moves it three pixels; after sixteen frames it's on the
next tile, and `arrive` says so. Its legs are four pictures to each tile,
`LEG_TIME` apart: a foot forward, standing, the other foot, standing, so it
takes a whole stride from one tile to the next (with only two, a foot
forward and standing, it seems to skate). `across_of` and `down_of` turn a way into numbers:
`"left"` is −1 across.

**What's happening (78.10, 78.11).** An `enum`: `PLAYING` while walking
about, `READING` while words are showing. The world keeps which, and the
words.

**A new walker (78.12).** Its sprite stands on its tile, with its feet
at the bottom, as in FireRed. A tree is a tile and a half tall, so a tree
on the tile below the hero is lower on the stage, and in front in the
depth order: its leaves hide the hero's feet, and walking above a row of
trees the hero goes behind them. A tree on the tile above is behind him,
so he walks right up to its trunk, or a sign's post, as in Pokémon.

**The lines after the map (78.13, 78.24).** `read_area` now keeps
the lines after `---` too, in `Things`. `thing_at "sign", 3, 2` finds the
line that starts `sign 3,2 ` and gives what follows it. Every part after
this one adds a kind of line: ways, music, people, things to pick up.

**Every frame (78.14 to 78.23).** `step` asks what's happening. Reading:
wait for Space. Otherwise `play_step`: part way through a step, the next
frame of it; on a tile, an arrow key held down starts the next step, and
Space looks at what's ahead. A new way only turns the hero, at first
(78.19): tapping an arrow turns them to face it, and holding it a little
longer (`TURN_TIME`, a tenth of a second, by `key_held_for`) walks. The
way they already face walks at once. Letting go of the key stops them on
the tile they're stepping onto, as they arrive. `is_free` says whether a tile can be walked
onto: on the map, and not one of `BLOCKING`'s tiles. Walking into
something is a step on the spot, and `bumped` says so.

**Words (78.25 to 78.30).** `look_ahead` finds the tile the hero faces; a
sign there is said with `say`. The stage's `on_draw`, `draw_over`, draws
them in a box at the bottom: `stage.lines_for` says how many lines they'll
take, so the box is as tall as they need, and `stage.write_wrapped`
writes them broken into lines. Drawing changes nothing in the world, so
these methods are `const`.

**Changed:** 77.2 (the hero is a `walker`; `Things`), 77.3 (`on_frame
step`, `stage.on_draw draw_over`), 77.4 (`new_walker` for the hero), 77.5
(the lines after the map are kept).

#### In short

* A `walker` for anyone who walks: a tile at a time, facing the way it
  goes, stopped by what's in the way.
* Signs, read with the action key, and words in a box.

#### Try this

* Make the hero quicker: `SPEED 4`.
* Put a second sign in `quest/woods.txt`: an `S` in the map, and a
  `sign <col>,<row> ...` line after the `---`.
* Let the water be walked on: take `~` out of `BLOCKING`.

[← Chapter 77](#chapter-77-the-map) · [Contents](#contents) · [Chapter 79 →](#chapter-79-areas-fades-and-going-inside)

---

### Chapter 79: Areas, fades, and going inside

*Program: `79-quest-areas.cby`. Maps: `quest/woods.txt`, `town.txt`,
`meadow.txt`, `bakery.txt`. Library: `game`'s `set_world_area`.*

There are two ways from one area to the next. The areas outside are
**joined**, edge to edge, as in Pokémon: a line after the woods' map,
`join south town 0`, says the town is beyond the woods' south edge, its
column 0 beside the woods' column 0 (the number is how far along the edge
it starts). The town is drawn there too, so it can be seen on the way, and
walking over the edge, the hero is in the town, without stopping. The
town has the line the other way round, `join north woods 0`, and joins the
meadow on its east edge. A **door** is a way instead, `way 4,5 bakery 6,7`:
stepping on it, the stage fades to black, and the bakery is built behind
the dark.

**An area next door**

* **79.3 to 79.7 An area next door.** A `neighbour`: its name, the edge
  it's beyond, its map, and where its top left tile is, counted on this
  area's map. Beyond the south edge, that's the row after the last; beyond
  the north edge, as far above 0 as the neighbour is tall (`beyond`).
  `has` and `tile` look at its tiles by this area's numbers.
* **79.16 to 79.18 and 79.24 The areas next door.** After the map is read, each
  `join` line reads that area's map (`read_map`: just the rows before its
  `---`), places it, and adds its tiles: the same `add_tiles_of` (77.6,
  changed) as this area's, from its own top left tile.
* **79.19 to 79.23 and 79.25 Corners.** The areas next door have areas
  next door of their own. One at a corner (east of the woods and north of
  the meadow, say) would be seen from the town, beyond the corner, so it's
  drawn too: each area next door's `join` lines are read (`read_joins`),
  and each area that isn't this one, or next door already, is placed
  beside that area next door (`beside`, 79.17, places a map beside any
  map, and `move_by` moves it along with that area). It's marked
  `Corner`: seen, but not walked into (it isn't joined to this area, so it
  can't be crossed into), and the world doesn't reach further for it.
  An area can be reached two ways: a grove east of the woods and north of
  the meadow is beside both. The two should agree where it is, and they
  do when the maps joined on are the same size: as wide as each other to
  the north and south, as tall to the east and west. If they don't,
  `check_place` says so as the area is built, before it looks wrong.
* **79.26 and 79.27 The world's edges.** The camera never shows past the
  world's edges, so the world reaches half a screen past each edge with an
  area beyond it, and the camera goes on following the hero right up to
  the edge. That's above row 0, or left of column 0, too:
  `set_world_area Left, Top, Width, Height` is `set_world_size` for a world
  whose top left corner isn't 0, 0.
* **79.28 Trees all round.** Where the world reaches past this map and
  the next (the town is wider than the woods, so above its right end
  there's no map at all), trees fill it, as Pokémon fills the edges with
  its border tiles.
* **79.33 to 79.35 Beyond the edge.** `on_map` is a tile of this area's
  own map. `is_free` (78.23, changed) asks the area next door about a tile
  beyond the edge: `join_at` finds which one has it, and its tile mustn't
  be in the way.
* **79.29 Over the edge.** When a step ends beyond the edge (78.17,
  changed), the area next door is entered: its map, its people, its
  music, the hero on the same tile counted on its own map. They're on the
  same spot of the screen, and the camera doesn't jump, so the change
  can't be seen. If the arrow is still held, they walk on.

**A door, and fading**

* **79.1 Fading.** A fade takes `FADE_FRAMES` frames each way.
* **79.8 Going somewhere else.** Which area the hero is in, where they're
  going next, how far through the fade, and the areas next door. `fading`
  is either way.
* **79.10 The kinds of ground.** Inside a house too: walls, the floor, an
  oven, a table, and a doormat.
* **79.11 and 79.12 Arriving, and entering.** `enter`: the area built, the
  hero in it, and its name showing. `start_at` enters, and fades in.
* **79.14 Clearing the stage.** Every sprite removed (`sprites_in ""` is
  all of them), and the old map and areas next door forgotten.
* **79.15 Facing the same way.** The hero goes on facing as they were.
* **79.30 Leaving.** `"bakery 6,7"` split into the area and the tile; then
  fade out.
* **79.31 A place's numbers.** `"6,7"` as a column and a row.
* **79.32 Each frame of a fade.** When it's dark, the next area is built
  behind the dark (`start_at`), and it fades in. When it's light, walking
  goes on.
* **79.37 The dark.** A black rectangle over everything, see-through at
  first: `stage.set_color "rgba(0, 0, 0, 0.5)"` is half see-through.

**A door** is a house's `D` tile with a way starting on it. It's blocked
like a wall unless a way does start there (78.23, changed). The bakery's
map is a room, and its doormat is a way back out.

**An area's name**

* **79.2, 79.9, 79.13 The name showing.** A line after the map,
  `name Cabby Town`, with no place: `setting "name"` (79.36) finds it. On
  entering, it's kept, with the frame it appeared in.
* **79.38 to 79.40 Drawing the name.** For `NAME_FRAMES` frames, in a box
  at the top left. `name_in` says how far in it is, from 0 to 1: it slides
  in over 12 frames, and out over the last 12. The box is drawn that far
  from just past the stage's left edge.

**Changed:** 78.1 (more tiles block), 78.10 (fading out and in), 77.3
(`name_grounds`; the game starts with `start_at`), 77.4 (clearing first,
then the areas next door), 77.6 (any map's tiles, from anywhere), 78.14
(fading comes first), 78.17 (over the edge, or a way out?), 78.23 (beyond
the edge), 78.28 (the name, and the dark, drawn over).

#### In short

* Areas joined edge to edge, and a door into the bakery.
* Fading to black and back between them, and each area's name shown as
  the hero arrives.

#### Try this

* Make the fades slower: `FADE_FRAMES 60`.
* Give the other house a way in too, with a map of its own.
* Join an area of your own to the woods' west edge.

[← Chapter 78](#chapter-78-walking-and-signs) · [Contents](#contents) · [Chapter 80 →](#chapter-80-music-and-sounds)

---

### Chapter 80: Music and sounds

*Program: `80-quest-sound.cby`. Library: `use sound` as well.*

* **80.2 The area's music.** A line after the map, `music town`, found by
  `setting "music"` (79.36). `sound.change_music "music/town.mid", 1000`
  fades out the music that's playing, over a second, then plays the MIDI
  file round and round, as Pokémon does from one area to the next. If it's
  the same music, it goes on. Through a door, Leaving (79.30, changed) has
  faded it out already, with the stage (`sound.fade_out_music` takes how
  many milliseconds), so the new music starts at once.
  `sound.crossfade_music` would play the two together for a moment
  instead, one getting louder as the other gets quieter.
* **80.3 A door.** A way out through a door, or the mat inside one, makes
  a sound.
* **80.4 Sound effects.** `effect "step"` plays `sounds/step.wav`. It plays
  while the game goes on; as many at once as happen.
* **80.1, 80.5, 80.6 Words a word at a time.** `Shown` says how many of the
  words are showing. A word appears every third frame, with a blip, and
  Space shows them all. The box is as big as all the words from the start,
  and only the first `Shown` are written. Wrapping breaks lines only at
  spaces, so the words never jump from one line to the next as they
  appear.

The music is four MIDI files that `tools/make-music.cby` wrote from notes
and beats, played with the computer's SoundFont, or Cabby's own
synthesizer without one. The sound effects are WAV files from
`tools/make-sounds.py`.

**Changed:** 79.12 (the music starts), 78.17 (a door's sound), 79.30 (the
music fades), 78.21 and 78.22 (steps and bumps make sounds), 78.26,
78.27, 78.28 and 78.29 (words a word at a time).

#### In short

* Music for each area, faded from one to the next; sound effects for
  steps, bumps and doors.
* Words in the box a word at a time.

#### Try this

* Write music of your own: change a tune in `tools/make-music.cby`, and
  run it.
* Make the words appear a letter at a time instead.
* Turn the music down while words are showing.

[← Chapter 79](#chapter-79-areas-fades-and-going-inside) · [Contents](#contents) · [Chapter 81 →](#chapter-81-people-who-walk-about)

---

### Chapter 81: People who walk about

*Program: `81-quest-people.cby`.*

A line after the map, `person 11,3 elder down wanders`, puts someone in the
area: on tile 11,3, facing down, with `pictures/elder-down-1.png` and the
rest, and walking about by themselves.

* **81.1 The other way.** `opposite "up"` is `"down"`: which way someone
  turns to face the hero.
* **81.2 Where it is, or is going.** A walker is in the way on its own tile
  and on the one it's stepping onto, so two can't step onto the same tile.
* **81.3 to 81.5 The people.** A `list walker`, made from the person lines.
  The list goes through `Things` by number: `add_person` changes the world,
  and a loop through one of the world's own lists can't let that happen
  (Cabby says so, if you try).
* **81.6 to 81.8 Wandering.** Each frame, the people part way through a
  step take the next frame of it. Every 75 frames, each person who wanders
  has a one in three chance of a step a random way, to a free tile, and
  never through a way out, or over the edge into the area next door.
* **81.9 and 81.10 Someone in the way.** The hero, or one of the people.
  `is_free` now asks this too.
* **81.11 Who can be talked to.** Someone standing on the tile: not part
  way through a step off it (they'd turn to talk from a tile away), nor
  on their way onto it. Being in the way and being there to talk to aren't
  the same, so they're asked for separately.
* **81.12 and 81.13 Making room.** Someone may be standing where the hero
  arrives: the elder wanders onto the path where it comes into town, and a
  game saved where he started puts the hero on his tile when it carries on
  (part 7). Whoever it is steps aside, to the first free tile next to it.
* **81.14 Talking to someone.** Facing someone and pressing Space turns
  them to face the hero, and they say something.

**Changed:** 77.4 (`add_people`), 79.12 (room is made), 79.14 (the people
go too), 78.15 (the people move),
78.23 (nobody walks through anybody), 78.25 (someone to talk to, before a
sign).

#### In short

* The townspeople as walkers of their own, in a list: some stand, some
  wander.
* Nobody walks through anybody; a person turns to the hero to talk.

#### Try this

* Make the baker wander too: add `wanders` to her line in
  `quest/town.txt`.
* Make the elder wander more often.
* Add a third person, drawn by `tools/make-town-pictures.py`.

[← Chapter 80](#chapter-80-music-and-sounds) · [Contents](#contents) · [Chapter 82 →](#chapter-82-conversations-and-a-quest)

---

### Chapter 82: Conversations, and a quest

*Program: `82-quest-talking.cby`.*

The elder asks the hero's name, and remembers it. The baker asks for help.

* **82.1 What the hero has done.** A struct, `progress`: the answers to
  questions, in a `lookup str str` by the question, and how far the quest
  has got. Part 7 saves it.
* **82.2 A question.** Which one is showing, its answers, and which one the
  arrow points at.
* **82.3 Asking.** Words, then answers to choose from:
  `ask "elder name", "What's your name?", {"Marcus", "Livia", "Tiro"}`.
* **82.4 to 82.6 Choosing.** Up and down move the arrow (with a click);
  Space chooses.
* **82.7 An answer.** It's kept in `Story.Answers`, under its question,
  and whoever asked answers back.
* **82.8 and 82.9 The elder.** He asks the hero's name once. After that,
  `Story.Answers` has it, and he uses it.
* **82.10 and 82.11 The baker, and the quest.** What she says depends on
  `Story.Quest`: `""` (she asks), `"asked"` (she reminds), or `"done"`.
  Saying yes starts the quest.
* **82.12 and 82.13 Talk, drawn.** The words, and the answers in a box at
  the top right, with an arrow by the one chosen.

**Changed:** 78.10 (`CHOOSING`), 78.14 (choosing is read on, too), 81.12
(the elder and the baker each have their own talk), 78.27 (choosing, once
the words are all showing), 78.28, 78.29 and 78.30 (the box can be
anywhere, any size).

#### In short

* Conversations: words, then a question with answers to choose from.
* What's been said is remembered, in a struct, and the baker's quest
  begins.

#### Try this

* Give the elder a second question: what brings the hero to Cabby Town?
* Let the hero type their own name, shown in the box.
* Make the baker remember a "Not now.".

[← Chapter 81](#chapter-81-people-who-walk-about) · [Contents](#contents) · [Chapter 83 →](#chapter-83-things-to-carry-and-saving)

---

### Chapter 83: Things to carry, and saving

*Program: `83-quest-saving.cby`. Libraries: `use directories` and `use
json` as well.*

* **83.1 to 83.3 Things to pick up.** A line after the map, `item 9,4
  spoon`, puts `pictures/item-spoon.png` on tile 9,4, unless it's been
  picked up already. Each is in a group of its own, `"item 9,4"`, to take
  it away by later.
* **83.8 A thing to pick up, here.** `item_at` says what's on a tile, if it
  hasn't been taken. Things on the floor are in the way, so they're faced
  and picked up, like talking to someone.
* **83.9 to 83.12 Picking up.** Space, facing it: it goes into
  `Story.Items`, its place into `Story.Taken` (so it isn't on the floor
  next time, saved or not), and its sprite off the stage.
* **83.13 to 83.15 Giving it back.** The baker, when the hero has her
  spoon: it goes from `Story.Items`, bread comes in, the quest is done, and
  a few notes play (`sound.play_notes`). The hero may find the spoon before
  she asks (the bakery's door is open to anyone): then she's surprised
  ("How did you know I'd lost it?"), and the quest is done all the same.
* **83.4 Where saved games go.** `directories.save_folder "Cabby Quest"`
  is the computer's own place for a program's saved games (made, if it
  isn't there): `~/.local/share/Cabby Quest` on Linux,
  `~/Library/Application Support/Cabby Quest` on a Mac.
* **83.5 Saving.** Where the hero is goes into `Story`, then all of
  `Story` into the file: `json.from(Story).pretty`. It's readable. Open it
  and see.
* **83.6 Carrying on.** `json.read(...).fill Story`, and back to where the
  hero was.
* **83.7 Beginning.** The saved game, if there is one; else the woods. (S
  saves, for now.)

**Changed:** 82.1 (`progress` has where the hero is, what they carry, and
what's been taken), 77.3 (`begin`), 77.4 (things as well as people),
78.18 (S saves), 78.23 (things on the floor are in the way), 78.25
(picking up), 82.10 (has the hero found it, asked or not?).

#### In short

* Things to pick up and to give, carried by the hero.
* The game saved as JSON in `directories.save_folder`, and read back.

#### Try this

* Run it again after saving: it carries on where you saved.
* Put a second thing to find in the woods, and someone who wants it.
* Save by itself each time the hero goes through a door.

[← Chapter 82](#chapter-82-conversations-and-a-quest) · [Contents](#contents) · [Chapter 84 →](#chapter-84-menus)

---

### Chapter 84: Menus

*Program: `84-quest-menus.cby`. The whole game.*

A title screen, a menu in the game, and options. **A menu is a question
nobody in the game asks** (82.3), so the answers, the arrow and Space all
work as they do in a conversation; `chosen` (82.6, changed) sends a menu's
answers to `menu_chosen`, and the people's to `answered`.

* **84.1 Options.** Two volumes, from 0 to 10. Fields start at 0, so
  `OptionsSet` says whether they've been given their first values yet.
* **84.2 to 84.4 The title.** No area (`Area` is `""`), the woods' music,
  and a question: New game, Continue (only when there's a saved game), or
  Options. A new game is a fresh `progress`, and the woods.
* **84.5 Drawing the title.** On the title screen, `draw_over` draws the
  game's name and the answers in the middle, instead of the box of words.
* **84.6 A pair of keys.** `pressed_way "down", "up"` is 1, -1 or 0.
  Choosing (82.4, changed) uses it for up and down, and gives left and
  right to the volumes.
* **84.7 to 84.9 Menus.** Which menu the answer is from, then what it does.
* **84.10 to 84.12 The game's menu.** Escape, while walking: the words say
  what the hero carries; the answers are Save, Options, Quit to the title,
  and Back.
* **84.13 to 84.17 The volumes.** The options are two sliders: "Music:
  70%" and "Sounds: 100%". Left and right, with the arrow on one, change it
  by a tenth, and the answer's words change where they are (the question
  isn't asked again, so the arrow stays put). `sound.set_music_volume` sets
  the music's volume, and `sound.set_effects_volume` the sound effects',
  each from 0 to 1. The first time, the music is a little quieter than the
  sounds.
* **84.18 and 84.19 Back.** To wherever the options were opened from: the
  title, or the game's menu.

**Changed:** 77.3 (the title first), 78.18 (Escape opens the menu), 78.28
(the title is drawn), 82.4 (left and right), 82.6 (menus' answers), 82.12
and 82.13 (the answers' box can be anywhere).

#### In short

* A title screen (New game, Continue, Options), a menu in the game, and
  options that change it.
* The whole game, from beginning to end.

#### Try this

* Remember the options too, in a file of their own.
* Add a "Things" answer to the menu, showing each thing carried.
* Make the next quest: someone in the woods who has lost something in the
  town.

[← Chapter 83](#chapter-83-things-to-carry-and-saving) · [Contents](#contents) · [Chapter 85 →](#chapter-85-being-noticed)

---

### Chapter 85: Being noticed

*Program: `85-quest-noticed.cby`. Maps: `quest/town.txt` and
`meadow.txt`, with new lines.*

The people notice the hero, as in Pokémon. Walking into town, the elder
sees the newcomer: a "!" over his head, a sound, and he comes over and
asks their name. Walking past in front of the baker, she sees the hero,
and comes to ask about her spoon. And in the meadow is Rufus, a rival,
with music of his own: he sees the hero, comes over, says what he thinks
of them, and goes off down the road to the coast.

Three lines after a map say who notices:

| Line | Means |
|---|---|
| `watch 8,1 elder` | stepping on tile 8,1 makes the elder notice the hero (a person can have several, so there's no way round them: Rufus has two) |
| `sees baker 3` | the baker sees the hero up to three tiles in front of her |
| `rival 13,7 left` | where Rufus stands, and which way he faces |

Everyone notices the hero only once: who has is kept in `Story`, and
saved with the rest.

* **85.1 Being noticed.** The "!" shows for `NOTICE_FRAMES` frames.
* **85.2 Who has noticed the hero.** `Noticed`, in `Story` (82.1): a game
  saved by part 8, without it, still carries on.
* **85.3 Someone coming over.** Who it is, how long since they noticed,
  how far each person sees (`Sight`), and whether someone is walking away.
  `NOTICED` (78.10, changed) is what's happening meanwhile.
* **85.4 and 85.5 The rival, and how far they see.** With the people and
  the things (83.1, changed): Rufus, unless he's met the hero, and the
  `sees` lines.
* **85.6 Waiting.** Every frame (78.14, changed) is a fade, playing, or
  waiting: for words, a question, or someone coming over.
* **85.7 to 85.12 Looking out for the hero.** Each time the hero arrives
  on a tile (78.16, changed). A `watch` tile there makes its person
  notice; otherwise someone who `sees` might. `in_sight` goes from their
  tile the way they face, a tile at a time, as far as they see: if the
  hero is on one before something's in the way (a tree, a wall, someone
  else), they've seen them. Someone part way through a step isn't looking.
* **85.13 and 85.14 Noticed.** The hero stands still, the "!" appears, and
  `notice.wav` plays. Rufus's music starts at once: `sound.play_music`.
* **85.15 to 85.20 Coming over.** After the "!", a step at a time toward
  the hero: the way that's further first, across or down, if that tile is
  free, and the other if it isn't. Each step is closer, so they always
  stop, next to the hero, or as near as they can get.
* **85.21 Face to face.** The hero turns to them, and `talk_to` (81.14,
  changed) does the rest: they turn to the hero, and say what they'd say
  if the hero had walked up to them. The elder asks their name, and the
  baker asks about her spoon.
* **85.22 to 85.24 The rival.** Rufus says his piece, and once the words
  have gone, walks away east (78.15, changed), a step at a time, as far as
  he can. Then he's gone, out of sight, and `start_music` brings the
  meadow's music back: `sound.change_music` fades his out first.
* **85.25 and 85.26 The "!".** Drawn over the stage (78.28, changed), so
  the camera is taken away from where they are: `Sprite.x - camera_x`. A
  white box, a point at the bottom, and a red `!`.

The pictures of Rufus, the sound and his music are made by the tools as
the rest were: `tools/make-town-pictures.py`, `make-sounds.py` and
`make-music.cby`.

**Changed:** 78.10 (`NOTICED`), 78.14 (waiting), 78.15 (walking away),
78.16 (looking out), 81.14 (the rival talks), 78.28 (the "!" is drawn),
83.1 (the rival, and who sees).

#### In short

* People who notice the hero: a watched tile, a line of sight, a "!"
  bubble, and coming over to talk.
* A rival, with music of his own.

#### Try this

* Give the elder sight as well: `sees elder 2` after town's map.
* Make someone in the woods who notices the hero and gives them a quest.

[← Chapter 84](#chapter-84-menus) · [Contents](#contents) · [Chapter 86 →](#chapter-86-the-hills)

---

### Chapter 86: The hills

*Program: `86-quest-hills.cby`. Maps: `quest/hills.txt`, new, south of
town (`join south hills 0` in `town.txt`).*

The path south out of town leads into the hills, which have the ground
FireRed's routes have: ledges to hop down, high ground with stairs up to
it, and a bridge between two hills, to walk over from up high and under
from below.

| Tile | Is |
|---|---|
| `h` | high ground |
| `C` | a cliff's face, where high ground drops to the ground: in the way |
| `z` | stairs, between the ground and high ground |
| `B` | a bridge (`P`: one over a path) |
| `v` `^` `<` `>` | a ledge, facing down, up, left or right |

**Two heights.** The hero is on the ground (`Level` 0) or up high (1).
Up high, they can only walk onto more high ground, so they can't walk
off the edge; on the ground, only onto more ground, so they can't walk
up a cliff. Stairs are both heights: a step onto them, or off them, is
always allowed, and that's how the hero goes up and down. A bridge is
whichever height the hero was when they stepped onto it: from up high,
they walk over it, drawn over the bridge; from the ground, under it,
hidden beneath. Either way, they leave it at the height they came.

**Ledges.** Walking into one the way it faces, the hero hops over it: a
step two tiles long, up and down again on the way. From any other side
it's in the way, like a fence.

The parts before this one don't know these tiles: there, the hills are a
field to walk about in.

* **86.1 Over, and under.** Two more layers: `BRIDGE` (over the ground,
  and over whoever's under it) and `ON_TOP` (over that: the hero, up
  high). How high a hop goes, and the two heights a tile can have that
  aren't heights: `STAIRS` (either) and `ON_BRIDGE` (as the hero was).
  `BLOCKING` (78.1, changed) has cliffs and ledges.
* **86.2 Up high.** Each walker's `Level`, and whether this step is a hop.
* **86.3 A hop.** A step, but two tiles long and twice as many frames.
  At its end (78.9, changed), the walker moves on a second tile.
* **86.4 and 86.5 Up, and down again.** Each frame of a step (78.8,
  changed) moves the sprite down by `rise` as well: the change in
  `height`. For the first quarter of the hop, half a tile, it's 0: the
  hero walks up to the lip, his feet at it. Then it goes up to
  `HOP_HEIGHT` and back over the rest, as a ball thrown up does
  (`4 * HOP_HEIGHT * Off * (Jump - Off) / Jump²`, `Off` frames into the
  `Jump` frames in the air).
* **86.6 How high, when saved.** `Level`, in `Story`: a game saved on top
  of the bridge carries on on top of it. Saving (83.5, changed) keeps it,
  and entering an area (79.12, changed) starts there.
* **86.7 A bridge.** What stands on the ground (77.9, changed) has a
  bridge too, a sprite on the `BRIDGE` layer. The kinds of ground (79.10,
  changed) are named for the new tiles, which are their pictures.
* **86.8 Up high, or not.** Each time the hero arrives on a tile (78.16,
  changed): high ground puts them up high, the ground down, and stairs
  and bridges leave them as they were. Up high, they're on `ON_TOP`.
* **86.9 to 86.12 Walking.** A step (78.21, changed) is a hop if there's
  a ledge ahead, or a step if `can_go`: the tile is free, and at the
  hero's height, or stairs or a bridge, or the hero's on stairs.
  `level_at` says how high a tile is.
* **86.13 to 86.15 Ledges.** A ledge ahead facing the way the hero's
  going, with a free tile beyond it: they hop, with `hop.wav`.

People (81.8, changed) stay on the ground, and off stairs and bridges:
they live down there.

The pictures (high ground, a cliff, stairs, a bridge and the four
ledges) and the sound are made by `tools/make-town-pictures.py` and
`make-sounds.py`.

**Changed:** 78.1 (cliffs and ledges in the way), 78.8 and 78.9 (a
step, and a hop), 79.10 (the new tiles), 79.12 (entering), 77.9 (a
bridge), 81.8 (people stay down), 78.16 (up high, or not), 78.21
(walking), 83.5 (saving).

**Where to from here.** Everything a bigger adventure needs is in these
parts: more areas are map files, joined to the ones beside them; more
people are lines in them; more quests are more questions and more
`Story`. The `Try this` notes at the end of each tutorial are places to
start.

#### In short

* Ground with two heights: ledges to hop down, high ground with stairs,
  and a bridge to walk over and under.
* Layers for what's drawn over whom.

#### Try this

* Make a ledge facing left (`<`), with something worth hopping down to.
* Put a person on the high ground.
* Make the hop higher: `HOP_HEIGHT 32`.

[← Chapter 85](#chapter-85-being-noticed) · [Contents](#contents) · [Chapter 87 →](#chapter-87-a-battle)

---

### Chapter 87: A battle

*Program: `87-quest-battle.cby`, Cabby Quest part 11. Maps: `quest/hills.txt`,
with tall grass (`w`) below the ledges.*

The hero isn't alone any more: Ember, a fox, goes with them. In the
hills there's tall grass now, and things live in it, as on FireRed's
routes: now and then, a step into the grass meets a field mouse or a hill
crow, and Ember and it have a battle.

The screen flashes, and the battle shows over the world: the wild one on
its patch of ground at the back, Ember (from behind) at the front, and a
card for each with its name, level and a bar for its health. Each turn,
the menu asks what Ember will do. **Fight** asks which of her moves;
**Run** gets away. Then the quicker of the two goes first, and the other
after, if it's still standing. Each hit's words show, then its bar falls,
green to yellow to red. The first to run out of health has lost.

**A battle is words, in turn.** Everything that happens in a turn is
worked out at once, when the move is chosen: who goes first, how hard
each hit is, who faints. What it leaves is a list of **beats**, each some
words to say and a hit to show after them (who's hit, and their health
then). Each frame of the battle does the next thing: a hit landing, its
bar falling a little further, or the next beat's words. When there are
no beats left, it asks again, or (once someone's fainted, or Ember ran)
the battle's over. The words and the menus are the ones the rest of the
game uses (`say`, `ask`): a battle's menus are menus (84.7).

**How hard a hit is.** As FireRed works it out, more or less: by the
attacker's level, the move's power, and its strength against the other's
toughness (`(Level × 2 / 5 + 2) × Power × Strength / Toughness / 50 + 2`),
then between 85% and all of it, by chance. Ember (level 5, strength 11)
pouncing (power 40) on a field mouse (toughness 8) takes about 6 of its 14.

| Creature | Level | Health | Strength | Toughness | Speed | Moves (power) |
|---|---|---|---|---|---|---|
| Ember, a fox | 5 | 21 | 11 | 9 | 12 | Pounce (40), Spark (55) |
| a field mouse | 3 | 14 | 8 | 8 | 10 | Nibble (35), Scratch (30) |
| a hill crow | 4 | 15 | 10 | 7 | 14 | Peck (35), Gust (40) |

The mouse is slower than Ember, so she goes first; the crow is quicker,
and hits harder, but is easier to hurt.

* **87.1 A battle.** How long the flash lasts, and how often a step in
  tall grass meets something: one in `MEETING`.
* **87.2 Ember's wounds.** In `Story`, so they're saved: how hurt she is.
  A battle won leaves her as hurt as it did; a battle lost, she rests,
  and is well again.
* **87.3 to 87.7 Creatures.** A `critter`: a name, a picture, a level,
  health, how hard it hits and is hurt, how quick it is, and its moves.
  `fox`, `field_mouse` and `hill_crow` make one of each.
* **87.8 to 87.10 A battle's state.** Whether a battle's on, the two of
  them, the bars (each falls toward its goal), and the beats to come.
* **87.11 The hero, stepping or not.** Walking about (78.15, changed)
  gives way to the battle while it's on; the rest of it is here.
* **87.12 and 87.13 Tall grass.** Each step that ends in tall grass
  (78.16, changed) may meet something: a mouse, or one time in three, a
  crow. Its music starts, and the flash.
* **87.14 Each frame of a battle.** The flash; or a hit landing; or, once
  the bars have caught up, what's said next.
* **87.15 and 87.16 The flash.** Then the bars, full, and the first
  words: "A wild field mouse appeared!", "Go, Ember!".
* **87.17 to 87.19 Beats.** `beat` adds one; `next_beat` says it, and
  keeps its hit for when the words have gone.
* **87.20 to 87.22 A hit landing.** A thump (`boom.wav`), and that bar's
  goal is its health now; `bars_caught_up` moves each a quarter of a
  point a frame toward its goal.
* **87.23 The turn's end.** Over, or the menu again.
* **87.24 What Ember does.** The battle's menu (84.8, changed, hands it
  on): Fight asks for a move; Run is over at once; a move is a turn.
* **87.25 to 87.30 A turn.** The quicker goes first. Each go is a hit, a
  beat for it, and, if that's the other out of health, a beat saying so.
* **87.31 The battle's end.** Back to walking, to the area's music.
* **87.32 to 87.37 Drawing it.** Over the world (78.28, changed): the
  flash, then the field (a pale sky, two patches of ground, the two of
  them), and a card for each. The title (84.5, changed) now says when it's
  drawn itself. A battle's menu (82.12, changed) is at the bottom right,
  over the end of the words' box.

The pictures (the three creatures, and the tall grass) are made by
`tools/make-town-pictures.py`: each creature is drawn from shapes (ovals
and triangles) on a 32 by 32 grid, then outlined in dark, as FireRed's
are. The battle's music is made by `tools/make-music.cby`.

**Changed:** 78.15 and 78.16 (walking, and the grass), 79.10 (the tall
grass), 84.7 and 84.8 (the battle's menus), 78.28, 84.5 and 82.12
(drawing).

#### In short

* Tall grass, a creature met in it now and then, and a battle a turn at
  a time: moves, health bars, who goes first, how hard a hit is.

#### Try this

* Have the elder heal Ember's wounds.
* A third move, or a third creature in the grass.
* Ember grows: a level up after each win.

[← Chapter 86](#chapter-86-the-hills) · [Contents](#contents) · [Chapter 88 →](#chapter-88-the-rivals-battle)

---

### Chapter 88: The rival's battle

*Program: `88-quest-rival.cby`, Cabby Quest part 12.*

Rufus has a creature of his own now: Bruno, a pup. When he sees the hero
in the meadow (as in part 9), he challenges them, and the battle that
follows can't be run from. Win or lose, he says what he thinks of it, and
goes on his way to the coast.

Battles now count for something. Each one won gives Ember **experience**,
ten points for each of the other's levels; ten times her own level of it,
and she grows a level. Her numbers grow with her level:

| | At level *L* | At 5 | At 6 |
|---|---|---|---|
| Health | 11 + 2 × *L* | 21 | 23 |
| Strength | 6 + *L* | 11 | 12 |
| Toughness | 4 + *L* | 9 | 10 |
| Speed | 7 + *L* | 12 | 13 |

Bruno is level 6 (23 health, strength 11, toughness 9, speed 11, Bite and
Tackle), so at level 5 Ember goes first, but it's close: Cabby's own test
loses by a hit. The elder, in town, sees to Ember's wounds when he's
talked to.

* **88.1 Ember's level.** Her level and experience, in `Story`, so
  they're saved. A game from before has a level of 0: `ember_level` (88.5)
  takes that to be 5.
* **88.2 and 88.3 Creatures.** Bruno; and `wild_one`, a crow or a mouse.
  `fox` (87.5, changed) works Ember's numbers out from her level.
* **88.4 Whose it is.** The foe's owner (`""` for something wild), and
  whether Rufus has challenged the hero.
* **88.5 Ember's level, now.** A battle (87.13, changed) starts with the
  one met and whose it is, and Ember at her level, less her wounds.
* **88.6 and 88.7 Who it is.** "The wild field mouse", or "Rufus's
  Bruno", in what's said (87.15, 87.26 and 87.27, changed); and "Rufus
  sent out Bruno!" instead of "A wild ... appeared!".
* **88.8 Running away.** Run (87.24, changed) gets away from something
  wild, not from someone's.
* **88.9 to 88.11 Won, or lost.** When one faints (87.30, changed): if
  it's the other, Ember gains experience, and perhaps a level; and an
  owner says what they think.
* **88.12 and 88.13 Rufus.** What he says (85.22, changed) ends with a
  challenge. Once its words have gone, walking away (85.23, changed)
  starts the battle first; after it, he steps away as before.

The elder (82.8, changed) sees to Ember's wounds. Bruno's picture is made
by `tools/make-town-pictures.py`, as the others are.

**Changed:** 87.5 (the fox, by level), 87.12 and 87.13 (meeting, and
starting), 87.15, 87.26 and 87.27 (who it is), 87.24 (running), 87.30
(fainting), 85.22 and 85.23 (Rufus), 82.8 (the elder).

#### In short

* Rufus's battle, which can't be run from; experience and levels; the
  elder heals.

#### Try this

* Choose Spark against Bruno: can Ember win at level 5? Then at level 6.
* Rufus waits for a rematch, until he's beaten.
* A move that heals, or one that makes the other's next hit weaker.

[← Chapter 87](#chapter-87-a-battle) · [Contents](#contents) · [Chapter 89 →](#chapter-89-coins-in-a-field)

---

## Part XV: Three dimensions

The `world` library is the `game` library's flow, in 3D: shapes instead
of sprites, metres instead of pixels, and a camera that can be anywhere.

### Chapter 89: Coins in a field

*Program: `89-coins.cby`, a walk round a field picking up coins. Library: `use world`.*

A world is built as a game is: an object made from `world.window`, whose
`build` adds what's in it, and an `on_frame` handler that runs 60 times a
second. What's added is shapes:

```cabby
add_plane 60, 60, "forestgreen", 0, 0, 0
add_cylinder 0.25, 2, "saddlebrown", X, 1, -12
add_cone 1.2, 3, "darkgreen", X, 3.5, -12
```

Each takes its size, its color, and where its middle goes: x across, y
up, z towards you. They're in metres. The ground is a plane at y 0, so a
tree trunk 2 metres tall has its middle at y 1, and the cone on it, 3
metres tall, its middle at 2 + 1.5 = 3.5. Thinking of the middle takes a
little getting used to; `top` gives where to put the next thing on.

The camera starts at 0, 1.6, 6: eye height, a few steps back, looking
along -z, into the screen. That's "forward" for shapes too. A shape's
angle is which way it faces, in degrees; `turn` turns it to its left, as
you'd turn, and `move_forward` moves it the way it faces:

```cabby
if key_hold "left" {
	Body.turn 3
}
if key_hold "up" {
	Body.move_forward 0.1
}
```

Three degrees and ten centimetres a frame is 180 degrees and 6 metres a
second: a brisk walk. The head is a ball moved to the body's place each
frame, since shapes don't join (yet).

The camera follows behind the walker, as in most 3D games:

```cabby
follow Body, 5, 2.5
```

5 metres behind it and 2.5 above, looking at it, turning as it turns.
For a first-person game, the camera is moved instead: `camera_forward`,
`turn_camera`, and the mouse's `mouse_moved_x` (world.md has one).

The coins are short cylinders, 8 centimetres thick, stood on their edges
with `set_tilt 90`, and put in a group. Each frame they spin, and one the
walker touches is picked up:

```cabby
for C in shapes_in "coins" {
	C.turn 2
	if C.touches Body {
		C.remove
		Found++
```

`touches` compares the boxes round the two shapes, which is quick and,
for coins and walkers, close enough. The score is text laid over the
world, which stays put however the camera moves: `add_text "Coins: 0",
14, 10`, then `set_text` as it changes.

Light comes from the sun (a little yellow here, with `set_sun_color`),
from all round (so the shade isn't black), and from a lamp by the wall,
whose orange light falls off over 6 metres.

Underneath, the world is drawn with OpenGL by the graphics card; under a
test script, Cabby draws it itself, so it's the same everywhere.

#### In short

* A `world.window`, and shapes in it: `add_box`, `add_ball`, `add_plane`,
  `add_cylinder`, `add_cone`, in metres, turned in degrees.
* `move_forward`, `turn`, `follow`, `touches`; text over the world; lamps.

#### Try this

* Make the walker jump: when "space" is pressed, give it an upward
  speed, and each frame move it up by that speed and take a little off
  (gravity) until it's back on the ground at y 0.7.
* Put a picture on the ground: grass.png with set_picture, repeated
  every 2 metres with set_picture_size 2.
* Make it night: set_background "midnightblue", set_sun_color "black",
  and a lamp carried by the walker (move it with the body each frame).
* Look round with the mouse instead: lock_mouse true, and each frame
  Body.turn -mouse_moved_x * 0.2.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Turning every way, and more of a shape.** A shape steps sideways,
tilts its nose up or down, rolls over, grows, hides, and goes; and it
knows how far it is from another:

<!-- gui: frames 2; close -->
```cabby
use world

object yard : world.window {
	void build {
		set_size 320, 240
		world.shape Crate = add_box 1, 1, 1, "peru", 0, 0.5, 0
		world.shape Ball = add_ball 0.5, "red", 3, 0.5, 4
		Crate.move_sideways 2
		Crate.set_tilt 30
		Crate.tilt_by 15
		Crate.set_roll 10
		Crate.roll_by -10
		Crate.set_scale 2
		println Crate.x, Crate.tilt, Crate.roll, Crate.scale
		println Crate.distance_to(Ball)
		Ball.set_visible false
		println Ball.visible
		Ball.remove
		println Ball.removed
		close
	}
}

int main {
	yard Y
	Y.run
}
```

```text
2.0 45.0 0.0 2.0
4.123105625617661
false
true
```

**The camera, by hand.** Instead of following a shape, the camera can be
put somewhere, moved, walked forward and sideways, pointed at a place,
turned, and zoomed:

<!-- gui: frames 2; close -->
```cabby
use world

object view : world.window {
	void build {
		set_size 320, 240
		add_box 1, 1, 1, "peru", 0, 0.5, 0
		move_camera_to 0, 2, 10
		point_camera_at 0, 0.5, 0
		println camera_x, camera_y, camera_z, camera_angle
		move_camera 1, 0, 0
		camera_forward 2
		camera_sideways -1
		turn_camera 10, 0
		set_camera_angle 0, -10
		set_field_of_view 40
		println camera_angle, camera_tilt
		close
	}
}

int main {
	view V
	V.run
}
```

```text
0.0 2.0 10.0 0.0
0.0 -10.0
```

**The mouse.** `on_click` and `on_mouse_move` are called for the
window's mouse, with `mouse_x` and `mouse_y`; `lock_mouse true` hides it
and keeps it in the window, and `mouse_moved_x` and `mouse_moved_y` say
how far it moved in each frame, for looking round as in a game seen from
the eyes:

<!-- check -->
```cabby
use world

object look_round : world.window {
	void build {
		add_plane 40, 40, "forestgreen", 0, 0, 0
		lock_mouse true
		on_frame look
		on_click clicked
	}

	void look {
		turn_camera -mouse_moved_x * 0.2, -mouse_moved_y * 0.2
	}

	void clicked {
		println "clicked at", mouse_x, mouse_y, "locked:", mouse_locked
	}
}

int main {
	look_round L
	L.run
}
```

[← Chapter 88](#chapter-88-the-rivals-battle) · [Contents](#contents) · [Chapter 90 →](#chapter-90-a-robot-from-blender)

---

### Chapter 90: A robot from Blender

*Program: `90-robot.cby`, a robot that hops and waves. Library: `use world`.*

Boxes and balls only go so far. Anything more is made in a 3D program,
Blender most of all, and brought in whole:

```cabby
Robot = add_model "models/robot.glb", 0, 0, 0
```

A `.glb` file is glTF, the format 3D programs share, which Blender
exports (File > Export > glTF 2.0). It holds the model's shapes, its
colors and pictures, the bones it bends with, and its animations. The
robot here is a body with a checked picture, a head, and an arm with a
shoulder and an elbow.

`add_model` gives a `world.shape`, so everything from the last chapter
works on it: `turn`, `move_forward`, `follow`, `touches`. Where it goes is
where its origin goes, which for a character is made at its feet, so at y
0 it stands on the ground.

Its animations are played by name, the names they had in Blender:

```cabby
if key_hold "up" {
	Robot.move_forward 0.05
	Robot.play "Hop"
} else if Robot.playing == "Hop" {
	Robot.stop_animation
}
```

`play` loops an animation. Said again while it's playing, it carries on
rather than starting over, which is why it can be said every frame the
key is held. `play_once` plays one to its end and stops there: a wave, a
jump. `playing` says what's playing now, and `animations` lists what
there is: here `[Wave, Nod, Hop]`.

Moving and animating are separate. The animation makes it look like it's
walking; `move_forward` is what moves it. Getting the two to match, so
feet don't slide, is a matter of the speed: `set_animation_speed` speeds
an animation up or slows it down.

A model faces the way it faced in Blender's front view, which becomes
-z, the way every shape faces, so forward is forward.

#### In short

* `add_model` for a model made in Blender (a `.glb` file), with its
  pictures and bones.
* `play`, `play_once`, `stop_animation` and `playing` for its animations.

#### Try this

* Make it nod when it's clicked: on_click, and mouse_on_shape and
  shape_at_mouse.same_as(Robot), then Robot.play_once "Nod".
* Slow its hop down with Robot.set_animation_speed 0.5.
* Make a model of your own in Blender: a cube with an armature, an
  action called "Walk", exported as a .glb beside this file.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**How far through an animation.** `animation_time` says how far through
the one playing it is, in seconds:

<!-- gui: frames 30; close -->
```cabby
use world

object dance : world.window {
	world.shape Robot

	void build {
		set_size 320, 240
		Robot = add_model "models/robot.glb", 0, 0, 0
		Robot.play Robot.animations[0]
		on_frame step
	}

	void step {
		if frame == 29 {
			println Robot.animation_time > 0
		}
	}
}

int main {
	dance D
	D.run
}
```

```text
true
```

[← Chapter 89](#chapter-89-coins-in-a-field) · [Contents](#contents) · [Chapter 91 →](#chapter-91-servers-and-connections)

---

## Part XVI: Talking over a network

[Chapter 41](#chapter-41-downloading) asked other computers for things.
This part answers: a server that programs connect to, a web server that
browsers visit, WebSockets that stay open both ways, all of it
encrypted, and email. Each chapter's program is a server and its own
visitors together, so it runs on its own; take the visitors out, and
anyone on the network can visit.

### Chapter 91: Servers and connections

*Program: `91-chat-server.cby`. Library: `use networks`.*

#### A server, and a connection to it

A **server** listens on a *port*, a number from 1 to 65535 that says which
program on a computer a connection is for. A **connection** is one
program talking to another, a line at a time. Here both ends are in one
program, so it can be run as it is:

<!-- run -->
```cabby
use networks
use events

object shouter {
	networks.server S
	networks.connection C

	void begin {
		S.on_line heard
		S.listen_here 0                 // 0: any free port
		C = networks.connect "localhost", S.port
		C.on_line answered
		C.send_line "hello"
		C.send_line "goodbye"
		events.run
	}

	// the server's handler, for each line a connection sends
	void heard {
		S.reply S.line.upper
	}

	// the connection's, for each line that comes back
	void answered {
		println "back came", C.line
		if C.line == "GOODBYE" {
			S.close
			C.close
		}
	}
}

int main {
	shouter Sh
	Sh.begin
}
```

```text
back came HELLO
back came GOODBYE
```

* **`S.listen_here 0`** listens on this computer alone, on any free port;
  `S.port` says which it got. **`S.listen 5000`** listens on port 5000
  for any computer that can reach this one (as its firewall allows).
* **`networks.connect Host, Port`** connects: `"localhost"` for this
  computer, or another's name or address (`"192.168.1.20"`). Nothing
  listening there is a panic that says so; `networks.can_connect` asks
  first.
* **`S.on_line`** and **`C.on_line`** call a method for each line that
  comes, with the line in `S.line` and `C.line`. **`S.reply`** answers
  the connection the line came from.
* The handlers are called from the event loop
  ([Chapter 35](#chapter-35-timers-and-the-event-loop)): `events.run`
  carries on while a server is listening or a connection has handlers,
  so closing them ends it.

A program that just takes turns needs no handlers: **`C.read_line`**
waits for the next line (`false` once the other end has gone), and
`C.line` is it.

#### Many connections

A server takes any number of connections. Each has a number, the same
for as long as it's open, so a lookup can keep what's known about each:

<!-- check -->
```cabby
use networks
use events

object chat_room {
	networks.server S
	lookup long str Names

	void begin {
		S.on_connect joined
		S.on_line said
		S.on_close left
		S.listen 5000
		events.run
	}

	void joined {
		Names[S.from] = "someone"
		S.reply "welcome! say 'name' and yours"
	}

	void said {
		if S.line.starts_with "name " {
			Names[S.from] = S.line[5 to]
		} else {
			S.send_line_to_all Names[S.from] + ": " + S.line
		}
	}

	void left {
		S.send_line_to_all Names[S.from] + " has gone"
		Names.remove S.from
	}
}

int main {
	chat_room Room
	Room.begin
}
```

* **`S.from`** is the connection a handler's about: in `on_connect` the
  one that came, in `on_line` the one that spoke, in `on_close` the one
  that went.
* **`S.send_line_to_all`** sends to every connection;
  **`S.send_line_to Who, Text`** to one; **`S.connections`** lists them.
* **`S.address_of Who`** is where one's from: `"192.168.1.20"`.
* **`S.close_connection Who`** sends one away. `on_close` is for the other
  end's closing (its program ended, or its computer left the network),
  so it isn't called for one closed this way.

With it running, `nc localhost 5000` in a terminal or two joins the room
(`nc` is netcat, on every Linux and Mac). A port below 1024 needs the
administrator, and one another program is listening on is a panic that
says so.

The chapter's program is the room with two visitors, Ada and Alan, who
join, talk, and leave, on a timer a step at a time.

#### In short

* `networks.server`: `listen` (any computer) or `listen_here` (this one),
  `port`; `on_connect`, `on_line`, `on_close`, with `from` and `line`;
  `reply`, `send_line_to`, `send_line_to_all`, `connections`,
  `address_of`, `close_connection`, `close`.
* `networks.connect Host, Port` gives a `networks.connection`:
  `send_line`, `read_line` and `line`, or `on_line`; `on_close`,
  `is_open`, `close`.
* A connection's number can be a lookup's key.

#### Try this

* Give the chapter's room a `me` command: `me waves` is heard as
  `* Ada waves`.
* Keep the last ten lines said, and send them to each newcomer.
* Run the room on its own with `listen 5000`, and join it from another
  computer: `nc` and this one's address (`networks.local_address`).

[← Chapter 90](#chapter-90-a-robot-from-blender) · [Contents](#contents) · [Chapter 92 →](#chapter-92-a-web-server)

---

### Chapter 92: A web server

*Program: `92-web-server.cby`. Library: `use networks`.*

#### Answering browsers

A browser asks a web server for a page by its *path* (`/`, `/about`,
`/search?for=milk`), and the server answers. `networks.web_server`
tells each request to one handler, which looks at what was asked and
answers it:

<!-- check -->
```cabby
use networks
use events

object site {
	networks.web_server Web
	int Visits

	void begin {
		Web.on_request answer
		Web.listen_here 8080
		println "open http://localhost:8080"
		events.run
	}

	void answer {
		Visits++
		if Web.path == "/" {
			Web.send_page "<h1>Hello!</h1><p>Visitor number " + Visits.text + ".</p>"
		} else if Web.path == "/hello" {
			Web.send_page "<p>Hello, " + Web.query_value("name") + "</p>"
		}
	}
}

int main {
	site S
	S.begin
}
```

With it running, `http://localhost:8080` in a browser is the page, and
`http://localhost:8080/hello?name=Ada` greets Ada. A request the handler
doesn't answer is answered "404 Not Found".

#### What was asked

| | |
|---|---|
| `method` | `"GET"` (a page asked for), `"POST"` (a form sent), `"PUT"`, `"DELETE"` |
| `path` | what's asked for: `"/search"` (decoded) |
| `query_value Name`, `has_query_value Name` | one value from after the `?`: `query_value "for"` is `"milk"` |
| `form_value Name`, `has_form_value Name` | one from a form that was sent (`<form method="post">`) |
| `body`, `body_json` | what came with it, as it came, or read as JSON |
| `header Name`, `cookie Name` | one of its headers; one of the browser's cookies |
| `address` | where it's from: `"192.168.1.20"` |

#### Answering

Each request gets one answer:

| | |
|---|---|
| `send_page Html` | a page |
| `send_text Text`, `send_json Value` | plain text; JSON, for a program or a page's script |
| `send_status Status, Why` | a short page saying so: `send_status 403, "Not for you."` |
| `send_not_found` | "404 Not Found" |
| `redirect Where` | sends the browser somewhere else: after a form, back to the list |
| `send_file Path` | a file, its kind from its name (`.png` a picture) |
| `send_from_folder Folder`, `send_from_folder_at Folder, Prefix` | the file in a folder that the path names, and nothing outside it |
| `add_header Name, Value`, `set_cookie Name, Value` | before it's sent: a header of its own; a cookie for the browser to keep and send back |
| `answered` | whether this request's been answered yet |

The chapter's program is a little recipe site: its pages, a search, a
form that adds a recipe and sends the browser back to the list, the
recipes as JSON, a cookie that remembers the last one added, a file from
a folder, a page that's forbidden, and one that isn't there. A worker
visits it as a browser would, one page after another, and prints what
came back:

```text
/: 200 <h1>Recipes</h1><p>pancakes, toast</p>
/search: 200 with milk: pancakes
POST /add: 200 <h1>Recipes</h1><p>pancakes, toast, porridge</p>
...
/secret: 403 (a page saying so)
/nowhere: 404 (a page saying so)
```

**Why a worker?** A web server answers from the event loop, between
other things; `networks.download` waits for its answer. Downloading from
the same program's server without a worker, it would wait for an answer
that can only come once it's stopped waiting. A worker
([Chapter 63](#chapter-63-workers)) waits on its own thread, and the
server answers meanwhile.

Requests are answered one at a time, in the order they come, as
everything in the event loop is: a handler that takes long keeps the
others waiting, so give a long job to a worker. It's HTTP; the next
chapters but one make it HTTPS.

#### In short

* `networks.web_server`: `on_request`, `listen` or `listen_here`, `port`.
* In the handler: `method`, `path`, `query_value`, `form_value`, `body`,
  `cookie`; then one answer: `send_page`, `send_text`, `send_json`,
  `send_status`, `redirect`, `send_from_folder`.
* A worker can visit a program's own server.

#### Try this

* Run the site on its own (`listen_here 8080`, and `events.run` after
  `Site.open`), and visit it in a browser. Add a page with a
  `<form method="post" action="/add">` to add recipes from.
* Make `/recipe?name=toast` show one recipe, or "404 Not Found".
* Count the visits to each page, and show them at `/visits` as JSON.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Asking whether, and sending a file.** `has_query_value` and
`has_form_value` say whether a value was sent at all (`query_value` is
`""` either way when it's empty or missing); `send_file` sends one file,
its kind from its name, and gives `false` if it isn't there, leaving
the request to be answered another way. A request sent with a body of
its own uses `set_body` (text and its kind) or `set_bytes`:

<!-- check -->
```cabby
use networks
use events

object pictures {
	networks.web_server Web

	void begin {
		Web.on_request answer
		Web.listen_here 8080
		events.run
	}

	void answer {
		if Web.path == "/search" && !Web.has_query_value("for") {
			Web.send_status 400, "Search for what?"
		} else if Web.method == "POST" && Web.has_form_value("name") {
			Web.send_text "thanks, " + Web.form_value("name")
		} else if !Web.send_file("public" + Web.path) {
			Web.send_not_found
		}
	}
}

int main {
	pictures P
	P.begin
}
```

<!-- check -->
```cabby
use networks

int main {
	networks.request R = networks.request_to "http://localhost:8080/notes"
	R.set_body "name,price\ntea,3.50\n", "text/csv"
	R.send
	println R.status, R.sent, "of", R.to_send, "bytes sent"
}
```

[← Chapter 91](#chapter-91-servers-and-connections) · [Contents](#contents) · [Chapter 93 →](#chapter-93-websockets)

---

### Chapter 93: WebSockets

*Program: `93-websockets.cby`. Library: `use networks`.*

#### Kept open, both ways

A page asks, and is answered, and that's all until it asks again. A
**WebSocket** is a connection a page (or a program) and the server keep
open, sending each other messages whenever they like: a chat, a game, a
score that changes as you watch. In a page's script it's
`new WebSocket("ws://...")`; on the server, a request that asks for one
comes to `on_request` like any other, and `accept_socket` takes it:

<!-- check -->
```cabby
use networks
use events

object scoreboard {
	networks.web_server Web
	int Home

	void begin {
		Web.on_request answer
		Web.on_message heard
		Web.listen_here 8080
		events.run
	}

	void answer {
		if Web.path == "/live" && Web.wants_socket {
			long Who = Web.accept_socket
			Web.send_message Who, "welcome: " + Home.text
		} else {
			Web.send_page "<h1>" + Home.text + "</h1>"
		}
	}

	void heard {
		if Web.message == "goal" {
			Home++
			Web.send_message_to_all "goal! " + Home.text
		} else {
			Web.reply "say 'goal'"
		}
	}
}

int main {
	scoreboard Board
	Board.begin
}
```

* **`wants_socket`** says the request asks to become a WebSocket, and
  **`accept_socket`** takes it, giving its number.
* **`on_message`** is called for each message one sends, with it in
  **`message`**; **`reply`** answers that one, **`send_message Who,
  Text`** sends to one, **`send_message_to_all`** to every one.
* **`on_socket_close`** is called when one goes; **`sockets`** lists
  those open, and **`close_socket Who`** closes one.

#### From a program

`networks.open_socket "ws://localhost:8080/live"` is a WebSocket from a
program, to Cabby's server or anyone's:

<!-- check -->
```cabby
use networks

int main {
	networks.socket Live = networks.open_socket "ws://localhost:8080/live"
	if Live.read_message {
		println Live.message           // "welcome: 0"
	}
	Live.send "goal"
	Live.read_message
	println Live.message               // "goal! 1"
	Live.close
}
```

`read_message` waits for the next one (`false` once it's closed);
`on_message` and `on_close` are handlers instead, from the event loop.

The chapter's program is the scoreboard, with two fans on a worker: one
keeps the score, and both see each goal the moment it's scored.

#### In short

* On a web server: `wants_socket`, `accept_socket`; `on_message` with
  `message`; `reply`, `send_message`, `send_message_to_all`; `sockets`,
  `on_socket_close`, `close_socket`.
* `networks.open_socket Url` from a program: `send`, `read_message` and
  `message`, `close`.

#### Try this

* Run the scoreboard on its own, and give its page a script that shows
  each message as it comes: `new WebSocket("ws://" + location.host +
  "/live").onmessage = e => document.body.innerHTML = e.data`.
* Keep the time of each goal, and send a newcomer every goal so far.
* Let only the first fan score: `Web.from` says which socket a message
  came from, and `close_socket` sends away one that tries anyway.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Bytes, not text.** A page's script can send bytes (a `Blob` or an
`ArrayBuffer`) rather than text; `message_is_binary` says which came:

<!-- check -->
```cabby
use networks
use events

object receiver {
	networks.web_server Web

	void begin {
		Web.on_request answer
		Web.on_message heard
		Web.listen_here 8080
		events.run
	}

	void answer {
		if Web.wants_socket {
			Web.accept_socket
		}
	}

	void heard {
		if Web.message_is_binary {
			Web.reply "thanks for " + Web.message.length.text + " bytes"
		} else {
			Web.reply "you said: " + Web.message
		}
	}
}

int main {
	receiver R
	R.begin
}
```

[← Chapter 92](#chapter-92-a-web-server) · [Contents](#contents) · [Chapter 94 →](#chapter-94-encrypted)

---

### Chapter 94: Encrypted

*Program: `94-encrypted.cby`. Library: `use networks`.*

#### Nothing read on the way

What goes over a network passes through other computers on the way, and
any of them could read it, or change it. **Encrypted**, none can: a web
server's pages are HTTPS, its WebSockets `wss://`, and a server's
connections TLS, as soon as it has a **certificate**:

<!-- check -->
```cabby
use networks
use events

object site {
	networks.web_server Web

	void begin {
		networks.make_certificate "localhost", "cert.pem", "key.pem"
		Web.set_certificate "cert.pem", "key.pem"
		Web.on_request answer
		Web.listen_here 8443
		println "open https://localhost:8443"
		events.run
	}

	void answer {
		Web.send_page "<h1>Hello, securely!</h1>"
	}
}

int main {
	site S
	S.begin
}
```

A certificate says which computer it is, signed by someone a browser
trusts, so a visitor knows it's talking to the right one. A site on the
internet gets one free from Let's Encrypt (with `certbot`), and uses its
two files as they are. For this computer, or a home network,
**`make_certificate`** makes one of its own: a browser warns that it
doesn't know who signed it (go on past the warning), and a Cabby program
is told to trust it with **`networks.trust_certificate`**.

| | |
|---|---|
| `networks.make_certificate Name, Certificate, Key` | a certificate of its own, and its key: two `.pem` files |
| `set_certificate Certificate, Key` | a web server's or a server's: encrypted from then on |
| `encrypted` | a server's or a connection's: whether it is |
| `networks.trust_certificate Path` | trusted from then on, as well as the system's own |
| `networks.connect_encrypted Host, Port` | an encrypted connection, its certificate checked as a browser checks a site's |
| `networks.can_connect_encrypted Host, Port` | whether it would connect, and be trusted |

Downloads from `https://` addresses, and `open_socket` to `wss://` ones,
are encrypted by themselves. A certificate that isn't trusted is a panic
that says so. It's OpenSSL underneath, which every Linux and Mac has.

The chapter's program is an encrypted site and an encrypted server (a
"vault"), with a visitor who can't connect until the certificate's
trusted, then can.

#### In short

* `make_certificate`, then `set_certificate`, and a server is encrypted.
* `trust_certificate` for one of your own; `https://`, `wss://` and
  `connect_encrypted` are encrypted connections.

#### Try this

* Take out `trust_certificate` in the chapter's program, and see what the
  download says.
* Make the certificate for this computer's name (`system.hostname`, with
  `use system`), `listen 8443`, and visit `https://` that name from
  another computer's browser.

[← Chapter 93](#chapter-93-websockets) · [Contents](#contents) · [Chapter 95 →](#chapter-95-emails)

---

### Chapter 95: Emails

*Program: `95-emails.cby`. Library: `use emails`.*

#### Making one

An email has who it's from and to, a subject, what it says, and files
attached, made as a mail program makes it:

<!-- run -->
```cabby
use emails
use files

int main {
	emails.message Note
	Note.set_from "Thomas Whalan <tom@example.com>"
	Note.add_to "ana@example.com"
	Note.set_subject "Lesson on Friday"
	Note.set_text "Hi Ana,\n\nThe lesson is at four this week.\n\nThomas"
	Note.save "cabby-note.eml"
	emails.received Back = emails.read_message "cabby-note.eml"
	println Back.from_name, "-", Back.subject
	files.delete "cabby-note.eml"
}
```

```text
Thomas Whalan - Lesson on Friday
```

| | |
|---|---|
| `set_from Address` | who it's from: `"ana@example.com"`, or with a name, `"Ana Rossi <ana@example.com>"` |
| `add_to`, `add_cc`, `add_bcc` | who it's to; copied in, as everyone sees; sent a copy, unseen |
| `set_reply_to Address` | where answers go |
| `set_subject`, `set_text`, `set_html` | the subject; what it says, as text, and as HTML too |
| `attach Path` | a file sent with it |
| `text`, `save Path` | the whole message as it's sent; saved as a `.eml` file |

#### Sending it

An **account** sends it, through its server, as a mail program does:

<!-- check -->
```cabby
use emails
use shell

int main {
	emails.message Note
	Note.set_from "tom@example.com"
	Note.add_to "ana@example.com"
	Note.set_subject "Lesson on Friday"
	Note.set_text "At four this week."
	str Password = shell.get "MAIL_PASSWORD"     // not written in the program
	emails.account Mail = emails.account_at "smtp.gmail.com", "tom@example.com", Password
	if Mail.send Note {
		println "sent"
	} else {
		println "it didn't go:", Mail.error
	}
}
```

Each provider says which server sends its mail: Gmail's is
`smtp.gmail.com`, iCloud's `smtp.mail.me.com` (with `set_port 587`),
Fastmail's `smtp.fastmail.com`. Most want an **app password**, made for
the program in the account's settings, rather than the account's own.
`emails.md` (Appendix I) has the others, and what each `error` means.

#### Reading a mailbox

<!-- check -->
```cabby
use emails
use shell

int main {
	emails.mailbox Inbox = emails.mailbox_at "imap.gmail.com", "tom@example.com", shell.get("MAIL_PASSWORD")
	println Inbox.unread_count, "unread of", Inbox.count
	for M in Inbox.unread {
		println M.from_name, "-", M.subject
		Inbox.mark_read M.number
	}
}
```

A mailbox gives `emails.received` messages: `from`, `subject`, `date`,
`text`, `html`, and their attachments (`attachment_name`,
`save_attachment`). `folders` and `open_folder` move between folders,
`latest`, `from`, `with_subject` and `containing` find messages, and
`move`, `delete`, `flag` and `save` look after them.

The chapter's program makes a lesson reminder with everything in it (a
copy, a hidden copy, HTML and an attachment), saves it, and reads it back
as a mailbox would give it.

#### In short

* `emails.message`: `set_from`, `add_to`, `set_subject`, `set_text`,
  `attach`, `save`.
* `emails.account_at Server, Login, Password`: `send`, `error`.
* `emails.mailbox_at`: `unread`, `count`, `mark_read`; and
  `emails.read_message` for a `.eml` file.

#### Try this

* Open the chapter's `.eml` file in a mail program (take out its
  `delete`).
* Send the reminder for real, with your own account and an app password.
* Read your inbox's newest five (`latest 5`), and print who each is from.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it. These need an account,
so they're checked rather than run.*

**Another port, and a server of your own.** `set_port 587` is for a
server that uses STARTTLS; `set_unencrypted` for one on this computer
alone (Proton Mail Bridge's, or a test's):

<!-- check -->
```cabby
use emails

int main {
	emails.account Bridge = emails.account_at "127.0.0.1", "tom@proton.me", "bridge-password"
	Bridge.set_port 1025
	Bridge.set_unencrypted
	emails.message Note
	Note.set_from "tom@proton.me"
	Note.add_to "ana@example.com"
	Note.set_subject "Hello"
	Note.set_text "Hi!"
	if !Bridge.send Note {
		println "it didn't go:", Bridge.error      // "Login denied", "Couldn't connect to server"...
	}
}
```

**Looking after a mailbox.** Folders, finding, flagging, attachments, and
copies put into a folder without sending:

<!-- check -->
```cabby
use emails
use shell

int main {
	emails.mailbox Mail = emails.mailbox_at "imap.gmail.com", "tom@example.com", shell.get("MAIL_PASSWORD")
	if !Mail.can_connect {
		println "it can't log in:", Mail.error
		return 1
	}
	Mail.open_folder "INBOX"
	for M in Mail.with_subject "lesson" {
		if !M.flagged {
			Mail.flag M.number
		}
		for I in M.attachments {
			M.save_attachment I, M.attachment_name(I)
		}
	}
	for M in Mail.from "ana@example.com" {
		Mail.mark_unread M.number
		Mail.unflag M.number
	}
	emails.message Draft
	Draft.set_from "tom@example.com"
	Draft.set_subject "Not sent yet"
	Mail.save_unread Draft, "Drafts"
	Mail.save_text "From: tom@example.com\r\nSubject: Saved\r\n\r\nKept.\r\n", "Archive"
}
```

[← Chapter 94](#chapter-94-encrypted) · [Contents](#contents) · [Chapter 96 →](#chapter-96-the-computer)

---

## Part XVII: The computer

What a program can find out about the computer it's running on, and
change: what it is and what it's doing, its battery, screen and volume,
its users, what's inside a Linux computer, and secrets kept safe. What a
computer says about itself is its own, so these chapters' outputs are one
computer's; yours will say something else.

### Chapter 96: The computer

*Program: `96-the-computer.cby`. Libraries: `use system`, `use os`.*

#### What it is

<!-- check -->
```cabby
use system

int main {
	println system.name, "(" + system.kind + ") on", system.hostname
	println system.processors, "processors"
}
```

| | |
|---|---|
| `system.kind` | `"linux"` or `"macos"`: for a program to do what each wants |
| `system.name`, `system.version` | `"Debian GNU/Linux 13 (trixie)"`, `"macOS 15.1"`; just the version |
| `system.hostname` | its name on the network |
| `system.kernel`, `system.machine` | the kernel's version; the processor's kind, `"x86_64"`, `"arm64"` |
| `system.processors` | how many processors: how many workers can run at once |
| `system.uptime` | seconds since it was turned on |

#### What it's doing

`os` is what's happening: how busy it is, its disks, and the programs
running.

<!-- check -->
```cabby
use os

int main {
	println "busy:", os.cpu_use.text(0) + "%; memory used:", os.memory_used / 1000000, "MB of", os.memory_total / 1000000
	for D in os.disks {
		println D.path + ":", D.free / 1000000000, "GB free of", D.size / 1000000000
	}
	for P in os.programs {
		if P.memory > 500000000 {
			println P.name, "(" + P.pid.text + ")", P.memory / 1000000, "MB"
		}
	}
}
```

* **`os.cpu_use`** is how busy the processors are, from 0 to 100;
  **`os.load`** how many programs wait for one, on average.
* **`os.disks`** are the computer's own disks, those plugged in, and those
  shared over the network: `path`, `size`, `free`, `kind`, `removable`.
  `os.free_space Path` is the free space where a file is.
* **`os.programs`** are those running, each with its `pid` (number),
  `name`, `command`, `user` and `memory`; `os.programs_named "firefox"`
  those with a name. `P.stop` asks one to stop, and `P.kill` stops it
  whatever it says.

#### Watching files change

An `os.watcher` is told of each change to a file or folder as it happens,
from the event loop:

<!-- check -->
```cabby
use os
use events

object watching {
	os.watcher Notes

	void begin {
		Notes.watch "notes"
		Notes.on_change changed
		events.run
	}

	void changed {
		if Notes.change == "renamed" {
			println Notes.old_path, "is now", Notes.path
		} else {
			println Notes.path, "was", Notes.change
		}
	}
}

int main {
	watching W
	W.begin
}
```

`change` is `"created"`, `"changed"`, `"deleted"` or `"renamed"`.
`watch` watches a folder and what's in it; `watch_all` the folders
inside it too.

#### Signals

A **signal** is the system tapping a program on the shoulder: Ctrl+C is
`"interrupt"`, a shutdown `"terminate"`. Caught, it's told to a handler
instead of ending the program, so a program can tidy up first:

<!-- check -->
```cabby
use os
use events

object careful {
	os.signals Signals

	void begin {
		Signals.catch "interrupt"
		Signals.on_signal stopping
		println "working: Ctrl+C to stop, tidily"
		events.run
	}

	void stopping {
		println "saving first, for", Signals.signal
		events.stop
	}
}

int main {
	careful C
	C.begin
}
```

`os.send_signal Pid, Name` sends one to another program (`"terminate"`
asks it to stop), and `os.pid` is this program's own number.

The chapter's program prints what its computer says about itself, then
watches a folder as it makes, renames and deletes a file in it, and
catches a signal it sends itself.

#### In short

* `system`: `kind`, `name`, `hostname`, `processors`, `memory`, `uptime`.
* `os`: `cpu_use`, `memory_used`, `disks`, `programs`; `os.watcher` with
  `watch` and `on_change`; `os.signals` with `catch` and `on_signal`.

#### Try this

* Print the five programs using the most memory.
* Watch your Downloads folder, and say each file that arrives.
* Catch `"interrupt"` in a program with a timer, and print how many times
  it ticked before stopping.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**More about memory and programs.** Memory available to programs, swap,
the processor time a program's had, and disks that can only be read:

<!-- check -->
```cabby
use os

int main {
	println os.memory_available / 1000000, "MB available; swap:", os.swap_used / 1000000, "of", os.swap_total / 1000000, "MB"
	for P in os.programs_named "firefox" {
		println P.pid, P.cpu_seconds.text(1), "seconds of processor time"
	}
	for D in os.disks {
		if D.read_only {
			println D.path, "can only be read"
		}
	}
}
```

**Every folder inside, and signals without an event loop.** `watch_all`
watches the folders inside a folder too (new ones as well). A program in
a loop of its own catches a signal with `os.catch_signal` and asks
`os.got_signal` whether it's come; `os.ignore_signal` ignores one, and
`os.usual_signal` puts it back as it was:

<!-- run -->
```cabby
use os

int main {
	os.catch_signal "user2"
	long Count = 0
	while !os.got_signal("user2") {
		Count++
		if Count == 1000 {
			os.send_signal os.pid, "user2"     // (another program would send it)
		}
	}
	println "stopped at", Count >= 1000
	os.ignore_signal "hangup"                // the terminal closing won't end it
	os.usual_signal "hangup"                 // and now it would again
}
```

```text
stopped at true
```

The signals a program can be sent are `"interrupt"`, `"terminate"`,
`"hangup"`, `"quit"`, `"user1"`, `"user2"`, `"resize"` (its terminal),
`"child"`, `"alarm"` and `"resume"`.

[← Chapter 95](#chapter-95-emails) · [Contents](#contents) · [Chapter 97 →](#chapter-97-power-brightness-and-volume)

---

### Chapter 97: Power, brightness and volume

*Program: `97-power-and-volume.cby`. Libraries: `use power`, `use brightness`, `use volume`.*

#### The battery

<!-- check -->
```cabby
use power

int main {
	if power.has_battery {
		print "battery:", power.battery_percent.text + "%"
		if power.charging {
			print " (charging)"
		}
		println ""
	} else {
		println "on mains power"
	}
}
```

`power.plugged_in` is `true` on mains power (charging, full, or with no
battery at all), and `power.battery_full` when it's as full as it gets.

#### The screen, and the sound

`brightness` and `volume` read and set how bright the screen is and how
loud the sound, from 0 to 100:

<!-- check -->
```cabby
use brightness
use volume

int main {
	if volume.can_set {
		int Was = volume.level
		volume.set 30
		volume.up 10                    // 40
		volume.mute
		println volume.muted
		volume.unmute
		volume.set Was                  // as it was
	}
	if brightness.can_set {
		brightness.down 20
		brightness.up 20
		println brightness.level
	}
}
```

* `level` is -1 when it can't be found out; `can_set` says whether
  there's anything to set.
* Volume is set with PipeWire's `wpctl`, PulseAudio's `pactl` or ALSA's
  `amixer` on Linux, whichever the computer has, and on a Mac through its
  own settings. Brightness is a laptop's own screen's (or a Mac's): most
  desktop screens set their own, with their buttons.

The chapter's program prints all three as a status bar would.

#### In short

* `power.has_battery`, `battery_percent`, `charging`, `plugged_in`.
* `brightness.level`, `set`, `up`, `down`, `can_set`.
* `volume.level`, `set`, `up`, `down`, `muted`, `mute`, `unmute`,
  `can_set`.

#### Try this

* Print a bar for each, ten blocks long: `"█".repeat(Level / 10)`, and
  `"░"` for the rest.
* Warn when the battery is below 20% and not plugged in.
* Fade the volume down to nothing over two seconds, with a timer.

[← Chapter 96](#chapter-96-the-computer) · [Contents](#contents) · [Chapter 98 →](#chapter-98-users)

---

### Chapter 98: Users

*Program: `98-users.cby`. Library: `use users`.*

#### Who's here

Everyone who logs in to a computer is a **user**: a login name, a home
folder, and **groups**, which say what they may do (the `sudo` group, or
`wheel`, may be the administrator).

<!-- check -->
```cabby
use users

int main {
	println "You're", users.name, "(" + users.full_name + "), at", users.home
	println "in", users.groups
	for Name in users.all {
		println Name, "is", users.full_name_of(Name)
	}
}
```

| | |
|---|---|
| `users.name`, `full_name`, `home`, `shell`, `id` | who's running the program |
| `users.is_root`, `users.groups`, `users.in_group Group` | whether they're the administrator; their groups |
| `users.exists Name` | whether there's a user called that |
| `users.full_name_of`, `home_of`, `id_of`, `groups_of` | the same, for someone else |
| `users.all`, `users.all_groups` | everyone who can log in; every group |

#### Making users

Making, changing and removing users needs the administrator: run the
program with `sudo`.

<!-- check -->
```cabby
use users

int main {
	users.make "guest", "A Guest", "visiting"
	users.make_group "band"
	users.add_to_group "guest", "band"
	println users.groups_of("guest")
	users.set_password "guest", "still visiting"
	users.remove_from_group "guest", "band"
	users.remove_group "band"
	users.remove "guest"                // and their home folder
}
```

`users.make_admin` makes one who may use `sudo`, and
`users.remove_keeping_home` keeps their home folder. They're done with
the system's own tools (`useradd` on Linux, `sysadminctl` on a Mac).

#### In short

* `users.name`, `home`, `groups`, `in_group`, `is_root`; `users.all`.
* With the administrator: `make`, `make_admin`, `set_password`, `remove`,
  `make_group`, `add_to_group`.

#### Try this

* Print each user's groups.
* Say which users share a group with you.
* As the administrator, make a user `guest`, check `users.exists`, and
  remove them again.

[← Chapter 97](#chapter-97-power-brightness-and-volume) · [Contents](#contents) · [Chapter 99 →](#chapter-99-linux)

---

### Chapter 99: Linux

*Program: `99-linux.cby`. Library: `use linux`.*

#### Inside the computer

What a Linux computer has that a Mac doesn't, read from the files the
kernel keeps for it. On a Mac, each is a panic that says so, so a program
for both asks `system.kind` first.

<!-- check -->
```cabby
use linux

int main {
	for T in linux.temperatures {
		println T.chip, T.label + ":", T.value, T.unit
	}
	for D in linux.block_devices {
		println D.name, D.kind, D.size / 1000000000, "GB", D.mounted_at
	}
	for U in linux.usb_devices {
		println "USB:", U.maker, U.name
	}
}
```

| | |
|---|---|
| `linux.sensors`, `linux.temperatures`, `linux.fans` | each with `chip`, `label`, `kind`, `value`, `unit`, and `high` and `critical` |
| `linux.block_devices` | disks, then their partitions: `name`, `kind`, `model`, `size`, `mounted_at`, `ssd`, `removable` |
| `linux.usb_devices` | `name`, `maker`, `vendor_id`, `product_id`, `speed` |
| `linux.modules`, `linux.module_loaded Name` | the kernel's drivers loaded; `load_module` and `unload_module` need the administrator |
| `linux.setting Name`, `linux.has_setting`, `linux.set_setting` | the kernel's settings: `"vm.swappiness"` is `"60"` |

#### Services

The programs systemd keeps running (ssh, Bluetooth, printing) are
**services**:

<!-- check -->
```cabby
use linux

int main {
	linux.service Ssh = linux.service_named "ssh"
	println Ssh.description + ":", Ssh.active, Ssh.enabled
	if !Ssh.running && !Ssh.start {
		println "starting it needs the administrator: sudo"
	}
	for S in linux.services {
		if S.active == "failed" {
			println "failed:", S.name
		}
	}
}
```

Each has `start`, `stop`, `restart`, `enable` (it starts with the
computer) and `disable`. `linux.user_services` are this person's own,
which need no administrator.

The chapter's program goes round inside its computer: temperatures,
disks, USB, modules, a setting, and the services that failed.

#### In short

* `linux.temperatures`, `fans`, `block_devices`, `usb_devices`, `modules`,
  `setting`.
* `linux.services`, `service_named`: `active`, `running`, `start`, `stop`.

#### Try this

* Warn when any temperature is within 10 degrees of its `high`.
* Say which disks can be taken out.
* List your own services (`linux.user_services`).

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it. Most of these need the
administrator, so they're checked rather than run.*

**When it's too hot, and changing things.** A sensor's `critical` is when
its chip says it's dangerous; the kernel's modules and settings, and the
system's services, can be changed by the administrator:

<!-- check -->
```cabby
use linux

int main {
	for T in linux.temperatures {
		if T.critical > 0 && T.value > T.critical - 10 {
			println "nearly too hot:", T.label, T.value
		}
	}
	for D in linux.block_devices {
		println D.name, "read only:", D.read_only
	}
	for U in linux.usb_devices {
		println U.name, "serial", U.serial
	}
	if !linux.module_loaded("btusb") {
		linux.load_module "btusb"            // (unload_module takes it out)
	}
	linux.set_setting "vm.swappiness", "10"  // until the computer starts again
	linux.service Cups = linux.service_named "cups"
	Cups.restart
	Cups.disable                             // it won't start with the computer
	Cups.enable
	for S in linux.user_services {
		println S.name, "is mine:", S.users_own
	}
}
```

[← Chapter 98](#chapter-98-users) · [Contents](#contents) · [Chapter 100 →](#chapter-100-secrets)

---

### Chapter 100: Secrets

*Program: `100-secrets.cby`. Library: `use secrets`.*

#### Random, for what no one must guess

`random 1 to 6` is quick (a game's dice, in [Chapter 68](#chapter-68-sprites-and-frames)), and
fine for games, but its numbers come from a pattern. `secrets`' come from
the system's own randomness, for a sign-in code, a password, or a key:

<!-- check -->
```cabby
use secrets

int main {
	println secrets.random_number(1, 6), secrets.random_text(12), secrets.random_password(16)
}
```

```text
4 rL9-x2Kq_mT0 Fw7#kPz2@qL9vTmB
```

#### Passwords

A program that signs people in should never keep their passwords. It
keeps a **hash** of each instead, which checks a password but can't be
turned back into one:

<!-- run -->
```cabby
use secrets

int main {
	str Kept = secrets.hash_password "correct horse battery"   // keep this
	println secrets.check_password("correct horse battery", Kept)
	println secrets.check_password("correct horse", Kept)
}
```

```text
true
false
```

`hash_password_argon2` uses Argon2id, the newest way, where the
computer's OpenSSL has it (`secrets.can_use_argon2`).

#### Locked with a password

<!-- run -->
```cabby
use secrets

int main {
	str Locked = secrets.lock "The safe's code is 4812.", "hunter2"
	println secrets.unlock(Locked, "hunter2")
	println secrets.can_unlock(Locked, "hunter3")
}
```

```text
The safe's code is 4812.
false
```

`lock` gives text to keep or send, which nothing but the password opens,
and which can't be changed without its being noticed. `lock_file` and
`unlock_file` do it for a file.

#### Signed

A **signature** shows that a message hasn't been changed, by whoever
has the key: `secrets.shared_signature Message, Key`, and `check_shared_signature` to
check it. With **public keys**, one key signs (the private one, kept)
and another checks (the public one, given to anyone):

<!-- run -->
```cabby
use secrets

int main {
	str Private = secrets.make_private_key
	str Public = secrets.public_key Private
	str Signed = secrets.sign "Cabby 1.0 is out", Private
	println secrets.check_signed("Cabby 1.0 is out", Public, Signed)
	println secrets.check_signed("Cabby 2.0 is out", Public, Signed)
}
```

```text
true
false
```

`secrets.same A, B` compares two codes in the same time however much of
them matches, so a stranger timing it learns nothing.

The chapter's program does each in turn: random codes, a password's hash,
a secret and a file locked, and messages signed both ways.

#### In short

* `random_number`, `random_text`, `random_password`.
* `hash_password` and `check_password`; never the password itself.
* `lock` and `unlock`; `shared_signature` and `check_shared_signature`; `sign` and
  `check_signed` with a key pair.

#### Try this

* Keep users' password hashes in a file, a line each, and write a log-in
  that checks one.
* Lock a note with a password typed in (`input`), and unlock it again.
* Sign a file's hash (`hashes.sha256_of_file`) with a private key, and
  check it with the public one.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Passwords without symbols, random bytes, Argon2, and bytes locked.**

<!-- run -->
```cabby
use secrets
use bytes

int main {
	str Plain = secrets.random_password_without_symbols 10
	println Plain.length, Plain.contains("#")
	bytes Key = secrets.random_bytes 32
	println Key.length
	if secrets.can_use_argon2 {
		str Kept = secrets.hash_password_argon2 "correct horse"
		println Kept.starts_with("$argon2id$"), secrets.check_password("correct horse", Kept)
	} else {
		println true, true                   // (an OpenSSL older than 3.2)
	}
	bytes Data = "a secret in bytes"
	bytes Locked = secrets.lock_bytes Data, "hunter2"
	println secrets.unlock_bytes(Locked, "hunter2").same_as(Data)
}
```

```text
10 false
32
true true
true
```

[← Chapter 99](#chapter-99-linux) · [Contents](#contents) · [Chapter 101 →](#chapter-101-the-desktop)

---

## Part XVIII: The desktop

The programs a person sees and hears around their own: notifications,
files opened in what opens them, the music playing, the printer, voices
that speak and listen, Bluetooth devices, D-Bus, which the desktop's
own programs talk over, and the monitors, mouse and keyboard's settings. Most of these make something happen on the
screen or the speakers, so their tutorials only do it when asked, with a
word after the program's name (`cabby 101-desktop.cby show`); without
it, as the tests run them, they only look.

### Chapter 101: The desktop

*Program: `101-desktop.cby`. Libraries: `use desktop`, `use mime`.*

#### A notification

<!-- check -->
```cabby
use desktop

int main {
	if !desktop.notify("Backup", "All 2,341 files copied.") {
		println "(nothing here shows notifications)"
	}
}
```

`desktop.notify` gives `false` when nothing shows it (no desktop, or no
notification server). `desktop.notify_numbered` gives its number, and
`desktop.notify_again Number, Title, Body` changes it where it is, for
progress: "37%", then "38%".

#### Opening things

`desktop.open` opens a file, a folder or a web address in whatever opens
it, as a double-click does:

<!-- check -->
```cabby
use desktop

int main {
	desktop.open "report.pdf"                 // in the PDF viewer
	desktop.open "https://www.example.com"    // in the browser
	desktop.open "mailto:ana@example.com"     // a new email to Ana
}
```

It's `xdg-open` on Linux, and `open` on a Mac. `desktop.on_a_mac` is
`true` on a Mac, for the rare thing that has to differ.

#### What kind of file

A file's **kind** (its MIME type) is what a browser, an email or a file
manager goes by: `image/png`, `text/plain`, `application/pdf`.

<!-- run -->
```cabby
use mime
use files

int main {
	println mime.of_name("holiday.jpeg"), mime.of_name("song.mp3"), mime.of_name("notes.md")
	files.write "cabby-what.txt", "just text"
	println mime.of_file("cabby-what.txt"), mime.is_text("cabby-what.txt"), mime.is_image("cabby-what.txt")
	files.delete "cabby-what.txt"
}
```

```text
image/jpeg audio/mpeg text/markdown
text/plain true false
```

`mime.of_name` goes by the name's ending, without looking (`""` for one
it doesn't know); `mime.of_file` by what's in the file, as the system's
`file` command does, so a picture renamed `.txt` is still a picture.
`is_image`, `is_text`, `is_audio` and `is_video` ask about a file.

The chapter's program says what kinds its files are, and with `show`
puts up a notification that counts to 100%, and opens a file and a web
page.

#### In short

* `desktop.notify`, `notify_numbered`, `notify_again`; `desktop.open`.
* `mime.of_name`, `of_file`; `is_image`, `is_text`, `is_audio`,
  `is_video`.

#### Try this

* Notify when a long job is done.
* List a folder's files, each with its kind, and count the pictures.
* Open a `mailto:` address with a subject:
  `"mailto:ana@example.com?subject=Lessons"`.

[← Chapter 100](#chapter-100-secrets) · [Contents](#contents) · [Chapter 102 →](#chapter-102-whats-playing)

---

### Chapter 102: What's playing

*Program: `102-media.cby`. Library: `use media`.*

Whatever's playing music or a video (Spotify, a browser's tab, VLC, the
Mac's Music) can be asked what it's playing, and told what to do, as a
keyboard's media keys do:

<!-- check -->
```cabby
use media

int main {
	if media.player == "" {
		println "nothing's playing"
		return 0
	}
	println media.player + ":", media.title, "by", media.artist, "from", media.album
	media.next
	println "now:", media.title
}
```

| | |
|---|---|
| `media.player` | the player's name (`""`: none) |
| `media.playing` | `true` while something's playing, not paused |
| `media.title`, `media.artist`, `media.album` | what's playing |
| `media.play`, `media.pause`, `media.play_pause` | |
| `media.next`, `media.previous` | the next song, or the one before |

On Linux it's MPRIS, over D-Bus, which every player speaks; with
several open, it's the one playing. On a Mac, Music and Spotify are
asked themselves.

The chapter's program says what's playing, and with `next`, `back`,
`pause` or `play` after its name, does that.

#### In short

* `media.player`, `playing`, `title`, `artist`, `album`.
* `play`, `pause`, `play_pause`, `next`, `previous`.

#### Try this

* Print what's playing every few seconds, but only when it changes.
* Make a window with ⏮ ⏯ ⏭ buttons.

[← Chapter 101](#chapter-101-the-desktop) · [Contents](#contents) · [Chapter 103 →](#chapter-103-printing)

---

### Chapter 103: Printing

*Program: `103-printing.cby`. Library: `use printing`.*

<!-- check -->
```cabby
use printing

int main {
	if !printing.can_print {
		println "no printer"
		return 1
	}
	println printing.printers, "and the default is", printing.default_printer
	printing.print "scales.pdf"
	printing.print_text "Lesson at four.\nBring the scales."
}
```

A file (a PDF, a picture, text) or text the program makes goes to the
default printer, or to another with settings:

<!-- check -->
```cabby
use printing

int main {
	printing.settings Sheet
	Sheet.set_printer "Office"
	Sheet.set_copies 2
	Sheet.set_two_sided
	Sheet.set_pages "1-3"
	Sheet.set_black_and_white
	Sheet.set_title "Week 7"
	str Job = printing.print_with "scales.pdf", Sheet
	println "printing as", Job
	while !printing.done(Job) {
		// (printing.cancel Job would stop it)
	}
	println "printed"
}
```

Each print gives the job's name (`""` if it didn't go): `printing.jobs`
lists those still waiting, `printing.done Job` says when one's finished,
and `printing.cancel` stops one. `set_landscape` prints sideways, and
`set_two_sided_short_edge` turns the pages as a notepad's. It's CUPS
underneath (`lp`), on Linux and a Mac.

The chapter's program lists the printers, and with `print` prints a
practice sheet twice, on both sides.

#### In short

* `printing.printers`, `default_printer`, `can_print`.
* `print`, `print_text`; `print_with` and `print_text_with` with a
  `printing.settings`.
* `jobs`, `done`, `cancel`.

#### Try this

* Print a file of your own.
* Print pages 1 to 3 only, sideways.
* Wait until a job's done, and say how long it took.

[← Chapter 102](#chapter-102-whats-playing) · [Contents](#contents) · [Chapter 104 →](#chapter-104-speech)

---

### Chapter 104: Speech

*Program: `104-speech.cby`. Library: `use speech`.*

#### Said aloud

<!-- check -->
```cabby
use speech

int main {
	speech.say "Hello from Cabby."
	speech.speaker Teacher
	Teacher.set_language "la"            // Latin
	Teacher.set_rate 120                 // slowly: words a minute
	Teacher.say "Salve, discipule."
	speech.save "The lesson is at four.", "reminder.wav"
}
```

`speech.say` waits until it's said. A `speech.speaker` keeps its
language, speed and voice (`set_voice`, one of `speech.voices`). It's
eSpeak NG on Linux (`espeak-ng`, a package on every Linux) and the Mac's
own voices; `speech.can_speak` says whether there's one.

#### Heard

What's said in a sound file, or to the microphone, as text, by Whisper,
on the computer itself (whisper.cpp's `whisper-cli`, and one of its
models, fetched once):

<!-- check -->
```cabby
use speech
use time

int main {
	if !speech.can_hear {
		speech.download_model "base.en"           // once: 148 MB
	}
	println speech.hear("reminder.wav")
	println "Say something:"
	println speech.hear_for(time.seconds(5))
}
```

A `speech.listener` hears in a language of its own (`set_language
"de"`), with a model of its own (`set_model`). Models ending `.en` hear
English alone, and do it better; the others hear about a hundred
languages, Latin among them. `speech.md` (Appendix I) says how to get
whisper.cpp on each computer.

The chapter's program, with `say`, speaks in English and in Latin; it
always saves what it would say into a file, and hears it back if Whisper
is there.

#### In short

* `speech.say`, `save`, `can_speak`; a `speech.speaker` with
  `set_language`, `set_rate`, `set_voice`.
* `speech.hear`, `hear_for`, `can_hear`, `download_model`.

#### Try this

* Say the time, every quarter of an hour.
* Read a file aloud, a line at a time, slowly.
* Write down what's said to the microphone, five seconds at a time, into
  a file.

[← Chapter 103](#chapter-103-printing) · [Contents](#contents) · [Chapter 105 →](#chapter-105-bluetooth)

---

### Chapter 105: Bluetooth

*Program: `105-bluetooth.cby`. Library: `use bluetooth`.*

#### What's known

<!-- check -->
```cabby
use bluetooth

int main {
	if !bluetooth.available {
		println "No Bluetooth here:", bluetooth.error
		return 1
	}
	bluetooth.turn_on
	for D in bluetooth.devices {
		println D.name, "(" + D.kind + "):", D.paired, D.connected, D.battery
	}
}
```

`bluetooth.devices` are those the computer knows: paired ones, and any
heard lately. Each has its `name`, `address`, `kind` (`"audio-headset"`,
`"input-keyboard"`), whether it's `paired`, `connected` and `trusted`,
its `battery` and how strongly it's heard (`strength`).

#### Searching, pairing and connecting

<!-- check -->
```cabby
use bluetooth
use time

int main {
	list bluetooth.device Heard = bluetooth.search time.seconds(10) else Why {
		println "It couldn't search:", Why
		return 1
	}
	bluetooth.device Phones = bluetooth.device_named "Ana's Headphones"
	if !Phones.found {
		println "not found: is it in pairing mode?"
		return 1
	}
	if !Phones.paired && !Phones.pair {
		println "it didn't pair:", bluetooth.error
		return 1
	}
	if !Phones.connect {
		println "it didn't connect:", bluetooth.error
	}
}
```

`disconnect` disconnects, `set_trusted true` lets it connect by itself,
and `forget` unpairs it, which is what a device whose keys have gone
stale needs before it pairs again (`examples/bluetooth-connect.cby` does
that). `bluetooth.error` says why the last thing didn't work. It's BlueZ
on Linux; on a Mac, it can look but not pair yet.

The chapter's program lists what's known, and searches first when given
`search`.

#### In short

* `bluetooth.available`, `is_on`, `turn_on`, `devices`, `search`,
  `device_named`.
* A device's `pair`, `connect`, `disconnect`, `forget`; `error`.

#### Try this

* Say which device is heard most strongly.
* Connect your headphones by name, and print `bluetooth.error` if it
  doesn't work.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Trusted, and turned off.** A trusted device connects by itself, without
asking; `turn_off` turns Bluetooth off:

<!-- check -->
```cabby
use bluetooth

int main {
	bluetooth.device Keys = bluetooth.device_named "Magic Keyboard"
	if Keys.found && Keys.paired {
		Keys.set_trusted true
	}
	bluetooth.turn_off
}
```

[← Chapter 104](#chapter-104-speech) · [Contents](#contents) · [Chapter 106 →](#chapter-106-d-bus)

---

### Chapter 106: D-Bus

*Program: `106-dbus.cby`. Library: `use dbus`.*

#### The desktop's own programs

On Linux, the desktop's programs talk to each other over **D-Bus**. Each
is a *service* on a *bus* (the session bus, the desktop's; the system
bus, the computer's), with *objects* that have *methods* to call and
*properties* to read. `desktop`, `media` and `bluetooth` are written in
Cabby over the `dbus` library; it's here for everything else:

<!-- check -->
```cabby
use dbus

int main {
	dbus.bus Bus = dbus.system
	str Battery = "/org/freedesktop/UPower/devices/DisplayDevice"
	json Charge = Bus.property "org.freedesktop.UPower", Battery, "org.freedesktop.UPower.Device", "Percentage"
	println "battery:", Charge.double, "%"
}
```

A method is called with a message, given its values one at a time:

<!-- check -->
```cabby
use dbus

int main {
	dbus.bus Bus = dbus.session
	dbus.message M = dbus.method_call "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner"
	M.add_str "org.freedesktop.Notifications"
	json Answer = Bus.call M
	println "notifications are shown by", Answer[0].str
}
```

* **`dbus.session`** and **`dbus.system`** connect, or panic with no bus;
  **`dbus.try_session`** gives one that isn't open instead (`is_open`).
* **`dbus.method_call Service, Path, Interface, Method`** makes a call;
  `add_str`, `add_int`, `add_bool` and the others give its values, in
  the order the method takes them.
* **`Bus.call M`** sends it and gives the answer as a JSON list;
  **`try_call`** doesn't panic, and `Bus.error` says why.
* **`Bus.property`** and **`Bus.properties`** read properties.

`busctl --user list` (and `busctl introspect`) shows what's on a bus, and
what each method takes.

#### Being told

`Bus.listen Interface, Signal` and `Bus.on_signal` call a handler when a
service says something's happened: a notification closed, a song
changed, a battery low. And a program can be a service itself, with
objects others call (`Bus.on_call`, `Bus.reply`). `dbus.md` (Appendix I)
has both.

The chapter's program lists the services on the session bus, and asks
who shows notifications.

#### In short

* `dbus.session`, `system`, `try_session`.
* `method_call`, `add_str` and the rest, `call`, `try_call`; `property`.
* `listen` and `on_signal`, to be told.

#### Try this

* Read the battery's percentage from the system bus.
* Be told when a notification is closed.
* Ask NetworkManager whether the computer's on a network
  (`org.freedesktop.NetworkManager`, property `State`).

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**Every kind of value.** A method's values are given in the order it
takes them, each of the kind its signature says; lists, dictionaries,
variants and structs are begun and ended around what's in them:

<!-- run -->
```cabby
use dbus

int main {
	dbus.message M = dbus.method_call "org.example.Shop", "/org/example/Shop", "org.example.Shop", "Order"
	M.add_path "/org/example/Shop/basket"
	M.add_bool true
	M.add_byte 7
	M.add_uint 3
	M.add_ulong 5000000000
	list str Items = {"tea", "cake"}
	M.add_strs Items
	M.begin_list "u"
	M.add_uint 1
	M.add_uint 2
	M.end_list
	M.begin_lookup "s", "v"
	M.add_str "note"
	M.begin_variant "s"
	M.add_str "no sugar"
	M.end_variant
	M.end_lookup
	M.begin_struct
	M.add_str "Ana"
	M.add_int 36
	M.end_struct
	println M.signature
}
```

```text
obyutasaua{sv}(si)
```

**Who sent a signal.** In a signal's handler, `signal_name` and
`signal_interface` say which it is and whose, `signal_path` the object
it came from, and `signal_sender` the program's own name on the bus:

<!-- check -->
```cabby
use dbus
use events

object listener {
	dbus.bus Bus

	void begin {
		Bus = dbus.session
		Bus.listen "org.freedesktop.Notifications", "NotificationClosed"
		Bus.on_signal heard
		events.run
	}

	void heard {
		println Bus.signal_interface + "." + Bus.signal_name, "from", Bus.signal_path, "(" + Bus.signal_sender + ")"
	}
}

int main {
	listener L
	L.begin
}
```

[← Chapter 105](#chapter-105-bluetooth) · [Contents](#contents) · [Chapter 107 →](#chapter-107-screens-the-mouse-and-the-keyboard)

---
### Chapter 107: Screens, the mouse and the keyboard

*Program: `107-screens.cby`. Libraries: `use screens`, `use mouse`, `use keyboard`.*

#### The monitors

<!-- check -->
```cabby
use screens

int main {
	if !screens.can_change {
		println "no screens here Cabby can change"
		return 1
	}
	for S in screens.all {
		println S.text
		println "  can show:", S.modes
	}
	screens.screen Main = screens.main
	println "the main one is", Main.name, "(" + Main.description + ")"
}
```

```text
DP-1: 2560x1440 at 59.95 Hz, scale 1.5, at 0,0
  can show: [2560x1440@59.95, 1920x1080@60]
HDMI-A-1: 1920x1080 at 60 Hz, at 1707,0
  can show: [1920x1080@60, 1920x1080@59.94, 1280x720@60]
the main one is DP-1 (Dell Inc. DELL U2720Q)
```

`screens.all` is every monitor plugged in, on or off. Each has its
`name` (`"DP-1"`, `"HDMI-A-1"`; a Mac's are `"built-in"` and `"display
2"`), its `description`, whether it's `on` and the `main` one, its
`width` and `height` in pixels, its `refresh`, `scale`, `x` and `y` on
the desktop, its `rotation`, and the `modes` it can show.
`screens.named "HDMI-A-1"` finds one by name.

Each desktop keeps these settings its own way, so Cabby asks each its own
way: Sway by `swaymsg`, GNOME by Mutter over D-Bus (as its Settings app
does), KDE Plasma by `kscreen-doctor`, any other desktop on X11 by
`xrandr`, and a Mac by CoreGraphics. `screens.desktop` says which it is.
Another Wayland compositor has no way Cabby knows yet, and there
`screens.all` is empty.

#### Changing one

<!-- check -->
```cabby
use screens

int main {
	screens.screen Side = screens.named "HDMI-A-1"
	if !Side.found {
		println "no HDMI-A-1 here"
		return 1
	}
	Side.set_rotation 90                     // on its side, for reading
	Side.set_position 2560, 0                // to the right of the main one
	Side.set_resolution 1920, 1080           // at the best refresh it has for it
	println Side.text
}
```

Each change gives `true` if it was done, and reads the screen again so
its values are the new ones; `set_refresh`, `set_scale`, `turn_on` and
`turn_off` work the same way. What a desktop can't do gives `false`: X11
has no scale, GNOME allows only the scales a mode lists, and a Mac can't
be turned or turned off from outside its own settings. Sway's changes
last until Sway starts again (its config keeps them for good); the
others' are kept as their Settings apps keep them.

#### The mouse and the touchpad

<!-- check -->
```cabby
use mouse

int main {
	println "speed:", mouse.speed, "natural scrolling:", mouse.natural_scrolling
	mouse.set_speed 0.3                      // from -1.0 to 1.0; 0.0 is the usual
	if mouse.has_touchpad {
		mouse.set_tap_to_click true
		mouse.set_touchpad_natural_scrolling true
	}
	println "cursor:", mouse.cursor_theme, mouse.cursor_size
	mouse.set_cursor_size 48                 // twice the usual 24
}
```

The mouse and the touchpad each have their own speed and scrolling,
since many people want natural scrolling on one and not the other.
`left_handed` swaps the buttons. The cursor's size and look
(`cursor_theme`) are set on Sway and GNOME; X11 can set the mouse and
touchpad but not the cursor. KDE Plasma's and a Mac's aren't here yet
(`mouse.can_change` is `false` there).

#### The keyboard

<!-- check -->
```cabby
use keyboard

int main {
	println keyboard.layouts, "typing with", keyboard.layout
	list str Mine = {"us", "de", "gr"}
	keyboard.set_layouts Mine                // English, German and Greek
	keyboard.set_repeat_delay 300            // milliseconds before a held key repeats
	keyboard.set_repeat_rate 40              // and then 40 times a second
	if keyboard.caps_lock {
		println "Caps Lock is on"
	}
}
```

A layout is named as X11's keyboard files name it, which Sway, GNOME and
X11 all use: `"us"`, `"gb"`, `"de"`, `"gr"`, and a variant in
parentheses: `"us(dvorak)"`. The first one set is the one typed with.
`caps_lock` and `num_lock` are read from the keyboard's own lights, on
any Linux.

The chapter's program shows what all three are set to, and changes
nothing unless asked: `cabby 107-screens.cby bigger` makes the cursor
twice as big for five seconds, then puts it back.

#### In short

* `screens.all`, `named`, `main`, `can_change`, `desktop`; a screen's
  `text`, `modes`, `set_resolution`, `set_rotation`, `set_scale`,
  `set_position`, `turn_on`, `turn_off`.
* `mouse.speed`, `natural_scrolling`, `left_handed`, the touchpad's own,
  `tap_to_click`, `cursor_size`, `cursor_theme`, each with its `set_`.
* `keyboard.layouts`, `set_layouts`, `repeat_delay`, `repeat_rate`,
  `caps_lock`, `num_lock`.
* Each desktop's own way, asked by Cabby: Sway, GNOME, KDE (screens),
  X11, a Mac (screens).

#### Try this

* Turn your second monitor on its side, and back again.
* Print the layout you're typing with every second, only when it
  changes.
* Write a "presenting" program that turns the projector on and makes the
  cursor bigger, and a "done" one that puts both back.

#### More examples

*The rest of what's in the reference tables, for when you need it. Skip
this on a first read: nothing later depends on it.*

**A mode of your own, and a monitor off.** `set_mode` sets the resolution
and refresh together; `turn_off` won't turn off the last monitor that's
on. A screen in a `for` loop over `screens.all` can only be read, so one
to change is found by name:

<!-- check -->
```cabby
use screens

int main {
	for S in screens.all {                   // (each S can be read, not changed)
		if !S.main && S.on {
			screens.screen Other = screens.named S.name
			Other.turn_off
		}
	}
	screens.screen Main = screens.main
	if !Main.set_mode(1920, 1080, 60.0) {
		println "it has no 1920x1080 at 60 Hz:", Main.modes
	}
	Main.set_scale 1.0
	println "screens:", screens.count, "on", screens.desktop
}
```

**One layout, and the speed of the touchpad.**

<!-- check -->
```cabby
use keyboard
use mouse

int main {
	keyboard.set_layout "us(dvorak)"
	println keyboard.repeat_delay, keyboard.num_lock, keyboard.has_lock_lights
	mouse.set_touchpad_speed 0.5
	mouse.set_left_handed false
	mouse.set_cursor_theme "Adwaita"
	println mouse.touchpad_speed, mouse.left_handed, mouse.can_change, keyboard.can_change
}
```

[← Chapter 106](#chapter-106-d-bus) · [Contents](#contents) · [Appendices →](#appendices)

---
## Appendices

Reference material, for looking things up once you know the language.

### Appendix A: The cabby command

| Command | What it does |
|---|---|
| `cabby` | start the interactive session ([Appendix B](#appendix-b-the-interactive-session)) |
| `cabby prog.cby [args...]` | run a program; the arguments go to `main` |
| `cabby -c prog.cby` | compile to a native program `prog` (`--compile` is the long form) |
| `cabby -c prog.cby -o name` | ...called `name` |
| `cabby -c prog.cby -O0` | ...with optimization level 0 to 3 (default `-O2`) |
| `cabby -c prog.cby --emit-llvm` | write the compiled code as LLVM IR (`prog.ll`) instead |
| `cabby -c --checked prog.cby` | build it with AddressSanitizer: reading past the end, memory used after it's freed, and (on Linux) memory never freed, each stopped and said where. For testing |
| `cabby --check prog.cby` | check the program without running it |
| `cabby --style prog.cby` | check, then list names, and calls' parentheses, that don't follow the style guide ([Appendix J](#appendix-j-style-guide)) |
| `cabby --dump-tokens prog.cby` | show how the program is split into tokens |
| `cabby --dump-ast prog.cby` | show the parsed program as a tree |
| `cabby --help` | list all of this |

Running and compiling give the same results, the same panic messages, and
the same exit status. Compiling needs clang 15 or newer (`$CABBY_CC` chooses
another clang).

**Where `use` looks.** `use name` searches `$CABBY_LIB`, then `lib/` next to
the `cabby` program, then `../lib/cabby` relative to it, then
`/usr/local/lib/cabby` and `/usr/lib/cabby`. `use local name` looks next to
the file that says it.

**Installing.** From Cabby's source folder: `make`, then `sudo make install`
(to `/usr/local`), or `make install PREFIX=$HOME/.local` for one user.
`sudo make uninstall` removes it again. `make test` runs Cabby's test suites,
including every tutorial and every example in this book.

**Running a file directly.** With `#!/usr/bin/env cabby` as its first line,
and `chmod +x prog.cby` once, `./prog.cby` runs it, as a shell script runs.
(That first line is the only place Cabby takes a `#`.)

**Editing in Vim.** `make install-vim` (run without sudo) gives Vim colors
for Cabby and indenting that follows its layout rules: Enter after `{` goes
one tab in, typing `}` lines it up, and `gg=G` re-indents a whole file. For
Neovim, add `VIMDIR=~/.config/nvim`. Spaces used for indenting and
semicolons at the ends of lines are marked in red.

[Contents](#contents)

### Appendix B: The interactive session

Run `cabby` with no file to type Cabby a piece at a time:

```text
$ cabby
Cabby 0.39.2 interactive session. Type statements and declarations; "exit" to finish, "cabby.help" for more.
Cabby comes with ABSOLUTELY NO WARRANTY, under the MIT License; "cabby.license" for details.
>>> int add int a, int b {
... 	return a + b
... }
>>> int x = add 2, 3
>>> x * 10
50
>>> exit
Save this session? [y/N] y
File name: adding
Session saved to adding.cby
```

* **What you type** is either a declaration (`use`, `object`, `struct`,
  `union`, `enum`, `aka`, `extern`, a function) or statements.
  Statements become the body of `main`, so variables you declare keep their
  values for the whole session. Functions can't see them; pass them in.
* **An expression on its own is printed**, as if you'd typed `println`
  before it: `x * 10` shows `50`.
* **Each prompt starts on a new line.** So `print "one"` and then
  `print " two"`, typed as two inputs, show on two lines, where a program
  would show `one two`. Lines typed between `{` and `}` run as one input,
  and their output is as a program's.
* **Everything is checked together.** After each input the whole session is
  checked again as one program, so a string you moved three inputs ago is
  still moved. An input that fails to check is reported and forgotten.
* **Several lines.** While a `{`, `(`, `[` or `/*` comment is open, or
  after a line like `if x > 0` that needs its `{`, the prompt becomes `...`
  and the next line is indented for you. Typing `}` removes a tab. In a
  `/*` comment that begins its line, the next line starts ` * `, as in Vim,
  and typing `/` straight after it closes the comment. After an `if` block, press
  Enter on an empty line to run it (or type the next statement), since an
  `else` might follow; after a final `else` block it runs straight away.
* **Redefining a function** replaces it, if every earlier input that calls
  it still checks. Types can't be redefined.
* **A panic or Ctrl-C** abandons that input and returns to the prompt.
* **Ending.** `exit` or Ctrl-D ends the session (destructors run, as at the
  end of `main`); `return n` ends it with exit status `n`. Cabby offers to
  save what you typed as an ordinary `.cby` program, with each function's
  latest version. If the session has an `exit` of its own (a variable, or
  `use standard`'s), `exit` means that, and `cabby.exit` ends the session.
* **The session's own commands** start with `cabby.`, so they never take a
  name from your program:

  | | |
  |---|---|
  | `cabby.exit` | end the session |
  | `cabby.clear` | clear the screen |
  | `cabby.preserve` | keep the session as it is, to bring back another time (`cabby.preserve work` gives it a name) |
  | `cabby.restore` | bring it back: its inputs run again, quietly, so its variables and functions are back (`cabby.restore work`) |
  | `cabby.remove` | remove a preserved session (`cabby.remove work`) |
  | `cabby.sessions` | list the preserved sessions |
  | `cabby.version` | which Cabby this is |
  | `cabby.license` | Cabby's license (MIT) |
  | `cabby.help` | list these |

  Preserved sessions are kept in `~/.cabby_sessions`. Restoring runs every
  input again, so anything they did outside the session (writing a file,
  say) happens again too.
* **Tab completes** a name or what follows a dot: `files.` then Tab lists
  `files.read` and the rest, `Rex.` lists the fields and methods of `Rex`'s
  object, `Names.` the members of a list, `gui.` the gui library's types,
  `Na` your variables, functions and keywords starting with `Na`, and
  `use ` the libraries (`use local ` the headers and folders next to you).
  One match is filled in; several are filled in as far as they agree, then
  listed under the line. When there's nothing to complete, Tab is a tab,
  and so is a second Tab straight after a list: `Name` Tab Tab `// note`.
* **Editing keys.** Left/Right, Home/End; Up/Down for history (kept in
  `~/.cabby_history`); Ctrl-U, Ctrl-K, Ctrl-W delete to the start, to the
  end, or the previous word; Ctrl-L clears the screen. Pasting a block keeps
  its indentation. `cabby < steps.txt` reads lines from a file without
  prompts.
* **Colors.** What you type is colored as you type it, in the colors Vim
  gives Cabby code: keywords yellow, types green, strings and numbers
  magenta, built-ins like `println` bold cyan, members like `.length` and
  escapes like `\t` pink, comments blue, `use` lines bright blue, and
  `cement`, a stray `;`, or `is`, `and`, `or` and `not` (Cabby writes
  `==`, `&&`, `||` and `!`) in red. The line of code shown under an error is
  colored the same way, in every mode. They're your terminal's own 16
  colors, so they follow its theme. Set `NO_COLOR=1` to turn color off,
  or change any color with `CABBY_COLORS`, written like `LS_COLORS`:
  `CABBY_COLORS="comment=36:keyword=1;33"` makes comments cyan and keywords
  bold yellow. The names are `keyword`, `type`, `constant`, `special`,
  `function`, `use`, `comment` and `error`, and the numbers are the
  terminal's: 30–37 for colors, 90–97 for bright ones, 1 for bold.

[Contents](#contents)

### Appendix C: Keywords

Every keyword is reserved: none can name a variable or function. `typedef` is reserved too, though Cabby uses `aka` instead.

| Keyword | Meaning | Chapter |
|---|---|---|
| `aka` | another name for a type: `double aka celsius` | [20](#chapter-20-aka-another-name-for-a-type) |
| `callback` | a C function pointer's shape: `callback int comparer address A, address B` | [28](#chapter-28-addresses) |
| `kind` | a number of its own: `long kind meters` (only a keyword there) | [20](#chapter-20-aka-another-name-for-a-type) |
| `and` `or` `not` `is` | reserved: Cabby writes `&&`, `\|\|`, `!` and `==` | [6](#chapter-6-if-and-else) |
| `auto` | reserved: Cabby asks for the type to be written | [2](#chapter-2-variables-and-the-basic-types) |
| `bool` `true` `false` | the boolean type and its values | [2](#chapter-2-variables-and-the-basic-types) |
| `break` | leave a loop or a `switch` | [8](#chapter-8-for-loops-break-and-continue), [9](#chapter-9-switch) |
| `by` | the step of a counting loop | [8](#chapter-8-for-loops-break-and-continue) |
| `case` `default` | labels inside a `switch` | [9](#chapter-9-switch) |
| `char` `short` `int` `long` | integer types | [3](#chapter-3-integer-types) |
| `const` | can't be changed; read-only borrows and methods | [5](#chapter-5-constants) |
| `continue` | go on with a loop's next turn | [8](#chapter-8-for-loops-break-and-continue) |
| `do` | a loop that tests at the end | [7](#chapter-7-while-until-and-do) |
| `double` `float` | floating-point types | [4](#chapter-4-floating-point) |
| `down` | count downwards: `for i in 10 down to 1` | [8](#chapter-8-for-loops-break-and-continue) |
| `else` | the other path of an `if` | [6](#chapter-6-if-and-else) |
| `enum` | named integer constants | [19](#chapter-19-enums) |
| `extern` | declare a C function | [27](#chapter-27-calling-c) |
| `for` | counting loops, and the three-part loop | [8](#chapter-8-for-loops-break-and-continue) |
| `goto` | jump forward to a label | [30](#chapter-30-goto-and-labels) |
| `if` | choose a path | [6](#chapter-6-if-and-else) |
| `in` | `for i in ...`, `for X in L`, `for K, V in L` | [8](#chapter-8-for-loops-break-and-continue), [15](#chapter-15-lists), [16](#chapter-16-lookups) |
| `inner` | a field or method only the object (and its children) can use | [24](#chapter-24-inheritance) |
| `list` | a growable list: `list int Scores` | [15](#chapter-15-lists) |
| `lookup` | values found by key: `lookup str int Ages` | [16](#chapter-16-lookups) |
| `object` | a class: fields, methods, destructor, parents | [22](#chapter-22-objects-and-methods) |
| `register` | a variable that can't be borrowed | [29](#chapter-29-register-and-volatile) |
| `return` | leave a function, with a value | [10](#chapter-10-functions) |
| `self` | the object a method was called on | [22](#chapter-22-objects-and-methods) |
| `signed` `unsigned` | integer signedness | [3](#chapter-3-integer-types) |
| `sizeof` | the size of a type or value in bytes | [3](#chapter-3-integer-types) |
| `static` | kept between calls; private to a file; methods with no object | [21](#chapter-21-static-and-global-variables), [22](#chapter-22-objects-and-methods) |
| `str` | the string type | [12](#chapter-12-strings) |
| `struct` | plain data with named fields | [17](#chapter-17-structs) |
| `switch` | choose by value | [9](#chapter-9-switch) |
| `to` | the end of a counting loop's range | [8](#chapter-8-for-loops-break-and-continue) |
| `union` | fields sharing memory | [18](#chapter-18-unions) |
| `until` | a loop that repeats until its condition is true | [7](#chapter-7-while-until-and-do) |
| `use` | bring in a header | [27](#chapter-27-calling-c), [33](#chapter-33-splitting-a-program-into-files) |
| `void` | no value | [10](#chapter-10-functions) |
| `volatile` | every access really happens | [29](#chapter-29-register-and-volatile) |
| `while` | a loop that tests first | [7](#chapter-7-while-until-and-do) |

`print` and `println` are built-in functions, not keywords. `handler` is the
type of an object's method kept to be called later, such as a window's event
handler ([Chapter 51](#chapter-51-your-first-window), [Chapter 26](#chapter-26-handlers-in-your-own-objects)).

[Contents](#contents)

### Appendix D: Types

| Type | Bytes | Copied or moved | Notes |
|---|---|---|---|
| `bool` | 1 | copied | `true`, `false` |
| `char`, `signed char`, `unsigned char` | 1 | copied | prints as a character |
| `short`, `unsigned short` | 2 | copied | |
| `int`, `unsigned` | 4 | copied | |
| `long`, `long long`, `unsigned long` | 8 | copied | |
| `float` | 4 | copied | about 7 digits |
| `double` | 8 | copied | about 16 digits |
| enum | 4 | copied | an `int` |
| `str` | 16 | moved | owned text |
| `T name[N]` | N × T | like T | fixed size, `.length` |
| `list T` | 24 | moved | grows; owns its elements |
| `lookup K V` | 64 | moved | values by key; owns its keys and values |
| `struct` | its fields, laid out as C expects | copied if all fields are | |
| `union` | its largest field | copied | plain-data fields only |
| `object` | 8 | moved | |
| `T &`, `const T &` | 8 | borrowed | parameters and locals only |
| `T name[]` | 16 | borrowed | parameters only: an array of any length |

**Conversions.** Inside expressions, the smaller of two types is widened to
the larger, and an integer meeting a floating-point value becomes floating
point.
Assignments, arguments and returns convert automatically only when nothing
can be lost: a smaller integer to a larger one, any integer to `float` or
`double`, `float` to `double`, `bool` to an integer, an integer to `bool`.
A constant converts if its value fits. Anything else needs a cast:
`(int)x`.

**Literals.**

```text
42  0x2A  0b101010  0o52      int, or long if too big; suffixes u U l L ul ll ull
1.5  2e10  1.5f               double; f for float
'a'  '\n'  '\x41'  '\0'       char
"text\n"                      str
true  false                   bool
```

**Built-in members.**

| | On | Gives |
|---|---|---|
| `.length` | strings, arrays, lists and lookups | the number of characters, elements or keys |
| `.copy` | strings, lists and lookups | an independent copy |
| `.text` | numbers, `bool`, `char` | the value as a `str` |
| `.contains`, `.find` | strings and lists | whether it's there; where it first is, or -1 |
| `.starts_with`, `.ends_with`, `.replace`, `.trim`, `.upper`, `.lower`, `.repeat` | strings | see [Chapter 12](#chapter-12-strings) |
| `.is_int`, `.to_int`, `.is_long`, `.to_long`, `.is_number`, `.to_double` | strings | text to numbers |
| `.split` | strings | a `list str` |
| `.add`, `.insert`, `.remove_at`, `.take_last`, `.clear` | lists | adding and removing ([Chapter 15](#chapter-15-lists)) |
| `.sort`, `.reverse` | lists | in order, smallest first; last element first |
| `.join` | a `list str` | one `str`, with a separator between |
| `.contains`, `.remove`, `.clear` | lookups | by key ([Chapter 16](#chapter-16-lookups)) |
| `.keys`, `.values` | lookups | a list of copies, in the order the keys were added |

[Contents](#contents)

### Appendix E: Operators

From tightest to loosest binding:

| Operators | Meaning |
|---|---|
| `f(...)` `a[i]` `a.b` | call, index, field or method |
| `-a` `+a` `!a` `~a` `(type)a` `sizeof` | negate, plus, not, bitwise not, cast, size |
| `*` `/` `%` | multiply, divide, remainder |
| `+` `-` | add (or join strings), subtract |
| `<<` `>>` | shift |
| `<` `<=` `>` `>=` | compare |
| `==` `!=` | equal, not equal |
| `&` | bitwise and |
| `^` | bitwise exclusive or |
| `\|` | bitwise or |
| `&&` | logical and (short-circuit) |
| `\|\|` | logical or (short-circuit) |

A call (`f a, b`) takes everything after it, to the end of the line or the
closing parenthesis or square bracket around it ([Chapter 10](#chapter-10-functions)).

These are statements, not operators inside expressions: `=`, the compound
assignments `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`, and `++` and
`--`.

Not in Cabby: `?:` (use `if`), the comma operator, and pointer operators
(`*p`, `&x`, `->`), because borrows replace pointers.

[Contents](#contents)

### Appendix F: What is checked, and when

**Before the program runs** (`cabby --check` does only this):

* the layout: tabs, braces, one statement per line;
* every type, conversion and call;
* `const`: nothing constant is changed;
* ownership: no value is used after it moves;
* borrowing: many readers or one writer, nothing moved while borrowed, no
  borrow outliving its value;
* objects: overrides match, hierarchies are unambiguous;
* `goto` jumps only forward; constant array indexes are in range;
* handlers name a window method with no parameters.

Warnings (the program still runs) point out likely mistakes, such as
`x & 1 == 0`.

**While the program runs** (a *panic*: exit status 101, a message giving
the file, line and column):

* an array or string index out of bounds;
* integer division or `%` by zero, and `INT_MIN / -1`;
* a shift by a negative amount or by the type's width or more;
* recursion deeper than 100,000 calls (interpreted), or running out of stack
  (compiled).

**Never**, because they're defined to be safe: integer overflow (wraps),
floating-point division by zero (`inf`, `NaN`), and casting a too-large
floating-point value to an integer (saturates).

**Not checked**: what a C function does with its arguments
([Chapter 27](#chapter-27-calling-c)).

[Contents](#contents)

### Appendix G: The gui library

`use gui`. All names start with `gui.`. This is a summary; `gui.md`
(Appendix I) has every detail.

**Windows** (`object mine : gui.window`):

| Member | |
|---|---|
| `build` | override it: add the contents and handlers |
| `run` | build (the first time), show, handle events; returns when closed |
| `set_title Text`, `set_size w, h`, `close` | the title bar, the starting size, closing |
| `every D, h`, `after D, h` (durations), `stop_timers` | timers ([Chapter 54](#chapter-54-timers)) |
| `show_message Text`, `ask Question, h` | message and yes/no dialogs |
| `ask_open_file h`, `ask_save_file Name, h`, `chosen_file` | file dialogs ([Chapter 55](#chapter-55-dialogs-and-several-windows)) |

**Containers** (windows, `gui.row`, `gui.column`) add, and return:

| | |
|---|---|
| `add_label Text` | `gui.label` |
| `add_button Text` | `gui.button`: `on_click h` |
| `add_entry` | `gui.entry`, one line: `on_change h`, `on_enter h` |
| `add_check Text` | `gui.check`: `checked`, `set_checked b`, `on_toggle h` |
| `add_choice` | `gui.choice`, a drop-down: `add_option t`, `selected`, `set_selected i`, `count`, `on_change h` |
| `add_text_area` | `gui.text_area`, several lines: `on_change h`, `set_editable b` |
| `add_list` | `gui.list`: `add_item t`, `clear`, `count`, `selected`, `set_selected i`, `on_select h` |
| `add_slider Min, Max` | `gui.slider`: `value`, `set_value v`, `on_change h` |
| `add_progress` | `gui.progress`: `set_fraction f` (0.0 to 1.0) |
| `add_image Path` | `gui.image`: `set_file Path` (`""` clears it) |
| `add_row`, `add_column` | `gui.row` (side by side), `gui.column` (top to bottom) |
| `add_separator` | a dividing line |

**Every widget** has `set_text Text`, `text` (for a drop-down or list, the
chosen entry), `set_enabled b`, `set_visible b` and `set_minimum_size w, h`.

**Handlers** name one of the window's own methods, with no parameters and
no result, written without parentheses: `SaveButton.on_click save`.

**Testing without a person.** With `CABBY_GUI_SCRIPT` set to `;`-separated
commands, windows follow it instead of waiting: `click <button>`,
`type <n> <text>`, `enter <n>`, `write <n> <text>` (a text area; `\n` for a
new line), `check <label>`, `choose <n> <option>`, `select <n> <item>`,
`slide <n> <value>`, `wait <ms>`, `answer yes|no`, `pick <path>`, `cancel`,
`shot <file.png>`, `close`. Numbers count widgets of that kind from 1. Each
`close` ends one window; the rest goes to the next. For example:

```sh
CABBY_GUI_SCRIPT="type 1 Ada; click Say hello; shot greeter.png; close" cabby 51-first-window.cby
```

[Contents](#contents)

### Appendix H: Not in Cabby yet

* Objects pointing at each other freely (a chain or tree can be built
  from objects holding a `list` of their own kind, or with `use addresses`;
  see [Chapter 28](#chapter-28-addresses)).
* Sorting a list of objects (by a field, or by a function that compares
  two of them); `.sort` takes numbers, `bool`, `char` and strings.
* `.upper` and `.lower` for letters beyond A to Z (é, α).
* Panels and bars of a desktop (a dock, a system tray) in the gui
  library.
* Shadows and physics in 3D (`use world`).
* The `?:` operator, `long double`, and backward `goto`.
* Borrowing tracks fields separately, but not list elements: while
  `Scores[0]` is borrowed, `Scores[1]` can't be changed either. This only
  ever refuses programs; it never lets an unsafe one through.

[Contents](#contents)

### Appendix I: Library reference pages

Each library has a reference page: everything there is to know about it, in
one place. They sit next to the headers, in Cabby's library folder
(`/usr/local/lib/cabby` after `sudo make install`, or `lib/` in the source
folder):

| Page | Library |
|---|---|
| `gui.md` | windows, widgets, timers and dialogs (`use gui`) |
| `tui.md` | the same windows in a terminal (`use tui`) |
| `game.md` | games: sprites, frames, keys, collisions and a camera (`use game`) |
| `tgame.md` | the same games in a terminal (`use tgame`) |
| `sound.md` | sound files, notes, MIDI songs, and recording (`use sound`) |
| `standard.md` | `printf`, `abs`, `rand`, `srand`, `exit`, `system` (`use standard`) |
| `math.md` | `sqrt`, `pow`, `sin`, `floor` and the rest (`use math`) |
| `strings.md` | `strlen`, `strcmp`, `strncmp` (`use strings`) |
| `files.md` | `files.read`, `files.lines`, `files.write` and the rest (`use files`) |
| `directories.md` | `directories.list`, `directories.make` and the rest (`use directories`) |
| `shell.md` | `shell.run`, `shell.status` and environment variables (`use shell`) |
| `processes.md` | programs your program starts and talks to (`use processes`) |
| `events.md` | timers and the event loop (`use events`) |
| `time.md` | moments, durations, the clock and sleeping (`use time`) |
| `dates.md` | dates, times of day, time zones, and dates in other languages (`use dates`) |
| `json.md` | reading and writing JSON, and structs to and from it (`use json`) |
| `csv.md` | tables in CSV, as spreadsheets save them: rows, records, and writing them (`use csv`) |
| `html.md` | web pages: reading them with CSS selectors, and making them (`use html`) |
| `xml.md` | XML documents: reading and writing them (`use xml`) |
| `bytes.md` | bytes: files that aren't text, numbers in either byte order (`use bytes`) |
| `addresses.md` | where things are in memory, and C's pointers (`use addresses`) |
| `patterns.md` | patterns in text: regular expressions (`use patterns`) |
| `hashes.md` | SHA-256, SHA-1 and MD5 of text and files (`use hashes`) |
| `images.md` | pictures in memory: drawn on, read and saved, shown on a canvas, handed on by a worker (`use images`) |
| `networks.md` | downloading, sending to web services (`POST` and the rest, headers, progress), programs talking to each other (connections and servers), web servers, WebSockets, encryption, web addresses, and this computer's address and Wi-Fi (`use networks`) |
| `archives.md` | zip files (with a password too), tar files, and gzipped and xz files (`use archives`) |
| `databases.md` | SQLite databases (`use databases`) |
| `threads.md` | workers: work done on a thread of its own (`use threads`) |
| `dbus.md` | D-Bus: notifications, the battery, media players and the rest of the desktop (`use dbus`) |
| `desktop.md` | notifications, and opening files and web addresses, the same on Linux and macOS (`use desktop`) |
| `power.md` | the battery: how full, charging, plugged in, the same on Linux and macOS (`use power`) |
| `volume.md` | the speakers' volume, and muting, the same on Linux and macOS (`use volume`) |
| `media.md` | what's playing, and play, pause, next and previous, the same on Linux and macOS (`use media`) |
| `system.md` | the system's name and version, the computer's name, processors and memory (`use system`) |
| `users.md` | who's running the program, their groups, the computer's other users, and making users and groups (`use users`) |
| `mime.md` | what kind of file a file is: `image/png`, `text/plain` (`use mime`) |
| `world.md` | 3D: shapes, light, models made in Blender, and a camera (`use world`) |
| `videos.md` | video files: what's in them, their pictures and sound, and playing them in a window (`use videos`) |
| `emails.md` | sending email, and reading a mailbox (`use emails`) |
| `os.md` | disks, processors, memory, the programs running, watching files, and signals (`use os`) |
| `linux.md` | what only Linux has: sensors, disks and partitions, USB, modules, settings and services (`use linux`) |
| `brightness.md` | the screen's brightness, the same on Linux and macOS (`use brightness`) |
| `secrets.md` | random codes, password hashes, locking, signatures and public keys (`use secrets`) |
| `printing.md` | printing files and text, with copies, both sides and more (`use printing`) |
| `speech.md` | text said aloud, and speech heard by Whisper (`use speech`) |
| `bluetooth.md` | Bluetooth devices: searching, pairing and connecting (`use bluetooth`) |
| `screens.md` | the monitors: resolution, refresh, rotation, scale, position, on and off (`use screens`) |
| `mouse.md` | the mouse, the touchpad and the cursor: speed, scrolling, tapping, the cursor's size (`use mouse`) |
| `keyboard.md` | the keyboard's layouts, how a held key repeats, and the lock keys (`use keyboard`) |

```sh
less /usr/local/lib/cabby/gui.md
```

[Contents](#contents)

### Appendix J: Style guide

How Cabby code is written, beyond the layout Cabby checks itself. The full
guide, with the reasons, is `STYLE.md` (installed in
`/usr/local/share/doc/cabby/`), and `cabby --style prog.cby` checks a
program's names, and its calls' parentheses, against it.

**Names**

| What | Written | Examples |
|---|---|---|
| Types (`struct`, `object`, `union`, `enum`, `aka`) | lower case, `_` between words | `point`, `order_form` |
| Functions and methods | lower case, `_` between words | `do_this`, `area` |
| Variables, parameters and fields | a capital at the start of each word | `DateOfBirth`, `IsReady` |
| Counters and coordinates | one lower-case letter | `i`, `n`, `x` |
| Constants and enum values | capitals, `_` between words | `DAYS_PER_WEEK`, `RED` |

Capitals mean stored, lower case means worked out: `Shape.Area` is a field,
`Shape.area` a method. Abbreviations are words (`UrlPath`, `Id`); C
functions and libraries keep their own names. Lists and arrays are plural,
their elements singular: `for Score in Scores`.

**Habits**

* Spaces around operators and after commas; lines under 100 columns.
* A file: a comment saying what it's for, `use` lines, constants, types,
  functions, `main` last.
* A struct for a value (plain data, lots of it, saved or handed to C), an
  object for a thing (methods, a destructor, handlers); see [Struct or
  object?](#struct-or-object).
* Objects: fields, then static methods that make one, then methods, then
  the destructor. Every method that doesn't change its object is `const`.
* Parameters that only read a string, struct or object are `const T &`.
* Calls: no parentheses at the start of a line (`greet`), when the call
  is the whole value (`int Result = volume 2, 3, 4`), or when it is the
  whole condition or loop (`if Counts.contains Word {`); parentheses around
  the arguments when it is part of something bigger (`add(1, 2) + 3`);
  never empty parentheses (`Clicks.value`, `rand`).
* `until Done` rather than `while !Done`.
* Go through a list with `for X in L`, and count with `for i in`; leave
  early rather than nesting deeply; `goto`
  only out of nested loops; a deliberate fall-through says so.
* Comments are full sentences, say why rather than what, and put names in
  backticks.

[Contents](#contents)
