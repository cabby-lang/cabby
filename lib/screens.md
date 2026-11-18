# screens — the monitors

```cabby
use screens
```

What each monitor is showing, and changing it: its resolution and
refresh, which way it's turned, how big things are drawn on it, where it
sits beside the others, and whether it's on. For a program that turns a
monitor on its side for reading, sets up a projector, or puts the
monitors back as they were.

Each desktop keeps these settings its own way, so each is asked its own
way:

| Desktop | Asked and changed by | Kept |
|---|---|---|
| Sway | `swaymsg` | until Sway starts again (its config keeps them for good) |
| GNOME | Mutter, over D-Bus, as GNOME's Settings app does | for good |
| KDE Plasma | `kscreen-doctor` | for good |
| another desktop on X11 | `xrandr` | until X starts again |
| macOS | CoreGraphics | for good |

Another Wayland compositor (Hyprland, river) has no way Cabby knows yet:
there, `screens.all` is empty and `screens.can_change` is `false`.
`screens.desktop` says which way it is: `"sway"`, `"gnome"`, `"kde"`,
`"x11"`, `"macos"`, or `""`.

<!-- check -->
```cabby
use screens

int main {
	if !screens.can_change {
		println "no screens here Cabby can change"
		return 1
	}
	for S in screens.all {
		println S.text                       // DP-1: 2560x1440 at 60 Hz, at 0,0
		println "  can show:", S.modes       // [2560x1440@60, 1920x1080@60, ...]
	}
	screens.screen Side = screens.named "HDMI-A-1"
	if Side.found {
		Side.set_rotation 90                 // on its side, for reading
		Side.set_position 2560, 0
	}
	return 0
}
```

## The screens

| Call | What it gives |
|---|---|
| `screens.all` | every monitor plugged in, on or off |
| `screens.named Name` | the one with that name (`found` is `false` if there's none) |
| `screens.main` | the main one, or else the first that's on |
| `screens.count` | how many are plugged in |
| `screens.desktop` | which desktop's way they're asked: `"sway"`, `"gnome"`, `"kde"`, `"x11"`, `"macos"`, or `""` |
| `screens.can_change` | `true` if there are screens here Cabby can change |

## A screen

| Call | What it gives |
|---|---|
| `S.name` | its name, as the system calls it: `"DP-1"`, `"HDMI-A-1"`, `"eDP-1"`; a Mac's are `"built-in"`, `"display 2"` |
| `S.description` | what it is, where the system says: `"Dell Inc. DELL U2720Q"` (`""` if it doesn't) |
| `S.found` | `false` for a `screens.named` that found none |
| `S.on` | `true` if it's showing something |
| `S.main` | the main one: the menu bar's (a Mac), the primary one (GNOME, KDE, X11), or the one in use (Sway) |
| `S.width`, `S.height` | its resolution, in pixels, before it's turned (0 when it's off) |
| `S.refresh` | how many times a second it's drawn: `60.0`, `143.97` |
| `S.scale` | how much bigger things are drawn: `1.0`, `1.5`, `2.0` |
| `S.x`, `S.y` | where its top left corner is on the desktop |
| `S.rotation` | how far it's turned, clockwise: 0, 90, 180 or 270 |
| `S.modes` | the modes it can show: `"2560x1440@60"`, `"1920x1080@59.94"` |
| `S.text` | all that in a line: `"DP-1: 2560x1440 at 60 Hz, scale 1.5, at 0,0"` |

## Changing one

Each gives `true` if it was done, and the screen is then read again, so
its values are the new ones. What a desktop can't do gives `false`.

| Call | What it does |
|---|---|
| `S.set_resolution W, H` | shows `W` by `H` pixels, at the best refresh it has for them |
| `S.set_refresh Hz` | the nearest refresh it has to `Hz`, at the resolution it has now |
| `S.set_mode W, H, Hz` | both at once |
| `S.set_rotation Degrees` | turns it clockwise: 0, 90, 180 or 270 |
| `S.set_scale Times` | draws things `Times` bigger |
| `S.set_position X, Y` | puts its top left corner at `X`, `Y` |
| `S.turn_on` | turns it on, at its preferred mode (on GNOME, to the right of the others) |
| `S.turn_off` | turns it off (not the last one that's on) |

What each desktop can't do:

* **X11** has no scale (`set_scale` gives `false`; `scale` is 1.0).
* **GNOME** allows only the scales a mode lists (1.0, 1.25, 1.5, 2.0 on
  most), and keeps the monitors touching, with none overlapping:
  `set_position` to somewhere that leaves a gap gives `false`. A new
  resolution that the scale doesn't suit gets the mode's own scale.
* **macOS** sets the resolution, refresh and position, but turning a
  screen, scaling it or turning it off isn't possible from outside its
  own settings. Its modes are the ones System Settings lists, by pixels.
* **Sway**'s changes last until Sway starts again; to keep them, put
  them in its config (`output HDMI-A-1 transform 90`).

## Notes

* Resolutions are the monitor's own, before it's turned: a 1920 by 1080
  monitor on its side still has a `width` of 1920.
* A monitor that's plugged in but turned off is in `screens.all`, with
  `on` false; one that isn't plugged in isn't.
* The brightness is `use brightness`'s.
