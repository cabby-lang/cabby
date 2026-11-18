# images — pictures in memory

```cabby
use images
```

A picture is its pixels, in memory: drawn on a pixel at a time or in
shapes, read from a file and saved as one, shown on a window's canvas,
and made by a worker to hand to the main program whole. Tutorial 66 and
chapter 66 of the manual teach it.

<!-- run -->
```cabby
use images

int main {
	images.picture Sun = images.new 120, 80
	Sun.fill_with "midnightblue"
	Sun.set_color "gold"
	Sun.fill_circle 60, 40, 25
	Sun.set_color "orange"
	Sun.circle 60, 40, 30
	for X in 120 {
		Sun.set_pixel_rgb X, 79, X * 2, 0, 0      // a red line along the bottom, getting brighter
	}
	Sun.save "sun.png"
	println Sun.width, "by", Sun.height, "and the middle is", Sun.red(60, 40), Sun.green(60, 40), Sun.blue(60, 40)
}
```

```text
120 by 80 and the middle is 255 215 0
```

Positions are in pixels from the top left corner, `y` counting down, as a
canvas's are. A new picture is transparent all over. What's drawn
replaces what was there, its transparency too (`"rgba(0, 0, 0, 0.5)"`
makes half see-through pixels), except `draw`, which puts one picture on
another and lets what's see-through in it show through.

| Function | What it does |
|---|---|
| `images.picture images.new int Width, int Height` | A new picture, every pixel transparent. From 1 to 32768 pixels each way. |
| `images.picture images.load const str &Path` | A picture from a file: PNG, JPEG, BMP or GIF (its first frame). One that isn't there, or isn't a picture, is a panic. |

## images.picture

| Member | What it does |
|---|---|
| `int width const`, `int height const` | Its size, in pixels. |
| `void set_color const str &Color` | The color for what's drawn next, as a window's colors are written: `"red"`, `"steelblue"`, `"#3366cc"`, `"rgba(0, 0, 0, 0.5)"`. Black to begin with. |
| `void fill_with const str &Color` | Every pixel that color. |
| `void set_pixel int X, int Y` | One pixel, in the color for drawing. One outside the picture is left out. |
| `void set_pixel_rgb int X, int Y, int Red, int Green, int Blue` | One pixel, from its red, green and blue, 0 to 255 each: the quickest way to fill a picture a pixel at a time. |
| `long pixel int X, int Y const` | A pixel's color as one number, `0xRRGGBBAA` (`0` outside the picture). |
| `int red int X, int Y const`, `green`, `blue` | A pixel's red, green or blue, from 0 to 255. |
| `void line int X1, int Y1, int X2, int Y2` | A line a pixel wide, in the color for drawing. |
| `void rectangle int X, int Y, int Width, int Height`, `fill_rectangle` | A rectangle's edge, or the whole of it. |
| `void circle int X, int Y, int Radius`, `fill_circle` | A circle's edge, or the whole of it, around X, Y. |
| `void draw const images.picture &Other, int X, int Y` | Another picture on this one, its top left corner at X, Y; what's see-through in it lets this one show through. |
| `void save const str &Path const` | Saves it: as a PNG, or by its name a JPEG (`.jpg`, `.jpeg`) or a BMP (`.bmp`). |
| `images.picture copy const` | A second picture, the same as this one. |
| `bool is_empty const` | `true` for a picture with nothing in it: one handed on with `tell_picture`, or never made. |

A picture is moved, not copied, as a list is: `images.picture B = A`
leaves `A` empty, and `copy` makes a second one.

## Changed copies

Each makes a new picture from this one, which is left as it is, so a
picture changed is written as one made again:

<!-- check -->
```cabby
use images

int main {
	images.picture Photo = images.load "holiday.jpg"
	Photo = Photo.turned 90                     // taken with the phone on its side
	images.picture Small = Photo.scaled_to_fit 400, 400
	Small.save "holiday-small.jpg"
	images.picture Face = Photo.cropped 120, 80, 300, 300
	Face.gray.save "face.png"
	return 0
}
```

| | |
|---|---|
| `scaled Width, Height` | at a new size, smoothly: every pixel counted when it's made smaller (so it doesn't shimmer), blended when it's made bigger |
| `scaled_to_fit Width, Height` | as big as fits, keeping its shape; never bigger than it is (a thumbnail) |
| `scaled_by Times` | `0.5` half its size, `2` twice |
| `cropped X, Y, Width, Height` | a part of it, cut out (what of it is inside the picture; a panic if none is) |
| `turned Degrees` | turned clockwise: `90`, `180`, `270` exactly; any other angle on a picture big enough for its corners, what's outside see-through |
| `mirrored`, `flipped` | left to right, as in a mirror; upside down |
| `gray` | in grays, as light as each color looks |
| `blurred Radius` | blurred, by about so many pixels |

What's see-through stays so: an edge between a color and see-through
doesn't darken as it's scaled, turned or blurred.

## Text

<!-- check -->
```cabby
use images

int main {
	images.picture Card = images.new 400, 120
	Card.fill_with "midnightblue"
	Card.set_color "gold"
	images.set_font "Serif Bold", 40
	str Words = "Ἀρχὴ ἥμισυ παντός"
	Card.text (400 - images.text_width(Words)) / 2, 40, Words
	Card.save "card.png"
	return 0
}
```

| | |
|---|---|
| `text X, Y, Text` | text, in the color for drawing, its top left corner at `X, Y` (`"\n"` between lines) |
| `images.set_font Name, Size` | the font from then on: a font of the computer's by name (`"Sans"`, `"Serif"`, `"Monospace"`, `"Serif Bold"`, `"DejaVu Sans"`), or a font's file (`"fonts/Garamond.ttf"`); `Size` is its height in pixels. `"Sans"`, 16, to begin with |
| `int images.text_width Text` | how wide it would be, in pixels: for putting it in the middle, or on the right |

A font's name is looked up as the desktop looks it up (fontconfig, on
Linux); a Mac without fontconfig uses Arial. Text is drawn a letter at a
time, so Latin, Greek and Cyrillic, with their accents, are as they
should be; Hebrew and Arabic, which a window lays out, aren't yet. With
no font to be found, a small one built in is used (as it is with
`images.set_font "builtin", 16`, which is the same on every computer).

## On a window

A gui canvas shows a picture with `picture X, Y, P`, in its `on_draw`
handler. The window keeps what it has shown of each picture, and sends a
picture's pixels to the screen again only when they've changed, so one
shown on every redraw costs no more than a picture from a file. gtk draws
it the same way, and tui with the terminal's blocks, two pixels to a
block.

## From a worker

A worker (use threads) can make pictures and draw on them, and hand each
to the main program with `tell_picture`, for its `on_message` handler,
which takes it with `picture`:

```cabby
// in the worker's work:
images.picture Strip = images.new 360, 20
...
tell_picture Strip          // the main program's now: Strip is empty

// in the main program's on_message handler:
if Painter.has_picture {
	images.picture Strip = Painter.picture
	Whole.draw Strip, 0, Top
	Canvas.redraw
}
```

The picture is moved, as it is: however big, none of its pixels is
copied, so handing on a picture is as quick as handing on a number. A
picture is only ever one thread's at a time, so drawing on it takes no
locks either.

## How it works

`runtime/images.c` keeps each picture's pixels, four bytes each (red,
green, blue, and how opaque), a row after another from the top, and
reads and writes files with stb_image and stb_image_write, which are built
in. Colors are read by the same code as the windows' (`runtime/colors.h`),
so a color means the same everywhere.

## Not in images yet

* Text in Hebrew and Arabic laid out as a window lays it out (right to
  left, joined), and colored emoji
* More shapes than lines, rectangles and circles; lines thicker than a
  pixel, and smoothed edges
