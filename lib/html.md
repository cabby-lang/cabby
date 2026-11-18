# html — reading web pages, and making them

```cabby
use html
```

## HTML in brief

A web page is written in HTML: text with **tags** around it that say what
each part is.

```html
<h1>Cabby Weekly</h1>
<p>Read the <a href="https://cabby.dev/kinds">full story</a>.</p>
```

* `<h1>` starts a heading and `</h1>` ends it. A tag with what's between
  it and its end is an **element**: here an `h1` element, holding the text
  "Cabby Weekly".
* Elements go inside elements: the `p` (paragraph) holds text and an `a`
  (a link).
* A tag can have **attributes**, `name="value"` pairs inside the `<...>`:
  the `a` has `href`, the address it links to.
* Two attributes are used everywhere to pick parts out: `class` names a
  kind of thing (`class="price"`; one element can have several, with
  spaces between), and `id` names one thing (`id="prices"`; only one
  element on a page has it).
* Some elements have no end: `<br>` (a line break), `<img src="...">` (a
  picture), `<input>`, `<hr>`, `<meta>`, `<link>`.

The tags you'll meet most:

| Tag | Is |
|---|---|
| `html`, `head`, `body` | the whole page, its unseen part (title, settings), and what's shown |
| `title` | the page's title (on its tab) |
| `h1` to `h6` | headings, from biggest to smallest |
| `p` | a paragraph |
| `a` | a link (`href`: where to) |
| `img` | a picture (`src`: the picture's address, `alt`: words for it) |
| `ul`, `ol`, `li` | a list (with bullets, or numbered), and an item in it |
| `table`, `tr`, `th`, `td` | a table, a row, a heading cell, a cell |
| `div`, `span` | a box, and a stretch of text: just there to hold a class or id |
| `strong`, `em` | important (bold), and stressed (italic) |
| `form`, `input`, `button` | a form to fill in |
| `script`, `style` | a program and colors for the browser (their insides aren't text) |

A whole page, then, looks like this:

```html
<!DOCTYPE html>
<html>
  <head><title>Cabby Weekly</title></head>
  <body>
    <h1 class="headline">Kinds are here</h1>
    <ul id="prices">
      <li class="price" data-item="tea">3.50</li>
      <li class="price" data-item="cake">5.00</li>
    </ul>
  </body>
</html>
```

## Reading a page

| Write | What it does |
|---|---|
| `html.read Text` | the page in `Text`, as an `html` |
| `E.find Selector` | the first element inside `E` that matches |
| `E.find_all Selector` | every element inside `E` that matches, in order (a `list element`) |
| `E.children` | the elements directly inside `E` |
| `E.parent` | the element `E` is inside |
| `E.tag` | its tag's name: `"a"` |
| `E.text` | all the text inside, as a person reads it (spaces tidied; also what `println E` shows) |
| `E.raw_text` | all the text inside, exactly as it is |
| `E.attribute "href"` | an attribute's value |
| `E.attribute_or "title", ""` | the same, or a default when it isn't there |
| `E.has_attribute "href"` | whether it's there |
| `E.attributes` | the names of all its attributes (a `list str`) |
| `E.is_missing` | `true` for what a `find` didn't find |

`html` is a kind of `element` (the whole page), so everything an element
has, a page has too. What you find in it are `element`s.

```cabby
use html
use networks

int main {
	html Page = html.read networks.download "https://example.com"
	println Page.find "title"
	println Page.find "h1"
	for Paragraph in Page.find_all "p" {
		println Paragraph
	}
	// every link on a page (example.com has none, so this prints nothing
	// there: try a page you know)
	for Link in Page.find_all "a" {
		println Link.text, "->", Link.attribute "href"
	}
}
```

(`example.com` is a real page, kept for examples like this one: a title, a
heading and a paragraph or two. What's on it has changed over the years,
so to see exactly what a page holds, `println Page.pretty`.)

Browsers forgive mistakes in HTML, and so does `html.read`: a `<p>` or
`<li>` left open is closed where the next one starts, capitals in tags
(`<DIV>`) mean the same as small letters, an attribute's value needn't be
in quotes, and an end tag with nothing to end is passed over. So any page
reads; `html.read` never panics.

Entities, the `&name;` way of writing characters, are turned into the
characters: `&amp;` is `&`, `&lt;` is `<`, `&nbsp;` a space that doesn't
break, `&copy;` ©, `&#169;` and `&#xA9;` too.

## Finding: CSS selectors

The text given to `find` and `find_all` is a **CSS selector**, the little
language web pages themselves use to pick elements (to color them, say).
Learn it once and it works everywhere on the web.

| Selector | Finds |
|---|---|
| `"a"` | every `a` element |
| `"*"` | every element |
| `".price"` | elements whose `class` includes `price` |
| `"#prices"` | the element whose `id` is `prices` |
| `"li.price"` | `li` elements with class `price` |
| `".price.sale"` | elements with both classes |
| `"a[href]"` | `a` elements that have an `href` |
| `"input[type=checkbox]"` | `input` elements whose `type` is `checkbox` (quotes are optional: `[type="checkbox"]`) |
| `"ul li"` | `li` elements anywhere inside a `ul` (a space: *somewhere inside*) |
| `"ul > li"` | `li` elements directly inside a `ul` (`>`: *directly inside*) |
| `"#prices li.price"` | the parts combine: class-`price` `li`s inside the element with id `prices` |
| `"h1, h2"` | either: every `h1` and every `h2` |
| `"h2 + p"` | a `p` straight after an `h2` (`+`: *the next one*) |
| `"h2 ~ p"` | every `p` after an `h2`, among the same parent's (`~`: *after it*) |
| `"a[href^=https]"` | `a`s whose `href` starts with `https`; `$=` ends with, `*=` has it anywhere, `~=` has it as one of its words (as `class` has), `\|=` is it or starts with it and a `-` (`[lang\|=en]` takes `en-GB`) |

And pseudo-classes, after a `:`, for where an element is and what's in
it:

| Pseudo-class | Finds elements that are |
|---|---|
| `:first-child`, `:last-child`, `:only-child` | first (last, alone) among their parent's elements: `li:first-child` |
| `:nth-child(3)` | the third; `(odd)`, `(even)`, and `(3n+1)` (the 1st, 4th, 7th...), `(-n+3)` (the first three) |
| `:nth-last-child(2)` | the second from the end |
| `:first-of-type`, `:last-of-type`, `:only-of-type`, `:nth-of-type(2)`, `:nth-last-of-type(2)` | the same, counting only elements of their own tag: `p:first-of-type` is the first `p`, after an `h1` or not |
| `:not(.sale)` | not like what's in the ( ): `li:not(.sale)`; a list of selectors too, `:not(.a, .b)` |
| `:is(h1, h2)`, `:where(h1, h2)` | like any of what's in the ( ): `:is(h1, h2) > a` |
| `:has(img)` | with something like that inside them: `a:has(img)`; `:has(> img)` directly inside, `:has(+ p)` straight after |
| `:empty` | with nothing inside (comments aside) |
| `:root` | the outermost element (`html`) |
| `:checked`, `:disabled`, `:enabled` | form parts ticked or chosen (`checked`, `selected`), turned off (`disabled`), or not |

<!-- run -->
```cabby
use html

int main {
	html Page = html.read "<ul><li>tea</li><li class='sale'>cake</li><li>jam</li><li>bread</li></ul>"
	println Page.find("li:first-child").text, Page.find("li:last-child").text
	for Item in Page.find_all "li:nth-child(odd)" {
		println " odd:", Item.text
	}
	println Page.find_all("li:not(.sale)").length, Page.find(".sale + li").text
	println Page.find_all("ul:has(.sale)").length, Page.find_all("ul:has(> p)").length
}
```

```text
tea bread
 odd: tea
 odd: jam
3 jam
1 0
```

Pseudo-classes about a page being shown in a browser (`:hover`,
`:visited`, `:focus`, `::before`) have no meaning for a page Cabby reads;
using one is a panic that says so.

<!-- run -->
```cabby
use html

int main {
	str Text = "<h1 class='headline'>Kinds are here</h1><ul id='prices'>"
	Text += "<li class='price' data-item='tea'>3.50</li>"
	Text += "<li class='price sale' data-item='cake'>5.00</li></ul>"
	html Page = html.read Text
	println Page.find "h1"
	for Price in Page.find_all "#prices > li.price" {
		println Price.attribute("data-item"), Price.text.to_double
	}
	println Page.find(".sale").attribute "data-item"
}
```

```text
Kinds are here
tea 3.5
cake 5.0
cake
```

A selector Cabby can't understand is a panic that says where in it.

## When something isn't there

* `find` that finds nothing gives a *missing* element: using it is a panic
  that says what was looked for (`nothing in the page matches "table"`).
  Check with `.is_missing` first when it might not be there.
* `find_all` that finds nothing gives an empty list, so a loop over it
  simply doesn't run.
* `attribute` on one that isn't there is a panic (`<a> has no attribute
  "title"`); `attribute_or` gives a default instead, and `has_attribute`
  checks.

## Making a page

| Write | What it does |
|---|---|
| `html.element "p"` | a new, empty element |
| `E.add_text Text` | text, at the end of what's inside |
| `E.add Child` | an element, at the end of what's inside (moved there, from anywhere it was) |
| `E.set_attribute "class", "intro"` | sets an attribute (or changes it) |
| `E.remove_attribute "class"` | takes one out |
| `E.remove` | takes `E` out of the element it's in |
| `E.clear` | takes everything out of `E` |
| `E.copy` | a copy of its own, inside nothing |
| `E.markup` | written out as HTML |
| `E.pretty` | written out with each element on its own line, indented |

<!-- run -->
```cabby
use html

int main {
	list str Names = {"Ada", "Tom & Jerry"}
	element List = html.element "ul"
	for Name in Names {
		element Item = html.element "li"
		Item.add_text Name
		List.add Item
	}
	println List.pretty
}
```

```text
<ul>
  <li>Ada</li>
  <li>Tom &amp; Jerry</li>
</ul>
```

Text you add is always written safely: `&`, `<` and `>` become `&amp;`,
`&lt;` and `&gt;`, so text can never break the page or put code into it.
That's why building a page with elements is better than joining strings:
`"<li>" + Name + "</li>"` goes wrong the day a name has a `<` in it.

`files.write "page.html", Page.pretty` saves a page to open in a browser.

## Parts of a page

An element found in a page is part of it, not a copy: changing it (adding
text, setting an attribute) changes the page, and it stays good for as
long as either is around. `.copy` makes one of its own.
