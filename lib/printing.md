# printing — printing files and text

```cabby
use printing
```

Printing a file (a PDF, a picture, text) or text, to the computer's
printers, and seeing what's waiting to be printed. Both Linux and macOS
print through CUPS, so it's the same on both: `lp`, `lpstat` and
`cancel` are what's used.

* [Printing](#printing)
* [Settings](#settings)
* [Printers](#printers)
* [Jobs](#jobs)

## Printing

<!-- check -->
```cabby
use printing

int main {
	if !printing.can_print {
		println "no printers"
		return 1
	}
	str Job = printing.print "scales.pdf"
	if Job is "" {
		println "it didn't print"
		return 1
	}
	println "printing it:", Job
	printing.print_text "Lesson at four.\nBring the scales."
	return 0
}
```

| Call | What it does |
|---|---|
| `str printing.print Path` | prints a file on the default printer: the job's name, `"Office-12"`, or `""` if it didn't go |
| `str printing.print_text Text` | prints text, as it is (`"\n"` between lines) |
| `str printing.print_with Path, Settings` | prints a file as [`Settings`](#settings) say |
| `str printing.print_text_with Text, Settings` | and text |

PDFs, pictures (PNG, JPEG) and text print as they are; CUPS turns each
into what the printer needs.

## Settings

<!-- check -->
```cabby
use printing

int main {
	printing.settings Two
	Two.set_printer "Office"
	Two.set_copies 2
	Two.set_two_sided
	Two.set_pages "1-3"
	printing.print_with "scales.pdf", Two
	return 0
}
```

| `printing.settings` | |
|---|---|
| `set_printer Name` | to this printer, not the default one |
| `set_copies Count` | how many copies |
| `set_two_sided` | on both sides of the paper, turned over as a book's pages are |
| `set_two_sided_short_edge` | on both sides, turned over as a notepad's (for landscape pages) |
| `set_pages Which` | only these pages: `"1-3"`, `"2,5,8-10"` |
| `set_landscape` | sideways on the page |
| `set_black_and_white` | in gray on a color printer |
| `set_title Name` | its name in the list of jobs |

What isn't set is the printer's own setting (its paper, its quality).

## Printers

| Call | What it says |
|---|---|
| `list str printing.printers` | the printers this computer knows, by name: `["Brother_HL_L2350DW", "Office"]` |
| `str printing.default_printer` | the one printed to when none is named (`""` if none) |
| `bool printing.can_print` | `true` if there's a printer to print to |

A printer is added in the system's settings (Settings > Printers, on
both). On Linux, CUPS comes with GNOME and KDE; otherwise it's the
`cups` package, with `sudo systemctl enable --now cups`.

## Jobs

| Call | What it does |
|---|---|
| `list str printing.jobs` | the jobs not printed yet: `["Office-12", "Office-13"]` |
| `bool printing.done Job` | `true` once it's printed (or canceled): no longer waiting |
| `bool printing.cancel Job` | stops it: `true` if it was stopped |
| `bool printing.cancel_all` | stops every job of this person's |
