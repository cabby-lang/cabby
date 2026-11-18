# patterns — regular expressions (regex)

```cabby
use patterns
```

A pattern describes what some text looks like: "four digits, a dash, two
digits, a dash, two digits" is a date, written `\d{4}-\d{2}-\d{2}`. These
are *regular expressions*, "regex" for short, as other languages and
text editors have them. With one you can ask whether text is like it,
find every piece of text that is, take it apart, or replace it.

<!-- run -->
```cabby
use patterns

int main {
	patterns.pattern Date = patterns.make "(\\d{4})-(\\d{2})-(\\d{2})"
	str Notes = "Lessons on 2026-11-10 and 2026-11-17."
	println Date.find_all(Notes)
	println Date.parts(Notes)
	println Date.replace_all(Notes, "\\3/\\2/\\1")
	println Date.matches("2026-11-10"), Date.matches("on 2026-11-10")
}
```

```text
[2026-11-10, 2026-11-17]
[2026, 11, 10]
Lessons on 10/11/2026 and 17/11/2026.
true false
```

In a Cabby string, `\\` is one `\`, so the pattern `\d` is written
`"\\d"`.

* [Making a pattern](#making-a-pattern)
* [What can go in a pattern](#what-can-go-in-a-pattern)
* [patterns.pattern](#patternspattern)
* [Letters, not bytes](#letters-not-bytes)
* [How it works](#how-it-works)

## Making a pattern

| Function | What it does |
|---|---|
| `patterns.pattern patterns.make const str &Pattern` | A pattern, checked as it's made: one that can't be used is a panic that says why (`the pattern '(unclosed' can't be used: unmatched ( or \(`). |
| `patterns.pattern patterns.make_ignoring_case const str &Pattern` | The same, but big and small letters alike: `"cabby"` finds `"Cabby"` and `"CABBY"`. |

Make a pattern once, outside a loop, and use it as often as you like.

## What can go in a pattern

| Write | Matches |
|---|---|
| `abc` | those letters (most letters just match themselves) |
| `.` | any one letter |
| `\d`, `\w`, `\s` | a digit; a letter, digit or `_`; a space, tab or line end |
| `\D`, `\W`, `\S` | anything else |
| `[aeiou]` | one of those; `[a-z]` from a to z; `[^0-9]` anything but a digit |
| `[\d.,]` | the shorthands work inside `[ ]` too, as does `\.` for a dot |
| `[[:alpha:]]`, `[[:upper:]]` ... | a letter, a capital ...: in any alphabet (see below) |
| `x*`, `x+`, `x?` | none or more `x`; one or more; none or one |
| `x{3}`, `x{2,4}`, `x{2,}` | three; two to four; two or more |
| `cat\|dog` | either one |
| `( )` | a part: kept, for `parts` and `\1` in a replacement, and to group (`(ab)+`) |
| `^`, `$` | the start of the text; its end |
| `\.`, `\(`, `\\` ... | that character itself, not its special meaning |
| `\t`, `\n` | a tab; a line end |
| `\b` | the edge of a word: between a letter, digit or `_` and something else (or the start or end) |
| `\B` | anywhere that isn't the edge of a word |
| `x*?`, `x+?`, `x??`, `x{2,4}?` | the same repeats, taking as little as they can |
| `\1` to `\9` | what part 1 (to 9) matched, again: `(\w)\1` is a doubled letter |
| `(?: )` | a group that isn't a part: `(?:ab)+`, with nothing kept |
| `x(?=y)` | `x`, if `y` comes next (`y` isn't part of what's found) |
| `x(?!y)` | `x`, if `y` doesn't come next |
| `(?<=y)x` | `x`, if `y` comes just before it (`y` isn't part of what's found) |
| `(?<!y)x` | `x`, if `y` doesn't come just before it |
| `(?<name> )` | a part with a name, for `part` and `\k<name>` (it's numbered too, as any part is) |
| `\k<name>` | what the part called `name` matched, again |

A repeat takes as much as it can: `a+` in `"baaad"` finds `"aaa"`, and
`<.+>` in `"<a><b>"` finds all of it. Add a `?` and it takes as little as
it can: `<.+?>` finds `"<a>"`, then `"<b>"`.

Where there are alternatives, the first that works is the one taken:
`cat|category` finds `"cat"` in `"category"` (put the longer first to
prefer it). `matches` still asks about the whole text, so
`patterns.make("a|ab").matches "ab"` is `true`.

<!-- run -->
```cabby
use patterns

int main {
	println patterns.make("\\bcat\\b").find_all("cat concat cat's bobcat")
	println patterns.make("<.+?>").find_all("<b>bold</b>")
	println patterns.make("\\b(\\w+) \\1\\b").find("it was the the best")
	println patterns.make("\\B(?=(\\d{3})+$)").replace_all("1234567", ",")
}
```

```text
[cat, cat]
[<b>, </b>]
the the
1,234,567
```

The last one puts a comma wherever three, six or nine digits (and so on)
come next, up to the end: where a thousands comma goes.

Looking behind, and parts with names:

<!-- run -->
```cabby
use patterns

int main {
	println patterns.make("(?<=\\$)\\d+").find_all("$12, €30, $7")
	patterns.pattern Date = patterns.make "(?<year>\\d{4})-(?<month>\\d\\d)-(?<day>\\d\\d)"
	println Date.part("year", "Due 2026-11-16."), Date.part_names
	println Date.replace("2026-11-16", "\\k<day>/\\k<month>/\\k<year>")
}
```

```text
[12, 7]
2026 [year, month, day]
16/11/2026
```

What looks behind can be any pattern (`(?<=\\$|€)`, `(?<=\\d+ )`): it's
tried at each place before, nearest first, until it ends just where the
match begins.

Not here: patterns across several lines (`^` and `$` are only ever the
start and end of all of the text).

## patterns.pattern

| Member | What it does |
|---|---|
| `bool matches const str &Text const` | Whether the whole text is like the pattern. |
| `bool found_in const str &Text const` | Whether it's somewhere in the text. |
| `long position const str &Text const` | Where it first is (counting bytes from 0, as `Text[I]` does), or -1. |
| `str find const str &Text const` | The first piece of the text like it (`""` if there's none). |
| `list str find_all const str &Text const` | Every piece like it, in order. |
| `list str parts const str &Text const` | What its `( )` parts matched, the first time it's found: `{}` if it isn't there. A part that matched nothing is `""`. |
| `str part const str &Name, const str &Text const` | What the part named `Name` matched, the first time it's found: `""` if it isn't there, a panic if no part has that name. |
| `bool has_part const str &Name const` | Whether it has a part with that name. |
| `list str part_names const` | Its parts' names, in order (`""` for a part without one). |
| `str replace const str &Text, const str &With const` | The text with the first piece like it replaced by `With`. In `With`, `\1` to `\9` are what its parts matched, `\k<name>` what a named part matched, `\0` all of it, and `\\` a `\`. |
| `str replace_all const str &Text, const str &With const` | The same, for every piece. |
| `list str split const str &Text const` | The text cut into pieces wherever it's found (what's found goes): `patterns.make("[,;\\s]+").split "a, b;c"` is `{"a", "b", "c"}`. |

A pattern that can match nothing (`a*`) finds the empty text between
letters too, as other languages' regular expressions do:
`find_all "baac"` is `{"", "aa", "", ""}`.

## Letters, not bytes

Text is matched as UTF-8, a letter at a time: `.` is one letter, `é` or
`φ` as much as `e`, and `\w` and `[[:alpha:]]` take in Greek, accents and
every other alphabet. `patterns.make("\\w+").find_all "Χαῖρε, ὦ φίλε"`
is `{"Χαῖρε", "ὦ", "φίλε"}`. Ignoring case does too: `"σ"` finds `"Σ"`.

## How it works

patterns matches by itself, the same way on Linux and on a Mac: a
pattern is read into steps once, when it's made, and matched by trying
each way in turn, going back to try the next when one doesn't work out,
as Perl's, Python's and JavaScript's regular expressions do. A repeat of
plain letters (`\w+`, `.*`, `(ab)+`) is quick however long the text is.

A pattern can be written that tries a great many ways before it gives up:
`(a+)+b` on a long line of a's, with no b, has more ways to split up the
a's than could be tried in a lifetime. Rather than run for ever, that's a
panic saying the pattern tried too many ways. Write it more simply
(`a+b`). The C side is `runtime/patterns.c`.
