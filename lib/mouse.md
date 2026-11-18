# mouse — the mouse, the touchpad and the cursor

```cabby
use mouse
```

How fast the pointer moves, which way scrolling goes, left-handed
buttons, tapping the touchpad to click, and how big the cursor is and
what it looks like. For a setup script that makes a new computer feel
like yours, or a program that makes the cursor bigger for someone who
finds it hard to see.

| Desktop | Asked and changed by | Kept |
|---|---|---|
| Sway | `swaymsg` | until Sway starts again (its config keeps them for good) |
| GNOME | `gsettings`, as GNOME's Settings app keeps them | for good |
| another desktop on X11 | `xinput`, for each mouse and touchpad (not the cursor) | until X starts again |

KDE Plasma's and a Mac's aren't here yet: there, `mouse.can_change` is
`false`, and the values are the usual ones (`0.0`, `false`, `-1`).

<!-- check -->
```cabby
use mouse

int main {
	if !mouse.can_change {
		println "no mouse settings here Cabby can change"
		return 1
	}
	println "speed:", mouse.speed, "natural scrolling:", mouse.natural_scrolling
	mouse.set_speed 0.3
	if mouse.has_touchpad {
		mouse.set_tap_to_click true
		mouse.set_touchpad_natural_scrolling true
	}
	mouse.set_cursor_size 48
	return 0
}
```

## The mouse

| Call | What it gives, or does |
|---|---|
| `mouse.speed` | how fast the pointer moves: from -1.0 (slowest) through 0.0 (the usual) to 1.0 (fastest) |
| `mouse.set_speed Speed` | sets it (kept from -1.0 to 1.0) |
| `mouse.natural_scrolling` | `true` if scrolling moves what's shown the way the fingers go, as on a phone |
| `mouse.set_natural_scrolling Yes` | sets it |
| `mouse.left_handed` | `true` if the buttons are swapped, for the left hand |
| `mouse.set_left_handed Yes` | sets it |

## The touchpad

| Call | What it gives, or does |
|---|---|
| `mouse.has_touchpad` | `true` if there's a touchpad whose settings can be read |
| `mouse.touchpad_speed`, `set_touchpad_speed Speed` | its own speed, from -1.0 to 1.0 |
| `mouse.touchpad_natural_scrolling`, `set_touchpad_natural_scrolling Yes` | its own scrolling (many people have it natural on the touchpad but not the mouse) |
| `mouse.tap_to_click`, `set_tap_to_click Yes` | `true` if a tap is a click |

## The cursor

| Call | What it gives, or does |
|---|---|
| `mouse.cursor_size` | how big it is, in pixels: 24 is the usual; 32, 48 and 64 are bigger (-1 if it can't be found out) |
| `mouse.set_cursor_size Pixels` | sets it, from 8 to 256 |
| `mouse.cursor_theme` | its look: `"Adwaita"`, `"Breeze"`, `"Bibata-Modern-Classic"` |
| `mouse.set_cursor_theme Name` | sets it: a folder in `~/.icons` or `/usr/share/icons` with a `cursors` folder inside |
| `mouse.can_change` | `true` if this desktop's mouse settings are ones Cabby can change |

Each setting call gives `true` if it was done, and `false` if it
couldn't be (no such device, or a desktop that can't).

## Notes

* On Sway, the cursor is set for every seat (`seat * xcursor_theme`), and
  programs already open may keep the old one until they're opened again.
* On X11, each mouse or touchpad that has the setting gets it; a device
  plugged in afterwards has the usual ones.
* A touchpad's tapping and scrolling are `libinput`'s, so they work the
  same under Sway, GNOME and X11.
