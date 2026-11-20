# system — what computer a program is running on

```cabby
use system
```

The system's name and version, the computer's name, and how much it has
to work with: processors and memory. The same on Linux and on a Mac.

<!-- check -->
```cabby
use system

int main {
	println "running on", system.name, "(" + system.machine + "),", "called", system.hostname
	println system.processors, "processors"
	println "on for", system.uptime / 3600, "hours"
	return 0
}
```

| Call | What it says | Linux | macOS |
|---|---|---|---|
| `system.kind` | `"linux"` or `"macos"` | | |
| `system.name` | the system's name and version | `"Debian GNU/Linux 13 (trixie)"`, `"Arch Linux"` | `"macOS 15.1"` |
| `system.version` | just the version | `"13"` (`""` for Arch, which has none) | `"15.1"` |
| `system.hostname` | the computer's name on the network | `"debian-box"` | `"mini"` (not `"mini.local"`) |
| `system.kernel` | the kernel's version | `"6.12.48-amd64"` | Darwin's: `"24.1.0"` |
| `system.machine` | the kind of processor | `"x86_64"`, `"aarch64"` | `"arm64"` (an Apple processor) |
| `system.processors` | how many processors (cores): how many workers can run at once | | |
| `system.uptime` | seconds since it was turned on (a `long`) | | |

How much memory it has, and how much is in use, is `os.memory_total` and
the rest, in `use os`.

## Naming the computer

`system.set_hostname Name` changes the computer's name on the network,
giving `true` if it was changed. It needs the administrator (`sudo`),
as changing it in the system's settings does:

| Linux | macOS |
|---|---|
| `hostnamectl set-hostname` (systemd's) | `scutil`, for its three names: `HostName`, `LocalHostName` (Bonjour's: `"mini"` in `mini.local`) and `ComputerName` (the one Finder and AirDrop show) |

A name for the network is best kept to letters, digits and `-`
(`"music-room"`): no spaces.

On Linux the name comes from `/etc/os-release`; on a Mac, from
`sw_vers`.

For the rare thing that has to be done differently on each,
`system.kind == "macos"` says which (`desktop.on_a_mac` is the same). Most
things don't: the `desktop`, `power`, `volume`, `brightness`, `printing`,
`speech`, `media`, `users` and `mime` libraries do each system's own way themselves.
