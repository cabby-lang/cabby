# gui — windows and everything in them

```cabby
use gui
```

Windows, buttons, text boxes, check boxes, drop-down lists, sliders,
lists, pictures, a canvas to draw on, timers and dialogs. Cabby draws them
itself, with SDL 3 underneath, the same on Linux and on a Mac, light or dark
as the desktop is. All of the library's names start with `gui.`, so they
never clash with yours.

Tutorials 51 to 55 and chapters 51 to 55 of the manual teach it step by
step. This page is the complete reference.

The [tui](tui.md) library has the same objects and methods, drawn in a
terminal: change `gui` to `tui` and the same program runs without a
desktop. This page is its reference too; `tui.md` covers what is
particular to the terminal.

The gtk library has them too, drawn by GTK 4 with its own widgets: change
`gui` to `gtk` (see [Drawn by GTK](#drawn-by-gtk-use-gtk)).

For games, the [game](game.md) library is built the same way: a window
with a stage, sprites and frames, drawn with SDL.

* [How a gui program works](#how-a-gui-program-works)
* [gui.window](#guiwindow)
* [Containers: windows, rows and columns](#containers-windows-rows-and-columns)
* [Every widget](#every-widget)
* [The widgets](#the-widgets)
* [gui.canvas: drawing](#guicanvas-drawing)
* [Menus and shortcuts](#menus-and-shortcuts)
* [Handlers](#handlers)
* [Timers](#timers)
* [Dialogs](#dialogs)
* [Several windows](#several-windows)
* [Testing a window without a person](#testing-a-window-without-a-person)
* [Building and running](#building-and-running)
* [Not in gui yet](#not-in-gui-yet)

## How a gui program works

```cabby
use gui

object greeter : gui.window {
	gui.label Message                // things handlers need later are fields
	gui.entry Name

	void build {                     // what is in the window
		set_title "Greeter"
		Message = add_label "What's your name?"
		Name = add_entry
		gui.button Hello = add_button "Say hello"
		Hello.on_click say_hello     // a handler: called on each click
	}

	void say_hello {
		Message.set_text "Hello, " + Name.text + "!"
	}
}

int main {
	greeter g
	g.run                            // returns when the window is closed
}
```

1. **Make your own window object** from `gui.window`.
2. **Fill it in `build`**: add widgets, keep the ones handlers need as
   fields, and attach handlers.
3. **Call `run`.** It calls `build` (once), shows the window, and then the
   program sleeps, using no processor time, until something happens. Then
   it calls your handler, the way an interrupt calls its service routine.
   `run` returns when the window is closed.

There is no event loop to write. Everything that happens in a window is
either a handler (a click, a key, a timer, a dialog's answer) or something
`build` set up.

## gui.window

A window. Inherits everything [containers](#containers-windows-rows-and-columns) have.

| Member | What it does |
|---|---|
| `void build` | Override it: add the window's contents and handlers. `run` calls it once. |
| `void run` | Builds the window (the first time), shows it, and handles what happens in it. Returns when it is closed. |
| `void set_title const str &Title` | The title in the window's title bar. |
| `void set_size int Width, int Height` | The size it opens at, in pixels (420 × 300 if not set). |
| `void close` | Closes the window, so `run` returns. |
| `void every duration Wait, handler h` | Calls `h` every so often: `every time.seconds(1), tick`. See [Timers](#timers). |
| `void after duration Wait, handler h` | Calls `h` once, after a while. |
| `void stop_timers` | Stops all of this window's timers. |
| `void show_message const str &Text` | Shows a message with an OK button. See [Dialogs](#dialogs). |
| `void ask const str &Question, handler h` | Asks a yes/no question; calls `h` if the answer is yes. |
| `void ask_open_file handler h` | Lets the person choose a file to open; calls `h` once one is chosen. |
| `void ask_save_file const str &Suggested, handler h` | Lets the person choose where to save, suggesting a file name; calls `h` once chosen. |
| `str chosen_file const` | The full path of the file the last file dialog chose. |
| `gui.menu add_menu const str &Title` | A menu on the window's menu bar. See [Menus and shortcuts](#menus-and-shortcuts). |
| `void add_shortcut const str &Keys, handler h` | Calls `h` when these keys are pressed: `"Ctrl+S"`, `"F5"`. |
| `void on_key handler h` | Calls `h` for a key nothing in the window uses: not typed into a text box, not a shortcut, not Tab. The arrow keys come here too, once there's an `on_key` (Tab still moves between things). See [Keys of the window's own](#keys-of-the-windows-own). |
| `str key const` | The key `on_key` was called for, named as `read_key` names keys: `"q"`, `"Q"`, `" "`, `"up"`, `"enter"`, `"escape"`, `"f5"`, and `"ctrl+r"` or `"alt+x"` with those held. |

A `gui.window` can also be used directly, without an object of your own,
when nothing needs a handler:

```cabby
gui.window w
w.set_title "Note"
w.add_label "Nothing to click here."
w.run
```

When a window object is dropped, its window is destroyed with it, along with
its timers. A window cannot be run again after it has been closed.

## Containers: windows, rows and columns

Windows, rows and columns all hold widgets and have the same `add_...`
methods. Each returns the new widget, to keep in a variable if you need it
later.

| Method | Adds | Returns |
|---|---|---|
| `add_label const str &Text` | a line of text | `gui.label` |
| `add_button const str &Text` | a button | `gui.button` |
| `add_entry` | a one-line text box | `gui.entry` |
| `add_check const str &Text` | a check box with a label | `gui.check` |
| `add_choice` | a drop-down list | `gui.choice` |
| `add_text_area` | a box for several lines of text, scrolling | `gui.text_area` |
| `add_list` | a list of items to pick from, scrolling | `gui.list` |
| `add_slider int Min, int Max` | a slider for a whole number from min to max | `gui.slider` |
| `add_number int Min, int Max` | a box for a whole number from min to max, with buttons for one more or less | `gui.number` |
| `add_color_button` | a button to choose a color with | `gui.color_button` |
| `add_font_button` | a button to choose a font with | `gui.font_button` |
| `add_progress` | a progress bar | `gui.progress` |
| `add_image const str &Path` | a picture from a file (`""` for none yet) | `gui.image` |
| `add_video long Source` | a video playing (`use videos`): `add_video Film.source` | `gui.video` |
| `add_canvas int Width, int Height` | a canvas of that many pixels, to draw on | `gui.canvas` |
| `add_separator` | a dividing line | nothing |
| `add_row` | a row: what is added to it goes side by side | `gui.row` |
| `add_column` | a column: what is added to it stacks top to bottom | `gui.column` |
| `add_scrolling_column` | a column with a scroll bar, for more than fits | `gui.column` |
| `add_tabs` | pages shown one at a time, chosen by tabs | `gui.tabs` |
| `add_table const list str &Headings` | rows and columns of text, under these headings | `gui.table` |

**Layout.** A window stacks things top to bottom in the order they are
added. A row puts them side by side; a column inside a row stacks them
again. Rows and columns nest as deeply as needed, so a form with labels on
the left and boxes on the right is a row holding two columns:

```cabby
gui.row Form = add_row
gui.column Labels = Form.add_column
gui.column Boxes = Form.add_column
Labels.add_label "Name:"
gui.entry Name = Boxes.add_entry
```

Text boxes, sliders, progress bars, text areas and lists stretch to fill the
width they are given; text areas, lists, tabs and scrolling columns also
share any spare height.

**Scrolling.** A column made with `add_scrolling_column` scrolls when
what's in it doesn't fit, with a scroll bar at its side: for a long form,
or a hundred check boxes. Fill it as any other column.

**Tabs.** `add_tabs` gives a `gui.tabs`: pages shown one at a time, each
chosen by its tab along the top. `add_tab "General"` adds a page and gives
back its column to fill:

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
		println "showing", Pages.text
	}
}

int main {
	settings s
	s.run
}
```

```text
showing Sound
```

| Member of `gui.tabs` | What it does |
|---|---|
| `gui.column add_tab const str &Title` | A page with its tab, after the others; fill the column it gives. |
| `int selected const` | The page showing, counting from 0. |
| `void set_selected int Index` | Shows another page (this calls `on_change`'s handler too). |
| `int count const` | How many pages there are. |
| `str text const` | The tab of the page showing. |
| `void on_change handler h` | Calls `h` each time a different page is shown. |

## Every widget

Every widget (`gui.label`, `gui.button`, `gui.entry`, `gui.check`,
`gui.choice`, `gui.text_area`, `gui.list`, `gui.slider`, `gui.number`,
`gui.color_button`, `gui.font_button`, `gui.progress`, `gui.image`,
`gui.canvas`, `gui.tabs`, `gui.table`) is a `gui.widget`, and has:

| Member | What it does |
|---|---|
| `void set_text const str &Text` | Changes its text: a label's words, a button's or check box's label, a text box's or text area's contents. |
| `str text const` | Its text, as above. For a drop-down or list, the chosen entry ("" if none). |
| `void set_enabled bool On` | `false` grays it out so it can't be used; `true` makes it usable again. |
| `void set_visible bool On` | `false` hides it (it takes no space); `true` shows it again. |
| `void set_description const str &Text` | What a screen reader says it is, when its text doesn't say: `"Our logo"` for a picture, `"The board"` for a canvas. (Said after the text, for something that has some.) |
| `void set_minimum_size int Width, int Height` | The smallest size it may be, in pixels; -1 for no minimum that way. |
| `void set_text_color const str &Color` | The color its text is written in: `"darkred"`, `"#3366cc"`, as for a canvas's `set_color`. `""` for the usual again. |
| `void set_background const str &Color` | The color behind it: a button's or text box's own color, or behind a label. `""` for the usual again. |

Rows, columns and windows have `set_text_color` and `set_background` too,
for everything in them: a window's `set_background "navy"` is the whole
window, and its buttons and text boxes, unless one has a color of its own.
A background with no text color given gets black or white text, whichever
reads on it. What has the keys still shows the usual blue ring, and a
grayed-out widget still looks grayed out.

<!-- gui: pixel 5 5; close -->
```cabby
use gui

object warning : gui.window {
	void build {
		set_title "Careful"
		set_background "darkred"
		gui.label Big = add_label "The disk is nearly full."
		Big.set_text_color "gold"
		add_button "OK"
	}
}

int main {
	warning W
	W.run
	return 0
}
```

```text
pixel 5 5: #8b0000
```

## The widgets

### gui.label

A line of text, which wraps onto more lines if it is long. Just the members
every widget has.

### gui.button

| Member | |
|---|---|
| `void on_click handler h` | Calls `h` each time the button is clicked. |

### gui.entry

A one-line text box. `text` is what is typed in it.

| Member | |
|---|---|
| `void on_change handler h` | Calls `h` every time the text changes (each key typed). |
| `void on_enter handler h` | Calls `h` when Enter is pressed in the box. |

### gui.check

A check box with a label: on or off.

| Member | |
|---|---|
| `bool checked const` | Whether it is ticked. |
| `void set_checked bool On` | Ticks or unticks it (this calls `on_toggle`'s handler too). |
| `void on_toggle handler h` | Calls `h` each time it is ticked or unticked. |

### gui.choice

A drop-down list: one choice out of several. The first option added is
chosen to begin with. `text` is the chosen option.

| Member | |
|---|---|
| `void add_option const str &Text` | Adds an option at the end. |
| `int selected const` | The chosen option, counting from 0 (-1 if there are none). |
| `void set_selected int Index` | Chooses an option by its number. |
| `int count const` | How many options there are. |
| `void on_change handler h` | Calls `h` each time a different option is chosen. |

### gui.text_area

Several lines of text, with a scroll bar when needed. `text` is all of it,
with `"\n"` between the lines; `set_text` replaces it all.

| Member | |
|---|---|
| `void on_change handler h` | Calls `h` whenever the text changes. |
| `void set_editable bool On` | `false`: the text can be read and copied but not changed. |

### gui.list

A list of items to pick from, with a scroll bar when needed. `text` is the
selected item ("" if none).

| Member | |
|---|---|
| `void add_item const str &Text` | Adds an item at the end. |
| `void clear` | Removes every item. |
| `int count const` | How many items there are. |
| `int selected const` | The selected item, counting from 0 (-1 if none is). |
| `void set_selected int Index` | Selects an item by its number (-1: none). |
| `void on_select handler h` | Calls `h` each time the selection changes, including to nothing (when the list is cleared, say), so check `selected >= 0`. |

### gui.table

Rows and columns of text, with a heading over each column, and a scroll
bar when there are more rows than fit. Clicking a heading sorts the rows
by that column, numbers as numbers (9 before 10) and text ignoring
capitals; clicking it again sorts the other way. Rows count from 0 **in
the order they were added**, however they're sorted, so `selected` and
`cell` always mean the same row. `text` is the selected row's first cell.

```cabby
gui.table Scores = add_table {"Name", "Score"}
Scores.add_row {"Ada", "1200"}
Scores.add_row {"Tom", "95"}
Scores.on_select picked              // Scores.cell(Scores.selected, 1) in picked
```

| Member | |
|---|---|
| `void add_row const list str &Cells` | Adds a row at the end: one text for each column (missing ones are empty). |
| `str cell int Row, int Column const` | One cell's text. |
| `void set_cell int Row, int Column, const str &Text` | Changes one cell. |
| `void remove_row int Row` | Takes a row out; the rows after it move up one. |
| `void clear` | Takes every row out (the headings stay). |
| `int count const` | How many rows. |
| `int columns const` | How many columns. |
| `int selected const` | The selected row (-1 if none is). |
| `void set_selected int Row` | Selects a row (-1: none). |
| `void on_select handler h` | Calls `h` each time the selection changes, including to none (when the selected row is taken out), so check `selected >= 0`. |

A row or column that isn't there is a panic.

### gui.slider

A slider for choosing a whole number between the two limits it was added
with. It starts at the lower limit.

| Member | |
|---|---|
| `int value const` | Its value. |
| `void set_value int v` | Moves it (this calls `on_change`'s handler too). |
| `void on_change handler h` | Calls `h` each time the value changes. |

### gui.number

A box for a whole number between the two limits it was added with, with
buttons to make it one more or one less; it can be typed in too, and the
arrow keys and mouse wheel change it. It starts at the lower limit. Use it
where the exact number matters more than dragging: a count, an age, a size.

| Member | |
|---|---|
| `int value const` | Its value. |
| `void set_value int v` | Changes it, kept between the limits (this calls `on_change`'s handler too). |
| `void on_change handler h` | Calls `h` each time the value changes. |

### gui.color_button

A button showing a color. Clicking it opens the desktop's color chooser;
what's chosen shows on the button. It starts black.

| Member | |
|---|---|
| `str color const` | The color chosen, as `"#rrggbb"` (`"#rrggbbaa"` if it's see-through): ready for a canvas's `set_color`. |
| `void set_color const str &Color` | Changes it: `"steelblue"`, `"#3366cc"`, `"rgb(51,102,204)"`, as for a canvas. |
| `void on_change handler h` | Calls `h` each time a different color is chosen. |

### gui.font_button

A button showing a font. Clicking it opens the desktop's font chooser. It
starts as `"Sans 12"`.

| Member | |
|---|---|
| `str font const` | The font chosen: its family, its style if any, and its size, as `"Serif Bold 16"`. |
| `void set_font const str &Font` | Changes it, written the same way. |
| `void on_change handler h` | Calls `h` each time a different font is chosen. |

A canvas's `set_font` takes the font just as it comes: `Card.set_font
Font.font`.

### gui.progress

A bar showing how much of something is done. It starts empty.

| Member | |
|---|---|
| `void set_fraction double Done` | From 0.0 (nothing) to 1.0 (all done). |

### gui.image

A picture from a file: PNG, JPEG, SVG and the other formats your desktop can
show. It is scaled to fit its space, keeping its shape.

| Member | |
|---|---|
| `void set_file const str &Path` | Shows a different file; `""` clears the picture. |

A relative path is relative to the folder the program was started from.

### gui.video

A video playing, from the [videos](videos.md) library: `add_video
Film.source`, where `Film` is a `videos.video`. Its picture fills the
widget's space, keeping its shape (black bars fill the rest), and its
sound plays. The video itself plays, pauses and seeks (`Film.play`,
`Film.seek 30`); the widget shows it.

| Member | |
|---|---|
| `void show_controls` | a play button, a track to go to any place along, and the time, under it |
| `void hide_controls` | (hidden, to begin with) |
| `void on_end handler h` | `h` runs each time the video comes to its end |

A click on the picture plays or pauses it. With it chosen, space does
too, left and right go back and on five seconds, and Home and End go to
its start and end. Under a test script it plays by the script's clock,
without sound, so `wait 1500` is a second and a half of it.

## gui.canvas: drawing

A canvas is a part of a window to draw on: lines, shapes, text and
pictures, in any color. It's drawn by a handler: give it one with
`on_draw`, and the canvas calls it whenever it needs drawing (when the
window first shows, and when it's uncovered or changes size), and after
`redraw`. The handler draws everything, from the program's fields, every
time. To change the picture, change the fields and call `redraw`.

<!-- gui: press 1 30 30; key right; close -->
```cabby
use gui

object sunny : gui.window {
	gui.canvas Sky
	int SunX

	void build {
		set_title "Sunny"
		SunX = 60
		Sky = add_canvas 240, 160
		Sky.on_draw draw_sky
		Sky.on_click clicked
		Sky.on_key pressed
	}

	void draw_sky {
		Sky.fill_with "lightskyblue"
		Sky.set_color "gold"
		Sky.fill_circle SunX, 50, 24
		Sky.set_color "forestgreen"
		Sky.fill_rectangle 0, 120, 240, 40
		Sky.set_color "black"
		Sky.write 8, 136, "a canvas"
	}

	void clicked {
		println "clicked at", Sky.mouse_x, Sky.mouse_y
	}

	void pressed {
		if Sky.key == "right" {
			SunX += 20
			Sky.redraw
			println "the sun moves to", SunX
		}
	}
}

int main {
	sunny s
	s.run
}
```

```text
clicked at 30 30
the sun moves to 80
```

**Positions** are in pixels from the canvas's top left corner: `x` counts
to the right, and `y` counts *down*, so `0, 0` is the top left and `Width,
Height` the bottom right. Each drawing starts on white, in black, with
lines 1 pixel wide and text 14 pixels high. A canvas stays the size it was
added with.

**Colors** are written as on web pages: a name (`"red"`, `"steelblue"`,
`"lightyellow"`: the 148 CSS names), `"#rrggbb"` in hexadecimal
(`"#3366cc"`), `"#rgb"` for short, or `"rgb(51, 102, 204)"`. An `a` on the
end makes it see-through: `"rgba(0, 0, 0, 0.5)"`, `"#3366cc80"`. A color
that isn't one is a panic that says so.

| Member | What it does |
|---|---|
| `void on_draw handler h` | Calls `h` to draw the canvas, whenever it needs drawing. |
| `void redraw` | Draws it again soon (calls `on_draw`'s handler), to show what changed. |
| `int width const`, `int height const` | Its size, in pixels, as it was added. |
| `void set_color const str &Color` | The color for what's drawn next. |
| `void fill_with const str &Color` | The whole canvas in one color (the drawing color stays as it was). |
| `void set_line_width double Width` | How wide lines are, in pixels (1 to begin with). |
| `void set_font_size double Size` | How high text is, in pixels (14 to begin with). |
| `void set_font const str &Font` | The font for text, written as a font button gives it: `"Serif Bold 16"`, its size in pixels (`"Sans 14"` to begin with). In tui only the size counts. |
| `void line double X1, double Y1, double X2, double Y2` | A line between two points. |
| `void rectangle double X, double Y, double Width, double Height` | A rectangle's outline; `X, Y` is its top left corner. |
| `void fill_rectangle ...` | The same, filled in. |
| `void circle double X, double Y, double Radius` | A circle's outline; `X, Y` is its center. |
| `void fill_circle ...` | The same, filled in. |
| `void ellipse double X, double Y, double Across, double Down` | An ellipse (an oval), `Across` and `Down` from its center to its edges. |
| `void fill_ellipse ...` | The same, filled in. |
| `void arc double X, double Y, double Radius, double From, double To` | Part of a circle, from one angle to another, in degrees *as on a clock*: 0 at the top, 90 at the right, going clockwise. |
| `void polygon const list double &Corners` | A shape through its corners, given as `x, y, x, y ...`: `{10, 10, 90, 10, 50, 80}` is a triangle. |
| `void fill_polygon ...` | The same, filled in. |
| `void write double X, double Y, const str &Text` | Text, its top left corner at `X, Y`. |
| `void image double X, double Y, const str &Path` | A picture from a file (PNG, JPEG...), its top left corner at `X, Y`. |
| `void picture double X, double Y, const images.picture &Picture` | A picture in memory ([images](images.md)), its top left corner at `X, Y`: one a worker drew, say. Its pixels go to the screen again only when they've changed, so it can be shown on every redraw. |
| `void save const str &Path` | Saves what `on_draw` draws as a PNG picture. |
| `void on_click handler h` | Calls `h` when the canvas is clicked (with any mouse button). |
| `void on_mouse_move handler h` | Calls `h` each time the mouse moves over it. |
| `int mouse_x const`, `int mouse_y const` | Where the mouse was then, in the canvas's pixels. |
| `void on_key handler h` | Calls `h` when a key is pressed while the canvas has the keys (after it's been clicked, or Tab has moved to it). Tab still moves on. |
| `str key const` | The key, named as `read_key` names keys: a character as itself (`"a"`, `" "`), the others by name (`"up"`, `"down"`, `"left"`, `"right"`, `"enter"`, `"escape"`, `"backspace"`, `"delete"`, `"home"`, `"end"`, `"page_up"`, `"page_down"`, `"f1"`...). |

The drawing methods work only while `on_draw`'s handler runs: drawing
from a click's handler is a panic that says to call `redraw` instead.
That's what keeps a canvas right when it's covered and uncovered: it can
always be drawn again from the program's fields.

**Shapes are drawn in order**, each over what's already there, so draw the
background first and the details last.

## Menus and shortcuts

`add_menu` puts a menu on the window's menu bar (which appears with the
first one), and the menu's `add_item` adds the things to choose in it.
Each item has a handler, called when it's chosen, and can have a keyboard
**shortcut**, shown beside it, that chooses it from anywhere in the
window:

<!-- gui: menu Edit > Shout; shortcut Ctrl+L; shortcut F1; close -->
```cabby
use gui

object shouter : gui.window {
	gui.entry Words

	void build {
		set_title "Shouter"
		gui.menu Edit = add_menu "Edit"
		Edit.add_item "Shout", "Ctrl+U", shout
		Edit.add_item "Whisper", "Ctrl+L", whisper
		Edit.add_separator
		Edit.add_item "Clear", "", clear_words          // "": no shortcut
		gui.menu Help = add_menu "Help"
		Help.add_item "About", "F1", about
		Words = add_entry
		Words.set_text "Hello there"
	}

	void shout {
		Words.set_text Words.text.upper
		println Words.text
	}

	void whisper {
		Words.set_text Words.text.lower
		println Words.text
	}

	void clear_words {
		Words.set_text ""
	}

	void about {
		println "Shouter, made with Cabby"
	}
}

int main {
	shouter s
	s.run
}
```

```text
HELLO THERE
hello there
Shouter, made with Cabby
```

| Member | What it does |
|---|---|
| `gui.menu add_menu const str &Title` (a window's) | A menu on the menu bar, after those already there. |
| `gui.menu_item add_item const str &Text, const str &Keys, handler h` (a menu's) | An item that calls `h` when it's chosen. `Keys` is its shortcut, or `""` for none. |
| `void add_separator` (a menu's) | A line between items, to group them. |
| `void set_enabled bool On` (an item's) | `false` grays it out: it, and its shortcut, can't be used until it's `true` again. |
| `void add_shortcut const str &Keys, handler h` (a window's) | A shortcut on its own, with no menu item. |

**Shortcuts** are written as menus show them: `"Ctrl+S"`, `"Ctrl+Shift+Z"`,
`"Alt+X"`, `"F5"`, `"Ctrl+Enter"`. Before the key go `Ctrl`, `Shift` and
`Alt`, joined with `+`; the key is a letter, a digit or another character,
`F1` to `F12`, or `Enter`, `Escape`, `Space`, `Tab`, `Backspace`, `Delete`,
`Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left` or
`Right` (capitals don't matter). Something else is a panic that says so.
The usual ones: `Ctrl+N` new, `Ctrl+O` open, `Ctrl+S` save, `Ctrl+Q` quit,
`Ctrl+Z` undo, `F1` help.

## Keys of the window's own

A window can take keys itself, for a program that's driven from the
keyboard: Space to play and pause, the arrows to move through photos, q
to quit. `on_key` is called for each key that nothing in the window uses,
and `key` says which:

<!-- gui: press-key Right; press-key Space; press-key Shift+Q; close -->
```cabby
use gui

object viewer : gui.window {
	gui.label Shown
	int At

	void build {
		set_title "Photos"
		Shown = add_label "photo 1"
		on_key pressed
	}

	void pressed {
		if key == "right" {
			At += 1
		} else if key == "left" && At > 0 {
			At -= 1
		} else if key == "q" || key == "Q" {
			close
		}
		Shown.set_text "photo " + (At + 1).text
		println key, "->", Shown.text
	}
}

int main {
	viewer V
	V.run
	return 0
}
```

```text
right -> photo 2
  -> photo 2
Q -> photo 2
```

What a text box has the keys for (typing, the arrows inside it) stays the
text box's, and a shortcut (`add_shortcut`, a menu's) comes before
`on_key`. In tui, Esc and Ctrl+Q still close the window.

## Handlers

A handler is the name of one of the window's own methods, given without
parentheses:

```cabby
SaveButton.on_click save        // save is a method of this window
```

It isn't called there: it is called later, each time the event happens.
The rules, all checked before the program runs:

* It must be a method of the window (or of an object made from it) whose
  handler you are setting, named on its own: `save`, not `save()` or
  `other.save`.
* It takes no parameters and returns nothing (`void save { ... }`). To share
  work between handlers, have small handlers call a method that does take
  parameters (tutorial 52 does this with `tip_10` and `choose`).
* The `handler` type can only be a parameter, passed straight on; it can't
  be stored in a variable or field.

Because handlers belong to their window, they live exactly as long as the
widgets that call them. A handler is never called after its window has
closed.

## Timers

```cabby
every time.milliseconds(100), tick   // tick is called ten times a second
after time.seconds(2), reset         // reset is called once, two seconds from now
stop_timers              // stops them all
```

Timers are handlers that the clock calls, with the same rules as other
handlers. They run only while their window is open and stop when it closes.
A busy computer may call a timer a little late, so for exact timing count
the calls rather than trusting each to be on time (tutorial 54).

## Dialogs

Dialogs don't stop the program to wait for an answer: like everything in
`gui`, they call a handler when the answer comes. If the person cancels,
the handler is not called at all.

```cabby
show_message "Saved."                     // an OK button; no handler
ask "Delete this item?", delete_it        // delete_it runs only on Yes
ask_open_file opened                      // opened runs once a file is chosen
ask_save_file "notes.txt", save_to        // save_to runs once a place is chosen

void opened {
	str Path = chosen_file                // the full path of the chosen file
}
```

## Several windows

A handler can make and run another window. Its `run` returns when that
window is closed, and the first window carries on:

```cabby
void show_settings {
	settings_window s
	s.run                                 // waits here until it is closed
	Status.set_text "settings closed"
}
```

## Testing a window without a person

With the environment variable `CABBY_GUI_SCRIPT` set, windows follow a
script instead of waiting for a person. Commands are separated by `;`:

| Command | Does |
|---|---|
| `click <button text>` | clicks the button with that label |
| `type <n> <text>` | types into the n-th text box (counting from 1), a character at a time |
| `enter <n>` | presses Enter in the n-th text box |
| `write <n> <text>` | sets the n-th text area's text (`\n` for a new line) |
| `check <label>` | ticks or unticks the check box with that label |
| `choose <n> <option>` | chooses an option in the n-th drop-down |
| `select <n> <item>` | selects an item in the n-th list |
| `slide <n> <value>` | moves the n-th slider |
| `number <n> <value>` | sets the n-th number box |
| `color <n> <color>` | chooses a color with the n-th color button |
| `font <n> <font>` | chooses a font with the n-th font button |
| `press <n> <x> <y>` | clicks the n-th canvas, at that pixel |
| `point <n> <x> <y>` | moves the mouse over the n-th canvas, to that pixel |
| `key <name>` | presses a key in the first canvas that takes keys (`space` for a space), or else gives it to the window's `on_key` |
| `menu <Menu> > <Item>` | chooses an item from a menu: `menu File > Save` |
| `tab <n> <title>` | shows that tab of the n-th set of tabs |
| `row <n> <first cell>` | selects the row of the n-th table whose first cell is that |
| `sort <n> <heading>` | sorts the n-th table by that column, as a click on its heading does |
| `shortcut <keys>` | presses a shortcut: `shortcut Ctrl+S` |
| `mouse <x> <y>` | clicks there in the window, as a person would (gui only) |
| `double <x> <y>`, `triple <x> <y>` | a double click (a word) or a triple click (a line) there (gui only) |
| `drag <x1> <y1> <x2> <y2>` | presses the mouse at one place and lets go at the other (gui only) |
| `press-key <key>` | presses a key in the window, as SDL names it: `Return`, `Left`, `Ctrl+Z`, `Alt+Left` (gui only) |
| `text <words>` | types the words into what has the keys (gui only) |
| `compose <text>` | an input method composing (shown underlined, not yet typed); `compose` alone ends it (gui only) |
| `wait <ms>` | lets the window run (and its timers fire) for that long |
| `pause <ms>` | really waits that long, answering what's asked meanwhile: a screen reader's questions, another program's D-Bus calls (gui only) |
| `answer yes` / `answer no` | answers the question `ask` asked |
| `pick <Path>` / `cancel` | answers a file dialog |
| `shot <file.png>` | saves a picture of the window |
| `pixel <x> <y>` | prints the color drawn at that place in the window: `pixel 5 5: #8b0000` (gui only) |
| `close` | closes the window |

Dialogs are not shown while a script runs; `answer`, `pick` and `cancel`
answer them. Each `close` ends one window; the rest of the script goes to
the next window that runs. For example:

```sh
CABBY_GUI_SCRIPT="type 1 Ada; click Say hello; shot greeter.png; close" cabby greeter.cby
```

Under a script, gui draws its windows off screen, so no display is needed,
and always in the light look. Cabby's own tests run its window tutorials
this way (and with gtk, on a virtual display, `xvfb-run`).

## Building and running

Window programs run both ways: `cabby app.cby`, or `cabby -c app.cby` for a
native program. The library (`libcabbygui`) is built with Cabby when SDL 3
is installed: `sdl3` on Arch, `libsdl3-dev` on Debian, `brew install sdl3`
on a Mac. Without it, `use gui` stops with a message saying what to install.
Pictures are read, and text drawn, by code built into Cabby (stb_image and
stb_truetype), with the computer's own fonts.

## Typing in text boxes

Text boxes and text areas work as text does elsewhere:

* **Undo:** Ctrl+Z undoes, Ctrl+Shift+Z or Ctrl+Y does it again. Letters
  typed one after another undo together, up to a space.
* **Selecting:** drag across text, double click a word, triple click a
  line; Shift with the arrow keys; Ctrl+A for everything.
* **Words:** Ctrl+Left and Ctrl+Right move a word at a time, Ctrl+Backspace
  and Ctrl+Delete delete one.
* **Copying:** Ctrl+C, Ctrl+X and Ctrl+V.
* **Accents and other alphabets:** dead keys, Compose and input methods
  work; what's being composed shows underlined at the cursor until it's
  done. A character the font doesn't have (Greek with breathings, þ and ƿ,
  arrows and symbols) is drawn from another font that does.

On a Mac, Cmd does what Ctrl does here (Cmd+Z, Cmd+C, and a `"Ctrl+S"`
shortcut is Cmd+S), Option+Left and Option+Right move a word, and
Cmd+Left and Cmd+Right go to the line's start and end. Menus show
shortcuts as a Mac's do: `"Ctrl+S"` as ⌘S, `"Ctrl+Shift+S"` as ⇧⌘S,
`"Alt+Left"` as ⌥←. The program still writes them the one way, `"Ctrl+S"`,
for both.

## Text in any language, and emoji

Text is shown as it's written, in any script, everywhere a window shows
text: labels, buttons, lists, text boxes, a canvas's `write`. Tutorial 61
is a phrasebook of them.

* **Right to left:** Hebrew and Arabic go right to left, with numbers and
  words of left-to-right languages in them left to right, in their
  places, as Unicode's bidirectional rules say (the usual part of them:
  explicit marks such as U+202B aren't followed). A paragraph's
  direction is its first letter's. In a text box, the cursor goes where
  the next letter will appear, and arrows move through the text in the
  order it's written.
* **Joined:** Arabic's letters join, and marks (Hebrew's points, Arabic's
  vowels) sit on their letters; the Indian scripts, Thai and the rest are
  shaped as they're written.
* **Emoji:** drawn in color, from the computer's color emoji font: Noto
  Color Emoji on Linux (the package `noto-fonts-emoji` on Arch,
  `fonts-noto-color-emoji` on Debian), Apple Color Emoji on a Mac. An emoji
  made of several (👨‍👩‍👧, a flag, 👍🏽 with a skin tone) is one picture.
  Without an emoji font, an emoji is drawn as the other fonts draw it.

The shaping is HarfBuzz's, the library GTK and the web browsers use,
loaded when text first needs it: every Linux desktop has it, and a Mac
has it with Homebrew (`brew install harfbuzz`). Without it, Cabby joins
Arabic's letters itself, and puts the text in order itself; what's then
missing is the Indian scripts' shaping, and an emoji made of several,
whose parts are drawn one by one.

Text that's all Latin, Greek and Cyrillic letters is drawn as it always
was, a letter at a time, which is quickest; other text is laid out once
and kept, so a window drawing it again and again does no more work.

`CABBY_HARFBUZZ=none` lays text out without HarfBuzz, and
`CABBY_EMOJI_FONT=file.ttf` (or `none`) chooses the emoji font: to see the
difference. `CABBY_TEXT_ORDER=file` writes each text laid out, and its
characters in the order they're shown, to the file: Cabby's tests check
that order (`tests/run_text_tests.sh`).

## Screen readers

A gui window can be read aloud, and used, by a screen reader: on Linux,
Orca (on GNOME, Super+Alt+S turns it on). Nothing needs doing for it. The
reader is told what's in each window, in order: what each thing is (a
button, a text box, a check box, a menu), what it's called, whether it's
ticked, chosen, grayed out or has the keys; and as the program runs, it's
told what changes. So as a person tabs around, the reader says "Your
name, text", "Loud, check box, not checked"; it reads what's typed, the
drop-down's choice, and a message or question when one shows. It can
press buttons and choose options itself.

What each thing is called comes from its text: a button's or check box's
label, a menu's items, a window's title. A text box, drop-down, list or
slider is called by the label just before it, as a person reading the
window would take it:

```cabby
add_label "Your name:"
Name = add_entry            // read as "Your name:, text"
```

What has no text needs telling, with `set_description`: a picture, or a
canvas (drawn by the program, so a reader sees only that it's there).

```cabby
gui.image Logo = add_image "logo.png"
Logo.set_description "The Cabby logo"    // otherwise just "image"
gui.canvas Board = add_canvas 400, 400
Board.set_description "The board"
```

The window's contents go to the reader over AT-SPI, the desktop's
accessibility bus, as GTK's and Qt's do, and only while a screen reader
is running: without one, nothing's sent and nothing's slower.
`CABBY_ACCESSIBILITY=1` sends them anyway, and `CABBY_ACCESSIBILITY=0`
never. (Cabby's tests read a window this way, with a stand-in for the
reader: `tests/run_accessibility_tests.sh`.)

On a Mac, VoiceOver doesn't read gui's windows yet: use gtk for that, or
the next version's gui.

## Drawn by GTK: `use gtk`

```cabby
use gtk
```

The gtk library has every object and method on this page, with `gtk.`
names: `gtk.window`, `gtk.button`, `add_button` ... Changing `gui` to
`gtk` (and `gui.` to `gtk.`) gives the same program drawn by GTK 4, with
GTK's own widgets and dialogs. Everything works the same, and the same
scripts test it.

Use it for GTK's look. Its widgets are read by a screen reader as gui's
are, by GTK itself (`set_description` is passed on to it). It's built with Cabby when
GTK 4.10 or newer is installed (`gtk4` on Arch, `libgtk-4-dev` on Debian).

GTK sometimes prints warnings of its own when a program starts, such as
unknown keys in `~/.config/gtk-4.0/settings.ini` left over from GTK 3. They
come from the desktop's settings, not from your program, and are safe to
ignore.

## Not in gui yet

* Trees (rows inside rows), and tables whose cells can be edited in place
* Date pickers
* Panels and bars for a desktop (the long-term aim)
* VoiceOver on a Mac (Linux's screen readers read gui; gtk's read on both)
* Right-to-left text boxes lined up on the right (they start on the left,
  as other text boxes do); bidirectional marks and embeddings
* Emoji fonts whose pictures are drawn as shapes (COLR, as Twemoji's
  are); the pictures of Noto Color Emoji and Apple Color Emoji are
