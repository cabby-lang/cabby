# xml — reading XML documents, and making them

```cabby
use xml
```

## XML in brief

XML is written like HTML (see [html](html.md)): elements made of tags,
with attributes and things inside. The difference is that **you choose
the tag names**, to describe your own data:

```xml
<?xml version="1.0"?>
<library owner="Sam">
  <book lang="en">
    <title>The Hobbit</title>
    <year>1937</year>
  </book>
  <book lang="de">
    <title>Momo</title>
    <year>1973</year>
  </book>
</library>
```

It's used for news feeds (RSS), SVG pictures, the insides of office
documents, many programs' settings, and older web services.

XML is strict where HTML is forgiving:

* every element is closed: `<title>...</title>`, or `<empty/>` for one
  with nothing inside;
* attribute values are always in quotes;
* capitals matter: `<Book>` and `<book>` are different;
* there is exactly one **top element**, holding everything else
  (`library` above);
* `&` and `<` in text are written `&amp;` and `&lt;`.

`<?xml ...?>` at the top, and `<!-- comments -->`, can be there or not.
`<![CDATA[ ... ]]>` holds text exactly as it is, `<` and `&` included.

## Reading a document

| Write | What it does |
|---|---|
| `xml.read Text` | the document in `Text`, as an `xml`: its top element |
| `xml.can_read Text` | `true` if `xml.read` would work |

Everything else is an element's, the same as in [html](html.md#reading-a-page):
`find`, `find_all`, `children`, `parent`, `tag`, `text`, `raw_text`,
`attribute`, `attribute_or`, `has_attribute`, `attributes`, `is_missing`.

<!-- run -->
```cabby
use xml

int main {
	str Text = "<library owner='Sam'><book lang='en'><title>The Hobbit</title><year>1937</year></book>"
	Text += "<book lang='de'><title>Momo</title><year>1973</year></book></library>"
	xml Library = xml.read Text
	println Library.tag, Library.attribute "owner"
	for Book in Library.find_all "book" {
		println Book.find("title"), Book.find("year").text.to_int, Book.attribute "lang"
	}
	println Library.find "book[lang=de] title"
}
```

```text
library Sam
The Hobbit 1937 en
Momo 1973 de
Momo
```

Elements are found with **CSS selectors**, the same as in HTML (the
[html](html.md#finding-css-selectors) page has the full table): a tag's name
(`"book"`), inside (`"book title"`), directly inside (`"library > book"`),
attributes (`"book[lang=de]"`), and pseudo-classes (`"book:first-child"`,
`"book:has(> review)"`). In XML, capitals matter in tags and attributes,
as they do in the document, and a name may have a `:` in it
(`"item > dc:creator"`): one that isn't a pseudo-class is part of the
name.

A name with a prefix, such as `dc:creator` in news feeds (from another
"namespace" of names), is found as it's written: `Story.find "dc:creator"`.

## When it's wrong

XML with a mistake is a panic that says where, and what was expected:

```text
panic at prog.cby:4:12: this isn't XML: line 3, column 12: expected </item> (opened at line 3), found </itm>
```

When the XML might not be right (a file someone else wrote), check first
with `xml.can_read`. Something that isn't there works as in
[html](html.md#when-something-isnt-there): `find` gives a missing element
(a panic when used; check with `.is_missing`), `find_all` an empty list,
and `attribute_or` a default.

## Making a document

| Write | What it does |
|---|---|
| `xml.element "note"` | a new, empty element |
| `E.add_text`, `E.add`, `E.set_attribute`, `E.remove_attribute`, `E.remove`, `E.clear`, `E.copy` | as in [html](html.md#making-a-page) |
| `E.markup` | written out as XML, on one line |
| `E.pretty` | written out with each element on its own line, indented |

<!-- run -->
```cabby
use xml

int main {
	element Note = xml.element "note"
	Note.set_attribute "id", "7"
	element To = xml.element "to"
	To.add_text "Ada & Alan"
	Note.add To
	Note.add xml.element "read"
	println Note.pretty
}
```

```text
<note id="7">
  <to>Ada &amp; Alan</to>
  <read/>
</note>
```

An element with nothing inside is written `<read/>`. Text is always
written safely (`&amp;`, `&lt;`), as in HTML.

To save a document with the usual first line:

```cabby
files.write "notes.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" + Note.pretty
```
