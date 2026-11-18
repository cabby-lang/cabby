# dates — calendar days, clock times and time zones

```cabby
use dates
```

Time as people keep it: dates on a calendar, times on a clock, in a time
zone. `use dates` brings in [time](time.md) as well, since it builds on
it; writing `use time` too is fine.

| Kind | What it is | `.text` |
|---|---|---|
| `date` | a calendar day, with no time and no time zone | `2026-11-03` |
| `time_of_day` | a time on a clock | `07:30:00` |
| `date_time` | a date and a time on the same clock | `2026-11-03 07:30:00` |
| `weekday` | an enum: `SUNDAY`, `MONDAY`... `SATURDAY` | `Tuesday` |
| `month` | an enum: `JANUARY` (1)... `DECEMBER` (12) | `November` |

`println` shows each one as its `.text`.

## Making them

| Write | Gives |
|---|---|
| `dates.today` | today's date, here |
| `dates.now` | the date and time now, here |
| `dates.time_now` | the time of day now, here |
| `dates.make Year, Month, Day` | a date; one that doesn't exist (31 February) is a panic |
| `dates.time Hour, Minute` | a time of day (hours 0 to 23); add `time.seconds(N)` for seconds |
| `D.at T` | a date at a time of day: a `date_time` |
| `dates.read Text, Pattern` | a date read from text (`"25/12/2026", "dd/mm/yyyy"`); text that doesn't fit is a panic |
| `dates.can_read Text, Pattern` | `true` if `dates.read` would work |
| `dates.read_time`, `dates.read_date_time` | the same, for a time of day, and a date and time |

## What they have

| Write | Gives |
|---|---|
| `D.year`, `D.day` | numbers (`long`) |
| `D.month` | the `month` (`D.month is DECEMBER`); `D.month_number` is 1 to 12 |
| `D.weekday` | the `weekday` (`D.weekday is SATURDAY`) |
| `D.day_of_year` | 1 for 1 January, up to 365 or 366 |
| `D.days_in_month`, `D.is_leap_year` | about its month and year |
| `D + N`, `D - N` | `N` days later or earlier (also `D.add_days N`) |
| `D2 - D1` | the days between two dates, a `long` |
| `D.add_months N`, `D.add_years N` | months or years on (or back, for a negative `N`); 31 January and a month is the last day of February |
| `T.hour`, `T.minute`, `T.second` | a time of day's parts; `T + time.hours(2)` is later, `T2 - T1` a duration |
| `DT.date`, `DT.time_of_day` | a date and time's parts; it has `.year` to `.second` and `.weekday` too |
| `DT + D`, `DT2 - DT1` | a duration later; the duration between |
| `X.format Pattern` | text, by a pattern (below), for any of the three |

## Patterns

Patterns are written as in a spreadsheet, so they may already be familiar:

| In the pattern | Gives, for Tuesday 3 November 2026, 4:05:09 pm |
|---|---|
| `d` / `dd` | the day: `3` / `03` |
| `ddd` / `dddd` | the weekday: `Tue` / `Tuesday` |
| `m` / `mm` | the month's number: `11` / `11` |
| `mmm` / `mmmm` | the month's name: `Nov` / `November` |
| `yy` / `yyyy` | the year: `26` / `2026` |
| `h` / `hh` | the hour: `16` / `16` (`4` / `04` with am/pm) |
| `m` / `mm` after an hour, or before seconds | the minutes: `5` / `05` |
| `s` / `ss` | the seconds: `9` / `09` |
| `am/pm` / `AM/PM` | `pm` / `PM`, and the hour goes from 1 to 12 |
| `"text"` or `\x` | written as it is |

Anything else (`/`, `-`, `:`, spaces, commas) is written as it is. So
`"d mmmm yyyy"` gives `3 November 2026`, `"dd/mm/yyyy"` gives
`03/11/2026`, and `"ddd d mmm, h:mm am/pm"` gives `Tue 3 Nov, 4:05 pm`.
`dates.read` takes the same patterns; it accepts month and weekday names
in any mix of capitals.

<!-- run -->
```cabby
use dates

int main {
	date D = dates.make 2026, 11, 3
	println D, D.weekday, D.format("d mmmm yyyy")
	date Christmas = dates.make D.year, 12, 25
	println Christmas - D, "days to go"
	println D + 14, D.add_months(3), D.weekday is TUESDAY
	time_of_day Alarm = dates.time 7, 30
	println Alarm.format("h:mm am/pm"), Alarm + time.hours(6)
	time_of_day Evening = dates.time 18, 30
	date_time Party = Christmas.at Evening
	println Party.format "dddd d mmm, h:mm am/pm"
	println dates.read("25/12/2026", "dd/mm/yyyy") == Christmas
}
```

```text
2026-11-03 Tuesday 3 November 2026
52 days to go
2026-11-17 2027-02-03 true
7:30 am 13:30:00
Friday 25 Dec, 6:30 pm
true
```

## Time zones

A `date_time` has no time zone of its own: it is what a clock showed. A
moment (from [time](time.md)) is the same everywhere, and becomes a date
and time in a time zone:

| Write | Gives |
|---|---|
| `M.local` | the moment, as the date and time here (the computer's time zone) |
| `M.utc` | the moment, as the date and time in UTC |
| `DT.moment` | the moment a date and time here is |
| `DT.utc_moment` | the moment a date and time in UTC is |
| `M.text` | `2026-11-03 08:27:41 +11:00`: the date and time here, and how far that is from UTC |

Daylight saving is taken into account, as the system knows it.

### Named time zones

Any of the world's time zones, by its name: a continent (or ocean) and
its biggest city, as `"Europe/Berlin"`, `"America/New_York"`,
`"Australia/Sydney"`, `"Asia/Kolkata"`, or `"UTC"`.

| Write | Gives |
|---|---|
| `M.in_zone Zone` | the moment, as the date and time there |
| `M.text_in Zone` | `2026-11-03 08:30:00 +01:00`: the date and time there, and how far that is from UTC |
| `M.offset_in Zone` | how far ahead of UTC it is there then, a duration (behind: negative) |
| `M.abbreviation_in Zone` | its short name then: `"CET"` or `"CEST"`, `"AEDT"` |
| `DT.moment_in Zone` | the moment a date and time there is |
| `dates.zone_here` | this computer's own: `"Australia/Sydney"` (`""` if it can't be told) |
| `dates.is_zone Name` | `true` if there's a time zone called that |
| `dates.zones` | every one there is, a `list str` |

<!-- run -->
```cabby
use dates

int main {
	// a lesson at 7 pm in Sydney: what time is that for a student in Berlin?
	date_time Lesson = dates.make(2026, 11, 3).at dates.time(19, 0)
	moment When = Lesson.moment_in "Australia/Sydney"
	println When.text_in("Australia/Sydney")
	println When.in_zone("Europe/Berlin").format("dddd h:mm am/pm"), When.abbreviation_in("Europe/Berlin")
}
```

```text
2026-11-03 19:00:00 +11:00
Tuesday 9:00 am CET
```

A date and time the clocks skipped (moving forward for summer: 2:30 on the
morning they jump from 2:00 to 3:00) is taken as the time an hour on; one
they showed twice (moving back), as the first.

The zones are read from the files every Linux and Mac keeps them in
(`/usr/share/zoneinfo`: the IANA time zone database, kept up to date with
the system's updates), so no zone is a panic that isn't there: one that
isn't says so. Their rules carry on past the last change they list, so
2090 is worked out as well as next year.

## Other languages

Month and weekday names in a pattern (`mmmm`, `mmm`, `dddd`, `ddd`) can be
in another language, given by its code or its name in English:

| Write | Gives |
|---|---|
| `X.format_in Pattern, Language` | `D.format_in("dddd d mmmm yyyy", "fr")` is `mardi 3 novembre 2026` |
| `dates.read_in Text, Pattern, Language` | reads one, its names in that language (any capitals) |
| `dates.read_date_time_in`, `dates.can_read_in` | the same, for a date and time, and to check first |
| `M.text_in Language`, `W.text_in Language` | a `month`'s or `weekday`'s name: `JULY.text_in "it"` is `luglio` |
| `dates.languages` | the languages it knows, as `"code Name"` |
| `dates.knows_language Language` | `true` if it knows that one |

| Code | Language | `"dddd d mmmm"` | `"ddd d mmm"` |
|---|---|---|---|
| `en` | English | Tuesday 3 November | Tue 3 Nov |
| `de` | German | Dienstag 3 November | Di 3 Nov |
| `fr` | French | mardi 3 novembre | mar 3 nov |
| `es` | Spanish | martes 3 noviembre | mar 3 nov |
| `it` | Italian | martedì 3 novembre | mar 3 nov |
| `pt` | Portuguese | terça-feira 3 novembro | ter 3 nov |
| `nl` | Dutch | dinsdag 3 november | di 3 nov |
| `sv` | Swedish | tisdag 3 november | tis 3 nov |
| `da` | Danish | tirsdag 3 november | tir 3 nov |
| `no` | Norwegian | tirsdag 3 november | tir 3 nov |
| `el` | Greek | Τρίτη 3 Νοεμβρίου | Τρί 3 Νοέ |
| `la` | Latin | dies Martis 3 November | Mar 3 Nov |
| `ang` | Old English | Tīwesdæġ 3 Blōtmōnaþ | Tīw 3 Blō |

Each is written in its own capitals (French's and Spanish's names are
small), and the words around them (the `de` in Spanish's `3 de
noviembre`, German's `3.`) are the pattern's to say: `"d \"de\" mmmm"`.
A Greek month is written as it's said after a day when the pattern has
one (`d` or `dd`): `3 Νοεμβρίου` ("of November"), but `Νοέμβριος 2026`
on its own, and either is read. Old English has the months Bede wrote down,
from `Æfterra Ġēola` (January) to `Ærra Ġēola` (December).

## Looser text

People write dates every which way. `dates.read_loosely` reads them as
they come, in English, from today:

| It reads | as |
|---|---|
| `today`, `tomorrow`, `yesterday`, `the day after tomorrow` | those days |
| `tuesday` (or `tue`) | the next Tuesday, or today if it's Tuesday |
| `next tuesday`, `last tuesday` | the first after today, the last before it |
| `next week`, `last month`, `next year` | a week, month or year on (or back) |
| `in 3 days`, `in a week`, `in two months` | that far on |
| `3 days ago`, `a year ago`, `2 weeks later` | that far back (or on) |
| `25 Dec`, `Dec 25th`, `the 3rd of January` | that day: this year's, or next year's if this year's has gone |
| `December 25 2027`, `2026-12-31`, `25/12/2026`, `3.4.2027` | that day (with `/` and `.`, the day first) |
| `5pm`, `5:30 pm`, `17:30`, `noon`, `midnight`, `5 o'clock` | a time of day, for a date and time |
| `now`, `in 2 hours`, `90 minutes ago` | from the time it is |

Commas, capitals, `the`, `of`, `on`, `at` and `this` don't matter, so
`friday at 5pm` and `Friday, 5 PM` are the same.

| Write | Gives |
|---|---|
| `dates.read_loosely Text` | a date; text it can't make sense of is a panic that says which word |
| `dates.read_date_time_loosely Text` | a date and time (midnight, if no time is said) |
| `dates.can_read_loosely Text` | `true` if it can |
| `dates.read_loosely_from Text, DT`, `dates.read_date_time_loosely_from Text, DT` | the same, as if it were `DT` now |

<!-- run -->
```cabby
use dates

int main {
	date_time Now = dates.make(2026, 11, 3).at dates.time(9, 15)    // a Tuesday morning
	list str Said = {"tomorrow", "next friday", "in 2 weeks", "25 Dec"}
	for Each in Said {
		println Each, "->", dates.read_loosely_from(Each, Now)
	}
	println dates.read_date_time_loosely_from("friday at 5pm", Now)
}
```

```text
tomorrow -> 2026-11-04
next friday -> 2026-11-06
in 2 weeks -> 2026-11-17
25 Dec -> 2026-12-25
2026-11-06 17:00:00
```

## Kinds of their own

`date`, `time_of_day` and `date_time` are kinds (the manual's chapter 20),
so they don't mix with plain numbers or with each other: `D + 1` is a day
later, but `D + time.hours(1)` is an error, and so is `date X = 20261103`.
A date is kept as days since 1 January 1970, a time of day as nanoseconds
since midnight, and a date and time as nanoseconds since 1970 on the same
clock.

Adding to a time of day doesn't wrap at midnight (`dates.time 23, 0` and
three hours is 26:00 inside); `.hour`, `.text` and `.format` show it on
the clock (`02:00:00`).
