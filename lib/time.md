# time — moments, and how long things take

```cabby
use time
```

Time as the computer keeps it: an exact **moment**, and a **duration**, a
length of time. They are the same everywhere in the world, with no
calendar or time zone; for days, months, clock times and time zones,
[dates](dates.md) builds on this library (and `use dates` brings it in).

| Write | What it means |
|---|---|
| `moment M` | one exact moment, to the nanosecond |
| `time.now` | the moment now |
| `time.since M` | how long ago `M` was, as a duration |
| `time.sleep D` | waits for the duration `D` |
| `duration D` | a length of time (it can be negative) |
| `time.nanoseconds N`, `time.milliseconds N`, `time.seconds N`, `time.minutes N`, `time.hours N`, `time.days N` | a duration of that many (a fraction is fine: `time.seconds 1.5`) |
| `D.milliseconds`, `D.seconds`, `D.minutes`, `D.hours`, `D.days` | the duration in that unit, as a `double` |
| `D.nanoseconds`, `D.whole_milliseconds`, `D.whole_seconds`, `D.whole_minutes`, `D.whole_hours`, `D.whole_days` | whole ones, as a `long`, for counting |
| `D.text` | "1.25 seconds", "2 minutes 5 seconds", "3 hours" (also what `println D` shows) |

## Durations and moments are kinds of their own

A duration isn't a plain number, so it can't be mixed up with one:
`time.sleep 5` is an error (five what?), and so is adding seconds to a
count of something else. It is a *strict* kind: other kinds take numbers
written in the program as their own (`meters M = 15`), but a duration
doesn't, since its name doesn't say the unit. What they can do:

| Write | Gives |
|---|---|
| `D1 + D2`, `D1 - D2`, `-D` | a duration |
| `D * 3`, `D / 2` (by a plain number) | a duration |
| `D1 / D2` | how many times one goes into the other, a `double` |
| `M + D`, `M - D` | a moment that much later or earlier |
| `M2 - M1` | the duration between two moments |
| `==`, `<`, `>=`, `is`... | comparing two durations, or two moments |

They are the manual's *kinds* (chapter 20): a duration is kept as a whole
number of nanoseconds, a moment as nanoseconds since the start of 1970,
UTC.

<!-- run -->
```cabby
use time

int main {
	duration Break = time.minutes(5) + time.seconds(30)
	println Break, "=", Break.seconds, "seconds"
	println Break * 2, Break / time.seconds(30)
	moment Start = time.now
	time.sleep time.milliseconds 20
	duration Took = time.since Start
	println Took >= time.milliseconds(20)
}
```

```text
5 minutes 30 seconds = 330.0 seconds
11 minutes 11.0
true
```

A call without parentheses takes everything after it, so when a call is
part of a sum, give it its parentheses: `time.minutes(5) +
time.seconds(30)`, not `time.minutes 5 + time.seconds 30` (that is
`time.minutes(5 + time.seconds 30)`).

## Measuring how long something takes

```cabby
moment Start = time.now
do_the_work
println "took", time.since(Start)
```

`time.now` is the computer's clock, which can be changed while a program
runs (by the system keeping it right); for measuring how long something
takes, that only matters for long runs.

## Timers

[events](events.md)' timers, and a window's own `every` and `after`, take
durations: `Heat.every time.seconds(1)`. `use events`, `use gui` and `use
tui` bring `time` with them.
