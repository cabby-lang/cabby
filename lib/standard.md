# standard — what Cabby borrows from C's standard library

```cabby
use standard
```

Seven functions from the C library, as C has them: formatted printing,
the size of a whole number, random numbers, leaving the program early, and
running a command. They keep their C names, so examples written for C read
the same. Cabby has its own way to do each of them now (below), so a
program only needs this header to follow C.

| Declaration | What it does |
|---|---|
| `extern int printf str format, ...` | Prints `format`, with each `%` code replaced by the next argument. Returns the number of bytes printed. |
| `extern int abs int x` | `x` without its sign: `abs -7` is 7. |
| `extern long labs long x` | The same for a `long`. |
| `extern int rand void` | A pseudo-random number from 0 to at least 32767 (2147483647 with glibc). |
| `extern void srand unsigned int seed` | Starts `rand` from a seed; the same seed gives the same numbers. |
| `extern void exit int status` | Ends the program at once with this exit status. |
| `extern int system str command` | Runs a shell command and waits for it. |

**What Cabby does itself.** C's other everyday functions aren't here,
because Cabby has its own, with no header needed:

| Instead of C's | Cabby has |
|---|---|
| `puts s`, `putchar c` | `println s`, `print c` |
| `getchar` | `read_char` (a line's first character, nothing left behind), `read_key` (one key, no Enter), `read_line` |
| `atoi`, `atol`, `atof` | `s.to_int`, `s.to_long`, `s.to_double`, with `s.is_int`, `s.is_long` and `s.is_number` to check first: text that isn't a number is a panic, not a silent 0 |
| `strlen`, `strcmp` | `s.length`, `<` and `>` (or `use strings` for C's) |
| `printf "%.2f", Price` | `println Price.text(2)`: a number with that many decimal places |
| `abs`, `labs` | `abs`, for every kind of number (a `double` too), giving the same kind |
| `rand`, `srand` | `random 1 to 6` (a whole number from 1 to 6, each as likely, or `random 0.0 to 1.0` for a double) and `random_seed 42` |
| `exit` | `exit`: the same, and it writes out what open files hold first |
| `system` | `shell.status` and `shell.run` (`use shell`) |

With this header, `abs` and `exit` are C's (its declarations come first);
`random` and `random_seed` are Cabby's either way.

## printf

The `%` codes say how to show each argument, in order:

| Code | For | Example | Shows |
|---|---|---|---|
| `%d` | `int` (also `char`, `short`) | `printf "%d", 42` | `42` |
| `%ld` | `long` | `printf "%ld", Big` | `5000000000` |
| `%u`, `%lu` | `unsigned int`, `unsigned long` | | |
| `%f` | `double` or `float` | `printf "%f", 2.5` | `2.500000` |
| `%.2f` | the same, with 2 decimals | `printf "%.2f", 2.5` | `2.50` |
| `%e`, `%g` | `double`, scientific or shortest | | |
| `%x`, `%o` | an integer in hexadecimal, octal | `printf "%x", 255` | `ff` |
| `%c` | a `char` | `printf "%c", 'A'` | `A` |
| `%s` | a `str` | `printf "%s", Name` | the text |
| `%%` | a percent sign | | `%` |

A number between `%` and the letter sets a width: `%5d` pads to 5 places on
the left, `%-5s` on the right, `%05d` with zeros.

<!-- run -->
```cabby
use standard

int main {
	printf "%d apples at $%.2f each\n", 3, 0.45
	printf "[%5s] [%-5s] [%05d]\n", "ab", "cd", 42
	str Name = "Ada"
	printf "%s has %ld bytes\n", Name, Name.length
}
```

```text
3 apples at $0.45 each
[   ab] [cd   ] [00042]
Ada has 3 bytes
```

**Be careful:** Cabby checks that each argument is a value C can take, but
not that it matches its `%` code. As in C, `printf "%d", 2.5` prints
nonsense, and `%s` with a number can crash the program. `println` has no
such trap.

A `str` is passed to C as ordinary text and stays yours afterwards: it is
lent, not moved.

## Mixing printf with println

`printf` and Cabby's `print`/`println` write to the same place, so their
output comes out in the order the program printed it.

`stderr some_call` (manual chapter 31) sends `printf`'s printing to
stderr too, as it does Cabby's own: Cabby's `printf` is C's, printing
wherever Cabby's printing goes now.

## Whole numbers without their sign: abs and labs

<!-- run -->
```cabby
use standard

int main {
	println abs(-7), abs(7), labs(-5000000000)
}
```

```text
7 7 5000000000
```

## Random numbers: rand and srand

`rand` gives the next number of a long, fixed sequence that looks random.
`srand Seed` says where in that sequence to start. The same seed always
gives the same numbers, in the same order:

<!-- run -->
```cabby
use standard

int main {
	srand 42
	int First = rand
	int Second = rand
	srand 42                        // start again from the same seed...
	println First == rand, Second == rand   // ...and the same numbers come out
}
```

```text
true true
```

Without `srand`, a program starts as if it had called `srand 1`, so it
gives the same numbers every run. That's handy while testing: a fixed seed
makes a bug happen the same way each time.

### Different numbers each run

For a game, seed from something that changes, such as the clock. C's `time`
gives the number of seconds since 1970; Cabby has no library for it yet,
so declare it yourself. It takes a place to store the time as well, which
can be 0 to mean "nowhere":

<!-- check -->
```cabby
use standard

extern long time long Unused

int main {
	srand (unsigned int)time 0      // seed once, at the start
	println "you rolled", rand % 6 + 1
}
```

Seed once, at the start of the program, not before each `rand`: two seeds
in the same second are the same seed, and give the same number.

### A number in a range

`rand` goes from 0 to a large number (`RAND_MAX` in C, at least 32767, and
2147483647 with glibc). To get a number from 1 to 6, take the remainder
and add 1:

<!-- run -->
```cabby
use standard

int main {
	srand 42
	bool Ok = true
	for i in 1000 {
		int Roll = rand % 6 + 1        // 1 to 6
		if Roll < 1 || Roll > 6 {
			Ok = false
		}
	}
	println "all between 1 and 6:", Ok
}
```

```text
all between 1 and 6: true
```

The exact numbers for a seed differ between C libraries (glibc on Linux,
macOS's own), so don't rely on them being the same everywhere. `rand` isn't
suitable for anything to do with security, such as passwords.

## Leaving early: exit

`exit Status` ends the program immediately with that exit status. Output
already printed is still written out, but **destructors don't run** and
nothing is dropped, as with a panic. Returning from `main` is the usual way
to finish; keep `exit` for leaving from deep inside a program.

## Running a command: system

`system "ls -l"` runs a shell command, waits for it to finish, and returns
C's *wait status*. On Linux that is the command's exit status times 256:
`system "exit 4"` returns 1024, and 0 means success. The command's output
goes straight to the screen. Cabby's own [shell](shell.md) library is
usually better: `shell.status` gives the exit status itself, and
`shell.run` gives what the command printed, as a `str`.

## See also

The manual's chapter 27, "Calling C", and chapter 31 for exit statuses.
