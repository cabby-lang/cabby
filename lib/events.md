# events — timers, and waiting for things to happen

```cabby
use events
```

Handlers are called when something happens: a timer going off, a process
printing a line, a click in a window. `events.run` is where a program waits
for those moments. It calls handlers as things happen, and returns when
nothing is left to wait for.

| Call | What it does |
|---|---|
| `timer T` | a timer (usually a field of one of your objects) |
| `T.on_tick h` | calls `h`, one of your object's methods, each time it goes off |
| `T.every D` | goes off every so often, until stopped: `T.every time.seconds(1)` |
| `T.after D` | goes off once, after a while: `T.after time.milliseconds(250)` |
| `T.stop` | stops it |
| `T.is_running` | `true` while it's waiting to go off |
| `events.run` | waits, calling handlers, until no timer is running and no process, connection or server with handlers is still going |
| `events.stop` | in a handler: ends `events.run` now |
| `events.step` | waits for the next thing to happen, and calls its handlers: for a loop of the program's own |
| `events.waiting` | `true` while there's something to wait for |

`D` is a duration from [time](time.md) (`use events` brings `time` in, so
`time.seconds` and the rest are there). Starting a timer that's already
running starts it again from now. A
dropped timer stops. A timer's handler follows the handler rules (the
manual's chapter 26): it's one of the methods of the object the timer is
part of.

## An example

<!-- run -->
```cabby
use events

object kettle {
	timer Heat
	int Degrees

	kettle {
		Degrees = 20
		Heat.on_tick warm_up
		Heat.every time.milliseconds 100
	}

	inner void warm_up {
		Degrees += 20
		println Degrees, "degrees"
		if Degrees >= 100 {
			Heat.stop
			println "boiled!"
		}
	}
}

int main {
	kettle K
	events.run
	println "nothing left to wait for"
}
```

```text
40 degrees
60 degrees
80 degrees
100 degrees
boiled!
nothing left to wait for
```

## One loop for everything

Cabby has one event loop, in its runtime, and everything that waits uses
it:

* **timers** from this library;
* **processes** with `on_line` or `on_end` handlers ([processes](processes.md)):
  their output is watched, and `events.run` waits until they end;
* **connections and servers** ([networks](networks.md)): lines from other
  programs, programs connecting and going;
* **D-Bus** signals and calls ([dbus](dbus.md));
* **windows**: a [gui](gui.md) or [tui](tui.md) window's `run` waits in the
  same loop, so timers and processes carry on while a window is open.

`events.step` is the loop gone round once: it waits for the next thing to
happen and returns once its handlers have been called. A program with
work of its own between events loops itself:

```cabby
use events
use networks

object worker {
	networks.server S
	int Lines

	void begin {
		S.on_line heard
		S.listen_here 5000
		while Lines < 10 {
			events.step                 // until a line (or anything else) comes
			println Lines, "lines so far"
		}
	}

	void heard {
		Lines += 1
	}
}
```

`events.step` returns at once when there's nothing to wait for, so it
never waits for ever by mistake.

So one program can watch a process, keep time and show a window at once.
(A window's own `every` and `after` still work as before.)

## Waiting is always written down

A program never waits by itself: if `main` ends while a timer is running,
the program ends there. The waiting is the `events.run` line, so the code
always shows where a program waits.

## How it works

The loop keeps a list of timers and of files to watch (a process's output
is a pipe; a connection is a socket). `events.run` asks the system to wait (with `poll`) until a
watched file has something to read or the next timer is due, then calls the
handlers whose moment has come, and goes round again. A repeating timer
that falls behind (because a handler took a long time) goes off once and
carries on from now, rather than several times in a row to catch up.
