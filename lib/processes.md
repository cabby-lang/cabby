# processes — programs your program starts and talks to

```cabby
use processes
```

A `process` is another program, started by yours: you can send it lines,
read the lines it prints, wait for it, or stop it. Like everything in
Cabby, it belongs to its variable: when that is dropped, your program waits
for the process to end, so none is ever left running by mistake.

For a single command whose output you want in one go, [shell](shell.md)'s
`shell.run` is simpler. A process is for programs that keep going: ones
you talk to, or whose lines you want as they come.

| Call | What it does |
|---|---|
| `P.start Command` | Starts `Command`, run by `/bin/sh` as a line in a terminal is, so pipes (`\|`) and the rest work. A panic if `P` is still running something. |
| `P.send Text` | Sends `Text` and a line end to its input. |
| `P.write Text` | Sends `Text` as it is, with no line end added. |
| `P.end_input` | Tells it nothing more will be sent. Programs such as `sort` wait for this before they finish. |
| `P.read_line` | The next line it printed, without its line end, waiting for one if needed. At the end it gives `""`. |
| `P.at_end` | `true` when it has ended and every line it printed has been read (it waits until it knows). |
| `P.line` | The line just read, or just handed to the line handler. |
| `P.wait` | Waits for it to end, and gives its exit status, an `int` (0: it worked). Its input is ended first. |
| `P.is_running` | `true` while it is running. |
| `P.status` | Its exit status once it has ended, or -1 before then. |
| `P.stop` | Ends it now: it is asked to stop, and made to after two seconds. |
| `P.detach` | Before `start`: makes it a daemon (see below). |
| `P.on_line h` | Calls `h` for each line it prints. |
| `P.on_end h` | Calls `h` when it ends. |
| `P.keep_errors_apart` | Before `start`: what it prints to stderr comes to your program as lines of their own (see below). |
| `P.read_error_line`, `P.errors_at_end`, `P.error_line`, `P.on_error_line h` | Its stderr's lines, as `read_line`, `at_end`, `line` and `on_line` are its stdout's. |

What it prints to stderr goes where your program's stderr goes, unless
`keep_errors_apart` was asked for. Something
that goes wrong (sending to a process whose input was ended, starting one
that's still running) is a panic that says what.

## Talking to a process

<!-- run -->
```cabby
use processes

int main {
	process Sorter
	Sorter.start "sort"
	Sorter.send "pear"
	Sorter.send "apple"
	Sorter.send "fig"
	Sorter.end_input               // sort waits for the end of its input
	until Sorter.at_end {
		println Sorter.read_line
	}
	println "exit status:", Sorter.wait
}
```

```text
apple
fig
pear
exit status: 0
```

## Lines as they come: handlers

A process field in one of your objects can be given two of the object's
methods, as a button is given one (the manual's chapter 26 explains
handlers). They are called while your program
waits: in `wait`, or between events in a window.

<!-- run -->
```cabby
use processes

object counter {
	process Count
	int Lines

	void go {
		Count.on_line got_line
		Count.on_end finished
		Count.start "printf 'one\ntwo\nthree\n'"
		Count.wait
	}

	void got_line {
		Lines++
		println Lines, Count.line
	}

	void finished {
		println "it ended with status", Count.status
	}
}

int main {
	counter C
	C.go
}
```

```text
1 one
2 two
3 three
it ended with status 0
```

The process has to be part of the object whose methods it is given (a
field), so it can never call an object that's gone; giving them to a
process in a local variable is an error, as for any handler.

## Its errors, apart

`keep_errors_apart` before `start` gives what it prints to stderr (its
warnings and errors) as lines of their own, so they can be told from its
output:

<!-- run -->
```cabby
use processes

int main {
	process Lister
	Lister.keep_errors_apart
	Lister.start "ls -d / /no/such/folder"
	until Lister.at_end {
		println "output:", Lister.read_line
	}
	until Lister.errors_at_end {
		println "error:", Lister.read_error_line.contains("no/such")
	}
	println "it failed:", Lister.wait != 0
}
```

```text
output: /
error: true
it failed: true
```

Both are read whichever is asked for, so a program that prints a great
deal to one while yours reads the other never gets stuck. `on_error_line`
is the handler, as `on_line` is.

## In a window

In a gui or tui window, a process's handlers are called as its lines
arrive, between clicks and key presses, so a window can show a program's
output while it runs:

<!-- tui: type 1 printf 'b\na\nc\n' | sort; click Run; processes; screen -->
```cabby
use tui
use processes

object runner : tui.window {
	tui.entry Command
	tui.list Output
	tui.label State
	process Job

	void build {
		set_title "Runner"
		set_size 400, 160
		tui.row Line = add_row
		Command = Line.add_entry
		tui.button Run = Line.add_button "Run"
		Run.on_click run_command
		Output = add_list
		State = add_label "ready"
		Job.on_line got_line
		Job.on_end finished
	}

	void run_command {
		Output.clear
		State.set_text "running"
		Job.start Command.text
	}

	void got_line {
		Output.add_item Job.line
	}

	void finished {
		State.set_text "finished: " + Job.status.text
	}
}

int main {
	runner R
	R.run
}
```

```text
┌──────────────────── Runner ────────────────────┐
│ printf 'b\na\nc\n' | sort              [ Run ] │
│  a                                             │
│  b                                             │
│  c                                             │
│                                                │
│                                                │
│                                                │
│ finished: 0                                    │
└────────────────────────────────────────────────┘
```

When a script drives a window (`CABBY_GUI_SCRIPT` or `CABBY_TUI_SCRIPT`),
the command `processes` waits for the processes it's watching to finish,
handing on what they print.

## When a process is dropped

A process belongs to its variable. When that is dropped, its input is
ended and your program waits for it to finish, reading and throwing away
anything more it prints. To end it sooner, call `stop` first.

## Daemons: detach

Most processes belong to your program: it waits for them, and they end
with it. A *daemon* is the opposite: a program meant to keep running on
its own after yours has ended, such as a server, a file watcher or a
music player. Say so *before* starting it:

<!-- check -->
```cabby
use processes

int main {
	process Server
	Server.detach
	Server.start "my-server --port 8080"
	println "started; this program can end now"
}
```

`detach` has to come before `start` because a daemon has to be set up as
it starts; once a process is running, there's no way to cut its ties.
(Detaching a process that has already started is a panic saying so.)

### What Cabby does: the double fork

Starting a detached process uses the method daemons have always used on
Unix, called the *double fork*:

1. **Your program makes a copy of itself** (a fork): the first child. Your
   program waits only for this first child, which ends almost at once, so
   nothing is left half-finished behind it.
2. **The first child starts a new session** (`setsid`). A session is the
   group of programs that belong to one terminal. In a session of its own,
   the daemon no longer belongs to your terminal: closing the terminal
   doesn't end it, and Ctrl-C there doesn't reach it.
3. **The first child forks again**, making the second child, and then ends.
   The second child, which becomes the daemon, isn't the leader of its new
   session, and a program that isn't a session leader can never be given a
   terminal by accident, even if it opens one later. With its parent gone,
   the system's first process (`init`, or systemd) adopts it, and tidies up
   after it when it ends.
4. **The second child connects its input, output and errors to
   `/dev/null`**, so it can't block waiting for a terminal that isn't there,
   or fail writing to one. Then it runs the command with `/bin/sh`.

That's why a detached process can't be talked to, waited for or stopped
through its `process` variable: by design, nothing connects the two any
more. Dropping the variable does nothing.

### What Cabby leaves to you

* **Its directory.** The daemon starts in your program's current
  directory, so relative paths in its command work. Some daemons move to
  `/` so they don't keep a disk busy; put `cd / &&` at the start of the
  command for that.
* **Its output.** Everything it prints is thrown away. To keep it, send it
  to a file in the command: `"my-server >> server.log 2>&1"`.
* **Stopping it later.** Nothing knows its process number, so have the
  command write it down, and stop it with the shell library:

  <!-- check -->
  ```cabby
  use processes
  use shell

  int main {
  	process Server
  	Server.detach
  	Server.start "echo $$ > server.pid; exec my-server"   // $$: this shell's number, kept by exec
  	// ... later, perhaps from another program:
  	shell.status "kill $(cat server.pid)"
  }
  ```

* **Running it as a service.** For a daemon that should start with the
  computer, restart if it fails and keep a log, use the system's service
  manager (a systemd unit file) rather than `detach`; it does all of this
  itself.
