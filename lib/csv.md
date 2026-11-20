# csv — tables in CSV

```cabby
use csv
```

CSV is how spreadsheets (LibreOffice Calc, Excel, Numbers, Google Sheets)
save a table: a row a line, its fields between commas. `use csv` reads
one into rows, or into records by the columns' names, and writes them
back, quoting what needs it.

<!-- run -->
```cabby
use csv
use files

int main {
	files.write "scores.csv", "Name,Score\nAda,95\n\"Hopper, Grace\",88\n"
	long Total = 0
	for P in csv.read_records_file "scores.csv" {
		println P["Name"], P["Score"]
		Total += P["Score"].to_long
	}
	println "total:", Total
	list list str Rows = csv.read_file "scores.csv"
	list str Extra = {"Alan", "91"}
	Rows.add Extra
	csv.write_file "scores.csv", Rows
	print files.read("scores.csv")
	files.delete "scores.csv"
}
```

```text
Ada 95
Hopper, Grace 88
total: 183
Name,Score
Ada,95
"Hopper, Grace",88
Alan,91
```

## Reading

| Call | What it gives |
|---|---|
| `csv.read Text` | the rows, each a `list str` of its fields |
| `csv.read_file Path` | the same, from a file (a panic if it can't be read, as `files.read`) |
| `csv.read_separated Text, Separator` | fields between `';'` or `'\t'` (some countries' spreadsheets, and `.tsv`) |
| `csv.read_records Text` | each row after the first as a `lookup str str`, by the names the first row gives its columns |
| `csv.read_records_file Path` | the same, from a file |
| `csv.columns Text` | the first row's fields: the columns' names |

A record whose row is short has `""` for the columns it hasn't; fields
past the last named column are left out.

## Writing

| Call | What it gives, or does |
|---|---|
| `csv.text Rows` | the rows as CSV text, a line each |
| `csv.text_separated Rows, Separator` | the same, with `';'` or `'\t'` between the fields |
| `csv.write_file Path, Rows` | writes the rows to a file |
| `csv.records_text Columns, Records` | the columns' names, then each record's values in that order (`""` for one it hasn't) |
| `csv.write_records_file Path, Columns, Records` | writes them to a file |
| `csv.field Field, Separator` | one field as CSV writes it: in quotes when it needs them |

## How it reads

As RFC 4180 has it, and as spreadsheets write it:

* A field in double quotes may hold the separator, line ends, and `""`
  for a quote: `"likes ""tea"", and cake"` is `likes "tea", and cake`.
* A row ends at `\n`, `\r\n` (Windows) or `\r`; empty lines are left out.
* The byte order mark Excel puts at the start of a UTF-8 file is left out.
* Every field is text. `.to_long` and `.to_double` turn one into a
  number (check first with `.is_long`, or `.is_number`).

Fields are written in quotes when they hold the separator, a quote, a
line end, or a space at either end, and as they are otherwise. Lines end
with `\n`, which every spreadsheet reads.
