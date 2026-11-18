# brightness — the screen's brightness

```cabby
use brightness
```

How bright the screen is, and setting it, the same way on Linux and on a
Mac: for a bar, a program that dims the screen at night, or one that
brightens it for reading.

| | Linux | macOS |
|---|---|---|
| a laptop's screen | `/sys/class/backlight`: written there if the program may, else by `brightnessctl`, else by logind (which lets whoever's sitting at the computer set it) | its own screen, as the brightness keys set it |
| a desktop's monitor | `ddcutil`, which sets it down the monitor's cable (DDC/CI) | not yet (a Mac mini's monitor isn't the Mac's to set) |

<!-- check -->
```cabby
use brightness

int main {
	if !brightness.can_set {
		println "no screen here whose brightness can be set"
		return 1
	}
	println "brightness:", brightness.level
	brightness.set 60
	brightness.down 10
	println "now:", brightness.level
	return 0
}
```

| Call | What it does |
|---|---|
| `brightness.level` | how bright, from 0 to 100 (-1: it couldn't be found out) |
| `brightness.set Level` | sets how bright, from 0 to 100 (less is 0, more is 100) |
| `brightness.up Amount` | brighter by `Amount` (out of 100) |
| `brightness.down Amount` | darker by `Amount` |
| `brightness.can_set` | `true` if there's a screen here whose brightness can be read |

Each setting call gives `true` if it was done, and `false` if it
couldn't be. A level of 1 is as dim as the screen goes without going
dark; only 0 turns a backlight off.

On Linux, most desktops (GNOME, KDE) let the person at the computer set
it through logind, so nothing more is needed. Otherwise `brightnessctl`
(a package of its own on Debian and Arch) can, once its user is in the
`video` group. For a desktop's monitor, `ddcutil` (also a package) needs
the `i2c-dev` module (`sudo modprobe i2c-dev`) and, to run without
`sudo`, the `i2c` group; it takes a second or so, as it speaks to the
monitor itself.
