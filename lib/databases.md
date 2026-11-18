# databases — SQLite and PostgreSQL databases

```cabby
use databases
```

A database keeps records in tables, as a spreadsheet does, and answers
questions about them. Each one is a single file, with no server to run:
SQLite, which Firefox, phones and much of the desktop keep their data in.
What's asked of it is SQL, the language every database speaks. A
[PostgreSQL](#postgresql) server's database is asked the same things, in
the same way.

<!-- run -->
```cabby
use databases
use files

int main {
	databases.database Shop = databases.open "shop.db"      // made if it's new
	Shop.run "CREATE TABLE IF NOT EXISTS items (name TEXT, price REAL)"
	Shop.run_with "INSERT INTO items VALUES (?, ?)", {"bread", "4.50"}
	Shop.run_with "INSERT INTO items VALUES (?, ?)", {"milk", "2.10"}
	for Row in Shop.rows "SELECT name, price FROM items ORDER BY price" {
		println Row["name"], Row["price"]
	}
	println Shop.value("SELECT count(*) FROM items"), "items"
	files.delete "shop.db"
}
```

```text
milk 2.1
bread 4.5
2 items
```

## Opening one

| Function | What it does |
|---|---|
| `databases.database databases.open const str &Path` | Opens the database in that file, or makes it if there's no such file. It's closed when the variable is dropped. |
| `databases.database databases.connect const str &Where` | Connects to a PostgreSQL server's database: `"postgresql://tom:secret@localhost/shop"`. See [PostgreSQL](#postgresql). |

## databases.database

| Member | What it does |
|---|---|
| `void run const str &Sql const` | Runs SQL that gives no rows back: making tables, putting rows in, changing them. Several statements, with `;` between them, run in turn. |
| `void run_with const str &Sql, const list str &Values const` | The same, with values for its `?`s, in order. |
| `list lookup str str rows const str &Sql const` | The rows a `SELECT` gives, each a lookup of its columns: `Row["price"]`. |
| `list lookup str str rows_with const str &Sql, const list str &Values const` | The same, with values for its `?`s. |
| `str value const str &Sql const`, `str value_with ...` | Just the first column of the first row: `"SELECT count(*) FROM items"`. `""` if there are no rows. |
| `long changes const` | How many rows the last `INSERT`, `UPDATE` or `DELETE` changed. |
| `long last_id const` | The number SQLite gave the last row put in (its `rowid`, or its `INTEGER PRIMARY KEY`). |
| `void begin const`, `void commit const`, `void rollback const` | Changes that go together: none of them are kept until `commit`, and `rollback` forgets them all. Many inserts between `begin` and `commit` are also far faster. |
| `json records const str &Sql const`, `json records_with ...` | The rows as JSON: a list of lookups, with numbers as numbers and nothing (`NULL`) as `null`. See [Rows into structs](#rows-into-structs). |
| `bytes bytes_value const str &Sql const`, `bytes bytes_value_with ...` | The first column of the first row as bytes: a picture kept in the database, say. None if there are no rows. |
| `databases.statement prepare const str &Sql const` | One statement, made once, to run as many times as it's needed. See [Statements made once](#statements-made-once). |

**Values go in through `?`s.** Write the SQL with a `?` for each value,
and give the values separately: `run_with "... WHERE name = ?",
{Name}`. Never paste a value into the SQL with `+`: a name like
`Grandma's scones` would break it, and text typed by someone could change
what it does ("SQL injection"). With `?`s, a value is only ever a value.

**Rows come back as text.** Each value in a row is a `str`, as
SQLite writes it: `"4.5"`, `"2"`, `""` for none (`NULL`). `.to_double`
and `.to_int` turn them into numbers. Values go in as text too, and
SQLite turns them into numbers for a column declared `REAL` or
`INTEGER`. (`records` gives numbers as numbers, and a statement made once
takes values of any kind: see below.)

SQL that can't be done (a table that isn't there, a word misspelt, the
wrong number of values for the `?`s) is a panic that says what SQLite
said: `the database 'shop.db' can't do "SELECT * FROM nothing": no such
table: nothing`.

If another program is writing to the same database, a change waits for
it, up to five seconds, rather than failing at once.

## Rows into structs

`records` gives the rows as JSON values, so `J.fill` (from the json
library) can put each into a struct, by the names of its fields. Numbers
come back as numbers here, not text:

<!-- run -->
```cabby
use databases

struct food {
	str Name
	double Price
	int Stock
}

int main {
	databases.database Shop = databases.open ":memory:"
	Shop.run "CREATE TABLE foods (name TEXT, price REAL, stock INTEGER)"
	Shop.run "INSERT INTO foods VALUES ('bread', 4.5, 12), ('milk', 2.1, 3)"
	list food Foods
	for Row in Shop.records "SELECT name, price, stock FROM foods ORDER BY price" {
		food F
		Row.fill F
		Foods.add F
	}
	for F in Foods {
		println F.Name, F.Price * F.Stock
	}
}
```

```text
milk 6.300000000000001
bread 54.0
```

A column whose name isn't a field's is left out, and a field with no
column keeps the value it had (as `fill` always does). `":memory:"` is a
database kept in memory alone, gone when it's dropped: good for trying
things out.

## Statements made once

`prepare` makes one statement ready to run as many times as it's needed,
with new values for its `?`s each time: quicker than giving its SQL each
time, for a statement run thousands of times. Its values can be of any
kind, not only text:

| Member | What it does |
|---|---|
| `void add_text const str &Text const` | Text, for its next `?`. |
| `void add_number double Number const` | A number. |
| `void add_whole long Number const` | A whole number, kept exactly. |
| `void add_bytes const bytes &Data const` | Bytes: a picture, a file, anything that isn't text (a `BLOB`). |
| `void add_nothing const` | Nothing: `NULL`. |
| `void run const`, `void run_with const list str &Values const` | Runs it, with the values added since it last ran (or these, as text). |
| `list lookup str str rows const`, `rows_with ...` | The rows it gives, as for a database's `rows`. |
| `json records const` | The rows as JSON, as for a database's `records`. |
| `str value const`, `bytes bytes_value const` | The first column of the first row, as text or as bytes. |

<!-- run -->
```cabby
use databases

int main {
	databases.database Album = databases.open ":memory:"
	Album.run "CREATE TABLE photos (name TEXT, taken INTEGER, picture BLOB)"
	databases.statement Add = Album.prepare "INSERT INTO photos VALUES (?, ?, ?)"
	list str Names = {"cat", "dog", "fish"}
	for Name in Names {
		Add.add_text Name
		Add.add_whole 2026
		Add.add_bytes bytes.from_hex("89 50 4e 47 0d 0a 1a 0a")
		Add.run
	}
	bytes Cat = Album.bytes_value_with "SELECT picture FROM photos WHERE name = ?", {"cat"}
	println Album.value("SELECT count(*) FROM photos"), "photos;", Cat.length, "bytes, a PNG:", Cat.starts_with(bytes.from_hex("89 50 4e 47"))
}
```

```text
3 photos; 8 bytes, a PNG: true
```

The values added go to the statement's next run, and each run starts
afresh. A statement made once is one statement: SQL with a `;` and
another after it is a panic (`run` takes several). It's let go of when
dropped.

## A little SQL

| SQL | Does |
|---|---|
| `CREATE TABLE IF NOT EXISTS foods (id INTEGER PRIMARY KEY, name TEXT UNIQUE, price REAL)` | a table; each row gets its own `id`, and no two can have the same name |
| `INSERT INTO foods (name, price) VALUES (?, ?)` | a row |
| `SELECT name, price FROM foods WHERE price < ? ORDER BY name` | rows, chosen and in order |
| `UPDATE foods SET price = price * 1.1 WHERE name = ?` | changes rows |
| `DELETE FROM foods WHERE name = ?` | takes rows out |
| `SELECT count(*), sum(price), avg(price) FROM foods` | adding up |
| `... JOIN needs ON needs.food = foods.id` | two tables together, matched up |

Tutorial 50 and chapter 50 of the manual make a recipe book with them:
what each recipe costs, and a shopping list.

## PostgreSQL

A PostgreSQL database is a server's, which many programs (and many
computers) can use at once. `databases.connect` gives the same
`databases.database`, asked the same things:

<!-- check -->
```cabby
use databases
use shell

int main {
	databases.database Shop = databases.connect "postgresql://tom@localhost/shop"
	Shop.run "CREATE TABLE IF NOT EXISTS items (id SERIAL PRIMARY KEY, name TEXT, price REAL)"
	Shop.run_with "INSERT INTO items (name, price) VALUES (?, ?)", {"bread", "4.50"}
	println "bread is number", Shop.last_id
	for Row in Shop.rows "SELECT name, price FROM items ORDER BY price" {
		println Row["name"], Row["price"]
	}
}
```

Where it is is said as PostgreSQL's own programs say it: a URL,
`postgresql://user:password@server:port/database` (a password is better
kept out of the program: in `~/.pgpass`, or the `PGPASSWORD` environment
variable), or `"host=localhost dbname=shop user=tom"`. A server on the
same computer, with no host given, is reached through its own socket.

The `?`s are made PostgreSQL's `$1`, `$2` ... (those in quotes and
comments are left as they are), and the values sent apart from the SQL,
as with SQLite. What's different is PostgreSQL's own:

* `last_id` is the number the last row was given by a `SERIAL` (or
  identity) column, in this connection; `INSERT ... RETURNING id`, with
  `value` or `rows`, gives it too, and any other column.
* `true` and `false` come back as `"t"` and `"f"` in `rows` (as `true`
  and `false` in `records`).
* A mistake inside `begin` stops the rest of what's done until
  `rollback`, as PostgreSQL does.

It needs PostgreSQL's client library, libpq (`libpq5` on Debian,
`postgresql-libs` on Arch, `brew install libpq` on a Mac), loaded the
first time a program connects; the server can be anywhere.

## How it works

SQLite comes with every Mac and nearly every Linux system (`libsqlite3-0`
on Debian, `sqlite` on Arch); it's loaded the first time a program opens
a database. If it isn't there, the panic says what to install. The
`sqlite3` command opens the same files, to look inside.

The tests start a PostgreSQL server of their own, where its programs are
installed (`tests/run_postgres_tests.sh`).

## Not yet

MySQL and MariaDB.
