# addresses — where things are in memory

```cabby
use addresses
```

An address says where a value is in memory. Most Cabby programs never need
one: borrows (`int &x`) let a function change your variable, and lists,
lookups and objects build any shape of data, all checked before the program
runs. Addresses are for the layer underneath: C functions that take or give
pointers, and memory at fixed places, such as a microcontroller's device
registers.

Cabby can't check what an address points at before the program runs, so
addresses need this `use` line: a file without it can't touch one, and a
reader can see which files need care.

| Write | What it means |
|---|---|
| `address int A` | an address of an `int` (C's `int *`) |
| `address A` | an address of nothing in particular (C's `void *`) |
| `addresses.of X` | where `X` is: a variable, field or element. For an array, its first element (as in C). |
| `addresses.of_text S` | the characters of the str `S`, then a `'\0'` (C's `char *`) |
| `addresses.of_items L` | the first item of the list `L` (of plain data), with the rest after it |
| `addresses.at 0x0200` | the address `0x0200`; the variable says what's there |
| `addresses.none` | no address (C's `NULL`) |
| `addresses.allocate N` | room for `N` values, all 0, until freed (C's `calloc`) |
| `addresses.reallocate A, N` | the same memory made room for `N`; it may move (C's `realloc`) |
| `addresses.free A` | gives memory from `allocate` back (C's `free`) |
| `A.value` | what's at the address, to read or change |
| `A[i]` | what's `i` values along: `(A + i).value` |
| `A + n`, `A - n` | `n` values along (not `n` bytes) |
| `B - A` | how many values apart two addresses are, as a `long` |
| `A == B`, `A < B` | comparing (and `!=`, `<=` and the rest) |
| `A.is_none` | `true` for `addresses.none` |
| `A.number` | the address as a `long`, to print or work out |
| `(address char)A` | the same memory, read as another kind |

An address can point at plain data only: numbers, `bool`, `char`, other
addresses, and structs and arrays of those, which is also all C
understands. Any address goes where an `address` of nothing in particular is
wanted; going the other way takes a cast.

Reading and writing through an address always really happens (as C's
`volatile`), since the memory may be a device's register that changes by
itself.

## Changing a variable through its address

<!-- run -->
```cabby
use addresses

void double_it address int A {
	A.value = A.value * 2
}

int main {
	int Count = 5
	address int A = addresses.of Count
	A.value = 6
	double_it A
	println Count
}
```

```text
12
```

## Going along an array

<!-- run -->
```cabby
use addresses

int main {
	int Scores[4] = {10, 20, 30, 40}
	address int First = addresses.of Scores
	address int Last = First + 3
	int Total = 0
	for address int P = First; P <= Last; P = P + 1 {
		Total += P.value
	}
	println Total, Last - First, First[2]
}
```

```text
100 3 30
```

## C functions

A C function that takes or gives a pointer is declared with addresses:

<!-- run -->
```cabby
use addresses

extern address char strchr address char Text, int Letter

int main {
	char Word[6] = {'h', 'e', 'l', 'l', 'o', '\0'}
	address char Start = addresses.of Word
	address char L = strchr Start, 'l'
	println "found at", L - Start
	L.value = 'L'
	println Word[2], Word[3]
}
```

```text
found at 2
L l
```

## Memory by hand

`addresses.allocate` gives memory that belongs to nobody: it stays until
`addresses.free` gives it back. The variable says what the memory holds:

<!-- run -->
```cabby
use addresses

struct point
{
	int X
	int Y
}

int main {
	address point Path = addresses.allocate 2       // room for 2 points, all 0
	Path[0].X = 3
	Path = addresses.reallocate Path, 3             // room for 3 now
	Path[2].Y = 4
	println Path[0].X, Path[1].X, Path[2].Y
	addresses.free Path
}
```

```text
3 0 4
```

* `allocate` sets everything to 0. A count below 0, or more memory than
  there is, is a panic.
* `reallocate` keeps what was there; any new room isn't set to anything.
  It may move the memory, so use the address it gives, never the old one.
  Reallocating `addresses.none` allocates.
* `free` of `addresses.none` does nothing. Only memory from `allocate` or
  `reallocate` (or a C function that says to free its result) can be
  freed. Cabby's own values (from `addresses.of`, `of_text` or `of_items`)
  give their memory back themselves when they're dropped: freeing one is
  an error when it's written directly (`addresses.free addresses.of_text
  Name`), a panic when interpreted otherwise, and, compiled, a crash when
  the value is dropped and its memory is given back a second time.

### The interpreter as a memory debugger

When interpreted, every block is tracked, and these are panics that say
where the memory was allocated and freed:

* using memory after `free`, or after `reallocate` moved it ("use the
  address reallocate gave instead");
* reading or writing past the end of a block;
* freeing the same memory twice;
* freeing an address inside a block rather than at its start, or the
  address of one of Cabby's own values.

When the program ends, blocks never given back are listed, by the line
that allocated them:

```text
note: 4 blocks from addresses.allocate never given back (136 bytes):
  100 bytes allocated at prog.cby:21:22
  36 bytes in 3 blocks allocated at prog.cby:30:20
```

### Memory lost as it happens

Memory is *lost* when nothing holds its address any more: it can never be
freed now. The interpreter notices this as it happens, in two places, and
says so once for each allocating line:

* when a variable holding the start of a block is changed
  (`A = addresses.allocate 8` while `A` holds the only address of the
  last block);
* when a line allocates again while an earlier block of its own is still
  live: the classic leak in a loop, where a block is allocated each time
  round and never freed.

<!-- check -->
```cabby
use addresses

int main {
	int Total = 0
	for i in 3 {
		address int Scores = addresses.allocate 10
		Scores[0] = i
		Total += Scores[0]
	}
	println Total
}
```

As it runs, this prints the following note on stderr, as well as the list
at the end:

```text
note: memory allocated at prog.cby:6:24 (40 bytes) was lost before that line ran again: nothing holds its address any more, and it was never given back
```

To decide, the interpreter looks through everything the program holds:
every variable of every call, the globals, lists, objects, and the bytes
of the blocks it can reach (so a linked list held by its first node is
held). That takes one pass over the program's data, so a line whose blocks
keep turning out to be held is looked at less and less often (after 2, 4,
8, 16... allocations). An address handed to a C function and kept there
can't be seen, so for memory a C library holds on to, the note can be a
false alarm.

In the interactive session the same note comes at `exit`, naming each
block by the line you typed to allocate it.

Freed memory is never reused while an interpreted program runs, so a
stale address is always caught. Compiled, none of this is checked: run
memory-handling code with `cabby prog.cby` first, or compile it with
`cabby -c --checked` (below).

## Strings and lists

C keeps a string as its characters followed by a `'\0'`, and an array as
its values one after another. `addresses.of_text` and `addresses.of_items`
give Cabby's strings and lists to C that way, with no copying:

<!-- run -->
```cabby
use addresses

extern long strlen address char Text
extern int toupper int Letter

long total address int Xs, long Count {
	long Sum = 0
	for i in Count {
		Sum += Xs[i]
	}
	return Sum
}

int main {
	str Name = "cabbage"
	address char T = addresses.of_text Name
	println strlen T
	T[0] = (char)toupper(T[0])      // the string's own characters
	println Name

	list int Scores = {10, 20, 30, 40}
	address int S = addresses.of_items Scores
	println total(S, Scores.length), S[3]
}
```

```text
7
Cabbage
100 40
```

* **`addresses.of_text S`** is an `address char` of the string's own
  characters, so a change through it changes `S`. A string literal is
  copied first, as for `S[i] = c`. The empty string gives a lone `'\0'`.
  Writing a `'\0'` into the middle doesn't make the string shorter:
  `S.length` stays the same.
* **`addresses.of_items L`** is an address of the first item of a list of
  plain data; stepping along goes through the rest, as along an array.
  `addresses.of L[i]` gives the same thing at item `i`.

Both need a variable (or field or element) holding the string or list, so
it's still there while the address is used, and both are good only until
the string or list changes: a string given a new value or grown with `+=`,
or a list that changes length (`add`, `insert`, `remove_at`, `take_last`,
`clear`), may move to new memory. When interpreted, using an address after
that is a panic that says what changed; changing an item (`L[1] = 5`) or a
character (`S[0] = 'c'`) is fine.

## Fixed addresses

On a microcontroller, devices are switched on and off by writing to memory
at fixed places:

```cabby
address char Port1 = addresses.at 0x0202
Port1.value = 0x01                         // pin 1 on
```

On a computer, the operating system gives each program its own memory, so a
made-up address almost certainly points at nothing: reading it stops the
program ("invalid memory access").

## What Cabby checks

When **compiled**, nothing: an address is just a number, as in C, and
reading where nothing is crashes the program or, worse, quietly reads
something else. Unless it's compiled with `cabby -c --checked prog.cby`:
then clang's AddressSanitizer is built in, and reading or writing past
an array or a block, using memory after it's freed, and (on Linux)
memory never freed each stop the program with a report of what and
where (the function, and those that called it). It's about half as
fast: for testing. It needs AddressSanitizer's runtime (`libclang-rt-dev`
on Debian, `compiler-rt` on Arch; a Mac's clang has it).

When **interpreted** (`cabby prog.cby`), Cabby checks addresses of its own
values as the program runs, and panics with a clear message:

* reading or writing before the start or past the end of an array;
* reading or writing an address stepped away from a single value;
* reading or writing `addresses.none`;
* reading an address as a different kind from the one it was made for;
* memory from `addresses.allocate` used wrongly (see above);
* an address kept after its value is gone (below).

So try a program that uses addresses with `cabby prog.cby` first, and
compile it once it runs cleanly.

### Addresses kept after their value is gone

An address of one of Cabby's own values is only good while the value is
there. The interpreter knows when a value goes, and using an address of it
afterwards is a panic that says what it was and why it went:

<!-- panic: this address is of 'Count' in make_counter, which is gone: 'make_counter' returned -->
```cabby
use addresses

address int make_counter {
	int Count = 5
	address int A = addresses.of Count
	return A
}

int main {
	address int A = make_counter
	println "got an address"
	println A.value
}
```

```text
got an address
```

```text
panic at prog.cby:12:10: this address is of 'Count' in make_counter, which is gone: 'make_counter' returned
```

A value goes when:

* its function returns (a parameter or local variable);
* the block it was declared in ends (an `if`'s, or a loop's each time
  round);
* it is dropped or replaced: an array or object given a new value of a
  different shape, or the list it is in dropped;
* its list changes length (`add`, `insert`, `remove_at`, `take_last`,
  `clear`): a list keeps its items together, and moves them all when it
  needs more room, so an address of an item is good only until then;
* its string is given a new value or grows (`+=`), for `addresses.of_text`.

Giving a struct or array of plain data a new value changes it where it is,
as in C, so an address of one of its fields or elements stays good.

Returning `addresses.of` a variable of the function itself is caught
before the program runs, in both modes, as a warning:

```text
warning: this gives the address of 'Count', which is gone once 'make_counter' returns
```

To give a caller memory that outlives the function, allocate it
(`addresses.allocate`), or have the caller pass in an address of its own
variable.

When an interpreted program gives C the address of one of its own values,
C gets a copy laid out as C expects, from that element to the end of its
array, which is copied back after the call; an address C gives back inside
that copy becomes an address of the Cabby value again.
