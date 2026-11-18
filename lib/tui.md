# tui — windows in a terminal

```cabby
use tui
```

Windows drawn in the terminal, with the same buttons, text boxes, check
boxes, drop-down lists, sliders, lists, timers and dialogs as the
[gui](gui.md) library, and the same names: `tui.window`, `tui.button`,
`add_label`, `on_click`, `every`, `ask` and the rest. Change `gui` to `tui`
(or back) and the same program runs in a terminal (or opens a desktop
window). It needs nothing but a terminal: no SDL, no desktop.

This page covers what is particular to the terminal. Everything a window
and its widgets can do is the same as in [gui.md](gui.md), which is the
complete reference for both.

* [How a tui program works](#how-a-tui-program-works)
* [What it looks like](#what-it-looks-like)
* [Keys and the mouse](#keys-and-the-mouse)
* [Sizes](#sizes)
* [Printing while a window is open](#printing-while-a-window-is-open)
* [Dialogs](#dialogs)
* [Testing a window without a person](#testing-a-window-without-a-person)
* [Differences from gui](#differences-from-gui)

## How a tui program works

As with gui: make your own window object from `tui.window`, add what's in
it in `build`, and connect buttons to the window's methods. `run` shows the
window and returns when it's closed.

<!-- tui: click Add one; click Add one; screen; click Reset; key tab; key enter; screen -->
```cabby
use tui

object counter : tui.window {
	tui.label Shown
	int Count

	void build {
		set_title "Counter"
		set_size 320, 96
		Shown = add_label "Count: 0"
		tui.row Buttons = add_row
		tui.button Up = Buttons.add_button "Add one"
		tui.button Reset = Buttons.add_button "Reset"
		Up.on_click add_one
		Reset.on_click reset
	}

	void add_one {
		Count++
		Shown.set_text "Count: " + Count.text
	}

	void reset {
		Count = 0
		Shown.set_text "Count: 0"
		println "reset"
	}
}

int main {
	counter c
	c.run
	println "final count:", c.Count
}
```

```text
┌────────────── Counter ───────────────┐
│ Count: 2                             │
│ [ Add one ] [ Reset ]                │
│                                      │
│                                      │
└──────────────────────────────────────┘
reset
┌────────────── Counter ───────────────┐
│ Count: 1                             │
│ [ Add one ] [ Reset ]                │
│                                      │
│                                      │
└──────────────────────────────────────┘
final count: 1
```

(The screens above were printed by the test script, which clicked, and
then pressed Tab and Enter, as a person would.)

## What it looks like

The window has a border with its title at the top. What's added to it
stacks top to bottom, and a row (`add_row`) puts things side by side, one
space apart. Text boxes, text areas, lists, sliders, progress bars and
dividing lines stretch to the window's width; lists and text areas share
out any spare height (and give it up first when the terminal is small).

| Widget | Drawn as |
|---|---|
| `tui.label` | its text (several lines if it has `"\n"`) |
| `tui.button` | `[ Save ]` |
| `tui.entry` | a colored field to type in |
| `tui.check` | `[x] Shout` or `[ ] Shout` |
| `tui.choice` | `< Large >`: the chosen option, between arrows |
| `tui.text_area` | a colored field of several lines; long lines wrap at spaces, as in gui |
| `tui.list` | its items, the selected one highlighted, with a scroll mark when they don't all fit |
| `tui.slider` | `[──────●────] 7` |
| `tui.number` | `‹ 7 ›` |
| `tui.color_button` | `[██ #4682b4]`: the color, then how it's written |
| `tui.font_button` | `[ Serif Bold 16 ]`: the font's name |
| `tui.progress` | `█████░░░░░  50%` |
| `tui.image` | `[picture: cat.png]` (a terminal can't show the picture) |
| `tui.canvas` | the drawing, roughly: see [Canvases](#canvases) |
| `tui.table` | its headings on a gray line, then its rows; the selected one highlighted, a `▲` or `▼` by the column it's sorted by |
| `tui.tabs` | ` General │ Sound ─────`: the tabs along a line, the page showing below |
| `add_scrolling_column` | a column of what fits, with a scroll bar (`│` and `█`) on its right |
| `add_separator` | a line across |

The thing that has the keys is highlighted, and a text box or text area
shows the cursor. Colors are the terminal's own 16, so they follow its
color scheme: fields are the terminal's own text color on its gray
("bright black"), so they stay readable whatever the scheme. A disabled thing is shown dim and can't be reached.

## Keys and the mouse

| Key | Does |
|---|---|
| Tab, Shift-Tab | the next or previous thing |
| Up, Down | the next or previous thing (in a list or text area: move inside it) |
| Enter, Space | press a button, tick a check box, change a drop-down |
| Left, Right | change a drop-down or slider, or the tab showing; sort a table by the column before or after; move in text |
| Page Up, Page Down, Home, End | move further in a list, slider, number box or text |
| digits, Backspace in a number box | type a number, or take its last digit off |
| Enter, Space on a color or font button | open a list to choose from: Up and Down, then Enter (Esc leaves it as it was) |
| Enter in a text box | calls its `on_enter` handler |
| Esc, Ctrl-Q, Ctrl-C | close the window (Esc and Ctrl-Q only when they aren't a shortcut of the window's) |
| F10, or Alt and a menu's first letter | open the menus; then Up and Down choose, Enter runs an item, Left and Right go to the next menu, and Esc closes them |
| a shortcut (Ctrl and a letter, Alt and a key, F1 to F12) | runs its menu item or handler |

Terminals send Ctrl and a letter, Alt and a key, and the F keys, so
shortcuts made of those work; Ctrl and Shift together (`Ctrl+Shift+Z`) arrive
as just `Ctrl+Z`, and a few Ctrl keys are other keys in a terminal
(`Ctrl+I` is Tab, `Ctrl+M` Enter, `Ctrl+H` Backspace), so choose others. The
menu bar is the first row inside the window's border, and its menus open
over the window, showing each item's shortcut.

A click with the mouse moves the keys to what was clicked and presses it
(a button, a check box), picks a list item, moves a slider, or puts the
cursor where it was clicked in text. The mouse wheel scrolls lists and text
areas, and columns that scroll; moving to something out of sight
scrolls to it.

## Canvases

A `tui.canvas` is drawn by the same program as a `gui.canvas`, with the
same calls and the same pixels (see
[gui.md](gui.md#guicanvas-drawing)). A terminal has characters, not
pixels, so it's shown roughly: each character stands for 8 by 16 pixels,
as two blocks one above the other (`▀`), each block in the color most of
its pixels are. Thin lines still show, since a block takes the color of
anything drawn in it rather than the background's. Colors are shown
exactly when the terminal can show any color (most can). In the screen
below, as a test script prints it, only the blocks' shapes show: the
navy background is blank, and the sun and stripe are blocks.

<!-- tui: screen; press 1 30 30; close -->
```cabby
use tui

object flag : tui.window {
	tui.canvas Flag

	void build {
		set_title "Flag"
		set_size 256, 96
		Flag = add_canvas 160, 64
		Flag.on_draw draw_flag
		Flag.on_click clicked
	}

	void draw_flag {
		Flag.fill_with "navy"
		Flag.set_color "gold"
		Flag.fill_circle 40, 32, 20
		Flag.set_color "white"
		Flag.set_line_width 8
		Flag.line 80, 32, 160, 32
		Flag.write 96, 8, "cabby"
	}

	void clicked {
		println "clicked at", Flag.mouse_x, Flag.mouse_y
	}
}

int main {
	flag f
	f.run
}
```

```text
┌──────────── Flag ────────────┐
│    ▄▄▄▄     cabby            │
│   ██████ ▄▄▄▄▄▄▄▄▄▄▄         │
│   ██████ ▀▀▀▀▀▀▀▀▀▀▀         │
│    ▀▀▀▀                      │
└──────────────────────────────┘
clicked at 30 30
```

* Text written on a canvas is shown as characters, at the character its
  top left corner falls in, whatever its size.
* A picture from a file (`image`) can't be shown: its name is, in square brackets. A
  picture in memory ([images](images.md)), on a canvas with `picture`, is
  shown, with the terminal's blocks: two of its pixels to each.
* `save` writes the drawing as a PNG picture, at its full size in pixels,
  without its text.
* `on_mouse_move` works in terminals that report the mouse moving (most
  do). The keys go to a canvas that has an `on_key` handler when Tab moves
  to it, or it's clicked; a small `◆` in its corner shows that it has
  them.

## Sizes

Sizes stay in pixels, as for gui, so a program can switch between the two
libraries: a terminal character counts as 8 pixels wide and 16 high.
`set_size 320, 160` gives a window of 40 by 10 characters, in the middle of
the terminal (never smaller than what's in it needs). Without `set_size`
the window fills the terminal, and follows it when the terminal is resized.
`set_min_size` works the same way.

## Printing while a window is open

A window covers the whole terminal, so anything the program prints while it
is open (from a handler, say) would be drawn over by the window. It is kept
instead, and printed when the last window closes, in the order it was
printed. In the example above, "reset" was printed by a handler.

## Dialogs

`show_message` and `ask` show a box over the window: `[ OK ]`, or
`[ Yes ]` and `[ No ]` (Left and Right choose; Enter answers; Y and N
answer too; Esc is no). `ask_open_file` and `ask_save_file` show the
current directory's files and folders above a box for the name: Tab moves
between the two, Enter on a folder (or `../`) goes into it, and Enter on a
file, or on a typed name, chooses it. Esc cancels. `chosen_file` is then
the full path, as with gui.

## Testing a window without a person

As with gui's `CABBY_GUI_SCRIPT`, the environment variable
`CABBY_TUI_SCRIPT` makes windows follow a script instead of the keyboard,
in a 60 by 20 screen (`CABBY_TUI_SIZE=WxH` to change it), drawing nothing
unless asked. Commands are separated by `;`:

| Command | Does |
|---|---|
| `click Text` | presses the button labeled `Text` |
| `check Text` | ticks or unticks the check box labeled `Text` |
| `type N Text` | types `Text` into the N-th text box (from 1), a character at a time |
| `enter N` | presses Enter in the N-th text box |
| `write N Text` | sets the N-th text area (`\n` for a new line) |
| `choose N Text`, `select N Text` | chooses an option of the N-th drop-down, or item of the N-th list |
| `slide N Value` | moves the N-th slider |
| `number N Value`, `color N Color`, `font N Font` | sets the N-th number box, or chooses with the N-th color or font button |
| `press N X Y`, `point N X Y` | clicks the N-th canvas at that pixel, or moves the mouse to it |
| `menu Menu > Item`, `shortcut Keys` | chooses a menu item, or presses a shortcut, as in gui |
| `tab N Title` | shows that tab of the N-th set of tabs |
| `row N First`, `sort N Heading` | selects a table's row by its first cell, or sorts it by a column, as in gui |
| `key Name` | presses a key: `tab`, `backtab`, `enter`, `space`, `esc`, `up`, `down`, `left`, `right`, `home`, `end`, `pageup`, `pagedown`, `backspace`, `delete`, `f1` to `f12`, `ctrl+r`, `alt+x`, or a character. If a canvas takes keys, it goes there, as in gui |
| `wait Ms` | lets that many milliseconds pass for timers (straight away: the clock is the script's) |
| `answer yes`, `answer no` | answers the open `ask` (or closes a message) |
| `pick Path`, `cancel` | answers the open file dialog |
| `screen` | prints the window as text |
| `cell X Y` | prints the character at that place on the screen (from 0) and its colors, if they're the program's own: `cell 2 1: 'W' text #ffd700 background #000080` |
| `close` | closes the window |

When the script runs out, the window closes. Tutorial 62 and the manual's
chapter 62 are tested this way.

## Colors of the program's own

`set_text_color` and `set_background` work as in gui, on any widget and on
rows, columns and windows. The terminal shows them exactly when it can show
any color (most can); without them, everything is in the terminal's own 16
colors, which follow its color scheme.

<!-- tui: cell 2 1; cell 10 3; close -->
```cabby
use tui

object warning : tui.window {
	void build {
		set_title "Careful"
		set_background "darkred"
		tui.label Big = add_label "Disk nearly full"
		Big.set_text_color "gold"
		add_label "Delete something?"
	}
}

int main {
	warning W
	W.run
	return 0
}
```

```text
cell 2 1: 'D' text #ffd700 background #8b0000
cell 10 3: ' ' text #f5f5f5 background #8b0000
```

(The second label, with no text color of its own, gets white text: on dark
red, that's what reads.)

The window's own keys work as in gui too (`on_key` and `key`), but Esc
and Ctrl+Q still close the window:

<!-- tui: key right; key space; key ctrl+r; key Q -->
```cabby
use tui

object viewer : tui.window {
	void build {
		set_title "Photos"
		add_label "photo 1"
		on_key pressed
	}

	void pressed {
		println "pressed", "[" + key + "]"
		if key == "Q" {
			close
		}
	}
}

int main {
	viewer V
	V.run
	return 0
}
```

```text
pressed [right]
pressed [ ]
pressed [ctrl+r]
pressed [Q]
```

## Differences from gui

* Pictures show as `[picture: name]`.
* A color button's list has 26 common colors, and a font button's 10
  common fonts; `set_color` and `set_font` can still give any. A terminal
  shows its own font whatever's chosen: the name is for the program to use,
  in a canvas say.
* Several windows can be open, as with gui, but a terminal shows one at a
  time: a window run from another's handler covers it until it closes.
* A `tui` program needs a terminal: with its input or output redirected to
  a file or pipe, `run` stops with a message.
* What's printed while a window is open appears after it closes (see
  above).
