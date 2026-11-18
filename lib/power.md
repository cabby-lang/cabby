# power — the battery

```cabby
use power
```

How the battery is doing, the same way on Linux and on a Mac: for a bar,
or a program that should save its work before the battery runs out.

| | Linux | macOS |
|---|---|---|
| read from | `/sys/class/power_supply` (no UPower needed) | `pmset -g batt` |

<!-- check -->
```cabby
use power

int main {
	if !power.has_battery {
		println "no battery: plugged in"
	} else if power.charging {
		println power.battery_percent, "% and charging"
	} else if power.battery_percent < 15 && !power.plugged_in {
		println "low:", power.battery_percent, "% left"
	} else {
		println power.battery_percent, "%"
	}
	return 0
}
```

| Call | What it says |
|---|---|
| `power.has_battery` | `true` for a laptop; `false` for a desktop (or a Mac mini) |
| `power.battery_percent` | how full it is, 0 to 100 (-1 with no battery) |
| `power.charging` | `true` while it's being charged |
| `power.plugged_in` | `true` on mains power: charging, full, or with no battery at all |
| `power.battery_full` | `true` when it's as full as it gets |

Each call looks again, so the answers are as they are now: a bar can ask
once a minute (with a timer, from `events`).

On Linux, a computer with more than one battery (some ThinkPads) gives
their average. Batteries in a mouse or a keyboard aren't counted.
