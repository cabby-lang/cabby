# os — what every system has

```cabby
use os
```

What every system has, the same way on Linux and on a Mac:

* [Watching files and folders](#watching-files-and-folders) for changes
* [Signals](#signals): Ctrl+C, and a program being asked to stop
* [Disks](#disks), and the space on them
* [Processors and memory](#processors-and-memory): how busy they are
* [Programs running](#programs-running), and stopping them

What only Linux has (temperatures, its disks' partitions, services, the
kernel's modules and settings) is in the [linux](linux.md) library. What a
computer *is* (its system's name and version, its name on the network,
how many processors) is in [system](system.md).

## Watching files and folders

<!-- check -->
```cabby
use os
use events

object watching {
	os.watcher Notes

	void begin {
		Notes.watch_all "notes"
		Notes.on_change changed
		println "watching notes: change something in it (Ctrl+C to stop)"
		events.run
	}

	void changed {
		if Notes.change == "renamed" {
			println Notes.old_path, "is now", Notes.path
		} else {
			println Notes.path, "was", Notes.change
		}
	}
}

int main {
	watching W
	W.begin
}
```

```text
notes/todo.txt was changed
notes/ideas.md was created
notes/draft.md is now notes/final.md
notes/old.txt was deleted
```

| `os.watcher` | |
|---|---|
| `watch Path` | a file, or a folder and what's in it (not what's in the folders inside it) |
| `watch_all Path` | a folder, what's in it, and everything in the folders inside it (new ones too) |
| `on_change Handler` | `Handler` runs for each change, from the event loop (`events.run`, or a window's `run`) |
| `str change` | in it: `"created"`, `"changed"`, `"deleted"` or `"renamed"` |
| `str path` | in it: what it happened to, beginning as the path given to `watch` |
| `str old_path` | in it, for `"renamed"`: what it was called |
| `stop` | stops watching |

A watcher can watch several things: `watch` each. A file changed many
times at once (as a program saves it) is told of once. A folder deleted
with things in it has each told of first, then the folder.

Linux tells a program what changed (inotify). A Mac tells it only that
something in a folder did (kqueue), so the folder is looked at again and
compared with how it was. A rename to another name in the same folder is
told of as `"renamed"`; moved to a folder that isn't watched, it's
`"deleted"`, and moved in from one, `"created"`.

## Signals

A signal is the system telling a program something: Ctrl+C in its
terminal, or that it's been asked to stop (as the computer shuts down,
or `kill` sends it). Most end the program, unless it catches them:

<!-- check -->
```cabby
use os
use events
use time

object worker {
	os.signals Signals
	timer Tick
	int Done

	void begin {
		Signals.on_signal stopping
		Signals.catch "interrupt"
		Signals.catch "terminate"
		Tick.on_tick work
		Tick.every time.seconds(1)
		println "working: Ctrl+C to stop, tidily"
		events.run
	}

	void work {
		Done++
		println "done", Done
	}

	void stopping {
		println "stopping, for", Signals.signal, "after", Done
		Tick.stop
	}
}

int main {
	worker W
	W.begin
	println "saved, and stopped"
}
```

`os.signals` catches them, each told to its handler from the event loop,
rather than in the middle of what the program was doing. It's kept as a
field of the object whose method its handler is, and lets go of its
signals when that's dropped.

| `os.signals` | |
|---|---|
| `catch Name` | caught from now on |
| `on_signal Handler` | `Handler` runs for each that comes |
| `str signal` | in it: the one that came |

| Signal | When it comes |
|---|---|
| `"interrupt"` | Ctrl+C, in its terminal |
| `"terminate"` | it's asked to stop: the computer's shutting down, or `kill` |
| `"hangup"` | its terminal's closed |
| `"quit"` | Ctrl+\\, in its terminal |
| `"user1"`, `"user2"` | another program's choice of meaning |
| `"resize"` | its terminal's changed size |
| `"child"` | a program it started has ended |
| `"alarm"`, `"resume"` | a timer of the system's; it's carrying on after being paused |

For a program working in a loop, with no event loop, `os.catch_signal
Name` catches one, and `os.got_signal Name` says whether it's come since
last asked:

<!-- check -->
```cabby
use os

int main {
	os.catch_signal "interrupt"
	long Count = 0
	while !os.got_signal("interrupt") {
		Count++
	}
	println "counted to", Count
}
```

| | |
|---|---|
| `os.catch_signal Name`, `bool os.got_signal Name` | as above |
| `os.ignore_signal Name` | nothing at all done when it comes |
| `os.usual_signal Name` | as it was to begin with (`"interrupt"` ends the program again) |
| `bool os.send_signal Program, Name` | sends one to a program, by its number: `"terminate"` asks it to stop, `"kill"` stops it whatever it says, `"pause"` and `"resume"` stop it for a while and start it again |
| `long os.pid`, `long os.parent_pid` | this program's number, and the number of the program that started it |

## Disks

<!-- check -->
```cabby
use os

int main {
	for D in os.disks {
		long Gb = 1000000000
		println D.name, "at", D.path + ":", D.free / Gb, "GB free of", D.size / Gb
	}
	println "free here:", os.free_space(".") / 1000000, "MB"
	return 0
}
```

```text
/ at /: 312 GB free of 999
home at /home: 455 GB free of 512
MY STICK at /media/tom/MY STICK: 30 GB free of 30
```

| `os.disk` | |
|---|---|
| `str path` | where it is: `"/"`, `"/home"`, `"/media/tom/USB"`, `"/Volumes/Backup"` |
| `str device` | the device it's on: `"/dev/nvme0n1p2"`, `"/dev/disk3s1"` |
| `str kind` | how its files are kept: `"ext4"`, `"btrfs"`, `"vfat"`, `"apfs"`, `"nfs"` |
| `str name` | its label, or the last part of where it is |
| `long size`, `long free`, `long used` | in bytes; `free` is what this person can use |
| `bool read_only`, `bool removable` | whether it can only be read; whether it can be taken out (a USB stick, an SD card) |

`os.disks` lists the computer's own disks, any plugged in, and any shared
over the network, without the system's own (`/proc`, memory pretending to
be disks, snaps; a Mac's Preboot and VM), and without what this person
can't look into (a share mounted in someone else's home folder, as `df`
leaves it out too). `os.disk_size Path`,
`os.free_space Path` and `os.used_space Path` are the same for the disk a
file or folder is on.

## Processors and memory

<!-- check -->
```cabby
use os

int main {
	long Mb = 1000000
	println "processors:", os.cpu_use, "% busy"
	println "memory:", os.memory_used / Mb, "MB used,", os.memory_available / Mb, "MB available"
	println "load:", os.load
	return 0
}
```

| | |
|---|---|
| `double os.cpu_use` | how busy the processors are, all together, from 0 to 100: since it was last asked (the first time, over a tenth of a second) |
| `long os.memory_total` | the memory, in bytes (`system.memory`) |
| `long os.memory_used`, `long os.memory_available` | in use, and available to programs |
| `long os.swap_total`, `long os.swap_used` | swap: memory kept on the disk when there's too little |
| `list double os.load` | how many programs are waiting for a processor, on average, over the last minute, five and fifteen |

Memory in use is as the system's own monitor says: on a Mac, as Activity
Monitor's "Memory Used" (apps', wired and compressed); on Linux, all but
what's available (`MemAvailable`).

## Programs running

<!-- check -->
```cabby
use os

int main {
	for P in os.programs {
		if P.memory > 500000000 {
			println P.name, "(" + P.pid.text + ")", P.memory / 1000000, "MB"
		}
	}
	for P in os.programs_named "sleep" {
		P.stop
	}
	return 0
}
```

| `os.program` | |
|---|---|
| `long pid`, `long parent_pid` | its number, and the number of the program that started it |
| `str name` | `"firefox"` |
| `str command` | how it was started: `"/usr/bin/firefox --new-window"` (another person's, on a Mac: just its file) |
| `str user` | whose it is: `"tom"`, `"root"` |
| `long memory` | the memory it's using, in bytes |
| `double cpu_seconds` | the processor time it's had |
| `bool stop`, `bool kill`, `bool running` | asks it to stop; stops it whatever it says; whether it's still running |

`os.programs` lists them all (without the kernel's own threads, on Linux),
`os.programs_named Name` those with that name, and `os.running Pid`
whether one is running. Another person's programs can't be stopped
without the administrator. The programs a program starts itself, and
talks to, are the [processes](processes.md) library's.

## How it works

`runtime/os.c`. On Linux it reads the files the kernel keeps
(`/proc/self/mounts`, `/proc/stat`, `/proc/meminfo`, `/proc/<pid>/`) and
watches with inotify; on a Mac it asks the system (`getmntinfo`, Mach's
statistics, `libproc`) and watches with kqueue. Signals are caught by a
handler that writes their number down a pipe, which the event loop
watches.
