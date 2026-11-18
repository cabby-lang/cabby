# strings — C's text functions

```cabby
use strings
```

Three functions from the C library that work on text. Cabby's own `str`
already does all of this and more, without a header: `s.length`, `==`,
`<` and `>` for dictionary order, `s.starts_with`, `s.find`, slices and
the rest (manual chapter 12). Prefer those; these are here for code
following C examples.

| Declaration | What it does |
|---|---|
| `extern unsigned long strlen str s` | The number of bytes in `s`, up to its first zero byte. `s.length` is the same and faster. |
| `extern int strcmp str a, str b` | Compares two texts in dictionary order: below 0 if `a` comes first, 0 if they are equal, above 0 if `b` comes first. |
| `extern int strncmp str a, str b, unsigned long n` | The same, looking at no more than the first `n` bytes. |

## Putting text in order: strcmp

`strcmp` compares byte by byte, as Cabby's `<` and `>` on strings do, so
capitals come before small letters ("Zebra" before "apple") and digits
before both. In Cabby, `A < B` says the same as `strcmp(A, B) < 0`.

<!-- run -->
```cabby
use strings

int main {
	println strcmp("apple", "banana") < 0, strcmp("pear", "pear") == 0
	println strcmp("Zebra", "apple") < 0
	println strncmp("cabbage", "cabby", 3) == 0, strlen("Cabby")
}
```

```text
true true
true
true 5
```

Only the sign of `strcmp`'s answer means anything; the number itself
differs between C libraries.

## Starts with

Cabby's `s.starts_with` does this directly. With C's functions, `strncmp`
with the length of the beginning checks whether a text starts with it:

<!-- run -->
```cabby
use strings

bool starts_with const str &s, const str &Start {
	return strncmp(s, Start, (unsigned long)Start.length) == 0
}

int main {
	println starts_with("cabby.hby", "cabby"), starts_with("tabby", "cab")
}
```

```text
true false
```

## Cabby's own

Splitting a string into a list and joining a list into a string are
Cabby's own: `"a,b,c".split ","` and `Names.join ", "` (manual chapters
12 and 15).

## See also

The manual's chapter 12, "Strings", and chapter 27, "Calling C".
