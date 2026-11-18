# threads — work done on a thread of its own

```cabby
use threads
```

A **worker** does one job on a thread of its own, while the rest of the
program carries on: a long sum, a big file to search, many files to
squeeze. You make one the way you make a window: an object of your own,
made from `threads.worker`, with a `work` method for what it does. You
give it what it needs in its fields, `start` it, and read what it worked
out once it's done.

<!-- check -->
```cabby
use threads
use files

object word_counter : threads.worker {
	str Text                    // what it's given
	long Words                  // and what it works out

	void work {                 // runs on the worker's own thread
		Words = Text.split(" ").length
	}
}

int main {
	word_counter Counter
	Counter.Text = files.read "book.txt"
	Counter.start               // off it goes, and main carries on
	println "counting..."
	Counter.wait                // until it's done
	println Counter.Words
	return 0
}
```

| Call | What it does |
|---|---|
| `W.start` | Runs `work` on a thread of its own, and carries on at once. A worker that's done can be started again; one still working is a panic. |
| `W.wait` | Waits until it's done. (Done already, or never started: carries on at once.) |
| `W.is_done` | `true` once it's done, or before it's started, without waiting. |
| `W.on_done h` | Before `start`: once it's done, calls `h`, a method of yours (see below). |
| `W.on_message h` | Before `start`: calls `h` for each line the worker `tell`s; `W.message` is the line. |
| `W.ask_to_stop` | Asks it to stop: `work` sees `stopping`, and can finish early. |
| `tell Text` | Inside `work`: sends a line to the `on_message` handler. |
| `tell_picture P` | Inside `work`: sends a picture ([images](images.md)), moved as it is: nothing is copied, and the worker's is empty afterwards. |
| `W.has_picture`, `W.picture` | In the `on_message` handler: whether what was sent is a picture, and the picture, taken (it's the main program's now). |
| `stopping` | Inside `work`: `true` once it's been asked to stop. |
| `threads.cores` | How many cores the computer has: how many workers can work at the same moment. |

Dropping a worker waits for it first, so none is ever left working after
its variable has gone.

## Told when it's done

A window's handlers have to be quick: while one runs, the window can't
redraw or react. So a long job goes to a worker, and the handler only
starts it. `on_done` says which of the window's methods to call once the
worker's done, as `on_click` does for a button:

```cabby
object counter_window : gui.window {
	gui.label Answer
	prime_counter Counter           // the worker is the window's

	void build {
		...
		Counter.on_done show_answer
	}

	void start_counting {
		Counter.Below = 200000
		Counter.start               // and straight back to the window
		Answer.set_text "Counting..."
	}

	void show_answer {              // once it's done
		Answer.set_text Counter.Primes.text + " primes"
	}
}
```

The handler is called on the main program's thread, from its loop,
between its other events: a window's (gui, tui), a game's (each frame),
or `events.run`'s, which waits for a worker with a handler as it waits
for a timer. `wait` calls it too, if the loop hasn't yet. It's called
once each time the worker's done: started again, it's called again.

<!-- run -->
```cabby
use threads
use events

object summer : threads.worker {
	long Upto
	long Total

	void work {
		for long I = 0; I < Upto; I++ {
			Total += I
		}
	}
}

object app {
	summer Sum

	void begin {
		Sum.Upto = 1000000
		Sum.on_done finished
		Sum.start
		println "started"
	}

	void finished {
		println "done:", Sum.Total
	}
}

int main {
	app A
	A.begin
	events.run                      // until the worker's done, and told
	return 0
}
```

```
started
done: 499999500000
```

Only the main program can be told: a worker that starts another can
`wait` for it.

## Saying how it's going, and stopping

A worker can `tell` the main program things while it works: how far it's
got, or each result as it's found. The line is copied, so once it's sent
it's the main program's. `on_message` names the method that hears each,
in order, called the same way as `on_done`'s, and before it:

```cabby
object prime_counter : threads.worker {
	...
	void work {
		for int N = 2; N < Below; N++ {
			if N % 1000 is 0 {
				if stopping {           // asked to stop
					return
				}
				tell ((long)N * 100 / Below).text   // "37"
			}
			...
		}
	}
}

// in the window
Counter.on_message show_progress

void show_progress {
	Bar.set_fraction Counter.message.to_int / 100.0
}

void cancel {                           // the Cancel button's
	Counter.ask_to_stop
}
```

`ask_to_stop` doesn't stop it by force (what it's in the middle of would
be left half done): it sets a flag, and `work` looks at `stopping` every
so often and finishes early. Dropping a worker that's still working asks
it to stop too, and then waits for it.

With no `on_message` handler, what's told isn't kept. `wait` hands on
what's been told meanwhile, so a worker that tells a lot never waits for
a program that's waiting for it.

<!-- run -->
```cabby
use threads
use events

object waiter : threads.worker {
	void work {
		tell "ready"
		until stopping {
		}
		tell "asked to stop"
	}
}

object app {
	waiter W

	void begin {
		W.on_message heard
		W.on_done finished
		W.start
	}

	void heard {
		println "heard:", W.message
		if W.message == "ready" {
			W.ask_to_stop
		}
	}

	void finished {
		println "done"
	}
}

int main {
	app A
	A.begin
	events.run
	return 0
}
```

```
heard: ready
heard: asked to stop
done
```

## Many at once

Workers side by side share out a job. Here each sums a quarter:

<!-- run -->
```cabby
use threads

object summer : threads.worker {
	long From
	long Upto
	long Total

	void work {
		for long I = From; I < Upto; I++ {
			Total += I
		}
	}
}

int main {
	summer Parts[4]
	for I in 4 {
		Parts[I].From = I * 1000000
		Parts[I].Upto = (I + 1) * 1000000
		Parts[I].start
	}
	long Total = 0
	for I in 4 {
		Parts[I].wait
		Total += Parts[I].Total
	}
	println Total
	return 0
}
```

```
7999998000000
```

## What keeps it safe

Two threads changing the same thing at once is the classic way threaded
programs go wrong: a number is read by one, changed by the other, and
written back by the first, so the change is lost; or a list grows while
another thread is reading it. Cabby doesn't let it happen.

- **While it works, a worker is its own.** Its fields, and its methods
  (other than `start`, `wait` and `is_done`), are its thread's: using one
  from anywhere else before it's done is a panic, saying to `wait` first.
  Before `start`, and after `wait` (or once `is_done` says so), they're
  yours again.
- **No globals that can change.** Anything `work` reaches (its methods,
  the functions they call, and theirs) can't use a global variable that
  can change: the checker says so, and where. Constants (`const int
  STEP = 2`) are fine. Give the worker what it needs in fields instead.
- **No handlers, and no destructor of its own.** A handler in a worker
  would let `work` call another object's method on the worker's thread;
  a destructor would run while `work` might still be running. (Its fields
  are let go of by themselves, once it's done.)

Moving a worker (into a list, say) is fine, even while it works: an
object is held by a handle, and the worker itself stays where it is.

What a library keeps between calls (a pattern's matches, a database's
rows, a download, a song being written or read) is kept for each thread on
its own, so workers can use them all at once.

**Sound** works from a worker too: a worker can play sounds and notes,
write and read songs, record, and play to a MIDI synthesizer, while the
main program does the same. What they play is mixed together. (A
keyboard's handlers, as any handlers, belong to the main program; a worker
can still play one through an instrument.)

## Compiled, and interpreted

Compiled (`cabby -c`), each worker is a real thread, and they run at the
same moment on as many cores as the computer has: four workers can be
four times as fast.

Interpreted, one thread at a time runs Cabby code, as in Python: a
thread lets the others have a turn while it waits (`wait`, a download, a
database), and every thousand or so steps in between. So the workers
take turns rather than running at once: a job isn't done any sooner, but
the rest of the program carries on meanwhile.

## Not yet

- Windows and games from a worker: use them from the main program, and
  have the worker `tell` it what to show, or `tell_picture` what it drew
  (tutorial 66).
