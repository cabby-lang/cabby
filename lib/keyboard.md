# keyboard — layouts, repeating, and the lock keys

```cabby
use keyboard
```

Which letters the keys type (the layouts to choose between), how soon a
held key repeats and how fast, and whether Caps Lock and Num Lock are
on. For a setup script, a bar that shows the layout, or a program for
someone learning to type in another language.

| Desktop | Asked and changed by | Kept |
|---|---|---|
| Sway | `swaymsg` | until Sway starts again (its config keeps them for good) |
| GNOME | `gsettings`, as GNOME's Settings app keeps them | for good |
| another desktop on X11 | `setxkbmap` and `xset` | until X starts again |

KDE Plasma's and a Mac's aren't here yet: there, `keyboard.can_change` is
`false`. The lock keys are read on any Linux, from the keyboard's own
lights.

<!-- check -->
```cabby
use keyboard

int main {
	println "layouts:", keyboard.layouts, "typing with:", keyboard.layout
	println "repeats after", keyboard.repeat_delay, "ms,", keyboard.repeat_rate, "times a second"
	if keyboard.caps_lock {
		println "Caps Lock is on"
	}
	list str Mine = {"us", "de"}
	keyboard.set_layouts Mine
	keyboard.set_repeat_delay 300
	keyboard.set_repeat_rate 40
}
```

## Layouts

A layout is named as X11's keyboard files name it, which Sway, GNOME and
X11 all use: `"us"`, `"gb"`, `"de"`, `"fr"`, `"es"`, `"gr"` (Greek), and a
variant in parentheses: `"us(dvorak)"`, `"de(neo)"`, `"gb(extd)"`.
`localectl list-x11-keymap-layouts` lists them all, and
`localectl list-x11-keymap-variants de` a layout's variants.

| Call | What it gives, or does |
|---|---|
| `keyboard.layouts` | the layouts to choose between, in order: `{"us", "de"}` (empty if they can't be found out) |
| `keyboard.layout` | the one typed with now (on GNOME and X11, the first) |
| `keyboard.set_layouts Names` | sets the layouts to choose between; the first is typed with now |
| `keyboard.set_layout Name` | sets just one |

## Repeating

| Call | What it gives, or does |
|---|---|
| `keyboard.repeat_delay` | how long a key is held before it repeats, in milliseconds (-1 if it can't be found out) |
| `keyboard.set_repeat_delay Milliseconds` | sets it, from 100 to 2000 |
| `keyboard.repeat_rate` | how many times a second a held key repeats |
| `keyboard.set_repeat_rate PerSecond` | sets it, from 1 to 100 |

## The lock keys

| Call | What it gives |
|---|---|
| `keyboard.caps_lock` | `true` if Caps Lock is on |
| `keyboard.num_lock` | `true` if Num Lock is on |
| `keyboard.has_lock_lights` | `true` if there's a keyboard light that says (a laptop's keyboard may have none for Num Lock) |
| `keyboard.can_change` | `true` if this desktop's keyboard settings are ones Cabby can change |

Each setting call gives `true` if it was done, and `false` if it
couldn't be (a name that isn't a layout's, a number out of range, or a
desktop that can't).

## Notes

* Sway says each layout's description ("English (US)"); Cabby finds its
  name in X11's list of layouts (`/usr/share/X11/xkb/rules/evdev.lst`).
* GNOME keeps the time between repeats rather than how many a second, so
  a rate comes back rounded: 40 a second is 25 ms apart, and back as 40.
* The lock keys are read from `/sys/class/leds`, which anyone may read;
  turning them on or off is the keyboard's own business.
