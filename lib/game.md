# game — sprites, frames and keys

```cabby
use game
```

A game is a window with a **stage**, and **sprites** on it: pictures or
shapes that move. Nothing loops in the program itself. The game runs in
**frames**, 60 a second, and calls the handler given to `on_frame` for
each one; the handler moves things a little and looks at the keys. The
stage is drawn again after every frame.

A `game.window` is built the way a [`gui.window`](gui.md) is (in `build`,
then `run`), and has gui's `set_title`, `close`, `every`, `after`,
`add_shortcut` and `show_message`; its stage draws with the same methods
as gui's canvas. Underneath is SDL 3, which draws with the graphics card,
so a game needs SDL 3 installed (`sdl3` on Arch, `libsdl3-dev` on Debian)
and nothing else, as gui does. Pictures are read, text written and PNG files
saved by code built into Cabby (stb_image, stb_truetype and
stb_image_write), with the computer's own fonts.

To play in a terminal instead, change `game` to `tgame`: see
[tgame](tgame.md).

Tutorials 68 to 73 and chapters 68 to 73 of the manual teach it step by step; for sound, see [sound](sound.md). This page
is the complete reference.

* [A game](#a-game)
* [game.window](#gamewindow)
* [game.sprite](#gamesprite)
* [Touching](#touching)
* [A bigger world, and a camera](#a-bigger-world-and-a-camera)
* [Layers: in front, and behind](#layers-in-front-and-behind)
* [Keys](#keys)
* [Frames and timers](#frames-and-timers)
* [Drawing over the stage](#drawing-over-the-stage)
* [Testing a game without a person](#testing-a-game-without-a-person)
* [Not in game yet](#not-in-game-yet)

## A game

<!-- check -->
```cabby
use game

object dodge : game.window {
	game.sprite Player

	void build {
		set_title "Dodge"
		set_size 480, 360
		set_background "black"
		Player = add_circle 12, "gold", 240, 300
		on_frame step
	}

	void step {
		if key_hold "left" {
			Player.move -5, 0
		}
		if key_hold "right" {
			Player.move 5, 0
		}
	}
}

int main {
	dodge d
	d.run
}
```

Positions are in pixels from the stage's top left corner, with y counting
**down**, as on a canvas. A sprite's position is its **middle**.

## game.window

| Member | What it does |
|---|---|
| `void build` | Override it: add the sprites and handlers here. |
| `void run` | Calls `build`, shows the window and runs the game, until the window is closed. |
| `void close` | Closes the window, ending `run`. |
| `void set_title const str &Title` | The window's title. |
| `void add_shortcut const str &Keys, handler h` | Calls `h` when these keys are pressed, even while paused: `"P"`, `"F5"`, `"Ctrl+S"`. |
| `void show_message const str &Text` | Shows a message in a box over the stage, with an OK button, and waits (the game stops) until OK, Enter, Space or Escape closes it. It's drawn by the game itself, so it appears at once. |
| `void set_size int Width, int Height` | The stage's size (640 by 480 to begin with). The window starts that size; made bigger (or full screen), the stage grows with it, keeping its shape, and is still that many pixels to the game. |
| `int width const`, `int height const` | The stage's size. |
| `void set_background const str &Background` | A color (`"skyblue"`, `"#336699"`), or a picture file, stretched to fill the stage. White to begin with. |
| `game.sprite add_sprite const str &Picture, double X, double Y` | A sprite from a picture file (PNG, JPEG, BMP or GIF), its middle at X, Y. |
| `game.sprite add_rectangle double Width, double Height, const str &Color, double X, double Y` | A rectangle sprite. |
| `game.sprite add_circle double Radius, const str &Color, double X, double Y` | A circle sprite. |
| `void on_frame handler h` | Calls `h` every frame, 60 times a second. |
| `long frame const` | How many frames have run. |
| `void pause`, `void resume`, `bool paused const` | Stops the frames and timers, and starts them again. The stage is still drawn, and menus and shortcuts still work (so a shortcut can resume). |
| `void every duration Wait, handler h` | Calls `h` every so often, counted in frames: `every time.milliseconds(800), drop_apple`. |
| `void after duration Wait, handler h` | Calls `h` once, after a while. |
| `void stop_timers` | Stops them all. |
| `bool key_press const str &Key const` | True in the frame the key went down. |
| `bool key_hold const str &Key const` | True in every frame the key is held down. |
| `bool key_release const str &Key const` | True in the frame the key was let go. |
| `int key_held_for const str &Key const` | How long it has been held, in milliseconds; in the frame it's let go, how long it was held. 0 otherwise. |
| `int random int From, int To const` | A whole number from From to To, both included. |
| `list game.sprite sprites_in const str &Group const` | Every sprite in the group, in the order they were added (`""`: every sprite). |
| `void set_world_size int Width, int Height` | The world's size, when it's bigger than the stage (see [the camera](#a-bigger-world-and-a-camera)). |
| `void set_world_area int Left, int Top, int Width, int Height` | The same, for a world whose top left corner isn't 0, 0: one that reaches above the top, or left of the left. |
| `int world_left const`, `int world_top const` | Its top left corner (0, 0 unless `set_world_area` says otherwise). |
| `int world_width const`, `int world_height const` | The world's size (the stage's, unless set). |
| `void follow const game.sprite &Sprite const` | The camera keeps the sprite in the middle of the stage. |
| `void sort_by_depth int Layer` | In that layer, a sprite lower on the stage is drawn in front (see [layers](#layers-in-front-and-behind)). |
| `void move_camera_to double X, double Y` | The camera shows the world from there (the point at the stage's top left), and stops following. |
| `double camera_x const`, `double camera_y const` | Where the camera is. |
| `game.canvas stage` | The canvas the game is drawn on (see [Drawing over the stage](#drawing-over-the-stage)). |

One game window runs at a time.

## game.sprite

| Member | What it does |
|---|---|
| `double x const`, `double y const` | Where its middle is. |
| `void move_to double X, double Y` | Moves its middle there. |
| `void move double Across, double Down` | Moves it by that much (negative: left or up). |
| `void move_forward double Distance` | Moves it the way it's facing. |
| `double angle const`, `void set_angle double Degrees` | Which way it faces, in degrees clockwise: 0 as it was drawn (facing up the stage), 90 facing right. |
| `void turn double Degrees` | Turns it clockwise (negative: anticlockwise). |
| `double scale const`, `void set_scale double Scale` | How big it's drawn: 1 as it is, 2 twice as big. |
| `double width const`, `double height const` | Its size on the stage, in pixels (before turning). |
| `bool visible const`, `void set_visible bool On` | Hides it, or shows it again. |
| `void remove`, `bool removed const` | Takes it off the stage for good. A removed sprite can still be asked where it was, and moving it does nothing. |
| `void set_picture const str &Path` | Shows another picture (a picture sprite). |
| `void set_color const str &Color` | Changes its color (a shape). |
| `void animate const list str &Pictures, int Ms` | Shows the pictures in turn, each for Ms milliseconds, round and round. `set_picture` stops it. |
| `void to_front`, `void to_back` | Drawn over all the others in its layer, or under them. Otherwise sprites are drawn in the order they were added. |
| `void set_layer int Layer`, `int layer const` | The layer it's drawn in: lower layers first. 0 to begin with. |
| `bool touches const game.sprite &Other const` | Whether it touches the other sprite. |
| `list game.sprite touching const str &Group const` | The sprites in the group that it touches (`""`: any). |
| `void set_group const str &Group`, `str group const` | A name for a kind of sprite (`"rock"`, `"wall"`), to find them by. |
| `double left const`, `top`, `right`, `bottom` | Its edges in the world: the box round what shows of it. |
| `bool on_stage const` | Whether any of it is where the camera can see. |

A sprite is a handle to what's on the stage, so all its methods are
`const`: even a read-only one, as `for Rock in sprites_in "rock"` gives,
can move it.

## Touching

`A.touches B` is true when the two sprites overlap. What counts:

* a circle is its circle;
* a rectangle is its box;
* a picture is the box round its pixels that show, so the see-through
  edges round a character don't count;
* a turned rectangle or picture is the box round it as turned.

A hidden or removed sprite touches nothing.

Groups save keeping lists of sprites. Put a sprite in one with
`set_group "rock"`; then `sprites_in "rock"` is every rock, and
`Bullet.touching "rock"` every rock that bullet touches:

```cabby
for Bullet in sprites_in "bullet" {
	Bullet.move_forward 8
	for Rock in Bullet.touching "rock" {
		Rock.remove
		Bullet.remove
	}
	if !Bullet.on_stage {
		Bullet.remove
	}
}
```

Each is a list made when it's asked for, so removing sprites while going
through it is safe.

## A bigger world, and a camera

By default the world is the stage. `set_world_size` makes it bigger, and
the stage then shows a stage-sized part of it, from where the **camera**
is. Sprites are always placed in the world (their `x`, `y`, `left` and
the rest are world positions), wherever the camera looks.

`follow Hero` keeps the camera on a sprite, in the middle of the stage;
near the world's edges it stops, so it never shows past them.
`move_camera_to X, Y` puts it somewhere instead (the world's point at the
stage's top left corner). A picture background is stretched over the
whole world.

A world can start above or to the left of 0, 0, too:
`set_world_area -240, 0, 1200, 600` is a world from x -240 to 960. It's for
a world that's drawn around a part of it, such as the area next door
drawn beyond the edge of a game's map: tiles placed at negative positions
are in the world, and the camera goes as far as they do.

What the stage's `on_draw` draws is not moved by the camera: it stays put
on the stage, as a score or a message should. To draw at a sprite's
place, take the camera away: `Hero.x - camera_x`. The mouse
(`stage.mouse_x`) is on the stage too; `camera_x + stage.mouse_x` is the
world's x under it.

## Layers: in front, and behind

Sprites are drawn in **layers**, the lowest first; each sprite starts in
layer 0. Within a layer they're drawn in the order they were added (or as
`to_front` and `to_back` put them), unless the layer is **sorted by
depth**: then a sprite whose bottom edge is lower on the stage is drawn in
front, worked out again every frame.

That's what a world seen from above wants. Put the ground (grass, paths,
water) in layer 0, and everything that stands on it (the hero, trees,
houses, people) in layer 1, sorted by depth:

```cabby
sort_by_depth 1
Hero.set_layer 1
Tree.set_layer 1
```

A tree taller than its tile then hides a hero standing just north of it,
behind its leaves, and is behind one standing south of it. Only what
shows of a picture counts for its bottom edge, so the see-through space
under a character's feet doesn't. A layer 2 would be over everyone:
clouds, or a roof to walk under.

## Keys

Keys are named as a canvas names them: `"a"` to `"z"`, `"0"` to `"9"`,
`"space"`, `"enter"`, `"escape"`, `"tab"`, `"backspace"`, `"up"`,
`"down"`, `"left"`, `"right"`, `"f1"` to `"f12"`; capitals don't matter.

```cabby
if key_press "space" {          // once, when it goes down: jump
	jump
}
if key_hold "right" {           // every frame it's held: walk
	Player.move 4, 0
}
if key_release "x" {            // when it's let go: fire, harder the longer it was held
	fire key_held_for("x")
}
```

The keys go to the game wherever the window's focus is, except Tab
(which still moves between buttons) and keys used with Ctrl, Alt or the
Super key, and any key that is one of the window's shortcuts.

## Frames and timers

Each frame, in order: the keys are looked at, `on_frame`'s handler runs,
then any timers that are due, and the stage is drawn again.

Timers in a game are counted in frames (800 milliseconds is 48 frames),
not by the clock, so they keep in step with everything else and wait
while the game is paused.

## Drawing over the stage

`stage` is the canvas the game is drawn on. The background and the sprites
are drawn first; then the stage's own `on_draw` handler, so what it draws
(a score, a message) is on top:

```cabby
void build {
	stage.on_draw draw_score
}

void draw_score {
	stage.set_color "white"
	stage.write 10, 10, "Score: " + Score.text
}
```

The stage is a `game.canvas`, with the same methods as a
[gui.canvas](gui.md#guicanvas-drawing): `set_color`, `fill_with`,
`set_line_width`, `set_font_size`, `set_font`, `line`, `rectangle`,
`circle`, `ellipse`, `arc`, `polygon` and their `fill_` forms, `write`,
`image`, and `on_click`, `on_mouse_move`, `on_key`, `mouse_x`, `mouse_y`
and `key` for the mouse and keys.

For words in a box (what someone says, a menu), the stage has more:

| Member | What it does |
|---|---|
| `int write_wrapped double X, double Y, double Width, const str &Text` | Writes the text broken into lines no wider than Width (at spaces, and at `"\n"`), its top left corner at X, Y. Gives how many lines it took. |
| `int lines_for double Width, const str &Text const` | How many lines `write_wrapped` would take: to size the box before writing in it. |
| `double text_width const str &Text const` | How wide the text is, in the font and size set now. |
| `double line_height const` | How far apart lines are. |

```cabby
int Lines = stage.lines_for(300, Message)
stage.set_color "white"
stage.fill_rectangle 20, 20, 320, Lines * stage.line_height + 20
stage.set_color "#2b2b38"
stage.write_wrapped 30, 30, 300, Message
```

`save` keeps a picture of the whole stage, sprites and all, as a PNG
file. Its `redraw` does nothing, since the stage is drawn every frame
anyway.

## Testing a game without a person

Under `CABBY_GUI_SCRIPT` (as for [gui](gui.md#testing-a-window-without-a-person)),
the game is drawn off screen (no display is needed), frames only run when
the script says, and `random` gives the same numbers every time, so a test
sees the same game every time. Commands, separated by `;`:

| Command | Does |
|---|---|
| `frames <n>` | runs n frames |
| `wait <ms>` | runs that long's frames (`wait 1000` is `frames 60`) |
| `hold <key>` | the key goes down, and stays down |
| `release <key>` | the key comes up |
| `tap <key>` | the key goes down for one frame, and comes up in the next |
| `key <name>` | calls the stage's `on_key` with that key |
| `press 1 <x> <y>`, `point 1 <x> <y>` | clicks the stage there, or moves the mouse there |
| `shortcut <keys>` | presses a shortcut: `shortcut P` |
| `shot <file.png>` | saves a picture of the stage |
| `close` | closes the window |

```sh
CABBY_GUI_SCRIPT="frames 60; hold right; frames 30; release right; shot game.png; close" cabby game.cby
```

## Not in game yet

* Several game windows open at once
