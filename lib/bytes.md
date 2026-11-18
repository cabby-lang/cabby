# bytes — files that aren't text

```cabby
use bytes          // or use files, which brings it
```

## Bytes in brief

Everything a computer keeps is **bytes**: numbers from 0 to 255. A text
file's bytes are its letters (`A` is 65, `a` is 97, a line end is 10). A
picture, a sound, a zip file or a game's save file are bytes too, but
they aren't letters, so reading them as lines of text doesn't work. A
`bytes` value holds them as what they are: numbers.

```cabby
bytes Data = files.read_bytes "photo.png"
println Data.length         // 48213 bytes
println Data[0]             // 137
```

Each kind of file has a **format**: a description of what each byte
means, written down by whoever made the format. For example, a PNG
picture always starts with the same 8 bytes, and its width is kept in
bytes 16 to 19.

### Hexadecimal

Bytes are usually shown in **hexadecimal** ("hex"), the numbers written
with 16 digits instead of 10: `0` to `9`, then `a` (10) to `f` (15). Two hex
digits are exactly one byte, from `00` (0) to `ff` (255), so every byte
takes the same room, and hex is what file formats, hex viewers and this
library all use:

| Decimal | 0 | 9 | 10 | 15 | 16 | 66 | 128 | 255 |
|---|---|---|---|---|---|---|---|---|
| Hex | `00` | `09` | `0a` | `0f` | `10` | `42` | `80` | `ff` |

To read a hex byte: the first digit counts 16s, the second 1s. `42` is
4 × 16 + 2 = 66; `ff` is 15 × 16 + 15 = 255.

`println` shows bytes in hex; `B[i]` gives one as an ordinary number.

<!-- run -->
```cabby
use bytes

int main {
	bytes Data = "Hi!"
	Data.add 0
	Data.add 255
	println Data
	println Data.length, Data[0], (char)Data[0]
	println bytes.from_hex "48 69"
}
```

```text
48 69 21 00 ff
5 72 H
48 69
```

## Making and changing bytes

| Write | What it does |
|---|---|
| `bytes B` | no bytes yet |
| `bytes B = "text"` | a `str`'s bytes (a `str` becomes bytes by itself where bytes are wanted) |
| `bytes.from_hex "89 50 4e 47"` | bytes written in hex, as they print (the spaces can be left out) |
| `bytes.filled Count, Value` | `Count` bytes, each `Value` |
| `B[i]` | byte `i` (from 0), as an `int` from 0 to 255 |
| `B[i] = 200` | changes one; a number outside 0 to 255 is a panic |
| `B.add 10` | one more byte on the end |
| `B.add_bytes Other` | all of `Other`'s bytes on the end; text works too: `B.add_bytes "more text"` |
| `B.length` | how many bytes |
| `B.part Start, Count` | `Count` bytes from `Start`, as bytes of their own |
| `B.copy` | a copy of its own |
| `for Byte in B` | each byte, as an `int` |

## Looking at bytes

| Write | What it does |
|---|---|
| `B.text` | hex, a space between bytes: `89 50 4e 47` (also what `println B` shows) |
| `B.dump` | a **hex dump**: 16 bytes a line, as below |
| `B.str` | the bytes as a `str`: back to text |
| `B.same_as Other` | `true` if they're the same bytes |
| `B.starts_with Other` | `true` if `B` begins with `Other`'s bytes: how to tell what kind of file it is |
| `B.find Other` | where `Other`'s bytes first are, or -1 |
| `B.find_from Other, From` | the same, looking from byte `From` on |

A hex dump is how programmers look at a file's bytes:

```text
00000000  89 50 4e 47 0d 0a 1a 0a  00 00 00 0d 49 48 44 52  |.PNG........IHDR|
00000010  00 00 00 03 00 00 00 01  08 02 00 00 00 94 82 83  |................|
```

On the left is where each line starts (in hex: `10` is byte 16), then
16 bytes, and on the right the bytes that are letters (a `.` for those
that aren't). Letters show where a format put words, such as `PNG` and
`IHDR` here.

## Numbers inside bytes

One byte only holds 0 to 255, so bigger numbers take several bytes in a
row, just as C keeps them: a `short` takes 2, an `int` 4, a `long` or a
`double` 8.

| Write | Reads or writes |
|---|---|
| `B.short_at At`, `B.int_at At`, `B.long_at At`, `B.double_at At` | the number at byte `At` |
| `B.unsigned_short_at At`, `B.unsigned_int_at At` | the same bytes counted from 0 up (see below) |
| `B.set_short_at At, N`, `set_int_at`, `set_long_at`, `set_double_at` | writes over the bytes at `At` |
| `B.add_short N`, `add_int`, `add_long`, `add_double` | adds the number's bytes on the end |
| the same names ending `_big` | the same, biggest byte first (see below) |

### Byte order

640 is too big for one byte. In bytes, it's 2 × 256 + 128: a byte
holding **2** for the 256s, and a byte holding **128** (`80` in hex) for
what's left, like the tens and ones of an ordinary number. As an `int`,
it takes 4 bytes: `00 00 02 80`.

The question is which order to put those bytes in, and there are two
answers:

* **Smallest byte first**, called *little-endian*: `80 02 00 00`. This is
  how every ordinary PC and phone keeps numbers in memory, so most file
  formats use it too: BMP pictures, WAV sounds, zip files.
* **Biggest byte first**, called *big-endian*: `00 00 02 80`. This is how
  people write numbers, and it's what networks send, and some formats
  use: PNG pictures, JPEG, MIDI music.

(The names come from *Gulliver's Travels*, where two countries go to war
over which end of a boiled egg to break first.)

Neither is better; what matters is reading a number the same way it was
written, and a format's description always says which it uses. The plain
methods (`int_at`, `add_int`) use smallest byte first; the ones ending in
`_big` use biggest byte first.

<!-- run -->
```cabby
use bytes

int main {
	bytes Little
	Little.add_int 640
	bytes Big
	Big.add_int_big 640
	println Little, "and", Big
	println Little.int_at(0), Big.int_at_big(0)
	println Big.int_at 0     // the wrong order: a different number
}
```

```text
80 02 00 00 and 00 00 02 80
640 640
-2147352576
```

Reading with the wrong order doesn't fail: it gives a wrong number.
When a number comes out impossibly big (or below 0), the order is the
first thing to check.

### Signed and unsigned

The same 2 bytes `ff ff` can mean -1 (as a `short`, which holds -32768 to
32767) or 65535 (counted from 0 up, as an `unsigned short`). Formats say
which; `short_at` and `int_at` give the first, `unsigned_short_at` and
`unsigned_int_at` the second. When writing, either range is fine:
`add_short 65535` and `add_short -1` write the same bytes. A number that
fits neither is a panic.

## Files and downloads

| Write | What it does |
|---|---|
| `files.read_bytes Path` | a whole file, as bytes |
| `files.write_bytes Path, B` | the file becomes `B`'s bytes (made if it's new) |
| `files.append_bytes Path, B` | `B`'s bytes on the end of the file |
| `F.read_bytes Count`, `F.write_bytes B` | in a file that's open (see [files](files.md#bytes-in-an-open-file)) |
| `networks.download_bytes Url` | what's at an address, as bytes |

## When something goes wrong

A byte past the end, a number that doesn't fit in a byte, and a number
read from past the end are each a panic that says what and where:

```text
panic at prog.cby:6:10: there's no byte [5] to read: there are 3 (from [0] to [2])
panic at prog.cby:7:9: a byte is a number from 0 to 255, not 300
panic at prog.cby:8:10: an int (4 bytes) at byte [20] goes past the end: there are 22 bytes
```

Check `B.length` first when a file might be shorter than its format
says.

## Bytes and str

A `str` can hold any bytes too, and `B.str` and `bytes B = Text` turn one
into the other. The difference is what they're for: a `str` prints and
works as text, while `bytes` prints as hex, gives each byte as a number,
and reads the numbers inside.
