# json — reading and writing JSON

```cabby
use json
```

JSON is the most common way programs pass data to each other as text: web
services answer in it, and many programs keep their settings in it. It has
six kinds of value, nested to any depth:

| JSON | Example | In Cabby |
|---|---|---|
| text | `"Sydney"` | `.str` |
| a number | `23.5`, `26` | `.double`, `.long` |
| true or false | `true` | `.bool` |
| null: nothing here | `null` | `.is_null` |
| a list | `[1, 2, 3]` | `J[0]`, `.length`, `for X in J` |
| names and values | `{"city": "Sydney", "high": 26}` | `J["city"]`, `.keys`, `.has` |

## Why "lookup", not "object"

JSON calls its `{ }` kind an *object*. In Cabby an object is something
else (a type with fields and methods, chapter 22), and the `{ }` kind is
exactly what Cabby calls a **lookup**: values found by name. So Cabby
calls it a lookup: `json.lookup` makes an empty one, and `.is_lookup` asks
whether a value is one. Wherever another language's JSON documentation
says "object", read "lookup".

## A json value

A `json` holds any JSON value. Its type isn't known until the text is
read, so you ask for what you expect, and Cabby checks:

<!-- run -->
```cabby
use json

int main {
	str Text = "{\"city\": \"Sydney\", \"now\": {\"temperature\": 23.5}, "
	Text += "\"days\": [{\"day\": \"Tuesday\", \"high\": 26}, {\"day\": \"Wednesday\"}]}"
	json Weather = json.read Text
	println Weather["city"].str
	println Weather["now"]["temperature"].double
	for Day in Weather["days"] {
		println Day["day"].str, Day["high"].long_or(0)
	}
}
```

```text
Sydney
23.5
Tuesday 26
Wednesday 0
```

| Write | What it does |
|---|---|
| `json.read Text` | the JSON in `Text`; text that isn't JSON is a panic saying where ("line 2, column 4: ...") |
| `json.can_read Text` | `true` if `json.read` would work |
| `J["name"]` | the value under that name, in a lookup |
| `J[i]` | item `i` (from 0), in a list |
| `.str`, `.double`, `.long`, `.bool` | the value as a Cabby value |
| `.str_or D`, `.double_or D`, `.long_or D`, `.bool_or D` | the same, or `D` if it isn't there or isn't that kind |
| `.is_str`, `.is_number`, `.is_bool`, `.is_null`, `.is_list`, `.is_lookup` | what kind it is |
| `.is_missing` | `true` for a name that isn't there, or an item past the end |
| `.has "name"` | whether a lookup has that name |
| `.length` | a list's items, or a lookup's names |
| `.keys` | a lookup's names, in order (a `list str`) |
| `for X in J` | each item of a list |

`J["a"]["b"]` and `J["days"][0]["high"]` go in as far as needed.

### Panics, unless you say otherwise

Asking for the wrong kind, or for a name that isn't there, is a panic that
says what was found and where in the JSON:

```text
panic at prog.cby:9:20: 'days[0].high' is text ("26"), not a number
panic at prog.cby:9:20: 'days[1]' has no "high"
```

So a program never quietly carries on with a wrong value. When something
really might be missing (a field only some answers have), use the `_or`
forms to give a default, or check first with `.has`, `.is_missing` or
`.is_number`. `.long` panics on `2.5` too (use `.double`).

## Writing JSON

<!-- run -->
```cabby
use json

int main {
	json Settings = json.lookup
	Settings["name"] = "Ada"
	Settings["volume"] = 7
	Settings["sounds"] = true
	json Recent = json.list
	Recent.add "notes.txt"
	Recent.add "plans.txt"
	Settings["recent"] = Recent           // moved in, like any owned value
	println Settings
	println Settings.pretty
}
```

```text
{"name":"Ada","volume":7,"sounds":true,"recent":["notes.txt","plans.txt"]}
{
  "name": "Ada",
  "volume": 7,
  "sounds": true,
  "recent": [
    "notes.txt",
    "plans.txt"
  ]
}
```

| Write | What it does |
|---|---|
| `json.lookup`, `json.list`, `json.null` | an empty lookup `{}`, an empty list `[]`, and null |
| `J["name"] = V` | sets a name in a lookup (adding it if new) |
| `J[i] = V` | changes an item of a list |
| `J.add V` | adds an item to the end of a list |
| `J.remove "name"` | takes a name out of a lookup |
| `.text` | the JSON on one line, for sending (also what `println J` shows) |
| `.pretty` | indented over several lines, for files people read |
| `.copy` | a copy of its own |

`V` can be a `json`, or a `str`, a number or a `bool`, which become JSON
by themselves wherever a `json` is wanted (`json Name = "Ada"` works too).
A number is written as briefly as it reads back exactly: `10.7`, `7`.

## Parts of a value

A value taken out of another, `json Day = Weather["days"][0]`, is *part*
of it, not a copy: changing `Day["high"]` changes `Weather` too, and the
part stays good for as long as either is around. `.copy` makes one of its
own. Giving a `json` to another (`Settings["recent"] = Recent`) moves it,
as with every owned value in Cabby, so it can't then be used by its old
name.

## Files

```cabby
json Settings = json.read files.read "settings.json"
files.write "settings.json", Settings.pretty
```

## Structs, to JSON and back

`json.from S` makes JSON from a struct's fields, and `J.fill S` fills a
struct's fields in from JSON, by their names. It's the easiest way to save
data to a file and load it again, or to read a web service's answer into
a struct.

<!-- run -->
```cabby
use json

enum level {
	EASY
	HARD
}

struct player {
	str Name
	int HighScore
	level Level
	list str Friends
}

int main {
	player P = {"Ada", 1200, HARD}
	P.Friends.add "Tom"
	str Saved = json.from(P).text
	println Saved

	player Loaded
	Loaded.HighScore = 100        // kept if the JSON doesn't have it
	json J = json.read "{\"name\": \"Grace\", \"level\": \"EASY\"}"
	J.fill Loaded
	println Loaded.Name, Loaded.HighScore, Loaded.Level == EASY
}
```

```text
{"name":"Ada","high_score":1200,"level":"HARD","friends":["Tom"]}
Grace 100 true
```

* **Names.** A field is written in JSON in small letters with `_` between
  words, the way most JSON is: `HighScore` is `"high_score"`. Reading
  takes `"high_score"`, `"highScore"` (JavaScript's way) or
  `"HighScore"`.
* **What can go in.** Text, every kind of number (and kinds, such as
  `meters`), `bool`, `char` (as one-letter text), enums (by their names:
  `"HARD"`), other structs, `json` fields (any JSON, kept as it is), and
  `list`s, `lookup str ...`s and arrays of those. A struct with anything
  else (an object, a file, an address) is an error that names the field.
* **Missing names.** `.fill` changes only the fields the JSON has (and
  leaves those it has as `null` alone), so whatever a struct held before
  is its default. Names the JSON has that the struct doesn't are passed
  over, so a program can read a web service's answer into a struct of
  just the parts it wants.
* **Wrong kinds.** A value of the wrong kind, a number that doesn't fit
  (300 for an `unsigned char`), or an enum name that isn't one is a panic
  that says where: `'level' is "MEDIUM", not a level (EASY or HARD)`.
* A struct's `list player` field is filled in by itself. For a list of
  structs on its own, fill them one at a time:

  ```cabby
  list player Players
  for Item in J["players"] {
  	player P
  	Item.fill P
  	Players.add P
  }
  ```

Saving and loading, then, are one line each:

```cabby
files.write "save.json", json.from(P).pretty
json.read(files.read("save.json")).fill P
```
