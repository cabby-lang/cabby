# Cabby

Cabby is a C-flavored, natively compiled language with Rust-style ownership:
values have a single owner, are moved rather than copied, and are cleaned up
automatically when their owner goes out of scope. There is no garbage
collector and no reference counting, and the compiler rejects use-after-move,
dangling references and conflicting borrows before the program runs.

```
cabby                                   start an interactive session (§12)
cabby prog.cby [args]                   run a program with the interpreter
cabby -c prog.cby [-o prog]             build a native executable (via LLVM)
cabby -c prog.cby --emit-llvm           write the LLVM IR (prog.ll) instead
cabby -c prog.cby --checked             build it with AddressSanitizer, for testing
cabby -c prog.cby --wasm                build WebAssembly (prog.wasm), to run in a web page
cabby --check     prog.cby              type- and ownership-check only
cabby --style     prog.cby              list names and calls that don't follow STYLE.md
cabby --dump-ast  prog.cby              print the syntax tree
cabby --dump-tokens prog.cby            print the token stream
cabby --version                         which Cabby this is: "Cabby 1.53.0 (2026-11-28)"
cabby --license                         Cabby's license (MIT)
```

`-c` is short for `--compile`. Both ways of running a program give the same
output and the same panic messages; compiled programs are typically 10–100
times faster.

Source files use the `.cby` extension; header files use `.hby`.

Each version is numbered MAJOR.MINOR.PATCH (`version.h` has it): the minor
number goes up with each stage of new things, and the patch number with
fixes alone; the major number, only for a change that breaks programs
already written. [TIMELINE.md](TIMELINE.md) says what each version brought.

Cabby is free to use, change and share, in open or closed programs, under
the MIT License: see [LICENSE](LICENSE). It comes as it is, with no
warranty, and its author isn't liable for how it's used.
Copyright (c) 2026 Thomas Paul Whalan.

**New to Cabby?** Start with [the Cabby Manual](tutorials/tutorials.md), a
book that teaches the language a chapter at a time, with a reference section
at the back. Each chapter goes with one of the 146 runnable programs
in [`tutorials/`](tutorials/README.md), one keyword or feature each, from a
first program to windows, games and 3D. [STYLE.md](STYLE.md) is the style guide: how to
name things and lay out code, checked by `cabby --style`.

---

## 1. A first program

```
object window {
	int Width
	int Height
	str Title

	int area const {
		return Width * Height
	}

	void resize int w, int h {
		Width = w
		Height = h
	}

	~window {
		println "closing", Title
	}
}

int main int Argc, str Argv[] {
	window w = {.Width = 4, .Height = 3, .Title = "demo"}
	w.resize 8, 6
	println "area is", w.area

	for i in Argc {
		println "argument", i, Argv[i]
	}
	return 0
}                                   // "closing demo" is printed here
```

---

## 2. Layout rules

**Blocks use braces and tab indentation, and both are checked.**

* A block opens with `{` and closes with `}`.
* Every line inside a block is indented by exactly **one more tab** than the
  line that opened it. Only tab characters count; a space in a line's leading
  whitespace is an error.
* The closing `}` lines up with the line that opened the block.
* `{` normally ends the header line (`if x > 5 {`). It may also sit alone on
  the next line, at the same indentation as the header.
* `} else {`, `} else if x {` and `} while cond` (for `do`) continue on the
  line of the closing brace.

**Statements end at the end of the line.** There are no semicolons, except
inside a `for` header. Inside `( )` and `[ ]`, and inside `{ }` initializer
lists, lines may wrap freely.

**Comments** are `// to end of line` and `/* block */`.

**Control flow has no parentheses** around conditions:

```
while x == y {
	...
}
if x > 5 && !Done {
	...
} else if x == 0 {
	...
} else {
	...
}
```

---

## 3. Keywords

```
auto      break     case      char      const     continue  default   do
double    else      enum      extern    float     for       goto      if
int       long      register  return    short     signed    sizeof    static
struct    switch    union     unsigned  void      volatile  while
str       object    bool      true      false     self      use
in        to        down      by        list      lookup    aka
is        not       and       or        until     inner
callback  link
```

These are C89's keywords (all but `typedef`) plus `str` and `object`, and these.
`auto` is reserved but not used: every variable writes its type, so the
reader sees it, and `auto X = ...` is an error that names the type to
write.

* `bool`, `true` and `false` (also keywords in C23), because comparisons
  produce booleans.
* `self`, which names the receiver inside a method. You rarely need it,
  because fields and methods can be used by their bare names.
* `use`, which brings in a header (§11).
* `in`, `to`, `down` and `by`, for counting loops: `for i in 1 to 10` (§10).
* `list`, for growable lists: `list int Scores` (§4).
* `lookup`, for values found by key: `lookup str int Ages` (§4).
* `is`, `not`, `and` and `or` are reserved, and a program that uses one
  is told what to write: `==`, `!`, `&&` or `||` (§10).
* `callback`, a C function pointer's shape, for C functions that call one
  of yours: `callback int comparer address A, address B` (§11).
* `link`, pointing at an object without owning it: `link person Best`
  (§9).
* `until`, a loop that runs until its condition is true: `until Done { }`,
  and `do { } until Done` (§10).
* `inner`, for an object's fields and methods that only it (and objects
  made from it) can use (§8).
* `aka`, which gives a type a second name: `double aka celsius` (§5). It
  takes the place of C's `typedef`, which stays reserved so that using it
  says what to write instead.

Keywords are reserved, so none of these can name a variable or function.

`extended` and `quadruple` are reserved too, for types to come: a whole
number larger than `long`, and a floating-point number larger than
`double`. So is `cement`, for build-time settings.

`print` and `println` are built-in functions, not keywords.

---

## 4. Types

| Type | Size | Notes |
|---|---|---|
| `bool` | 1 | `true` / `false` |
| `char`, `signed char`, `unsigned char` | 1 | one byte; printed as a character |
| `short`, `unsigned short` | 2 | |
| `int`, `unsigned` (`unsigned int`) | 4 | |
| `long`, `long long`, `unsigned long` … | 8 | |
| `float` | 4 | IEEE single |
| `double` | 8 | IEEE double |
| `str` | 16 | owned UTF-8 string |
| `T name[N]` | N × size | fixed-size array |
| `struct` | C layout | plain data, see §7 |
| `union` | largest field | plain data only, see §7 |
| `enum` | 4 | named `int` constants |
| `object` | 8 (handle) | class with methods, inheritance and a destructor |
| `T &name`, `const T &name` | 8 | borrows, see §9 |
| `T name[]` (parameter) | 16 | borrows an array of any length |

The sizes match C on 64-bit Linux and macOS, so `extern` C functions see
exactly the types they expect.

**Integers** wrap around on overflow (two's complement, for signed types
too). These cause a *panic* (the program stops with an error):

* recursing too deeply: more than 100,000 nested calls in the interpreter,
  or running out of stack in a compiled program;
* dividing by zero, and `INT_MIN / -1`;
* shifting by a negative amount or by at least the type's width;
* indexing an array out of bounds.

**Conversions** follow C's usual arithmetic conversions inside expressions.
Assignments, arguments and return values convert implicitly only when no
information can be lost: a smaller integer to a larger one, any integer to
`float` or `double`, `float` to `double`, `bool` to an integer, and an integer
to `bool` (non-zero is `true`). A constant also converts implicitly if its
value fits, so `char c = 65` and `float f = 1.5` are fine. Anything that
could lose information needs a C cast:

```
long Big = 5000000000
int Small = (int)Big          // explicit narrowing
double d = 7 / 2              // 3.0: integer division happens first
double e = (double)7 / 2      // 3.5
```

Casting a floating-point value to an integer truncates toward zero and
saturates at the type's limits (NaN becomes 0).

**Literals**

```
42  0x2A  0b101010  0o52      int, or long (then unsigned long) if it doesn't fit; suffixes u U l L ul ll ull
1.5  2e10                     double; suffix f for float (1.5f)
'a'  '\n'  '\x41'  '\0'       char (one byte)
"text\n"                      a string literal (static, never freed)
true  false                   bool
```

C's leading-zero octal (`0755`) is rejected because it is too easy to write
by accident; write `0o755`.

**Strings.** A `str` owns its text (UTF-8 bytes). Every string has:

```
s.length            // the number of bytes, as a long (free: the length is stored)
s[i]                // one char, numbered from 0; bounds-checked; s[i] = 'x' changes it
a + b               // a new string; each side a str or a char
s += b              // add a str or a char to the end
s.copy              // an independent copy (s.copy() works too)
a == b   a != b     // compare text
```

Strings move rather than copy (§9), so `.copy` is how you get a second
string you can change while keeping the first.

**Numbers as text.** Every number, `bool` and `char` has `.text`: the same
text `print` would show. `"Total: " + total.text` builds a string with a
number in it; `+` itself only joins strings and chars. A number's
`.text Places` gives it with that many decimal places, rounded:
`Price.text 2` is `"4.50"`, `(2.0 / 3).text 3` is `"0.667"`, `7.text 2`
is `"7.00"`.

**abs, close_to, random and exit** are Cabby's own, with no header:

```
abs -7                    // 7: any number, without its sign, in its own type
close_to 0.1 + 0.2, 0.3   // true: the same but for rounding (and see below)
close_to A, B, 0.01       // true when A and B are no more than 0.01 apart
int Roll = random 1 to 6  // a whole number from 1 to 6, each as likely
double R = random 0.0 to 1.0   // a double from 0 up to (not including) 1
random_seed 42            // the same numbers after this every time (for tests, replays)
exit 1                    // ends the program now, with that exit status
```

`random`'s numbers come from a fast generator (xoshiro256**), started
from the system's own random numbers, so each run differs unless
`random_seed` says where to start; it's for games, shuffles and dice
(`secrets.random_number` is for what no one must guess). `exit` writes
out what's printed and what open files hold, then ends at once:
destructors don't run, as when `main` returns from inside a call they
would. A program or header with its own `abs`, `close_to`, `random` or
`exit` (C's, from `use standard`) uses that instead.

`close_to A, B` asks for the two to be the same to about 9 significant
digits (5 when both are `float`s), and takes anything within 1e-12 of zero
(1e-6 for `float`s) as close to zero. Since floating-point numbers are
rounded, `==` or `!=` with arithmetic or a decimal with a fraction on one
side (`0.1 + 0.2 == 0.3`, `D == 0.3`) warns and suggests `close_to`;
comparing with a whole number (`D == 0.0`) or two variables doesn't.

**stderr.** `print` and `println` write to stdout. Put `stderr` in front of
a call to send everything that call prints (including what the functions it
calls print) to stderr, until it returns: `stderr println "Error:", Code`,
or `stderr report "disk full", 28` for a function of your own. The call
still gives back its value (`int n = stderr check_files`), and `stdout`
works the same way, as does C's `printf` (`use standard`).

**String tools.** `a < b` (and `<=`, `>`, `>=`) compare in dictionary
order. Searching: `s.contains t`, `s.starts_with t`, `s.ends_with t`, and
`s.find t` (where `t` starts, or -1); `t` can be a `str` or a `char`. New
strings from old ones: `s.replace a, b`, `s.trim`, `s.upper`, `s.lower`,
`s.repeat n`, and `s.pad_left n`, `s.pad_right n` and `s.centered n`
(spaces in front, after, or either side, to make it `n` characters wide:
columns, with `.text(2)` for the decimals). `.upper` and `.lower` know
the letters of Latin, Greek (Ancient Greek's too), Cyrillic and Armenian:
`"ἀρχή".upper` is `"ἈΡΧΉ"`, and a Σ ending a word goes down to ς. A slice `s[a to b]` is a new string of characters `a` to `b`,
both included; `s[a to]` goes to the end and `s[to b]` starts at 0. A slice
outside the string panics, except that the end may be one before the
start, giving `""`.

```
str Line = "  the cat sat  ".trim
if Line.starts_with "the" {
	println Line.replace("cat", "dog"), Line.find("sat"), Line[4 to 6]
}
```

**Text as numbers.** `s.to_int`, `s.to_long` and `s.to_double` turn text
into a number, and panic if it isn't one; check first with `s.is_int`,
`s.is_long` or `s.is_number`. The whole text must be the number, apart
from spaces around it.

**Reading input.** `input "Name: "` shows a prompt and gives back the line
typed, without its Enter; `read_line` is the same without a prompt. At the
end of the input (Ctrl-D, or the end of a file piped in) they give `""`
and `end_of_input` becomes true, so an empty line and no more input are
told apart. Reading again after that is a panic, so a loop that forgets to
check `end_of_input` stops rather than printing empty lines forever:

```
str Name = input "Name: "
str Age = input "Age: "
while !Age.is_int {
	Age = input "A whole number, please: "
}
println Name, "will be", Age.to_int + 1
```

`read_char` gives the first character of the next line (the rest of the
line is read too, so nothing is left behind, unlike C's `getchar`; an
empty line gives `'\n'`). `read_key` gives one key the moment it's
pressed, without Enter, as a `str`: the character itself (`"q"`), or a
name (`"enter"`, `"escape"`, `"up"`, `"left"`, `"page_down"`, `"f1"`...).

**Arrays** have `.length` too: a constant for `T name[N]`, and the length of
whatever array a `T name[]` parameter borrows.

**Lists.** `list T Name` is a list of `T` that grows and shrinks; it starts
empty, or with values: `list int Scores = {90, 85}`. A list owns its
elements and is moved, like a `str`.

```
Scores.add 70              // on the end (a str, object or list is moved in)
Scores.insert 0, 100       // at a position, 0 to .length
Scores.remove_at 1         // out, closing the gap
int Last = Scores.take_last
Scores.clear
Scores.length   Scores[i]  // read or change an element; every index is checked
Scores.contains 70   Scores.find 70     // true/false; the position or -1
Scores.copy   Scores[1 to 3]            // a new list: all of it, or a slice
Scores.sort   Scores.reverse          // smallest first; last first
Pets.sort_by Age   Pets.sort_by name_of // by a field, or a function's key
Pets.sort_with older                    // bool older const pet &A, const pet &B
println Scores             // [100, 90, 70]; a list str prints as [Ada, Alan]
list str Words = Line.split " "         // and back: Words.join ", "
for Score in Scores { }    // each element, read-only: a copy for numbers,
                           // a read-only borrow for anything else
```

A list can't be changed inside a `for` loop that goes through it, and an
element can't be moved out with `[i]` (borrow it, `.copy` it, or take it
out with `.take_last` or `.remove_at`). Lists can hold anything, including
other lists (`list list int Grid`); `.copy`, slices, `.contains`, `.find`
and `.sort` need elements that can be copied or compared; `.sort_by` and
`.sort_with` put anything in order, keeping equals in the order they were.

**Lookups.** `lookup K V Name` holds a value of type `V` for each key of
type `K`, kept in the order the keys were added: `lookup str int Ages =
{"Ada": 36, "Alan": 41}`. Keys are whole numbers, `char`, `bool` or `str`;
values are anything. A lookup owns its keys (copied in) and values (moved
in), and is moved, like a list.

```
Ages["Grace"] = 85         // adds Grace, or replaces her value
Ages["Ada"] += 1           // reads first: a panic if Ada isn't there
Ages["Bob"]                // a panic: check first with Ages.contains "Bob"
Ages.remove "Alan"   Ages.clear   Ages.length
Ages.copy   Ages.keys   Ages.values       // a new lookup; lists of copies
for Name, Age in Ages { }  // in the order added (for Name in Ages: keys only)
println Ages               // {Ada: 37, Grace: 85}
```

As with lists, a lookup can't be changed inside a loop that goes through
it, and a value can't be moved out with `[key]`.

---

## 5. Declarations

```
int Count                         // zero-initialized: every variable starts at 0, 0.0, false, "" …
int Total = 10
int a, b = 2, c[3]                // several declarators, as in C
double Xs[3] = {1.0, 2.0, 3.0}
int Primes[] = {2, 3, 5, 7}       // length taken from the initializer
const int LIMIT = 100             // cannot be changed
str Name = "Ada"                  // owned copy of the literal
window w                          // object with every field zeroed
window v = {.Width = 5}           // the rest zeroed
point p = {3, 4}                  // positional, in field order
```

There is no uninitialized memory: a declaration without an initializer sets
the variable to its type's zero value.

**Storage classes**

* `static` inside a function makes a variable keep its value between calls.
  At the top level it makes a variable or function private to its file.
* `register` forbids borrowing the variable. Otherwise it is a hint, as in C.
* `volatile` makes every read and write really happen in compiled code, as
  in C.
* `extern` declares a C function (see §11).

**Globals.** Top-level variables may be `const` of any plain type, `const
str`, or a mutable variable of a plain-data type (numbers, `bool`, `char`,
enums, and structs, unions and arrays made of those). Mutable globals cannot
hold strings or objects, because nothing would ever own them. Global
initializers must be constant expressions.

**aka** gives an existing type a second name: the type, then `aka`, then
the new name, at the top level of a file. It reads as "double, also known
as celsius". The two names are the same type, and mix freely.

```
unsigned long aka size_t
double aka vec3[3]
```

**kind** makes a number of its own, which doesn't mix with plain
variables or other kinds (`kind` only means this here, so it is still a
fine name elsewhere). A number written in the program (`15`, `-3`, `60 *
60`) is taken as the kind where one is wanted: setting, passing,
returning, comparing, `+`, `-` and `+=`. A kind adds to and takes from
itself, and is multiplied and divided by plain numbers; the same kind
divided by itself gives a `double`. `strict long kind duration` takes no
plain numbers at all, for a kind whose name doesn't say its unit. `, steps T` makes one measured from a starting point: it can't
be added to itself, but takes steps of `T`, and two of them give the `T`
between. Functions named `kind_name.method` (taking it first) are its
methods, and `kind_name.text` is how it prints; an enum can have methods
the same way. A cast turns a kind into a number and back.

```
long kind meters
long kind marker, steps meters
meters Walk = 400   Walk += 50   if Walk > 100 ...   marker Shop = Start + Walk   meters Apart = Shop - Start
str meters.text meters M { ... }                                // Walk.text, println Walk
```

**sizeof** gives a type's or an expression's size in bytes, as an `unsigned
long`: `sizeof(int)`, `sizeof xs`, `sizeof(window)`.

---

## 6. Functions

A function is its return type, its name and its parameters, with no
parentheses:

```
int add int a, int b {
	return a + b
}

void greet {                     // no parameters
	println "hello"
}

int main int Argc, str Argv[] {
	...
}
```

A **prototype** is the same header without a body. Prototypes are optional,
because functions may be used before they are defined, but if one is given
it must match the definition. A prototype for a function with no parameters
is written with `void` (`int tick void`), otherwise it would read as a
global variable.

**Calls** can be written with parentheses, as in C, or without them. Without
parentheses, the arguments are everything after the function's name, up to the
end of the line or the closing parenthesis around the call:

```
greet                          // greet()
print "x =", x                 // print("x =", x)
w.resize 8, 6                  // w.resize(8, 6)
println add 1, 2               // println(add(1, 2))
int n = strlen s               // strlen(s)
int m = (add a, b) * 2         // add(a, b) * 2: parentheses show where the call ends
int k = add(a, b) * 2          // the same: name(...) is an ordinary C call
int t = tick + 1               // tick(): a name alone calls a function with no parameters
```

Inside an expression, `name(...)` is always an ordinary call, and a call
without parentheses starts only when the name is followed directly by a value
(a name, a number, a string, `!` or `~`). A minus sign starts the argument
only after a function or method that takes arguments: `abs -7` passes -7,
while `n - 1` and `n -1` subtract.
Because the arguments run to the end, a comparison after a bare call is
one of its arguments: `show 2 == 2` passes `true`, and `if sq 2 == 4` means
`sq(2 == 4)`. To compare the call's result, put the call in parentheses:
`if (sq 2) == 4`.

At the start of a statement there is one more rule: a parenthesis straight
after the name holds the call's arguments only if it holds all of them.
Otherwise it is the start of the first argument, so spaces never matter:

```
print a + b * 2                // 5   (with a = 1, b = 2)
print (a + b) * 2              // 6
print(a + b) * 2               // 6
print(a + b)                   // 3
```

`main` returns `int` (or `void`) and takes either no parameters or
`int Argc, str Argv[]`. `Argv[0]` is the program's name, and the rest are
the command-line arguments (`cabby prog.cby one two`). If `main`
reaches its closing brace without a `return`, it returns 0, as in C.

**Built-ins.** `print a, b, ...` prints its arguments separated by spaces.
`println` does the same and ends the line. Both evaluate every argument
before printing anything.

---

## 7. struct, union, enum

```
struct point {
	int x
	int y
}

union bits {
	float f
	unsigned int u
}

enum color {
	RED                // 0
	GREEN = 5
	BLUE               // 6
}
```

* A **struct** is plain data: fields only, with no methods, inheritance or
  destructor. A struct whose fields are all copyable is itself copyable.
  One containing a `str` or an object moves instead.
* A **union** stores all its fields at the same address and is as large as
  its largest field. Every field must be plain data (no `str`, objects or
  borrows), so reading a different field from the one last written just
  reinterprets the bytes. It can never cause a memory error.
* An **enum** defines named `int` constants. A value of type `color` is an
  `int` and converts freely.

Values are initialized with `{...}`, either positionally or by field name,
and missing fields are zeroed. In an expression the type can be given in
front: `draw(point{1, 2})`. An initializer cannot appear directly in an
`if`, `while`, `for` or `switch` condition; put it in parentheses there.

---

## 8. Objects

An object is a class: fields, methods, a destructor, and **multiple
inheritance**.

```
object animal {
	str Name
	int Legs

	void speak const {                      // const: may not change the object
		println Name, "makes a sound"
	}
	void describe const {
		println Name, "has", Legs, "legs"
		speak                               // bare call to a method of this object
	}
}

object pet : animal {
	str Owner
	void speak const {                      // overrides animal.speak
		println Name, "purrs for", Owner
	}
}

object worker : animal {
	str Job
}

object catbot : pet, worker {               // multiple inheritance
	int Model
	static catbot make str n {              // static: no receiver
		return catbot{.Name = n, .Legs = 4}
	}
	~catbot {                               // destructor
		println "recycling", Name
	}
}
```

* Inside a method, fields and other methods can be used by their bare names.
  `self` is the receiver itself, for passing it on or for reaching a field
  that a local variable shadows.
* Methods take the object by mutable borrow, unless they are marked `const`
  (a shared borrow), or `static` (no receiver, called as `catbot.make("Tom")`).
* **Dispatch is virtual.** Calling a method through a borrow of a base type
  runs the most-derived override.
* **A base type holds what's made from it.** A `square` can be given,
  kept or put wherever a `shape` goes: `list shape Shapes` takes squares
  and circles, `shape Held = Square` keeps one, and each still answers as
  what it is (`X.area` is the square's own area), and is dropped as what
  it is. Only the base's own fields and methods can be used through it.
  This costs nothing extra for an object nothing is made from; one that
  others are made from finds its fields and methods through its class,
  as through a borrow.
* **Diamonds share their base.** `catbot` contains one `animal`, not two.
  Method lookup follows the C3 order, the same as Python's: catbot, pet,
  worker, animal. Field order (used by positional initializers) is base-most
  first, then left to right: Name, Legs, Owner, Job, Model.
* **Destructors** run automatically when the object is dropped. Each class's
  `~name` runs from the most derived to the base-most, then the fields are
  dropped.
* **What runs when an object is made** is written `name { ... }`, the
  destructor's opposite: it runs whenever one is made (declared, made from
  `{...}` after those fields are set, or as part of another object or
  array). An object's parts are made first, and parents run before
  children. It takes no parameters and is never called by hand.
* **`inner`** before a field or method keeps it for the object's own
  methods and those of objects made from it; anything else using it is an
  error. An override must be inner exactly when what it overrides is.
* The compiler rejects inheritance cycles, hierarchies with no consistent C3
  order, two parents contributing fields with the same name, overrides with a
  different signature, and objects that contain themselves.
* **Handlers.** An object can keep another object's method in a `handler`
  field, and call it later, as a window's button does:

  ```
  object countdown {
  	handler Done
  	void on_done handler h {
  		Done = h                  // only from a handler parameter
  	}
  	void tick {
  		Done.call                 // does nothing if it isn't set
  	}
  }
  // in a method of an object with a field `countdown Count`:
  Count.on_done launch              // one of this object's own methods
  ```

  `.is_set` and `.clear` go with `.call`. Three rules, checked before the
  program runs, keep handlers safe. A handler can only be given to a part
  of its object (a field, or an element of a fixed array field), so it
  can't outlive the object it calls; which methods keep a handler is worked
  out from their bodies. A call to a part that has handlers counts, for
  borrowing, as a change to the whole object, since it may call back into
  it. Destructors can't call handlers. Objects live in blocks that don't
  move, so a handler stays valid when its object moves.

---

## 9. Ownership and borrowing

Plain values (numbers, `bool`, `char`, enums, and structs, unions and arrays
made only of those) are **copied**. Everything else (`str`, objects, and any
struct or array that contains them) is **moved**:

```
window a = {.Title = "one"}
window b = a            // a is moved into b; using a afterwards is an error
take(b)                 // b is moved into the call
b = window{.Title = "two"}   // b holds a value again
```

To use something without taking ownership, **borrow** it with a reference
parameter or a reference variable (the `&` goes in the declaration, as in
C++):

```
void show const window &w {      // shared borrow: read-only
	println w.Title
}
void rename window &w, str t {   // mutable borrow
	w.Title = t
}

show(a)                          // the borrow is implicit at the call
rename(a, "new")
const window &r = a              // a local borrow
```

The rules, all checked before the program runs:

* While a value is borrowed, it cannot be moved. It also cannot be changed,
  unless the borrow is a mutable one.
* There can be any number of shared borrows, **or** exactly one mutable
  borrow, at any one time.
* A borrow ends at its last use, not at the end of its block.
* Different fields are separate: while `p.Name` is borrowed, `p.Age` can
  still be changed, and a method can go through one field of its object
  while changing another. A method call that can change the object counts
  as changing all of it. Elements of an array or a list at positions
  written as numbers are separate too (`Scores[0]` borrowed, `Scores[1]`
  changed); at a position worked out as it runs (`Scores[i]`), an element
  counts as all of them, and `.add` or `.remove_at` changes the whole list.
* References cannot be returned, stored in fields or put in arrays, so a
  borrow can never outlive what it points to.
* A string literal is a static string. It converts to an owned `str` (a heap
  copy) wherever one is needed.

Every value is dropped exactly once. Locals are dropped at the end of their
block in reverse order, including when the block is left early by `return`,
`break`, `continue` or `goto`. Temporaries are dropped at the end of their
statement. A value that has been moved away is not dropped again.

**Links.** `link T Name` points at an object of `T`'s without owning it, so
objects can point at each other freely: a child at its parent, friends at
each other, in circles if need be, while each still has one owner.

```
object person {
	str Name
	link person Best
}
Ada.Best = Alan                  // a link to Alan; Alan isn't moved
println Ada.Best.Name            // through it, as if it were Alan
Ada.Best.is_set                  // false once Alan has been dropped
Ada.Best.clear                   // points nowhere
Ada.Best == Grace.Best           // the same object?
```

Using a link whose object has been dropped is a panic that names it, never
a read of freed memory: each linked object has a place in a table, whose
generation changes when it's dropped. Links are 8 bytes and copied, like
numbers. Since the borrow checker can't see that `Ada.Best` and `Alan` are
one object, a link reads anything; changes numbers, `bool`s, `char`s, plain
structs and links (not strings, lists or lookups, which are changed through
their owner); lends nothing (`.copy` first); and calls only `const` methods
that take nothing borrowed. A link is used by the worker whose object it
points at.

---

## 10. Statements

```
if cond { } else if cond { } else { }
while cond { }
until cond { }                      // while !cond
do {
	...
} while cond                        // or: } until cond
for i in n { }                      // i = 0, 1, ... n-1
for i in 1 to 10 { }                // 1, 2, ... 10 (both ends included)
for i in 10 down to 1 { }           // 10, 9, ... 1
for i in 0 to 100 by 5 { }          // 0, 5, ... 100
for long i in n { }                 // the counter's type, if you want to choose it
for Score in Scores { }             // each element of a list, array or string
for int i = 0; i < n; i++ { }       // C's own for; any part may be empty: for ;; { }
switch x {
	case 1:
		...
		break
	case 2:
	case 3:
		...                         // falls through as in C
	default:
		...
}
goto done                           // forward only, to a label in this block or an enclosing one
done:
x++   x--   ++x   --x               // statements only, not inside expressions
x += 1   x <<= 2 ...                // all of C's compound assignments
return value
break   continue
```

**Counting loops.** `for i in ...` declares its counter `i` and counts
through the range. The range is worked out once, before the loop starts.
Its counter has the range's type (the common type of the two ends; `'a' to
'z'` counts in `char`), unless you name a type: `for long i in n`. The
counter is read-only inside the loop, and the loop never steps past the end,
so `for unsigned i in n down to 0` stops at 0 rather than wrapping around.
The step after `by` must be positive; count down with `down to`. For
anything else (two counters, multiplying, a `double`) use C's three-part
`for`.

Conditions accept `bool` or any number; non-zero means true, as in C.
`&&` and `||` short-circuit and give a `bool`. They are separate operators
from the bitwise `&`, `|`, `^` and `~`.

There are no words for these, as there are in Python: `Answer is "quit"`
or `Ready and not Done` is an error saying what to write (`==`, `&&`,
`!`). `!` turns round only what's straight after it, so a comparison it
turns round goes in parentheses: `!(Count == 0)`, or `Count != 0`.
(They were Cabby's until 0.36.0; `tools/words-to-symbols.py` rewrites a
program that has them.)

Operator precedence is C's. As in C, `x & 1 == 0` means `x & (1 == 0)`.
Cabby warns about it and suggests `(x & 1) == 0`.

Each `case` section is its own scope, so a variable declared after one
`case` label is not visible after the next.

`goto` may only jump forward, to a label in the same block or an enclosing
one, and not past a declaration. Locals in blocks that it leaves are
dropped. For a backward jump, use a loop.

**When something fails: `else`.** A panic ends the program, unless the
statement it happens in has an `else`. Then nothing is shown, and the
`else` part is done instead:

```
str Notes = files.read "notes.txt" else ""          // the value it has if it fails
int Age = Answer.to_int else 0
Total += Prices[I] else 0
str Page = networks.download Url else Why {         // Why: what the panic said
	println "couldn't fetch it:", Why
	return 1
}
files.delete "old.txt" else { }                     // after a statement: carry on
```

`else` goes after a declaration (one variable, with a value), an
assignment or a call on a line of its own. It catches a panic anywhere in
working it out, in the functions it calls and the libraries they call: a
file that isn't there, a server that doesn't answer, text that isn't a
number, a list's position past its end. `else Value` gives the variable
(or the assignment) that value instead; `else { }` runs a block, and
`else Why { }` runs it with the panic's message in `Why`, a `str`. After a
declaration, a block has to leave (`return`, `break`, `continue`), since
the variable has no value otherwise.

What the failed calls had made is dropped, as when they return: their
destructors run, the last made first (so a file one had opened is
closed). A value only half made when the panic came, such as an object
whose fields were still being worked out, is let go without its
destructor. Checking first (`files.exists`, `.is_int`, `Ages.contains`)
still reads best when failing is expected. Stopping with Ctrl-C, running
out of stack, and `exit` aren't panics, so `else` doesn't catch them.

---

## 11. Headers and C functions

```
use standard               // standard.hby from Cabby's library directory
use local util             // util.hby next to this file
use local shapes/circle    // shapes/circle.hby, relative to this file
```

`use` brings in the declarations of a header file (a `.hby` file; leave out
the extension). Each header is read once per program, even if several files
use it. For `use local util`, if a `util.cby` sits next to `util.hby`, it is
compiled into the program too; with no `util.hby`, `util.cby` is used on
its own (a library of plain functions needs no header). `static` top-level
names stay private to their file. A `use` line goes at the top level, on
its own line.

A first line `#!/usr/bin/env cabby` (the only `#` Cabby takes) makes a
program runnable as `./prog.cby` after `chmod +x prog.cby`.

C functions are declared with `extern`, using C's types directly:

```
extern long time long Unused            // a C function no header declares
extern int printf str fmt, ...
extern double sqrt double x
```

Calls into C are only as safe as C: Cabby checks the types of the arguments
against the `extern` declaration, but cannot check, for example, that a
`printf` format matches its arguments.

Three headers declare the C library functions Cabby doesn't have its own
way to do: `use standard` (`printf`, `abs`, `labs`, `rand`, `srand`, `exit`,
`system`), `use math` and `use strings`. C's `puts`, `putchar`, `getchar`,
`atoi`, `atol` and `atof` aren't declared: Cabby's `println`, `print`,
`read_line`, `input`, `.to_int`, `.to_long` and `.to_double` do their jobs,
and using one of C's names says which to use instead.

**Callbacks.** A C function that calls one of yours (`qsort`'s
comparison, a C library's event callback) takes a function pointer.
`callback` names its shape, as a function header without a body, and a
function of the program's with that shape is given by its name:

```
callback int comparer address A, address B
extern void qsort address Base, long Count, long Size, comparer Compare
qsort addresses.of_items(Numbers), Numbers.length, sizeof(long), by_number
```

A callback takes and gives numbers of 32 bits or more, `double`, and
addresses (C's `void *` is a plain `address`), and can only be a
parameter of an `extern` function. Cabby checks the function's shape
against it. Compiled, C gets the function itself; interpreted, a function
(libffi's) that runs it in the interpreter, from any thread C calls it
from (manual chapter 28).

**Files and directories.** `use files` and `use directories` turn on
Cabby's own built-in functions for them (manual chapter 32):

```
str Text = files.read "notes.txt"          list str Lines = files.lines "data.csv"
files.write "out.txt", Text                files.append "log.txt", "started\n"
files.exists "notes.txt"   files.size "notes.txt"   files.copy A, B   files.rename A, B
files.delete "old.txt"
directories.list "photos"  directories.all_files "photos"   directories.exists "photos"
directories.make "a/b/c"   directories.remove "a/b/c"   directories.rename A, B
directories.current        directories.change_to "photos"   directories.home
file Log = files.open_to_add "log.txt"    // or open_to_write; files.open to read
Log println "started"                     // a file in front of a call, as stderr is
str Line = In.read_line    In.at_end      // a line at a time; closed when dropped
directory D = directories.open "photos"   // held open, however it's moved or renamed
while D.next { println D.name }           D.read "notes.txt"   D.write "seen.txt", "yes"
```

Anything that goes wrong is a panic that says why ("can't read
'notes.txt': no such file or directory"); check first with `files.exists`
or `directories.exists`. Each library has a reference page (`files.md`,
`directories.md`) next to its header.

**Commands and environment variables.** `use shell` runs commands through
`/bin/sh` (so pipes and redirections work):

```
str Out = shell.run "ls | wc -l"      // what it printed, without the final line end
int Status = shell.status "make"      // its exit status; its output goes to the screen
str Home = shell.get "HOME"           shell.has "EDITOR"
shell.set "LANG", "C"                 shell.unset "LANG"
str Sorted = shell.run_with "sort", Names            // the command given input
str Listed = shell.run_program {"ls", "-l", Folder}  // no shell: nothing needs quoting
int Found = shell.program_status {"grep", "-q", Word, Name}
```

`shell.run` panics if the command fails (an exit status other than 0) and
`shell.get` if the variable isn't set; `shell.status` and `shell.has` are
the checks. Everything printed before a command, to the screen or into open
files, is written out before it runs. The reference page is `shell.md`.

**Processes.** `use processes` adds the `process` type, for programs a
program starts and talks to:

```
process P
P.start "sort"                    // by /bin/sh; stderr goes where ours does
P.send "pear"                     // a line to its input
P.end_input                       // no more input
str Line = P.read_line            P.at_end         // "" at the end
int Status = P.wait               P.is_running     P.status     P.stop
P.on_line got_line                P.on_end finished              // handlers; P.line
P.detach                          // before start: a daemon, on its own
P.write "y"                       // text with no line end
P.keep_errors_apart               // before start: its stderr as lines of their own
str Error = P.read_error_line     P.errors_at_end  P.on_error_line h
```

A process belongs to its variable: dropping it ends its input and waits for
it. Its handlers are called while the program waits, in `P.wait` or
between a gui or tui window's events, and follow the handler rules (§8).
The reference page is `processes.md`; manual chapter 34 and tutorial 34
teach it.

**Timers and the event loop.** `use events` adds the `timer` type and
`events.run`:

```
timer Tick                        // a field of one of your objects
Tick.on_tick show                 // one of the object's methods
Tick.every time.seconds(1)        Tick.after time.milliseconds(500)        Tick.stop     Tick.is_running
events.run                        // waits, calling handlers, until nothing is left
events.stop                       // in a handler: ends events.run now
```

Cabby has one event loop, in its runtime. Timers, processes with handlers
and windows all wait in it (a window's `run` waits there too), so they work
together in one program. `events.run` returns when no timer is running and
no process with handlers is still going; nothing waits unless the program
says so. The reference page is `events.md`; manual chapter 35 and tutorial
35 teach it.

**Time and dates.** `use time` gives `moment` (an exact moment) and
`duration`; `use dates` (which brings `time` in) gives `date`,
`time_of_day`, `date_time`, and the enums `weekday` and `month`. All are
kinds, so they don't mix with plain numbers: a moment and a duration give
a moment, two moments the duration between; a date steps by days.

```
moment Start = time.now        duration D = time.since(Start)      time.sleep time.seconds(2)
date Today = dates.today       Today + 14      Christmas - Today   Today.add_months(1)
Today.format "d mmmm yyyy"     dates.read "25/12/2026", "dd/mm/yyyy"     Today.weekday == SATURDAY
M.local    M.utc    DT.moment  (the computer's time zone, with daylight saving)
```

Patterns are spreadsheet-style: `d dd ddd dddd`, `m mm mmm mmmm`, `yy
yyyy`, `h hh`, `mm` (minutes, after an hour), `ss`, `am/pm`. Panics from a
library's C code are reported at the line of the program that called the
library. The reference pages are `time.md` and `dates.md`; manual chapter
39 and tutorial 39 teach them.

**JSON.** `use json` gives `json`, which holds any JSON value. `json.read
Text` reads it (a panic says where text isn't JSON); `J["name"]` and
`J[i]` go in, `for X in J` goes through a list; `.str`, `.double`,
`.long` and `.bool` give Cabby values, and panic with what they found
unless the `_or` form gives a default (`J["high"].long_or 0`). JSON's
`{ }` "object" is called a lookup (`json.lookup`, `.is_lookup`), since
that's what it is in Cabby. `J["name"] = V`, `J.add V`, `.text` (one
line) and `.pretty` (indented) write it; text, numbers and bools become
JSON where a `json` is wanted. A part taken out (`J["days"][0]`) is part
of the whole; `.copy` makes one of its own. `json.from S` makes JSON from
a struct's fields, and `J.fill S` fills them in from JSON, by name
(`HighScore` is written `"high_score"`; `"highScore"` and `"HighScore"`
are read too). `.fill` changes only the fields the JSON has, so the
struct's own values are defaults. Fields can be text, numbers, kinds,
`bool`, `char`, enums (by name), structs, `json`, and lists, `lookup
str ...`s and arrays of those; with `use json`, Cabby writes the two
functions for each struct (`json.from_struct_<name>`,
`json.fill_struct_<name>`) as it reads the program.

**CSV.** `use csv` reads tables as spreadsheets save them:
`csv.read_file "scores.csv"` gives the rows, each a `list str` of its
fields, and `csv.read_records_file` gives each row after the first as a
`lookup str str`, by the first row's names (`P["Age"]`). Quoted fields
(commas, `""` for a quote, line ends inside) are RFC 4180's;
`read_separated Text, ';'` reads semicolons or tabs. `csv.text Rows` and
`csv.write_file Path, Rows` write them, quoting what needs it, and
`csv.records_text Columns, Records` writes records. `lib/csv.md` is the
reference.

The library is an ordinary object in `json.hby`, using three things any
library can: `[ ]` on an object with `item`/`item_at` (and `set_item`/
`set_item_at` for `[ ] =`, and `for X in` with `length`), conversions
through functions named `type.from_str` (`from_long`, `from_double`,
`from_bool`), and methods named after a type with a `_` after them
(`long long_`, called as `J.long`). The reference page is `json.md`;
manual chapter 40 and tutorial 40 teach it.

**Downloading.** `use networks` gives `networks.download Url` (what's
there, as a `str`), `networks.download_to Url, Path` (saved as a file;
nothing is left behind if it fails), `networks.can_reach Url` and
`networks.status Url` (the HTTP status: 200, 404...; 0 if nothing
answered). A failed download is a panic that says why. `https://`,
redirects, proxies and `file://` work: it uses libcurl, loaded the first
time a program downloads, so neither building Cabby nor other programs
need it. `json.read networks.download Url` reads a web service's answer in
one line. `networks.post`, `post_json` and `post_form` send to a web
service; `networks.request_to Url` sets up any method (`PUT`,
`DELETE`...), headers and a time limit, with `on_progress` for big
transfers. The reference page is `networks.md`; manual chapter 41 and
tutorial 41 teach it.

**Programs talking to each other.** `networks.connect Host, Port` is a
connection to another program (`send_line`, and `read_line` to wait for
the answer, or `on_line` to be told of each line), and `networks.server`
listens on a port for programs to connect (`on_connect`, `on_line`,
`reply`, `send_line_to_all`). It's TCP, the same on Linux and on a Mac,
so a Cabby program talks to another, or to `nc` or a Python script.
Manual chapters 91 to 95 (and their tutorials) are servers, a web
server, WebSockets, encryption and email.
`events.step` goes round the event loop once, for a program with a loop
of its own. `networks.md` has them.

**Who's on the network.** `networks.answering Hosts, Port, Wait` gives
those of `Hosts` with a program listening on `Port`, every one asked at
once (`networks.nearby_addresses` is the network's other 253, for a home
or office one), and `networks.pings Host, Wait` and `ping_time` send a
ping (ICMP), by a socket of the program's own where the system allows
it, or the `ping` command.

**A web server.** `networks.web_server` answers browsers: `on_request`'s
handler looks at `path` (and `query_value`, `form_value`, `cookie`,
`body_json`) and answers with `send_page`, `send_json`, `send_file`,
`send_from_folder "public"` or `redirect`; a request it leaves is "404
Not Found". HTTP/1.1, with a browser's connection kept open between
requests.

**WebSockets and HTTPS.** A web server's `accept_socket` turns a request
into a WebSocket, a page and the program sending each other messages
whenever they like (`on_message`, `reply`, `send_message_to_all`), and
`networks.open_socket "ws://..."` is one from a program. With a
certificate (`set_certificate "fullchain.pem", "privkey.pem"`, from Let's
Encrypt, or one `networks.make_certificate` makes for trying it), a web
server is HTTPS and `wss://`, and a server's connections are encrypted
(TLS); `networks.connect_encrypted` connects to one. Through OpenSSL.

**Email: `use emails`.** An `emails.message` (`set_from`, `add_to`,
`add_cc`, `add_bcc`, `set_subject`, `set_text`, `set_html`, `attach`),
sent through an account's server: `emails.account_at "smtp.gmail.com",
Login, AppPassword`, then `send`, `true` if it went (`error` says why
not). MIME made by Cabby, sent with libcurl, encrypted. A mailbox is read too
(IMAP): `emails.mailbox_at "imap.gmail.com", Login, AppPassword`, then
`folders`, `unread_count`, `unread`, `latest 10`, `from "ana"`, and
`mark_read`, `move` and `delete`; each `emails.received` has its `from`,
`subject`, `text`, `html` and attachments, decoded whatever language or
character set they came in, and `save Message, "Sent"` puts one into a
folder. The reference is `lib/emails.md`.

**Secrets: `use secrets`.** Random numbers, text and passwords no one can
guess (`secrets.random_text 22`); passwords kept as hashes
(`hash_password`, `check_password`); text, bytes and files locked with a
password (`secrets.lock Text, Password`, `unlock`); signatures (HMAC);
public keys (`make_private_key`, `sign`, `check_signed`: Ed25519); and
Argon2id passwords where OpenSSL is 3.2 or later. Built on OpenSSL on Linux and CommonCrypto on a Mac, not on
cryptography of Cabby's own. The reference is `lib/secrets.md`.

**Videos: `use videos`.** A video file opened with FFmpeg (MP4, MKV,
WebM, MOV ...) and asked what's in it: `Film.width`, `length`,
`frame_rate`, `picture_codec`. Played in a gui window with `add_video
Film.source` (its picture and sound kept together, with a play button and
a track under it, `show_controls`), and played, paused and moved by the
video: `Film.play`, `Film.seek 30`, `on_end`. `picture_at` gives the
picture at any moment as an `images.picture` (a thumbnail), and
`save_sound` its sound as a WAV file. The reference is `lib/videos.md`;
tutorial 67 is a player.

**The system: `use os` and `use linux`.** `os` is what every system has,
the same on Linux and a Mac: an `os.watcher` told of each file created,
changed, deleted or renamed in a folder (`watch_all` for every folder in
it); `os.signals`, catching Ctrl+C and being asked to stop, told to a
handler; `os.disks` and their free space; `os.cpu_use`, memory and load;
and `os.programs`, the programs running, to stop by number. `linux` is
what only Linux has: `linux.temperatures` and `fans` (hwmon), its disks
and partitions, USB devices, the kernel's modules and settings
(`linux.setting "net.ipv4.ip_forward"`), and systemd's services
(`linux.service_named "ssh"`, `start`, `stop`, `enable`). The references
are `lib/os.md` and `lib/linux.md`.

**The screen, printing and speech.** `use brightness` reads and sets
the screen's brightness, 0 to 100 (`brightness.set 60`: a laptop's
backlight, or a monitor through ddcutil, on Linux; a Mac's own screen).
`use printing` prints files and text through CUPS, the same on both
(`printing.print "scales.pdf"`, with `printing.settings` for copies,
two-sided and pages), and lists printers and jobs. `use speech` reads
text aloud (`speech.say "Hello"`; a `speech.speaker` for a voice, a
language and a speed, Latin and Ancient Greek among them), or saves it
to a sound file: espeak-ng on Linux, a Mac's own voices. It hears, too:
`speech.hear "lesson.mp3"` and `speech.hear_for time.seconds(5)` give
what's said as text, by Whisper (whisper.cpp) on the computer itself. And
`system.set_hostname` names the computer. The references are
`lib/brightness.md`, `lib/printing.md`, `lib/speech.md` and
`lib/system.md`.

**HTML and XML.** `use html` and `use xml` read and build trees of
elements. `html.read Text` reads a web page as browsers do, forgiving
mistakes; `xml.read Text` is strict, and a mistake is a panic with its
line and column (`xml.can_read` checks first). A page (`html`) or document
(`xml`) is a kind of `element`; `E.find Selector` gives the first match and
`E.find_all Selector` every one, with CSS selectors (`"a"`, `".price"`,
`"#prices"`, `"li.price"`, `"ul li"`, `"ul > li"`, `"a[href]"`,
`"[type=checkbox]"`, `"h1, h2"`). Elements have `.tag`, `.text` (tidied;
what `println` shows), `.raw_text`, `.attribute` (`_or`,
`has_attribute`, `attributes`), `.children`, `.parent` and `.is_missing`.
`html.element "li"` / `xml.element "note"` make one; `.add`, `.add_text`
(always escaped), `.set_attribute`, `.remove`, `.clear`, `.markup` and
`.pretty` build and write. The parser and selectors are Cabby's own, in
its runtime. The reference pages are `html.md` and `xml.md`; manual
chapters 43 and 44 and tutorials 43 and 44 teach them.

**Bytes and binary files.** `use bytes` (which `use files` brings) gives
`bytes`, a run of numbers from 0 to 255 for files that aren't text.
`B[i]` is one byte (an `int`), `.add`, `.add_bytes`, `.part`, `.length`,
`.same_as`, `.starts_with`, `.find`; it prints in hex (`89 50 4e 47`),
`.dump` gives a hex dump, `.str` and `bytes B = "text"` go between bytes and
text, and `bytes.from_hex` reads hex. Numbers inside are read and written
as C keeps them, smallest byte first (`short_at`, `int_at`, `long_at`,
`double_at`, `unsigned_short_at`, `unsigned_int_at`, with `set_..._at`
and `add_...`), or biggest byte first with the same names ending `_big`.
`files.read_bytes`, `files.write_bytes` and `files.append_bytes` work on
whole files; `files.open_to_change Path` opens a file to read and write
anywhere in it, with `F.read_bytes N`, `F.write_bytes B`, `F.position`,
`F.move_to N`, `F.move_to_end` and `F.size` (these work in files opened
the other ways too). `networks.download_bytes`
downloads bytes. The reference page is `bytes.md`; manual chapters 45 and
46 and tutorials 45 and 46 teach it, with byte order explained.

**Addresses.** `use addresses` adds addresses (C's pointers) of plain data,
for C functions that take or give pointers and memory at fixed places:

```
address int A = addresses.of Count   // an array gives its first element
A.value = 6        A[2]        A + 1        B - A        A < B
address short Port = addresses.at 0x0200      address int N = addresses.none
A.is_none          A.number    (address char)A            // `address` alone: void *
extern address char strchr address char s, int c
address char T = addresses.of_text Name       address int S = addresses.of_items Scores
address int B = addresses.allocate 100    B = addresses.reallocate B, 200    addresses.free B
```

Steps are whole values. Reads and writes always happen (as with C's
`volatile`). Nothing is checked when compiled; when interpreted, stepping
outside an array, reading `addresses.none`, reading an address as
another kind, and using an address after its value is gone (its function
returned, its block ended, its list changed length) are panics; returning
`addresses.of` a local is a warning. Memory from `allocate` (zeroed) is tracked when
interpreted: use after free, past the end, double free, the old address
after `reallocate` and freeing inside a block are panics, memory nothing holds
any more is noted as it's lost (once per line), and blocks never freed are
listed when the program ends. Interpreted calls to C get a C-shaped copy of
Cabby values (from the element to the end of its array), copied back
afterwards, and addresses into that copy map back to the Cabby value. The
reference page is `addresses.md`; manual chapter 28 and tutorial 28 teach
it.
In the interpreter, extern functions are looked up in the C library and the
math library at run time.

**Namespaces.** A library can keep its names apart from yours by declaring
them with a prefix: `object gui.window`, `struct geo.size`. After `use`,
they are written in full (`gui.window w`, `object app : gui.window`), so
two libraries, or a library and your program, can both have a `window`. The
prefix (`gui`) is then reserved for those names. Inside its own object, a
method or destructor uses the short name (`~window`).

**Native code.** A library header in Cabby's library directory may come
with C code of its own: `libcabby<name>.so` (which the interpreter loads)
and `libcabby<name>.a` plus `<name>.link` (linked into compiled programs).
That is how `use gui` reaches SDL, and `use gtk` GTK.

**C of your own.** `use local mylib`, with `mylib.c` beside `mylib.hby`
(or beside a `mylib.cby` with no header), needs no library built first:
the interpreter builds `mylib.c` with the system's C compiler (`cc`, or
`$CABBY_CC`) into a `.so` it keeps in `~/.cache/cabby` (or
`$CABBY_CACHE`), and builds it again only when the `.c` changes; `cabby
-c` compiles it into the program. Its functions are declared with
`extern` in the header. Flags it needs (`-lsqlite3`, `-DDEBUG`) go in
`mylib.link` beside it.

---

## 12. The interactive session

Run `cabby` with no file to type Cabby a piece at a time:

```
$ cabby
Cabby 1.53.0 interactive session. Type statements and declarations; "exit" to finish, "cabby.help" for more.
Cabby comes with ABSOLUTELY NO WARRANTY, under the MIT License; "cabby.license" for details.
>>> int add int a, int b {
... 	return a + b
... }
>>> int x = add 2, 3
>>> x * 10
50
>>> str Name = "Ada"
>>> str Other = Name
>>> println Name
<input>:1:9: error: use of moved value 'Name'
    println Name
            ^
<earlier input>:1:13: note: 'Name' was moved here; a 'str' is moved, not copied
    str Other = Name
                ^
>>> exit
Save this session? [y/N] y
File name: adding
Session saved to adding.cby
```

**What you type.** An input is either a declaration — `use`, `object`,
`struct`, `union`, `enum`, an `aka` line, `extern`, or a function definition —
or statements. Statements become the body of `main`, so a variable you
declare at the prompt is a local of `main`: it keeps its value until the
session ends, and functions cannot see it (pass it to them instead).

**Output.** Each prompt starts on a new line, so `print "one"` and then
`print " two"`, typed as two inputs, show on two lines where a program would
show `one two`. Lines typed between `{` and `}` run as one input, and their
output is as a program's.

**Checking.** After every input the whole session is checked again as one
program, so the usual rules apply across inputs: a string you moved three
inputs ago is still moved. An input that fails to check is reported and
forgotten; nothing from it runs. Only errors are shown for earlier inputs;
warnings are shown once, for the input that causes them.

**Echoing.** An expression on its own line is printed, as if it were
`println expression`: `x * 10` shows `50`, and so does `add 40, 10` or
`add(40, 10)`. A call to a C function written without parentheses
(`printf "%d\n", n`) is not echoed, since it is called for what it does.

**Colors.** At a terminal, what you type is colored as you type it, in
the colors Vim gives Cabby code (keywords yellow, types green, strings and
numbers magenta, built-ins bold cyan, comments blue), and so is the line of
code shown under any error. `NO_COLOR=1` turns color off; `CABBY_COLORS`
changes it, e.g. `CABBY_COLORS="comment=36:keyword=1;33"`.

**Several lines.** When an input is not finished — a `{` is still open, a
parenthesis is unclosed, a `/*` comment has no `*/` yet, or a line is a
header like `if x > 0` waiting for its `{` — the prompt changes to `...` and
the next line is indented for you with tabs; typing `}` removes one tab. In
a `/*` comment that begins its line, each next line starts ` * `, as in Vim,
and typing `/` straight after that closes the comment. An `if` waits for one
more line in case an `else` follows: press Enter on an empty line to run it
(or simply type the next statement); after a final `else` block it runs at
once.

**Tab completion.** Right after a name or a dot, Tab completes: `files.`
lists the files library's functions, `Rex.` the fields and methods of the
variable `Rex` (for an object, its parents' too), `Names.` a list's members,
`gui.` the gui library's types (a library's names show before its `use`
too), a partial name (`Na`) your variables,
functions, types and keywords, and `use ` the libraries there are (`use
local ` the `.hby` headers and folders next to you, `use local shapes/` the
ones in `shapes`). One match is filled in; several are filled
in as far as they agree, then listed. When there's nothing to complete
(at the start of a line, after a number or a finished name), the Tab key
inserts a tab, and so does a second Tab straight after a list, so
`Name` Tab Tab `// note` gives a tab before the comment.

**Redefining.** A function definition with the name of an earlier one
replaces it, as long as every earlier input that calls it still checks.
Types cannot be redefined.

**Panics and Ctrl-C.** A panic, or Ctrl-C while an input runs, abandons
that input and returns to the prompt; the input is not kept. If it had
already moved a variable from an earlier input, that variable is reset to
its zero value (you are told). Variables it had changed keep their new
values.

**The session's own commands** start with `cabby.`, so none takes a name
from programs: `cabby.exit`, `cabby.clear` (clears the screen),
`cabby.preserve [name]` (keeps the session in `~/.cabby_sessions`),
`cabby.restore [name]` (runs its inputs again, quietly, so its variables
and functions are back), `cabby.remove [name]`, `cabby.sessions`,
`cabby.version`, `cabby.license` and `cabby.help`.

**Ending.** `exit` on its own line (unless the session has an `exit` of
its own; `cabby.exit` always works), or Ctrl-D, ends the session. Every
variable is dropped, so destructors run, as at the end of `main`. `return n`
ends the session too, with `n` as cabby's exit status. If anything was
entered, cabby then asks whether to save the session. The saved file is an
ordinary program (`cabby adding.cby` runs it): the declarations in the order
you entered them, using the latest definition of each function, then
`int main` with every statement that ran. Inputs that panicked are kept as
comments, marked as not kept.

**Line editing.** Left/Right, Home/End, Up/Down for history (kept in
`~/.cabby_history`), Ctrl-U / Ctrl-K / Ctrl-W to delete to the start / to the
end / the previous word, Ctrl-L to clear the screen, Ctrl-C to abandon the
current input. Pasting a block keeps its own indentation. With input from a
pipe (`cabby < steps.txt`) no prompts are printed and lines are read as
typed.

---

## 13. Windows: the gui library

`use gui` gives windows and what goes in them (buttons, text boxes, check
boxes, drop-down lists, sliders, text areas, lists, progress bars and
pictures), plus timers and dialogs. Cabby draws them itself, with SDL 3
underneath, so a window looks and works the same on Linux and on a Mac, in
a light look or a dark one as the desktop is. The full reference is
`lib/gui.md`.

```
use gui

object greeter : gui.window {
	gui.label Message
	gui.entry Name

	void build {                            // what is in the window
		set_title "Greeter"
		Message = add_label "What's your name?"
		Name = add_entry
		gui.button Hello = add_button "Say hello"
		Hello.on_click say_hello            // a handler: called on each click
	}

	void say_hello {
		Message.set_text "Hello, " + Name.text + "!"
	}
}

int main {
	greeter g
	g.run                                   // shows the window; returns when it is closed
}
```

* **A window** is an object made from `gui.window`. Its `build` method adds
  the contents; `run` calls `build`, shows the window, and then sleeps
  until something happens, using no processor time. It returns when the
  window is closed. For a window without handlers you can also use a
  `gui.window` directly: `gui.window w`, `w.add_label "Hi"`, `w.run`.
* **Layout.** Things stack top to bottom in the order they are added.
  `add_row` returns a `gui.row`, whose contents go side by side, and
  `add_column` a `gui.column`, which stacks them again inside a row.
* **Widgets.** `add_label`, `add_button`, `add_entry` (one line),
  `add_check`, `add_choice` (a drop-down), `add_slider min, max`,
  `add_text_area` (several lines), `add_list`, `add_progress`,
  `add_image path` and `add_separator`. Every widget has `set_text`, `text`,
  `set_enabled`, `set_visible` and `set_minimum_size`. Windows also have
  `set_title`, `set_size w, h` and `close`.
* **Handlers.** `button.on_click name`, `entry.on_change name`,
  `check.on_toggle name`, `list.on_select name` and the rest take the name of
  one of the window's methods, which has no parameters and returns nothing.
  The name is not a call here: the method runs later, each time the event
  happens, like an interrupt routine. Handlers belong to windows, so they
  always live as long as the widgets that call them.
* **Timers and dialogs.** `every time.seconds(1), name` and `after time.seconds(2), name` call a
  handler on a timer. `show_message`, `ask question, name`,
  `ask_open_file name` and `ask_save_file suggested, name` show dialogs and
  call the handler with the answer (`chosen_file` for files). A handler can
  also open and run a second window.
* **Drawing.** `add_canvas width, height` adds a `gui.canvas`; its
  `on_draw` handler draws it whenever needed, and `redraw` asks for that
  again. It draws `line`, `rectangle`, `circle`, `ellipse`, `arc`,
  `polygon` (`{x, y, x, y ...}`), `write` (text) and `image`, filled with
  `fill_...`, in colors written as on the web (`"steelblue"`, `"#3366cc"`),
  in pixels from the top left with y counting down. `on_click`,
  `on_mouse_move` and `on_key` with `mouse_x`, `mouse_y` and `key`; `save`
  writes a PNG. In `tui` the same canvas is drawn with blocks.
* **Tabs and scrolling.** `add_tabs` gives a `gui.tabs`, whose `add_tab
  "Title"` adds a page and gives back its column; `selected`,
  `set_selected`, `count`, `text` and `on_change`. `add_scrolling_column`
  is a column that scrolls when what's in it doesn't fit.
* **Tables.** `add_table {"Name", "Score"}` gives a `gui.table`;
  `add_row {"Ada", "1200"}`, `cell Row, Column`, `set_cell`, `remove_row`,
  `clear`, `count`, `selected`, `on_select`. Clicking a heading sorts by
  it (numbers as numbers); rows keep the numbers they were added with.
* **Numbers, colors and fonts.** `add_number Min, Max` gives a
  `gui.number` (`value`, `set_value`); `add_color_button` a
  `gui.color_button` (`color`, as `"#4682b4"`); `add_font_button` a
  `gui.font_button` (`font`, as `"Serif Bold 16"`). Each has `on_change`,
  and a canvas's `set_color` and `set_font` take their choices as they
  come.
* **Menus and shortcuts.** `gui.menu File = add_menu "File"` puts a menu
  on the window's menu bar; `File.add_item "Save", "Ctrl+S", save` adds an
  item with its handler and shortcut (`""` for none), `add_separator` a
  line; the item it gives back has `set_enabled`. `add_shortcut "F5",
  refresh` makes a shortcut on its own.
* **The `handler` type** is what `on_click` takes: an object's method, bound
  to the object. It is passed to C as two pointers, the window and the
  method. Your own objects can have handlers too (§8).

GUI programs run both ways: `cabby app.cby`, or `cabby -c app.cby` for a
native program. The gui library is built when SDL 3 is installed (`sdl3` on
Arch, `libsdl3-dev` on Debian, `brew install sdl3` on a Mac); without it,
`use gui` says so. Tutorials 51 to 55 build windows step by step.

**The same windows drawn by GTK: `use gtk`.** The gtk library has the same
objects and methods (`gtk.window`, `gtk.button` ...), drawn by GTK 4 with
its own widgets. Changing `gui` to `gtk` is all it takes. It's there for
those who want GTK's look. It's built when GTK 4.10 or newer is installed
(`gtk4` on Arch, `libgtk-4-dev` on Debian).

**Screen readers.** A gui window is read aloud by Linux's screen reader
(Orca), with nothing to do in the program: what each thing is and is
called, what has the keys, and what changes as it runs. Pictures and
canvases are told what to say with `set_description`. (A Mac's VoiceOver
is still to come: gtk's windows are read there today.)

**Testing a window without a person.** With `CABBY_GUI_SCRIPT` set, windows
follow a script of `;`-separated commands instead of waiting, such as
`click <button text>`, `type <n> <text>`, `check <label>`,
`choose <n> <option>`, `wait <ms>`, `answer yes`, `pick <path>`,
`shot <file.png>` and `close` (the full list is in `lib/gui.md`). Each
`close` ends the current window; the rest goes to the next one. Under a
script, gui windows are drawn off screen, so no display is needed; `make
test` uses this (and `xvfb-run`, when it's installed, for gtk).

**Windows in a terminal: `use tui`.** The tui library has the same objects
and methods as gui (`tui.window`, `tui.button`, `add_row`, `on_click`,
`every`, `ask` and the rest), drawn in the terminal with text, lines and
the terminal's own colors. Changing `gui` to `tui` runs the same program
in a terminal; it needs no desktop, so it works over SSH. Tab and
the arrow keys move around, Enter presses, Esc closes, and the mouse works
in terminals that pass clicks on. Sizes stay in pixels (8 by 16 to a
character). What handlers print is kept until the window closes.
`CABBY_TUI_SCRIPT` takes gui's script commands plus `key <name>` and
`screen`, which prints the window as text. The full reference is
`lib/tui.md`; tutorial 62 is a to-do list.

**Games: `use game`.** A `game.window` is a window with a stage and
sprites on it, built like a gui window and run in frames, 60 a second,
with no loop in the program. It's drawn with SDL 3 (and the graphics
card), as gui is:

```cabby
use game

object catch_game : game.window {
	game.sprite Basket

	void build {
		set_size 480, 360
		set_background "skyblue"
		Basket = add_rectangle 70, 20, "saddlebrown", 240, 330
		on_frame step                   // called every frame
	}

	void step {
		if key_hold "left" {
			Basket.move -6, 0
		}
	}
}
```

Sprites come from pictures (`add_sprite "ship.png", X, Y`) or shapes
(`add_rectangle`, `add_circle`), and `move`, `move_to`, `turn`,
`move_forward`, `set_scale`, `animate` and `remove`. Each frame
`key_press` (just went down), `key_hold` (held) and `key_release` (just
let go) read the keys, and `key_held_for` says how long. `every` and
`after` count frames, so they wait while the game is `pause`d; `random`
gives whole numbers; `stage.on_draw` draws over the sprites.
`A.touches B` says whether two sprites touch; `set_group "rock"` puts a
sprite in a group, and `sprites_in "rock"` and `Bullet.touching "rock"`
find them. `set_world_size` makes a world bigger than the stage, and
`follow Hero` keeps the camera on a sprite as the world scrolls.
`set_layer` puts a sprite in a layer (the ground under everything), and
`sort_by_depth` draws what's lower on the stage in front, so a hero can
walk behind a tree. Test scripts add `frames <n>`, `hold <key>`,
`release <key>` and `tap <key>`. The full reference is `lib/game.md`;
tutorial 68 is a whole game, and tutorials 69 to 73 an adventure.

**Games in a terminal: `use tgame`.** The same games, in a terminal: change
`game` to `tgame` and the stage is shown with half-block characters (two
pixels to each), as big as the terminal allows, with what `stage.write`
writes as real text. Keys are exact in terminals with kitty's keyboard
protocol (kitty, foot, WezTerm, Ghostty, Alacritty), and guessed from
their repeats in others; Ctrl+C or Ctrl+Q ends the game. It needs SDL 3,
which draws off screen, but no window or desktop, so it works over SSH.
The reference is `lib/tgame.md`.

**3D: `use world`.** A `world.window` looks into a world of shapes, built
as a game is (`build`, then `on_frame` 60 times a second): boxes, balls,
planes, cylinders, cones and wedges, in metres with y up, colored or with
pictures on them, lit by the sun, the light all round and lamps, and seen
by a camera that can follow a shape, as in most 3D games, or be the
player's eyes, turned with the mouse. `turn` and `move_forward` walk a
shape round; `touches` and `shapes_in` pick up coins; `add_text` writes a
score over it all. It's drawn with OpenGL (through SDL 3, so nothing more
to install), or by Cabby itself where there's none, and under a test
script, where it's the same to the pixel everywhere (`pixel <x> <y>`
prints a color). Models made in Blender come in with `add_model`
(glTF `.glb` files, as Blender exports them), with their colors, pictures
and bones, and their animations played by name: `Hero.play "Walk"`. The
full reference is `lib/world.md`; tutorial 89 is a walk round a field,
and 88 a robot from Blender.

**Sound: `use sound`.** `sound.play "boom.wav"` plays a sound file (WAV,
MP3 or FLAC) and the program goes straight on; `sound.play_music` loops
one; `sound.load` gives a `sound.track` to pause, loop and so on. Notes are
named as on a stave: `sound.play_note "A4", 500`,
`sound.play_chord {"C4", "E4", "G4"}, 1000`,
`sound.play_notes "C4 D4 E4 - G4", 300`, and `start_note` / `stop_note`
for held keys, played with `set_instrument "violin"` and the rest, from the
computer's SoundFont (or Cabby's own synthesizer without one).
`sound.play_midi "song.mid"` plays MIDI files, and `sound.wait` waits for
it all to finish. miniaudio and TinySoundFont are built in, so nothing
needs installing. The full reference is `lib/sound.md`; tutorial 72 is a
keyboard to play.

**MIDI keyboards and synthesizers.** `sound.open_keyboard` listens to a
MIDI keyboard: `play_through "piano"` plays it through the computer, heard
at once, and `on_note` tells a handler of each key (`note`, `velocity`,
`pressed`), with `on_control` for pedals and knobs. `sound.open_synthesizer`
plays notes on a MIDI synthesizer. Recording can be heard as it goes, with
`sound.set_monitoring true`. Sound works from workers too. Tutorial 76
plays a keyboard as a piano and writes down what's played.

**Pictures: `use images`.** `images.new 320, 200` is a picture in memory:
`set_pixel_rgb`, lines, rectangles, circles, and `draw` to put one on
another; `images.load` and `save` read and write PNG, JPEG and BMP. A
canvas shows one with `picture X, Y, P`, sending its pixels to the screen
again only when they've changed. A worker can paint pictures and hand
each to the window with `tell_picture`, moved rather than copied.
Tutorial 66 paints the Mandelbrot set that way. The reference page is
`lib/images.md`. Pictures are changed
into new ones: `scaled`, `scaled_to_fit` (a thumbnail), `cropped`,
`turned`, `mirrored`, `flipped`, `gray`, `blurred`; and `text` is written
on them in a font of the computer's (`images.set_font "Serif Bold", 40`).

**Every language.** A window shows text in any script: Hebrew and Arabic
right to left (numbers and English words in them left to right), Arabic's
letters joined, and emoji in color, from the computer's emoji font
(Noto Color Emoji, Apple Color Emoji). The shaping is HarfBuzz's, loaded
when it's needed; without it, Cabby joins Arabic and orders the text
itself. Tutorial 61 is a phrasebook.

**Archives: `use archives`.** `archives.make "lessons.zip", {"lessons"}`
makes a zip (or a .tar, .tar.gz or .tar.xz, by its name); `unpack`,
`list` and `read` take them apart, and `compress` squeezes one file.
`archives.make_with_password` keeps a zip's files with AES-256, as WinZip
and 7-Zip do, and `unpack_with_password` reads it (and the old ZipCrypto
of `zip -e`). The reference page is `lib/archives.md`.

**Users: `use users`.** Who's running the program (`users.name`,
`users.full_name`, `users.groups`), anyone else by name, and, run with
`sudo`, making them: `users.make "ada", "Ada Lovelace", Password`,
`make_admin`, `set_password`, `remove`, and groups (`make_group`,
`add_to_group`). Linux's own programs do it (useradd and the rest), and a
Mac's (sysadminctl, dseditgroup). The reference page is `lib/users.md`;
manual chapter 98 and tutorial 98 teach it, and chapters 96 to 100 the
rest of the computer: `system` and `os`, power, brightness and volume,
`linux`, and `secrets`.

**Bluetooth: `use bluetooth`.** `bluetooth.turn_on`, the devices it knows
(`bluetooth.devices`, each with its name, address, kind, signal strength
and battery), `bluetooth.search time.seconds(10)` for more, and a
device's `pair`, `connect`, `disconnect` and `forget`, each true if it
worked, with `bluetooth.error` saying why not ("Page Timeout": it's off
or too far away). A keyboard's pairing code is shown to type on it. A
`bluetooth.watcher` tells a handler when a device connects, disconnects
or is found. It's BlueZ, over D-Bus, on Linux, and the Mac's own
(IOBluetooth) on a Mac.
The reference page is `lib/bluetooth.md`; manual chapter 105 teaches it,
and chapters 101 to 107 the rest of the desktop: notifications and file
kinds, what's playing, printing, speech, D-Bus, and the next three.

**Screens, the mouse and the keyboard.** `use screens` lists the monitors
(`screens.all`, `screens.named "HDMI-A-1"`), each with its resolution,
refresh, scale, rotation, place and modes, and changes them
(`set_resolution 1920, 1080`, `set_rotation 90`, `set_scale 1.5`,
`set_position 2560, 0`, `turn_off`). `use mouse` reads and sets the
pointer's speed, natural scrolling and left-handed buttons, the
touchpad's own, tapping to click, and the cursor's size and theme. `use
keyboard` reads and sets the layouts (`keyboard.set_layouts {"us",
"de"}`) and how a held key repeats, and says whether Caps Lock and Num
Lock are on. Each desktop is asked its own way: Sway by swaymsg, GNOME
by Mutter and gsettings, KDE Plasma by kscreen-doctor (screens), X11 by
xrandr, xinput, setxkbmap and xset, and a Mac by CoreGraphics (screens).
The reference pages are `lib/screens.md`, `lib/mouse.md` and
`lib/keyboard.md`; manual chapter 107 teaches them.

---

## 14. Building and testing

Cabby is written in C11 and needs a C compiler and libffi (`libffi` on Arch,
`libffi-dev` on Debian). `--compile` needs clang 15 or newer (it emits LLVM IR
with opaque pointers); set `CABBY_CC` to use a different clang. Compiled
programs link the small runtime `libcabbyrt.a`, found the same way as the
headers, and libm.

The gui, game, tgame and world libraries (§13) are built too when SDL 3 is installed:
`pkg-config sdl3` finds it (`sdl3` on Arch, `libsdl3-dev` on Debian,
`brew install sdl3` on a Mac). The gtk library is built when GTK 4 is
installed: `pkg-config gtk4` finds it (`gtk4` on Arch, `libgtk-4-dev` on
Debian). The videos library is built when FFmpeg's libraries are
installed: `pkg-config libavformat` finds them (`ffmpeg` on Arch;
`libavformat-dev`, `libavcodec-dev`, `libswscale-dev` and
`libswresample-dev` on Debian; `brew install ffmpeg` on a Mac). The sound
library needs nothing installed.

`-c` (`--compile`) takes `-o <file>` (default: the source name without `.cby`),
`-O0` to `-O3` (default `-O2`), `--emit-llvm` to write the IR instead, and
`--checked` to build it with clang's AddressSanitizer: reading or writing
past what's there, using memory after it's freed, and (on Linux) memory
never freed each stop the program with a report of where, naming the
function and those that called it. It's slower (about twice, and built
with `-O1` at most, so the optimizer leaves the reads it would check), for
testing code that uses addresses. It needs AddressSanitizer's runtime:
`libclang-rt-dev` on Debian (`libclang-rt-19-dev` for clang 19),
`compiler-rt` on Arch; a Mac's clang has it.

```
make                 # builds ./cabby
make test            # lexer, parser and checker suites, every program both
                     #   interpreted and compiled, the interactive session,
                     #   and every example in the manual
make debug           # AddressSanitizer + UBSan build in build-debug/
sudo make install    # /usr/local/bin/cabby, /usr/local/lib/cabby (headers,
                     #   runtime, gui library), /usr/local/share/doc/cabby
sudo make uninstall  # removes all of that again
```

The suites can also be run on their own, from the cabby folder, from
`tests/`, or from anywhere else, once `make` has built Cabby:

```
tests/run_all.sh             # every suite, as make test runs them
tests/run_program_tests.sh   # one suite (each uses ./cabby by default)
tests/run_manual_tests.sh ./cabby lib/json.md   # one page of the manual
```

**Vim and Neovim.** `make install-vim` (without sudo) installs colors and
automatic tab indenting for `.cby` and `.hby` files into `~/.vim`; add
`VIMDIR=~/.config/nvim` for Neovim. See `editor/vim/README.md`.

`make install PREFIX=$HOME/.local` installs for one user without sudo
(make sure `~/.local/bin` is on your `PATH`).

**Packages.** Cabby can also be installed as a package, so the system's
own package manager can upgrade it and take it away again:

```
packaging/arch/build.sh -i         # Arch: builds the package with makepkg, and installs it
packaging/debian/build-deb.sh      # Debian: builds a .deb, then:
sudo apt install ./packaging/debian/out/cabby_*.deb
packaging/homebrew/build-tap.sh    # Mac: a Homebrew formula in a tap of its own, installed
```

Each installs under `/usr` (or Homebrew's folders), with the manual, the
Vim files and, on Linux, the file icons. `packaging/README.md` says
more.

**In a browser.** `make wasm-site` builds the interpreter for WebAssembly
(with wasi-sdk) and puts in `build/wasm-site` what a web page needs to run
Cabby programs in the visitor's own browser, with a page to try it.
`make wasm` builds the runtime for WebAssembly too, and then
`cabby -c prog.cby --wasm` compiles a program to `prog.wasm`, which a page
runs the same way, as fast as a native program (2 to 30 times faster than
interpreted there). `cabby -c --wasm` finds wasi-sdk in `$CABBY_WASI_SDK`,
`~/tools` or `/opt/wasi-sdk`. `wasm/README.md` says how to build them, how
a page uses them, and what a browser hasn't (sound, 3D, the network,
threads, other programs). Windows and games (`use gui`, `use game`) show
on the page, and are played there, once SDL is built for WebAssembly
(`tools/build-sdl-wasm.sh`).

`use name` searches `$CABBY_LIB`, then `lib/` next to the `cabby` binary,
then `../lib/cabby` relative to it, then the `lib/cabby` of the folder it
was built for (`PREFIX`), then `/usr/local/lib/cabby` and `/usr/lib/cabby`.

## 15. What is not in Cabby (yet)

* Panels and bars for a desktop (a dock, a system tray) in the gui
  library.
* In a browser: sound, 3D (`use world`), and gui's file choosers.
* The `?:` operator, `long double`, and backward `goto`.
* Borrowing tells apart elements at positions written as numbers, but not
  at positions worked out as it runs (`Scores[i]` and `Scores[j]` count as
  the same). Borrows in two branches of an `if` can occasionally conflict
  when Rust would accept them. Both only ever reject programs; they never
  let an unsafe one through.
