# Cabby style guide

Cabby already checks its own layout: tabs for indenting, braces, one
statement per line. This guide covers what it leaves to you, so that every
Cabby program reads the same way: names, spacing, the order of things, and
a few habits.

`cabby --style prog.cby` checks the names in a program against this guide,
and its calls' parentheses, and suggests the right way to write each one
that doesn't fit.

* [Names](#names)
* [Spacing and lines](#spacing-and-lines)
* [The order of a file](#the-order-of-a-file)
* [Objects](#objects)
* [Calls](#calls)
* [Control flow](#control-flow)
* [Comments](#comments)
* [A whole program in this style](#a-whole-program-in-this-style)

## Names

| What | Written | Examples |
|---|---|---|
| Keywords | lower case (the language requires it) | `int`, `while`, `object` |
| Types: `struct`, `object`, `union`, `enum`, `aka` | lower case, `_` between words | `point`, `order_form`, `gui.window` |
| Functions and methods | lower case, `_` between words | `do_this`, `say_hello`, `area` |
| Variables, parameters and fields | a capital at the start of each word | `DateOfBirth`, `Total`, `IsReady` |
| Counters and coordinates | one lower-case letter (digits allowed) | `i`, `j`, `n`, `x`, `y2` |
| Constants (every `const` value) and enum values | capitals, `_` between words | `DAYS_PER_WEEK`, `NOT_FOUND` |
| Labels for `goto` | lower case, `_` between words | `found:` |
| Files | lower case, `_` between words | `order_form.cby`, `geometry.hby` |

### Why

The case says what a name is, at a glance, without reading the word:

* **Capital letters: stored.** `Total`, `Clicks.Count` and `IsReady` are
  values kept in a variable or field.
* **Lower case: worked out.** `total`, `Clicks.count` and `is_ready` are
  functions and methods. In Cabby a function or method with no parameters
  is called by its name alone (`int Next = tick + 1`), so this is how you
  tell `Shape.Area` (a field) from `Shape.area` (a method that computes
  it).
* **All capitals: fixed.** `DAYS_PER_WEEK` never changes.
* **Lower case after a type's position:** `point P`, `gui.window W`. Types
  only appear where a declaration or conversion starts, so they never mix
  with functions in practice.

### Details

* **Abbreviations are words.** Write `UrlPath`, `HttpCode`, `Id`, not
  `URLPath` or `ID`, so that a run of capitals always means a constant.
* **Single letters** are for counters and coordinates (`for i in 10`,
  `X`, `Y` are both fine for a point). Anything that lives longer than a
  few lines deserves a real name.
* **Every constant is in capitals**, inside a function too:
  `const int WEEKS = 4`. Capitals say "this can't change" wherever it is.
  A read-only borrow (`const account &View = Acc`) isn't a constant, just a
  variable that can't change what it points to, so it is written like a
  variable.
* **True-or-false names read as yes/no questions:** `IsReady`, `HasName`,
  `CanSave`. A method that answers one is lower case: `is_ready`.
* **Names from outside keep their names.** C functions (`printf`, `strlen`)
  and their `extern` declarations use C's names; libraries' members are as
  their documentation gives them (`Name.text`, `List.count`).
* **Lists and arrays are plural, their elements singular:** `Scores`, and
  `for Score in Scores`, so a line says whether it means one or all.
  Lookups too, named for their values: `Ages`, `for Name, Age in Ages`.
* **US spelling,** as most programming languages and libraries use it:
  `set_color`, `center`, `gray`, `initialize`. Cabby's own libraries and
  documentation are written that way, so a program's names match the
  names it calls.
* **Say what it holds:** `TotalPrice` rather than `Tp` or `Data`. The usual
  short forms are fine: `Max`, `Min`, `Id`, `Url`, `Count`.

## Spacing and lines

* **Spaces** go around binary operators (`A + B`, `X == 3`, `Total += 1`)
  and after commas. None inside parentheses, none around `.`, and none
  between a unary operator and its value (`-X`, `!Done`, `~Mask`).
* **Lines** stay under 100 columns, counting a tab as 8. To break a long
  statement, break after a comma inside parentheses; the next line goes one
  tab further in (Cabby and the Vim plugin both expect this).
* **Blank lines:** one between functions and between methods; inside a
  function, one between steps of the work. None straight after a `{` or
  before a `}`.
* **One declaration per line**, except a few closely related variables of
  the same type: `int X = 0, Y = 0`.

## The order of a file

1. A comment saying what the file is for.
2. `use` lines: libraries first (`use standard`), then your own files
   (`use local shapes`).
3. Constants and global variables.
4. Types: `enum`, `struct` and `union`, then `object`.
5. Functions, with `main` last.

A header (`.hby`) holds only what other files need: types, constants and
prototypes. Bodies go in the matching `.cby`, and anything only that file
uses is `static`.

## Objects

* **A struct for a value, an object for a thing.** Plain data (a point, a
  color, a record in a file, a million of anything) is a struct: small,
  fast, copied, and ready for C and JSON. Something with a job and a life
  of its own (a window, a counter, a running game's player) is an object.
  Start with a struct, and make it an object when it needs methods, a
  destructor or handlers.
* **Inside an object:** fields first, then static methods that make one
  (`static counter starting_at ...`), then the other methods, then the
  destructor last.
* **Mark every method that doesn't change its object `const`.** It says so
  to the reader, and lets the method be used through a read-only borrow.
* **Parameters that only read** a string, struct or object are `const T &`.
  Numbers, `bool` and `char` are small: take them by value.
* **Name the fields** in an initializer once there are more than two:
  `dog Rex = {.Name = "Rex", .Legs = 4, .Breed = "collie"}`. It still
  works if the fields are reordered.
* **Use `self` only when needed:** to pass the object on, or when a
  parameter has the same name as a field (`self.Label = Label`).

## Calls

* **At the start of a line, no parentheses:** `greet`,
  `say_hello Name`, `Clicks.add 10`.
* **A call that is the whole value of a declaration, assignment or
  `return`, no parentheses,** however many arguments it has: its arguments
  run to the end of the line. `int Result = volume 2, 3, 4`,
  `Total = add Price, Tax`, `return area Width, Height`.
* **A call that is the whole condition of an `if` or `while`, or what a
  `for` goes through, no parentheses:** `if Counts.contains Word {`,
  `while !Words.contains "stop" {`, `for Word in Line.split " " {`. Its
  arguments run to the `{`. Joined to anything else (`&&`, a comparison), it is part
  of something bigger: `if Counts.contains(Word) && Counts[Word] > 1 {`.
* **A call that is part of something bigger, parentheses around its
  arguments:** in a calculation (`int Sum = add(1, 2) + 3`,
  `return n * factorial(n - 1)`) or as one of several arguments
  (`println "area:", area(W, H), "m2"`). The other form, `(add 1, 2) + 3`,
  works the same; pick this one so programs look alike.
* **No empty parentheses.** A function or method with no parameters is
  called by its name: `Total = Clicks.value`, `int Roll = rand % 6 + 1`,
  not `Clicks.value()` or `rand()`.

## Control flow

* **Go through a list with `for X in L`:** `for Score in Scores`. Count
  with `for i in` when the position matters (`for i in Scores.length`, to
  change elements) and for numbers (`for i in 1 to 10`). Keep C's
  three-part `for` for loops that don't count. Go through a lookup with
  `for Key, Value in L`, naming both for what they are.
* **Prefer leaving early to nesting deeply:** `continue`, `break` and an
  early `return` keep the main path of a function at the left.
* **`goto`** only to leave several loops at once.
* **In a `switch`,** end every case with `break` unless it falls through on
  purpose, and then say so: `// falls through`.
* **Conditions say what they mean:** `if Count > 0`, rather than relying on
  a number being true (`if Count`).
* **Conditions in words are Cabby's own style:** `is`, `is not`, `and`,
  `or` and `not`, as in `if Name is "Ada" and not Locked`. They read the
  way you'd say them, and `is` can't be mistyped as an assignment the way
  `==` can. The symbols (`==`, `!=`, `&&`, `||`, `!`) are fine too,
  especially in code that stays close to C; but keep to one or the other
  in a file, rather than mixing them. Words take the place of
  programmers' symbols only: `<`, `>`, `<=` and `>=` are the math
  everyone knows, and stay as they are (`if Age >= 18 and not Banned`).
* **Use `until` when the loop waits for something to become true:**
  `until end_of_input`, `do { ... } until Guess is Secret`, rather than
  `while !end_of_input`. Keep `while` when the condition says why to go
  on.

## Comments

* **`//` for comments.** Keep `/* ... */` for the rare long note.
* **Full sentences**, with a capital and a period. A short note at the
  end of a line can be a phrase: `// in tenths of a second`.
* **A comment above each function or method** whose purpose isn't obvious
  from its name, saying what it does (not how).
* **Comment why, not what.** `Total += 1` needs no comment; why the total
  starts at 1 might.
* **Names in comments go in backticks:** `` `Total` ``, so they stand out
  and stay right when the name changes.

## A whole program in this style

<!-- run -->
```cabby
// shop.cby: works out the price of a shopping basket.

use standard

const double TAX_RATE = 0.1

enum category {
	FOOD
	DRINK
	OTHER
}

struct item {
	str Name
	double Price
	int Quantity
	category Kind
}

object basket {
	item Items[3]
	int Count

	void add const str &Name, double Price, int Quantity, category Kind {
		Items[Count] = item{.Name = Name.copy, .Price = Price, .Quantity = Quantity, .Kind = Kind}
		Count++
	}

	// Food is not taxed; everything else is.
	double total const {
		double Total = 0.0
		for i in Count {
			double Cost = Items[i].Price * Items[i].Quantity
			if Items[i].Kind is not FOOD {
				Cost += Cost * TAX_RATE
			}
			Total += Cost
		}
		return Total
	}

	bool is_empty const {
		return Count is 0
	}
}

int main {
	basket Shopping
	Shopping.add "Bread", 3.5, 2, FOOD
	Shopping.add "Juice", 4.0, 1, DRINK
	if Shopping.is_empty {
		println "nothing to pay"
		return 0
	}
	printf "Total: $%.2f\n", Shopping.total
}
```

```text
Total: $11.40
```

## Checking

```sh
cabby --style prog.cby
```

lists each name that doesn't follow this guide, with the spelling it should
have, and each call written with parentheses it doesn't need (see
[Calls](#calls)), written without them, and exits with status 1 if there
are any. It checks names and calls only (not spacing), and only in your own
files: libraries and C functions are left alone. Cabby's
own tutorials, manual examples and libraries are checked with it as part of
`make test`.
