# volume — the speakers' volume

```cabby
use volume
```

How loud the computer is, and muting it, the same way on Linux and on a
Mac: for a bar, a media program's volume keys, or a program that should
be quiet at night. It's the default speakers' (or headphones') volume:
the one the computer's own volume keys change.

| | Linux | macOS |
|---|---|---|
| set with | PipeWire's `wpctl`, or PulseAudio's `pactl`, or ALSA's `amixer`: the first the computer has | its volume settings, through `osascript` |

<!-- check -->
```cabby
use volume

int main {
	if !volume.can_set {
		println "nothing here can set the volume"
		return 1
	}
	println "volume:", volume.level, "muted:", volume.muted
	volume.set 30
	volume.up 5
	println "now:", volume.level
	return 0
}
```

| Call | What it does |
|---|---|
| `volume.level` | how loud, from 0 to 100 (-1: it couldn't be found out) |
| `volume.muted` | `true` if it's muted |
| `volume.set Level` | sets how loud, from 0 to 100 (less is 0, more is 100) |
| `volume.up Amount` | louder by `Amount` (out of 100) |
| `volume.down Amount` | quieter by `Amount` |
| `volume.mute` | silent, keeping the level |
| `volume.unmute` | back to the level it was |
| `volume.can_set` | `true` if there's something here to read and set it with |

Each setting call gives `true` if it was done, and `false` if it couldn't
be (with none of `wpctl`, `pactl` or `amixer` on Linux, say). Muting
keeps the level: `volume.level` still says it, and unmuting goes back to
it.

On Linux, PipeWire (Debian 12 and later, Arch, Fedora) comes with
`wpctl`; PulseAudio's `pactl` also works on PipeWire, through its
`pipewire-pulse`; `amixer` (from `alsa-utils`) sets ALSA's `Master`
control on a computer with neither.
