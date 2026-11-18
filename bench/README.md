# Cabby benchmarks

How fast Cabby is, and how much memory it uses, compared with Python
(for the interpreter) and with C and Rust (for compiled programs).

## Running them

```sh
make                          # build cabby first
python3 bench/run.py          # everything, best of 5 runs (about 10 minutes)
python3 bench/run.py --quick  # one run each, for a quick look
python3 bench/run.py --tier interp       # only Cabby's interpreter against Python
python3 bench/run.py --tier compiled     # only compiled Cabby against C and Rust
python3 bench/run.py fib sort            # only some programs
```

It needs Python 3, gcc (or set `CC=clang`) and clang (for `cabby -c`).
Rust is optional: without `rustc` it is left out (`sudo pacman -S rust`
on Arch). The results are printed; a full run (no `--quick`, `--tier` or
program names) also writes them to `bench/results.md`, with a note of the
computer and compiler versions. Results differ between
computers, so compare runs from the same machine.

## The programs

Each is in `programs/`, once per language, with the same algorithm written
plainly (no language's special tricks). Every language's output is checked
to be identical before anything is timed.

| Program | Measures |
|---|---|
| `hello` | start-up time, and the memory a program needs just to exist |
| `fib` | function calls: recursive Fibonacci |
| `sieve` | loops and array indexing: counting primes |
| `mandelbrot` | floating-point arithmetic |
| `sort` | quicksort: recursion, and arrays passed to functions |
| `strings` | building a long string a piece at a time, then reading it |
| `objects` | making and dropping objects, and virtual method calls |
| `text` | the string tools on every line: trim, find, slices, contains, replace, upper, ordering |
| `lists` | adding to a list, going through it, searching it, and splitting and joining text |
| `lookups` | counting by whole-number key, going through every entry, removing half, then counting words by string key (Python dict, Rust HashMap, a plain C hash table) |

**Time** is wall-clock time, the best of several runs. **Memory** is the
peak: the most RAM the program held at any moment, including its runtime
(Python's interpreter, or Cabby's). A small launcher, `memtime.c`, starts
each program so the measuring script's own memory isn't counted.

## Findings (first run, 2026-10-30)

Measured on a 2-core Intel Xeon (2.1 GHz) virtual machine; the exact
figures are in `results.md`. Times are relative: "3×" means three times as
long.

### A bug found and fixed on the way

Building a string with `s += ...` copied the whole string on every append,
so the time grew with the *square* of the string's length: the `strings`
program ran for over 8 minutes before being stopped. Strings now keep
spare room and grow by doubling, as Python, C++ and Rust do. The same
program takes 0.8 s compiled, and appending is fixed in the interpreter
too. A test in Cabby's suite covers it.

### Compiled Cabby against C and Rust

| Program | Time vs C | Time vs Rust | Memory vs C | Notes |
|---|---|---|---|---|
| hello | same | faster | same | 1 ms to start |
| fib | 1.9× | 0.9× | same | gcc has a special trick for this recursion; against C compiled with clang, Cabby is level |
| sieve | 1.08× | 0.84× | same | |
| mandelbrot | 1.04× | 1.06× | same | |
| sort | 1.26× | 1.07× | same | the division checks (below) |
| strings | 1.9× | 4.3× | same | `.text` makes a temporary string (below) |
| objects | 1.37× | 1.03× | same | division checks and field look-ups (below) |

**Memory is effectively equal to C everywhere**, within a fraction of a
megabyte: ownership means no garbage collector and nothing extra kept
around. **Speed is at C's and Rust's level for loops, arrays and floating
point, and level with Rust for objects.** Where Cabby is slower, there are
specific, fixable causes. (Run to run, times on this virtual machine moved
by up to about 20%, so treat differences under 1.2× as a tie.)

### The interpreter against Python

| Program | Time vs Python | Memory vs Python |
|---|---|---|
| hello | **8× faster** to start (2 ms) | **4× less** (2.5 MB) |
| fib | 3.5× | 4.7× less |
| sieve | 4.9× | 1.3× more |
| mandelbrot | 2.5× | 4.5× less |
| sort | 2.0× | 3.2× less |
| strings | 4.1× | 2.8× less |
| objects | 1.9× | 4.7× less |

The interpreter starts eight times faster than Python and uses about a
quarter of the memory, but runs code two to five times slower.

### Compiled Cabby against Python

The same programs at the compiled sizes, Python run once:

| Program | Python | Compiled Cabby | Cabby is |
|---|---|---|---|
| fib | 3.8 s | 0.11 s | 33× faster |
| sieve | 8.0 s, 401 MB | 0.31 s, 50 MB | 26× faster, 8× less memory |
| mandelbrot | 11.2 s | 0.22 s | 52× faster |
| sort | 35.6 s, 402 MB | 1.0 s, 40 MB | 36× faster, 10× less memory |
| strings | 3.2 s | 0.80 s | 4× faster |
| objects | 9.4 s | 0.57 s | 16× faster |

So for anything that runs for long, `cabby -c` is the answer.

## Second round (2026-10-31)

After the string tools were added, a new run, with `text` added, and these
fixes:

| Change | Effect |
|---|---|
| Objects are allocated with `malloc` and zeroed in the generated code, not with `calloc` (glibc's `calloc` skips its fast cache) | compiled `objects`: 1.8× C → 1.07× C |
| `s[i]` checked and read inline; whole numbers turned into text without `printf` | compiled `strings`: 1.8× C → level with C |
| `a + b + c` joined in one allocation instead of one per `+` | compiled `text`: 1.5× C → 1.16× C (Rust: 1.14×) |
| Interpreter: locals of a call taken from a reusable stack, temporaries recycled | interpreted `fib` 2.8× → 2.1× Python, `strings` 4.1× → 3.3×, `objects` 2.1× → 1.6× |

`fib` stays at about 1.9× C because the C here is built with gcc, which has
a special trick for this kind of recursion. Built with clang, which is what
Cabby uses underneath, the same C takes as long as Cabby.

## What to speed up first

*(Written after the first run. Done since: compiled objects and strings,
and the interpreter's memory requests per call; see "Second round".)*

In order of payoff for the effort:

**Compiled programs**

1. **Division checks.** Every `/` and `%` calls a runtime function to check
   for division by zero, even for `i % 2`, where it can't happen. Skipping
   the check when dividing by a constant, and making the rest a quick
   inline comparison, should close most of the gap in `sort` and
   `objects`. Small change.
2. **Field positions worked out ahead of time.** To support diamonds, each
   field read looks up where the field sits in this particular object (two
   extra memory reads). Cabby compiles the whole program at once, so it can
   see when a class's fields are at the same place in every object that
   contains it (most classes) and use that place directly. Medium change.
3. **`s += n.text` without a temporary.** `.text` makes a new string that is
   copied into `s` and then freed: two memory requests for every number.
   Writing the digits straight onto the end of `s` removes both. Small.

**The interpreter**

1. **No memory requests per call or per loop turn.** About a quarter of
   the interpreter's time goes on asking for and freeing memory for local
   variables, on every function call and every turn of a loop. Taking it
   from one reusable stack instead is a medium change, worth perhaps 1.3×.
2. **Conversions decided once.** About a tenth of its time goes on
   re-deciding whether a value needs converting, every time a line runs.
   The checker already knows; the answer can be stored. Small to medium.
3. **Compact arrays.** Each array element takes 16 bytes in the
   interpreter, so a `bool` array of 2 million uses 32 MB (compiled: 2 MB).
   Storing plain-data arrays as plain bytes fixes the one place it uses
   more memory than Python. Medium.
4. **A bytecode interpreter.** The interpreter walks the program's tree
   directly. Python first turns the program into a compact list of
   instructions (bytecode) and runs those, which is the main reason it's
   faster. Doing the same is the big step, likely 2 to 5×, enough to match
   or beat Python. Large change; best done after the language settles.

## The bytecode, stage 1 (2026-11-13)

The interpreter now makes each function into bytecode the first time it's
called (`interpreter_bytecode.c`), and hands whatever the bytecode can't do
yet to the tree-walker, on the same cells. Stage 1 does numbers: variables,
arithmetic, comparisons, `if`, loops, calls, and elements of arrays and
lists of numbers. On the same computer (best of 5), the tree-walker alone
(`CABBY_BYTECODE=0`) against the bytecode, and Python:

| Program | tree-walker | bytecode | faster | python |
|---|---|---|---|---|
| fib | 178 ms | 50 ms | 3.6× | 78 ms |
| sieve | 599 ms | 175 ms | 3.4× | 166 ms |
| mandelbrot | 236 ms | 50 ms | 4.7× | 90 ms |
| sort | 727 ms | 508 ms | 1.4× | 396 ms |
| strings | 289 ms | 126 ms | 2.3× | 83 ms |
| objects | 92 ms | 58 ms | 1.6× | 49 ms |
| text | 69 ms | 58 ms | 1.2× | 45 ms |
| lists | 69 ms | 37 ms | 1.9× | 30 ms |
| lookups | 116 ms | 65 ms | 1.8× | 49 ms |

fib and mandelbrot now beat Python, and sieve is level with it. The rest
spend their time on what stage 2 is for: fields, strings, methods, and
calls that borrow (sort's arrays).

## The bytecode, stage 2 (2026-11-13)

Every call of the program's own functions and methods now goes through the
bytecode (arguments that aren't numbers are worked out by the tree-walker,
as it would: moved, or lent), and so do number fields, `s[i]`, `.length`,
and conversions (decided once, when the function's made, not each time).
Best of 5, against Python:

| Program | stage 1 | stage 2 | python |
|---|---|---|---|
| fib | 50 ms | 49 ms | 78 ms |
| sieve | 175 ms | 142 ms | 174 ms |
| mandelbrot | 50 ms | 53 ms | 88 ms |
| sort | 508 ms | 210 ms | 390 ms |
| strings | 126 ms | 76 ms | 81 ms |
| objects | 58 ms | 41 ms | 51 ms |
| text | 58 ms | 57 ms | 45 ms |
| lists | 37 ms | 30 ms | 28 ms |
| lookups | 65 ms | 68 ms | 49 ms |

Seven of the ten beat Python now. What's left (text's string tools,
lookups) is stage 3.

## The bytecode, stage 3, part one (2026-11-13)

Lookups of numbers to numbers (`Counts[K] += 1`, `contains`, going
through one) are bytecode now, and each instruction jumps straight to the
next (computed goto) instead of going back round a switch. Best of 5 (a
slower run of the computer: Python's times are up too):

| Program | cabby | python | |
|---|---|---|---|
| fib | 51 ms | 81 ms | 0.63× |
| sieve | 167 ms | 226 ms | 0.74× |
| mandelbrot | 48 ms | 101 ms | 0.48× |
| sort | 200 ms | 429 ms | 0.47× |
| strings | 71 ms | 86 ms | 0.83× |
| objects | 43 ms | 56 ms | 0.77× |
| text | 56 ms | 48 ms | 1.17× |
| lists | 30 ms | 31 ms | 0.97× |
| lookups | 47 ms | 52 ms | 0.90× |

Nine of the ten beat Python. text, the string tools, is the one left: most
of its time is making, trimming and freeing strings, which part two takes
out of the tree-walker.

## The bytecode, stage 3, part two (2026-11-13): all ten

| Program | cabby | python | |
|---|---|---|---|
| fib | 50 ms | 79 ms | 0.63× |
| sieve | 112 ms | 171 ms | 0.65× |
| mandelbrot | 45 ms | 92 ms | 0.49× |
| sort | 174 ms | 404 ms | 0.43× |
| strings | 58 ms | 83 ms | 0.70× |
| objects | 39 ms | 50 ms | 0.78× |
| text | 35 ms | 45 ms | 0.78× |
| lists | 16 ms | 30 ms | 0.53× |
| lookups | 44 ms | 48 ms | 0.92× |

Best of 5. What did it, for text and lookups: strings joined in one go
(`"item " + i.text + ":"`, one string made, not three); short strings'
blocks kept for the next string instead of going back to malloc (Python's
own allocator does the same); `upper`, `lower` and `replace` made straight
into their string; slices, lookups by a string key, `remove` and `L.add`
in bytecode; and, for everything, loops tested at the bottom (one jump a
turn, not two) and literal numbers carried by the instruction that uses
them. Memory: everything but sieve uses less than Python, sieve because
each array element still takes 16 bytes in the interpreter.

## Arrays of numbers as bytes (2026-11-13): ahead on memory too

| Program | cabby | | python | |
|---|---|---|---|---|
| fib | 58 ms | 2.9 MB | 105 ms | 10.8 MB |
| sieve | 114 ms | 4.7 MB | 183 ms | 26.0 MB |
| mandelbrot | 62 ms | 2.7 MB | 95 ms | 10.8 MB |
| sort | 248 ms | 4.1 MB | 436 ms | 22.0 MB |
| strings | 74 ms | 4.7 MB | 86 ms | 12.6 MB |
| objects | 46 ms | 2.7 MB | 54 ms | 10.8 MB |
| text | 38 ms | 2.9 MB | 49 ms | 10.8 MB |
| lists | 23 ms | 7.5 MB | 38 ms | 19.6 MB |
| lookups | 52 ms | 14.4 MB | 53 ms | 25.1 MB |

Best of 5, on a busier day for this computer than the run above (Python
was slower too). The interpreter keeps an array of numbers (bool, char,
int, long, double...) as C does, each in its own size, rather than as
16-byte Values: sieve's 2 million bools take 2 MB, not 32, and the
program 4.7 MB in all (34 MB before). An array whose element is lent
(`bump A[2]`), or has an address taken, is spread out into Values first,
so that element keeps its place; `CABBY_COMPACT=0` keeps every array
spread out, to compare. Interpreted Cabby is now ahead of Python at every
benchmark, for both time and memory.

## Compiled lookups level with C (2026-11-14)

| Program | cabby -c | c | rust |
|---|---|---|---|
| lookups | 693 ms, 181 MB | 675 ms, 258 MB | 1.67 s, 146 MB |
| sieve | 353 ms | 354 ms | 357 ms |
| sort | 983 ms | 892 ms | 994 ms |
| objects | 484 ms | 463 ms | 581 ms |

Best of 5. Lookups were twice C's time (913 ms against 451 on the first
run). The table itself was fine: the time went on calling the runtime for
every look, which kept the processor from waiting on several at once, as
C's tight loop lets it. Now, for whole-number keys, `cabby -c` writes the
search into the program itself (the hash, one multiply, and the probe of
the index), and calls the runtime only to add a key, or to say one's
missing. The hash is Fibonacci hashing now (as the C program's), and the
runtime remembers where it last found a key, so `if L.contains(K) {
L[K] += 1 }` finds K once, for string keys too.

The interpreter: `switch` is in bytecode (a loop with one in it, 5×
faster than before), and so is making an object whose fields are given
numbers (`square S = {.Size = I % 100}`).
