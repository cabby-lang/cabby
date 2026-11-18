# tgame — a game in a terminal

```cabby
use tgame
```

The [game](game.md) library again, shown in a terminal instead of a
window: `tgame.window`, `tgame.sprite` and `tgame.canvas`, with every
method `game` has, doing the same things. Change `game` to `tgame` (and
`game.` to `tgame.`), and the same program runs in a terminal, as
[tui](tui.md) does for [gui](gui.md). That page, `game.md`, is the whole
reference; this one says what's different in a terminal.

* [How it's shown](#how-its-shown)
* [Keys](#keys)
* [The mouse, messages and printing](#the-mouse-messages-and-printing)
* [Testing](#testing)
* [Needs](#needs)

## How it's shown

The stage is drawn just as `game` draws it (by SDL, off screen), then made
as big as the terminal allows, keeping its shape, and shown with half
blocks: each character of the terminal is two pixels, one above the other.
A bigger terminal, or a smaller font, shows more of the picture's detail.
The game runs at 60 frames a second, as in a window; the terminal is
updated 30 times a second, and only the characters that changed are
written, so it plays smoothly even over SSH.

Text written with `stage.write` isn't drawn into the picture, where it
would be too small to read: it's put in the terminal as real characters,
at its place, in its color. Its font and size don't count.

Colors are exact in terminals that have 24-bit color (most do; they say
so with `COLORTERM=truecolor`), and the nearest of 256 otherwise.

## Keys

`key_press`, `key_hold`, `key_release` and `key_held_for` work as in a
window in terminals that tell a program when a key is let go: those that
follow kitty's keyboard protocol, such as kitty, foot, WezTerm, Ghostty,
Alacritty and iTerm2 (with "Report keys using CSI u" turned on). tgame
asks the terminal, and uses it when it's there.

Other terminals only send a key again and again while it's held. Then a
key is taken as held until its repeats stop, so holding works, but a key
can't be told apart from a quick tap until the terminal starts repeating
it, and two keys held at once may not both count.

**Ctrl+C** or **Ctrl+Q** ends the game (as closing its window does).

## The mouse, messages and printing

* **The mouse**: clicks on the stage reach `stage.on_click`, at the
  stage's own pixels, where the terminal passes clicks on (most do).
  `on_mouse_move` works as well.
* **`show_message`** shows a box in the middle of the terminal, over the
  game, which waits until Enter, Space or Escape closes it.
* **Printing**: what the program prints while the game runs is kept, and
  shown in the terminal after the game ends, so it doesn't land on the
  picture.

## Testing

Under `CABBY_GUI_SCRIPT` (see [game's testing](game.md#testing-a-game-without-a-person))
a tgame runs exactly as a game does, without a terminal, with every
command game has, and one more:

| Command | Does |
|---|---|
| `terminal <file>` | saves the stage as the terminal would show it (the characters and their colors, as codes a terminal understands), `CABBY_TERMINAL_SIZE` big (`80x24` to begin with) |

`cat` the file in a terminal to see it.

## Needs

SDL 3, as `game` does (`sdl3` on Arch, `libsdl3-dev` on Debian, `brew
install sdl3` on a Mac), which draws the stage into a picture in memory;
no window, screen or desktop is needed, so it works over SSH. And a
terminal that understands 24-bit or 256 colors and Unicode, as nearly
every terminal does.

A `game` (or `gui`) program started over SSH says to use `tgame` (or
`tui`) instead: on Linux it can't open a window there, and on a Mac its
window opens on the Mac's own screen, not in front of you.
