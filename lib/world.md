# world — 3D: shapes, light and a camera

```cabby
use world
```

A world is a window looking into a place in three dimensions: **shapes**
(boxes, balls, planes, cylinders and cones), colored or with pictures on
them, lit by the **sun**, the light all round, and **lamps**, and seen by
a **camera**. It runs as a [game](game.md) does: `build` adds what's in
it, and the handler given to `on_frame` runs 60 times a second, moving
things a little and looking at the keys. The world is drawn again after
every frame.

Underneath is OpenGL, through SDL 3, so the world library needs SDL 3
installed (`sdl3` on Arch, `libsdl3-dev` on Debian, `brew install sdl3` on
a Mac), as gui and game do, and nothing else: the graphics card's OpenGL
is found when the window opens. Where there isn't one, Cabby draws the
world itself, more slowly.

Tutorials 89 and 90, and chapters 89 and 90 of the manual, are a first
walk through it. This page is the complete reference.

* [A world](#a-world)
* [Where things are](#where-things-are)
* [world.window](#worldwindow)
* [world.shape](#worldshape)
* [Models from Blender](#models-from-blender)
* [Light](#light)
* [The camera](#the-camera)
* [Touching](#touching)
* [Joined shapes](#joined-shapes)
* [Physics](#physics)
* [Text over the world](#text-over-the-world)
* [Keys and the mouse](#keys-and-the-mouse)
* [Frames and timers](#frames-and-timers)
* [How it's drawn](#how-its-drawn)
* [Testing a world without a person](#testing-a-world-without-a-person)
* [Not in world yet](#not-in-world-yet)

## A world

<!-- gui: frames 60; pixel 160 220; close -->
```cabby
use world

object spinner : world.window {
	world.shape Cube

	void build {
		set_title "Spinner"
		set_size 320, 240
		add_plane 20, 20, "forestgreen", 0, 0, 0
		Cube = add_box 1, 1, 1, "orange", 0, 0.5, 0
		point_camera_at 0, 0.5, 0
		on_frame step
	}

	void step {
		Cube.turn 1          // a degree a frame
	}
}

int main {
	spinner S
	S.run
	println "turned", S.Cube.angle, "degrees"
}
```

```text
pixel 160 220: #239024
turned 60.0 degrees
```

## Where things are

Distances are in **metres**, as a person would think of them, and **y is
up**. The ground is usually a plane at y 0. A shape's position is its
**middle**: a box a metre high stands on the ground at y 0.5, and a ball
of radius 0.3 rests on it at y 0.3.

The camera starts where a person might stand, at 0, 1.6, 6, looking
along **-z** (into the screen). For a shape too, -z is forward: at angle
0 it faces -z, and `move_forward` moves it that way. Angles are in
**degrees**: `turn` turns a shape round, to its left (counterclockwise,
seen from above), as a person turns.

## world.window

| Member | What it does |
|---|---|
| `void build` | Override it: add the shapes, lamps and handlers here. |
| `void run` | Shows the window and runs the world, until it's closed. |
| `void close` | Closes the window, ending `run`. |
| `void set_title const str &Title` | The window's title. |
| `void set_size int Width, int Height` | The window's size to begin with, in pixels (800 by 600). Made bigger, more of the world is seen. |
| `void set_background const str &Color` | The sky: `"skyblue"` to begin with. |
| `void set_gravity double G`, `double gravity` | How fast things fall: see [Physics](#physics). |
| `void show_message const str &Text` | Shows a message, with an OK button. |
| `void add_shortcut const str &Keys, handler h` | Calls `h` for these keys: `"P"`, `"F5"`, `"Ctrl+S"`. |
| `world.shape add_box double Width, double Height, double Depth, const str &Color, double X, double Y, double Z` | A box, Width across (x), Height high (y), Depth deep (z), its middle at X, Y, Z. |
| `world.shape add_ball double Radius, const str &Color, double X, double Y, double Z` | A ball. |
| `world.shape add_plane double Width, double Depth, const str &Color, double X, double Y, double Z` | Flat, facing up: the ground, a floor. Seen from below too. |
| `world.shape add_cylinder double Radius, double Height, const str &Color, double X, double Y, double Z` | A cylinder, standing up. |
| `world.shape add_cone double Radius, double Height, const str &Color, double X, double Y, double Z` | A cone, its point up. |
| `world.shape add_model const str &Path, double X, double Y, double Z` | A model made in Blender: see [Models from Blender](#models-from-blender). |
| `list world.shape shapes_in const str &Group` | The shapes in a group, in the order they were added (`""`: all). |
| `list world.shape touching const world.shape &Shape, const str &Group` | Those in a group (`""`: any) that the shape touches. |
| `world.lamp add_lamp double X, double Y, double Z, const str &Color, double Reach` | A lamp: see [Light](#light). |
| `world.text add_text const str &Text, double X, double Y` | Text over the world: see [Text over the world](#text-over-the-world). |
| `int random int From, int To` | A whole number from From to To, both included. |

The light, the camera, keys, the mouse, frames and timers are in their own
sections below.

## world.shape

| Member | What it does |
|---|---|
| `double x`, `double y`, `double z` | Where its middle is. |
| `void move_to double X, double Y, double Z` | Puts it there. |
| `void move double Dx, double Dy, double Dz` | Moves it by that much each way. |
| `void move_forward double Distance` | Moves it the way it faces, flat (its height stays as it is). Below 0: backwards. |
| `void move_sideways double Distance` | Moves it to its right (below 0: its left). |
| `double angle`, `void set_angle double Degrees`, `void turn double Degrees` | Which way it faces, round y: 0 to 360, 0 facing -z. `turn` turns it to its left (below 0: its right). |
| `double tilt`, `void set_tilt double Degrees`, `void tilt_by double Degrees` | Its nose up (below 0: down), round its own x. A cylinder tilted 90 lies on its side, a coin on its edge. |
| `double roll`, `void set_roll double Degrees`, `void roll_by double Degrees` | Leaning over sideways. |
| `void set_size double Width, double Height, double Depth` | Its size, in metres. |
| `double scale`, `void set_scale double Scale` | Bigger or smaller, all ways: 2 is twice the size. |
| `void set_color const str &Color` | Its color: `"orange"`, `"#336699"`. |
| `void set_picture const str &Picture` | A picture on it (PNG, JPEG, BMP or GIF): on each side of a box, round a ball or cylinder, over a plane. Its color tints the picture: `"white"` for none. Where the picture is see-through, so is the shape. |
| `void set_picture_size double Metres` | The picture repeated, each copy that many metres across: grass on a field, tiles on a floor. 0: once, stretched. |
| `bool visible`, `void set_visible bool On` | Hidden shapes aren't drawn (and can't be clicked). |
| `void remove`, `bool removed` | Takes it out of the world. |
| `void set_group const str &Group`, `str group` | A name for shapes of a kind: `"coins"`, `"walls"`. |
| `bool touches const world.shape &Other` | See [Touching](#touching). |
| `double distance_to const world.shape &Other` | How far its middle is from the other's. |
| `double top`, `double bottom` | The height of its top and bottom: `Lid.move_to 0, Box.top + 0.05, 0`. |
| `bool same_as const world.shape &Other` | True for two names for one shape. |
| `void join const world.shape &Other`, `void unjoin`, `bool joined`, `double world_x`, `world_y`, `world_z` | See [Joined shapes](#joined-shapes). |
| `void set_falls bool On`, `bool falls`, `bool on_ground`, `void set_solid bool On`, `bool solid` | See [Physics](#physics). |
| `void set_velocity double X, double Y, double Z`, `void push double X, double Y, double Z`, `double velocity_x`, `velocity_y`, `velocity_z`, `void set_bounce double Bounce`, `void set_friction double Friction` | See [Physics](#physics). |

A shape is turned round y first, then tilted, then rolled: so `turn` always
turns it as a person turns, however it's tilted.

## Models from Blender

A model made in Blender (or any 3D program that writes glTF 2.0) is
brought in with `add_model`, and is a `world.shape` like any other: it
moves, turns, scales, touches and is clicked on the same way.

```cabby
Robot = add_model "models/robot.glb", 0, 0, 0
Robot.play "Walk"
```

**Exporting it.** In Blender, File > Export > glTF 2.0, as **glTF Binary
(.glb)**: one file, its pictures inside it. (A `.gltf` file, with its
`.bin` and pictures beside it, is read too.) Make the model:

* **facing Blender's front** (towards you in the front view): it then
  faces -z, as every shape does, and `move_forward` walks it forwards;
* **its feet at the origin**: X, Y, Z in `add_model` is where Blender's
  origin goes, so a character put at y 0 stands on the ground;
* **a metre being a metre** (Blender's units are metres already).

What comes with it:

* **Its meshes**, each as it hangs from its object in Blender, moved,
  turned and sized as there.
* **Its materials' colors** (Base Color), and their **pictures** (an Image
  Texture into Base Color). A picture set to Closest in Blender stays
  sharp, as pixel art should. Where a picture is see-through, so is the
  model. `set_color` tints the whole model (`"white"`: as made), and
  `set_picture` puts a picture of your own on all of it.
* **Its armature**: a character's mesh bends with its bones.
* **Its animations**: each of Blender's actions, by its name.

A model is the size it was made: `set_scale` makes it bigger or smaller
(`set_size` is for the built-in shapes). Its box, for `touches`, `top` and
`bottom`, is the box round it as made.

| Member | What it does |
|---|---|
| `void play const str &Name` | Plays an animation over and over: `Hero.play "Walk"`. Playing it already, it goes on, so it can be said every frame a key is held. |
| `void play_once const str &Name` | Plays one once, to its end, where it stays: `"Jump"`, `"Wave"`. |
| `void stop_animation` | Back to how it was made. |
| `void set_animation_speed double Speed` | 1 as made, 2 twice as fast, 0.5 slower, -1 backwards. |
| `str playing` | What it's playing now (`""`: nothing, or a `play_once` has ended). |
| `double animation_time` | How far through it, in seconds. |
| `list str animations` | The animations it has, by name. |

An animation runs with the frames, so it waits while the world is paused,
and under a test script it's the same every time.

Not yet: shape keys (morph targets), cameras and lights made in Blender,
and Blender's own materials beyond their color and picture (shininess,
bumps).

## Light

Every shape is lit by three kinds of light, added together:

* **The sun**: light from far away, all one way. It starts coming down
  from behind the camera's left. `set_sun X, Y, Z` gives the way it shines
  (`0, -1, 0` is straight down, at noon); `set_sun_color` its color
  (`"white"` is bright; `"black"` turns it off, for night).
* **The light all round** (ambient), which reaches even the shade:
  `set_ambient "#59596a"` to begin with. Brighter, nothing is dark;
  `"black"`, the shade is black.
* **Lamps**: light from a place, falling off with distance until it
  reaches no further. `add_lamp X, Y, Z, Color, Reach` adds one; a
  `world.lamp` has `x`, `y`, `z`, `move_to`, `set_color`, `set_reach` and
  `remove`. A lamp carried by the walker is moved with it each frame. The
  eight nearest the camera are lit, if there are more.

**Shadows.** The sun casts them: what's in its way shades what's behind
it, from the ground under a ball to a wall's shadow across a path, and
where a picture is see-through (leaves), its shadow is too. They're cast
in the 30 metres round the camera, ahead of it, and move with it.
Lamps cast none.

| Member | What it does |
|---|---|
| `void set_shadows bool On`, `bool shadows` | Shadows, on to begin with: `set_shadows false` for none. |
| `void set_shadow_reach double Metres` | How far round the camera they're cast: 30 to begin with, from 1 to 500. Further, a larger world is shadowed, and its shadows' edges are a little blurrier. |

With the sun out (`set_sun_color "black"`) or shining up, there are none.

A ball a metre and a half up, the sun shining a little sideways: its
shadow falls on the ground a metre and a half to its right.

<!-- gui: frames 1; pixel 133 100; pixel 60 100; close -->
```cabby
use world

object noon : world.window {
	void build {
		set_size 200, 200
		add_plane 20, 20, "gray", 0, 0, 0
		add_ball 0.5, "orange", 0, 1.5, 0
		set_sun 1, -1, 0             // shining to the right, and down
		move_camera_to 0, 8, 0.001   // looking straight down
		point_camera_at 0, 0, 0
	}
}

int main {
	noon N
	N.run
}
```

```text
pixel 133 100: #2d2d33
pixel 60 100: #7a7a7c
```

## The camera

| Member | What it does |
|---|---|
| `void move_camera_to double X, double Y, double Z` | Puts it there. |
| `void move_camera double Dx, double Dy, double Dz` | Moves it by that much. |
| `void camera_forward double Distance` | Forward the way it looks, flat, as a person walks. |
| `void camera_sideways double Distance` | To its right (below 0: left). |
| `void point_camera_at double X, double Y, double Z` | Turns it to look at that place. |
| `void turn_camera double Left, double Up` | Turns it, in degrees: left (below 0: right), and up (below 0: down). |
| `void set_camera_angle double Angle, double Tilt` | Which way it looks: its angle round, as a shape's, and up or down (from -89 to 89). |
| `double camera_x`, `camera_y`, `camera_z`, `camera_angle`, `camera_tilt` | Where it is, and which way it looks. |
| `void set_field_of_view double Degrees` | How much it sees, top to bottom: 60 to begin with. Less is zoomed in. |
| `void follow const world.shape &Shape, double Distance, double Height` | Keeps it Distance metres behind the shape and Height above it, looking at it, as it moves and turns: the camera of most 3D games. Moving or pointing the camera stops it. |

A first-person game moves the camera itself, as the player's eyes:

<!-- check -->
```cabby
use world

object walk : world.window {
	void build {
		add_plane 40, 40, "forestgreen", 0, 0, 0
		add_box 2, 3, 2, "brown", 0, 1.5, -6
		lock_mouse true
		on_frame step
	}

	void step {
		turn_camera -mouse_moved_x * 0.2, -mouse_moved_y * 0.2
		if key_hold "w" {
			camera_forward 0.08
		}
		if key_hold "s" {
			camera_forward -0.08
		}
		if key_hold "a" {
			camera_sideways -0.08
		}
		if key_hold "d" {
			camera_sideways 0.08
		}
	}
}

int main {
	walk W
	W.run
}
```

## Touching

`A.touches B` is true when the two overlap: the boxes round them do, as
they stand (turned, the box round a shape grows to hold it). It's quick,
and right for boxes standing straight; for balls it's true a little early,
at the corners of their boxes. A plane is touched by anything resting on
it. `touching Shape, "coins"` gives every coin the shape touches.

## Joined shapes

A shape joined to another moves and turns with it: a car's wheels, a hat
on a head, a lamp's shade. `Wheel.join Car` joins it where it is; from
then on its `x`, `y`, `z` and angles are counted from the car (along the
car's own directions), so `Car.turn 90` carries the wheel round with it,
and `Wheel.roll_by 10` still spins it on the car. `world_x`, `world_y` and
`world_z` say where it is in the world, joined or not.

| Member | What it does |
|---|---|
| `void join const world.shape &Other` | Joined to Other, staying where it is. A shape is joined to one other at a time (joining again moves it), and not to itself or to one joined to it. |
| `void unjoin`, `bool joined` | On its own again, where it is. |
| `double world_x`, `world_y`, `world_z` | Where its middle is in the world. |

Hiding or removing a shape hides or removes what's joined to it. A
joined shape doesn't fall by itself: what it's joined to does, and it
lands with it (a car on its wheels).

## Physics

A shape that **falls** is pulled down by gravity, lands on what's solid,
bounces, and is stopped by what it runs into, all by itself, each frame
after `on_frame`'s handler:

```cabby
Ball.set_falls true
```

Everything is **solid** to begin with: the ground, walls, other shapes.
`set_solid false` lets falling things through (a coin to be picked up, a
ghost), and `touches` still sees them.

| Member | What it does |
|---|---|
| `void set_falls bool On`, `bool falls` | Falling, landing and bouncing, or not (to begin with). |
| `bool on_ground` | Standing on something: for jumping only when it can (`if Hero.on_ground { Hero.push 0, 5, 0 }`). |
| `void set_velocity double X, double Y, double Z` | How fast it's going each way, in metres a second. |
| `void push double X, double Y, double Z` | As much again, added: a jump, a kick. |
| `double velocity_x`, `velocity_y`, `velocity_z` | How fast it's going now. |
| `void set_bounce double Bounce` | How much of its speed it keeps bouncing back: 0, none (a sack); 1, all (a superball); 0.3 to begin with. Slower than a metre a second, it doesn't bounce, and comes to rest. |
| `void set_friction double Friction` | How quickly it slows on the ground: 0 is ice, 0.5 to begin with. A ball rolls on, a tenth as slowed. |
| `void set_solid bool On`, `bool solid` | Whether falling things land on it and are stopped by it (true to begin with). |
| `void set_gravity double G`, `double gravity` | The world's (world.window's): 9.8 metres a second, each second, the Earth's, to begin with. The Moon's is 1.6; 0, nothing falls. |

A shape with a velocity that **doesn't** fall just moves by it, each
frame, through whatever's there: a platform, a lift, a door. What stands
on it rides along.

Moving a falling shape by hand (`move_forward`, `move_to`) works as
before, and what's solid still stops it: a hero who falls is walked with
`move_forward` and can't walk through walls. Two falling shapes push
each other apart, and stack.

A ball is a ball; any other shape is the box round it, as it stands (see
[Touching](#touching)): a cylinder lands as a box would, and a turned box
on its box. It's quick, and right for the shapes games are mostly made
of.

A ball dropped from four metres, bouncing lower each time until it rests
on the ground:

<!-- gui: frames 400 -->
```cabby
use world

object bouncing : world.window {
	world.shape Ball
	int Landings
	bool WasDown

	void build {
		add_plane 20, 20, "forestgreen", 0, 0, 0
		Ball = add_ball 0.5, "orange", 0, 4, 0
		Ball.set_falls true
		Ball.set_bounce 0.7
		on_frame step
	}

	void step {
		if Ball.on_ground && !WasDown {
			Landings++
		}
		WasDown = Ball.on_ground
		if frame == 300 {
			close
		}
	}
}

int main {
	bouncing B
	B.run
	println "landed", B.Landings, "times, and rests at", B.Ball.y
}
```

```text
landed 7 times, and rests at 0.5
```

## Text over the world

`add_text "Score: 0", 10, 10` writes text in front of everything, its top
left corner 10, 10 pixels from the window's, staying there as the camera
moves: a score, a message, what's being aimed at. A `world.text` has:

| Member | What it does |
|---|---|
| `void set_text const str &Text`, `str text` | What it says. |
| `void move_to double X, double Y` | Where it is, in pixels. |
| `void set_color const str &Color` | `"white"` to begin with. |
| `void set_size double Size` | Its height, in pixels: 20 to begin with. |
| `void set_visible bool On`, `void remove` | Hides it, or takes it away. |

It's written in the desktop's bold sans-serif font (as gui finds fonts),
or with none, Cabby's own.

## Keys and the mouse

Keys are read in frames, as in a game, by name (`"up"`, `"space"`, `"w"`,
`"enter"`, `"f1"`):

| Member | What it does |
|---|---|
| `bool key_press const str &Key` | True in the frame the key went down. |
| `bool key_hold const str &Key` | True in every frame it's held. |
| `bool key_release const str &Key` | True in the frame it was let go. |
| `int key_held_for const str &Key` | How long it's been held, in milliseconds. |
| `void on_key handler h`, `str key` | Calls `h` for each key pressed; `key` says which (`"a"`, `" "`, `"left"`). |
| `void on_click handler h`, `void on_mouse_move handler h` | Calls `h` when the window is clicked, or the mouse moves. |
| `int mouse_x`, `int mouse_y` | Where the mouse is, in pixels from the window's top left. |
| `bool mouse_on_shape`, `world.shape shape_at_mouse` | Whether there's a shape under the mouse, and which: the nearest. |
| `void lock_mouse bool On`, `bool mouse_locked` | Hides the mouse and keeps it in the window, for looking round. Escape lets it go. |
| `double mouse_moved_x`, `double mouse_moved_y` | How far the mouse moved in this frame, in pixels: for looking round with a locked mouse. |

## Frames and timers

| Member | What it does |
|---|---|
| `void on_frame handler h` | Calls `h` every frame, 60 times a second. |
| `long frame` | How many frames have run. |
| `void pause`, `void resume`, `bool paused` | Stops the frames and timers (the world is still drawn). |
| `void every duration Wait, handler h` | Calls `h` every so often: `every time.seconds(2), spawn`. |
| `void after duration Wait, handler h` | Calls `h` once, after a while. |
| `void stop_timers` | Stops them all. |

Timers count frames, so they wait while the world is paused.

## How it's drawn

With **OpenGL** 3.3 (a Mac's 4.1, which has everything 3.3 has): each
shape's surface lit at every pixel, edges smoothed where the graphics card
can, pictures smoothed as they're seen nearer or further. Linux's OpenGL
comes with the desktop (Mesa); a Mac's is built in.

Where there's no OpenGL (over SSH, in a virtual machine without it), Cabby
draws the world itself, the same way, lit the same: slower (fine for a
window 800 by 600 and a few hundred shapes), and with sharper edges and
pictures. `CABBY_WORLD_DRAWING=cabby` asks for that anyway.

Shadows are drawn the same way by both: the world drawn first as the sun
sees it, keeping only how far each place is (a map 2048 by 2048 with
OpenGL, 1024 when Cabby draws), and each pixel in the sun only if nothing
in that map is nearer the sun, looked at nine times round, for a softer
edge.

## Testing a world without a person

With `CABBY_GUI_SCRIPT` set, a world follows a script instead of waiting
for a person, as a [game](game.md#testing-a-game-without-a-person) does,
and frames run only when the script says. The commands are game's
(`frames`, `wait`, `hold`, `release`, `tap`, `key`, `shortcut`, `shot`,
`processes`, `close`), with the mouse's place in the window (`press 120
80` clicks there, `point 120 80` moves there), and:

| Command | Does |
|---|---|
| `look <dx> <dy>` | the mouse moved that far, as a locked mouse is read |
| `pixel <x> <y>` | prints the color drawn there: `pixel 160 220: #239024` |

Under a script, Cabby draws the world itself, off screen, so no display is
needed and the picture is the same on every computer, to the pixel.
(`CABBY_WORLD_DRAWING=opengl` draws it with OpenGL instead, to compare.)

## Not in world yet

* Physics beyond boxes and balls: a shape's own outline, things that tip
  over and spin when they're hit
* Sounds placed in the world, louder nearer
* A terminal twin, as tgame is game's
