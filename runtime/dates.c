/* dates.c — the C side of the time and dates libraries (lib/time.hby,
 * lib/dates.hby). Part of Cabby's runtime, so both the interpreter and
 * compiled programs have it.
 *
 * Every value is a whole number (an int64_t):
 *   duration     nanoseconds
 *   moment       nanoseconds since 1970-01-01 00:00 UTC
 *   date         days since 1970-01-01
 *   time_of_day  nanoseconds since midnight
 *   date_time    a date and a clock time, as nanoseconds since 1970-01-01
 *                00:00 on the same clock (no time zone of its own)
 *
 * C can't hand Cabby a string directly, so text is made here into a
 * buffer, and Cabby reads it a character at a time (cby_dates_char). */
#include "rt.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NS_PER_S 1000000000LL
#define NS_PER_DAY (86400LL * NS_PER_S)

static _Thread_local char out[512];
static _Thread_local int out_len;

char cby_dates_char(int64_t i) { return i >= 0 && i < out_len ? out[i] : '\0'; }

static void put(const char *s)
{
	size_t n = strlen(s);
	if (out_len + (int)n >= (int)sizeof out)
		n = sizeof out - 1 - (size_t)out_len;
	memcpy(out + out_len, s, n);
	out_len += (int)n;
	out[out_len] = '\0';
}

static int64_t floor_div(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
static int64_t floor_mod(int64_t a, int64_t b) { return a - floor_div(a, b) * b; }

/* ---- the calendar (Howard Hinnant's days-from-civil, proleptic Gregorian) */

int64_t cby_days_from_civil(int64_t y, int64_t m, int64_t d)
{
	y -= m <= 2;
	int64_t era = floor_div(y, 400);
	int64_t yoe = y - era * 400;
	int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, int64_t *m, int64_t *d)
{
	z += 719468;
	int64_t era = floor_div(z, 146097);
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = yoe + era * 400 + (*m <= 2);
}

static bool leap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int64_t month_days(int64_t y, int64_t m)
{
	static const int n[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return m == 2 && leap(y) ? 29 : n[m - 1];
}

int cby_date_valid(int64_t y, int64_t m, int64_t d)
{
	return m >= 1 && m <= 12 && d >= 1 && d <= month_days(y, m) && y >= -999999 && y <= 999999;
}

/* which: 0 year, 1 month, 2 day, 3 weekday (0 Sunday), 4 day of the year,
 * 5 days in the month, 6 leap year (1 or 0) */
int64_t cby_date_part(int64_t days, int64_t which)
{
	int64_t y, m, d;
	civil_from_days(days, &y, &m, &d);
	switch (which) {
	case 0: return y;
	case 1: return m;
	case 2: return d;
	case 3: return floor_mod(days + 4, 7);   /* 1970-01-01 was a Thursday */
	case 4: return days - cby_days_from_civil(y, 1, 1) + 1;
	case 5: return month_days(y, m);
	default: return leap(y);
	}
}

/* months later (or earlier); a day past the new month's end becomes its
 * last day: 31 January and a month is 28 or 29 February */
int64_t cby_date_add_months(int64_t days, int64_t months)
{
	int64_t y, m, d;
	civil_from_days(days, &y, &m, &d);
	int64_t k = y * 12 + (m - 1) + months;
	y = floor_div(k, 12);
	m = floor_mod(k, 12) + 1;
	if (d > month_days(y, m))
		d = month_days(y, m);
	return cby_days_from_civil(y, m, d);
}

void cby_date_check(int64_t y, int64_t m, int64_t d)
{
	if (!cby_date_valid(y, m, d)) {
		if (m < 1 || m > 12)
			rt_panic(NULL, 0, 0, "there is no month %lld (months go from 1 to 12)", (long long)m);
		rt_panic(NULL, 0, 0, "%lld-%02lld-%02lld isn't a date: that month has %lld days", (long long)y,
			 (long long)m, (long long)d, (long long)month_days(y, m));
	}
}

void cby_time_check(int64_t h, int64_t m, int64_t s)
{
	if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
		rt_panic(NULL, 0, 0, "%lld:%02lld:%02lld isn't a time of day (hours go from 0 to 23, minutes and "
			 "seconds from 0 to 59)", (long long)h, (long long)m, (long long)s);
}

/* ---- the clock */

int64_t cby_time_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}

void cby_time_sleep(int64_t ns)
{
	if (ns <= 0)
		return;
	rt_flush_files();
	fflush(stdout);
	struct timespec ts = { (time_t)(ns / NS_PER_S), (long)(ns % NS_PER_S) };
	while (nanosleep(&ts, &ts) != 0)
		;
}

/* seconds east of UTC, here, at that moment (daylight saving included) */
int64_t cby_local_offset(int64_t moment)
{
	time_t t = (time_t)floor_div(moment, NS_PER_S);
	struct tm tm;
	localtime_r(&t, &tm);
	return tm.tm_gmtoff;
}

/* a local date and time as a moment */
int64_t cby_local_to_moment(int64_t wall)
{
	int64_t days = floor_div(wall, NS_PER_DAY), rest = floor_mod(wall, NS_PER_DAY);
	int64_t y, m, d;
	civil_from_days(days, &y, &m, &d);
	struct tm tm = { 0 };
	tm.tm_year = (int)(y - 1900);
	tm.tm_mon = (int)(m - 1);
	tm.tm_mday = (int)d;
	tm.tm_hour = (int)(rest / (3600 * NS_PER_S));
	tm.tm_min = (int)(rest / (60 * NS_PER_S) % 60);
	tm.tm_sec = (int)(rest / NS_PER_S % 60);
	tm.tm_isdst = -1;
	time_t t = mktime(&tm);
	return (int64_t)t * NS_PER_S + rest % NS_PER_S;
}

/* ---- text */

/* Month and weekday names in other languages: each language's own, in
 * its own capitals (French's and Spanish's are small), and their short
 * forms as each writes them (German's weekdays are two letters). Kept
 * here, not taken from the system's locales, which differ from one
 * computer to the next and are often not installed. */
typedef struct {
	const char *code, *name;
	const char *months[12], *months_short[12];
	const char *days[7], *days_short[7];
} Language;

static const Language languages[] = {
	{ "en", "English",
	  { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" },
	  { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
	  { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
	  { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" } },
	{ "de", "German",
	  { "Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember" },
	  { "Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez" },
	  { "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag" },
	  { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" } },
	{ "fr", "French",
	  { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre" },
	  { "janv", "févr", "mars", "avr", "mai", "juin", "juil", "août", "sept", "oct", "nov", "déc" },
	  { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" },
	  { "dim", "lun", "mar", "mer", "jeu", "ven", "sam" } },
	{ "es", "Spanish",
	  { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" },
	  { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sept", "oct", "nov", "dic" },
	  { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" },
	  { "dom", "lun", "mar", "mié", "jue", "vie", "sáb" } },
	{ "it", "Italian",
	  { "gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre" },
	  { "gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic" },
	  { "domenica", "lunedì", "martedì", "mercoledì", "giovedì", "venerdì", "sabato" },
	  { "dom", "lun", "mar", "mer", "gio", "ven", "sab" } },
	{ "pt", "Portuguese",
	  { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" },
	  { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" },
	  { "domingo", "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado" },
	  { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" } },
	{ "nl", "Dutch",
	  { "januari", "februari", "maart", "april", "mei", "juni", "juli", "augustus", "september", "oktober", "november", "december" },
	  { "jan", "feb", "mrt", "apr", "mei", "jun", "jul", "aug", "sep", "okt", "nov", "dec" },
	  { "zondag", "maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag" },
	  { "zo", "ma", "di", "wo", "do", "vr", "za" } },
	{ "sv", "Swedish",
	  { "januari", "februari", "mars", "april", "maj", "juni", "juli", "augusti", "september", "oktober", "november", "december" },
	  { "jan", "feb", "mar", "apr", "maj", "jun", "jul", "aug", "sep", "okt", "nov", "dec" },
	  { "söndag", "måndag", "tisdag", "onsdag", "torsdag", "fredag", "lördag" },
	  { "sön", "mån", "tis", "ons", "tor", "fre", "lör" } },
	{ "da", "Danish",
	  { "januar", "februar", "marts", "april", "maj", "juni", "juli", "august", "september", "oktober", "november", "december" },
	  { "jan", "feb", "mar", "apr", "maj", "jun", "jul", "aug", "sep", "okt", "nov", "dec" },
	  { "søndag", "mandag", "tirsdag", "onsdag", "torsdag", "fredag", "lørdag" },
	  { "søn", "man", "tir", "ons", "tor", "fre", "lør" } },
	{ "no", "Norwegian",
	  { "januar", "februar", "mars", "april", "mai", "juni", "juli", "august", "september", "oktober", "november", "desember" },
	  { "jan", "feb", "mar", "apr", "mai", "jun", "jul", "aug", "sep", "okt", "nov", "des" },
	  { "søndag", "mandag", "tirsdag", "onsdag", "torsdag", "fredag", "lørdag" },
	  { "søn", "man", "tir", "ons", "tor", "fre", "lør" } },
	{ "el", "Greek",
	  { "Ιανουάριος", "Φεβρουάριος", "Μάρτιος", "Απρίλιος", "Μάιος", "Ιούνιος", "Ιούλιος", "Αύγουστος", "Σεπτέμβριος", "Οκτώβριος", "Νοέμβριος", "Δεκέμβριος" },
	  { "Ιαν", "Φεβ", "Μάρ", "Απρ", "Μάι", "Ιούν", "Ιούλ", "Αύγ", "Σεπ", "Οκτ", "Νοέ", "Δεκ" },
	  { "Κυριακή", "Δευτέρα", "Τρίτη", "Τετάρτη", "Πέμπτη", "Παρασκευή", "Σάββατο" },
	  { "Κυρ", "Δευ", "Τρί", "Τετ", "Πέμ", "Παρ", "Σάβ" } },
	{ "la", "Latin",
	  { "Ianuarius", "Februarius", "Martius", "Aprilis", "Maius", "Iunius", "Iulius", "Augustus", "September", "October", "November", "December" },
	  { "Ian", "Feb", "Mar", "Apr", "Mai", "Iun", "Iul", "Aug", "Sep", "Oct", "Nov", "Dec" },
	  { "dies Solis", "dies Lunae", "dies Martis", "dies Mercurii", "dies Iovis", "dies Veneris", "dies Saturni" },
	  { "Sol", "Lun", "Mar", "Mer", "Iov", "Ven", "Sat" } },
	{ "ang", "Old English",
	  { "Æfterra Ġēola", "Solmōnaþ", "Hrēþmōnaþ", "Ēastermōnaþ", "Þrimilce", "Ærra Līþa", "Æfterra Līþa", "Weodmōnaþ", "Hāliġmōnaþ", "Winterfylleþ", "Blōtmōnaþ", "Ærra Ġēola" },
	  { "Æft Ġē", "Sol", "Hrē", "Ēas", "Þri", "Ær Lī", "Æft Lī", "Weo", "Hāl", "Win", "Blō", "Ær Ġē" },
	  { "Sunnandæġ", "Mōnandæġ", "Tīwesdæġ", "Wōdnesdæġ", "Þunresdæġ", "Frīġedæġ", "Sæternesdæġ" },
	  { "Sun", "Mōn", "Tīw", "Wōd", "Þun", "Frī", "Sæt" } },
};

#define N_LANGUAGES (int)(sizeof languages / sizeof languages[0])

/* the language format and reading use now (English, unless asked) */
static _Thread_local const Language *lang;

static const Language *language(void) { return lang ? lang : &languages[0]; }

/* a language by its code ("de") or its name in English ("German"), any
 * capitals; NULL if there's no such */
static const Language *language_named(const char *name)
{
	for (int i = 0; i < N_LANGUAGES; i++)
		if (strcasecmp(languages[i].code, name) == 0 || strcasecmp(languages[i].name, name) == 0)
			return &languages[i];
	return NULL;
}

static const Language *must_language(const char *name)
{
	const Language *l = language_named(name);
	if (!l) {
		char known[400] = "";
		for (int i = 0; i < N_LANGUAGES; i++) {
			strcat(known, i ? ", " : "");
			strcat(known, languages[i].code);
		}
		rt_panic(NULL, 0, 0, "the dates library doesn't know the language '%s' (it has: %s)", name, known);
	}
	return l;
}

int cby_dates_language_known(const char *name) { return language_named(name) != NULL; }

/* every language, as "code name" lines */
int64_t cby_dates_languages(void)
{
	out_len = 0;
	out[0] = '\0';
	for (int i = 0; i < N_LANGUAGES; i++) {
		if (i)
			put("\n");
		put(languages[i].code);
		put(" ");
		put(languages[i].name);
	}
	return out_len;
}

int64_t cby_name_text(int64_t which, int64_t i)
{
	out_len = 0;
	out[0] = '\0';
	const Language *l = language();
	if (which == 0 && i >= 1 && i <= 12)
		put(l->months[i - 1]);
	else if (which == 1 && i >= 0 && i <= 6)
		put(l->days[i]);
	return out_len;
}

int64_t cby_name_text_in(int64_t which, int64_t i, const char *name)
{
	const Language *was = lang;
	lang = must_language(name);
	int64_t n = cby_name_text(which, i);
	lang = was;
	return n;
}

static void put_count(int64_t n, const char *unit)
{
	char b[64];
	snprintf(b, sizeof b, "%lld %s%s", (long long)n, unit, n == 1 ? "" : "s");
	put(b);
}

/* "1.25 seconds", "2 minutes 5 seconds", "3 hours", "350 milliseconds" */
int64_t cby_duration_text(int64_t ns)
{
	out_len = 0;
	out[0] = '\0';
	if (ns < 0) {
		put("-");
		ns = ns == INT64_MIN ? INT64_MAX : -ns;
	}
	char b[64];
	if (ns == 0) {
		put("0 seconds");
	} else if (ns < 1000) {
		put_count(ns, "nanosecond");
	} else if (ns < 1000000) {
		snprintf(b, sizeof b, "%.3g microseconds", (double)ns / 1e3);
		put(b);
	} else if (ns < NS_PER_S) {
		snprintf(b, sizeof b, "%.3g milliseconds", (double)ns / 1e6);
		put(b);
	} else if (ns < 60 * NS_PER_S) {
		double s = (double)ns / 1e9;
		snprintf(b, sizeof b, "%.3f", s);
		char *e = b + strlen(b) - 1;   /* 1.250 -> 1.25, 2.000 -> 2 */
		while (*e == '0')
			*e-- = '\0';
		if (*e == '.')
			*e = '\0';
		put(b);
		put(strcmp(b, "1") == 0 ? " second" : " seconds");
	} else {
		int64_t s = ns / NS_PER_S;
		int64_t days = s / 86400, h = s / 3600 % 24, m = s / 60 % 60, sec = s % 60;
		int64_t big, small;
		const char *bu, *su;
		if (days) {
			big = days, bu = "day", small = h, su = "hour";
		} else if (h) {
			big = h, bu = "hour", small = m, su = "minute";
		} else {
			big = m, bu = "minute", small = sec, su = "second";
		}
		put_count(big, bu);
		if (small) {
			put(" ");
			put_count(small, su);
		}
	}
	return out_len;
}

/* Spreadsheet-style patterns: yyyy yy, m mm mmm mmmm (the month; minutes
 * after an hour or before seconds), d dd (the day), ddd dddd (the
 * weekday), h hh (the hour: 0-23, or 1-12 with am/pm), ss s, am/pm AM/PM.
 * Anything else is written as it is; "quoted" text and \x too. */

typedef struct {
	int64_t y, mo, d, wd, h, mi, s;
} Parts;

static Parts parts_of(int64_t wall)
{
	Parts p;
	int64_t days = floor_div(wall, NS_PER_DAY), rest = floor_mod(wall, NS_PER_DAY);
	civil_from_days(days, &p.y, &p.mo, &p.d);
	p.wd = floor_mod(days + 4, 7);
	p.h = rest / (3600 * NS_PER_S);
	p.mi = rest / (60 * NS_PER_S) % 60;
	p.s = rest / NS_PER_S % 60;
	return p;
}

static int run_of(const char *p, char c)
{
	int n = 0;
	while (tolower((unsigned char)p[n]) == c)
		n++;
	return n;
}

static bool has_ampm(const char *p)
{
	for (; *p; p++)
		if ((p[0] == 'a' || p[0] == 'A') && (p[1] == 'm' || p[1] == 'M') && p[2] == '/')
			return true;
	return false;
}

/* is the m-run at i minutes? After an h (skipping separators), or before an s */
static bool m_is_minutes(const char *pat, int i, int n)
{
	for (int j = i - 1; j >= 0; j--) {
		char c = (char)tolower((unsigned char)pat[j]);
		if (c == 'h')
			return true;
		if (isalpha((unsigned char)c))
			break;
	}
	for (int j = i + n; pat[j]; j++) {
		char c = (char)tolower((unsigned char)pat[j]);
		if (c == 's')
			return true;
		if (isalpha((unsigned char)c))
			break;
	}
	return false;
}

/* Months as they're written after a day, where a language says them
 * differently than on their own: Greek's "3 Νοεμβρίου" (of November), but
 * "Νοέμβριος 2026". NULL where it's the same. */
static const char *const *months_after_day(const Language *l)
{
	static const char *const greek[12] = { "Ιανουαρίου", "Φεβρουαρίου", "Μαρτίου", "Απριλίου", "Μαΐου", "Ιουνίου",
					       "Ιουλίου", "Αυγούστου", "Σεπτεμβρίου", "Οκτωβρίου", "Νοεμβρίου", "Δεκεμβρίου" };
	return strcmp(l->code, "el") == 0 ? greek : NULL;
}

/* does the pattern have the day of the month in it (d or dd, not the
 * weekday's ddd or dddd), outside quotes? */
static bool has_day_number(const char *pat)
{
	for (int i = 0; pat[i];) {
		if (pat[i] == '"') {
			i++;
			while (pat[i] && pat[i] != '"')
				i++;
			if (pat[i])
				i++;
			continue;
		}
		if (pat[i] == '\\' && pat[i + 1]) {
			i += 2;
			continue;
		}
		if (tolower((unsigned char)pat[i]) == 'd') {
			int n = run_of(pat + i, 'd');
			if (n <= 2)
				return true;
			i += n;
			continue;
		}
		i++;
	}
	return false;
}

int64_t cby_dates_format(int64_t wall, const char *pat)
{
	out_len = 0;
	out[0] = '\0';
	Parts p = parts_of(wall);
	bool twelve = has_ampm(pat);
	char b[64];
	for (int i = 0; pat[i];) {
		char c = (char)tolower((unsigned char)pat[i]);
		int n;
		if (pat[i] == '"') {
			int j = i + 1;
			while (pat[j] && pat[j] != '"')
				j++;
			snprintf(b, sizeof b, "%.*s", j - i - 1, pat + i + 1);
			put(b);
			i = pat[j] ? j + 1 : j;
			continue;
		}
		if (pat[i] == '\\' && pat[i + 1]) {
			snprintf(b, sizeof b, "%c", pat[i + 1]);
			put(b);
			i += 2;
			continue;
		}
		if ((c == 'a') && (pat[i + 1] == 'm' || pat[i + 1] == 'M') && pat[i + 2] == '/' &&
		    (pat[i + 3] == 'p' || pat[i + 3] == 'P') && (pat[i + 4] == 'm' || pat[i + 4] == 'M')) {
			bool upper = pat[i] == 'A';
			put(p.h < 12 ? (upper ? "AM" : "am") : (upper ? "PM" : "pm"));
			i += 5;
			continue;
		}
		switch (c) {
		case 'y':
			n = run_of(pat + i, 'y');
			if (n >= 3)
				snprintf(b, sizeof b, "%04lld", (long long)p.y);
			else
				snprintf(b, sizeof b, "%02lld", (long long)floor_mod(p.y, 100));
			break;
		case 'm':
			n = run_of(pat + i, 'm');
			if (n <= 2 && m_is_minutes(pat, i, n))
				snprintf(b, sizeof b, n == 2 ? "%02lld" : "%lld", (long long)p.mi);
			else if (n == 1)
				snprintf(b, sizeof b, "%lld", (long long)p.mo);
			else if (n == 2)
				snprintf(b, sizeof b, "%02lld", (long long)p.mo);
			else if (n == 3)
				snprintf(b, sizeof b, "%s", language()->months_short[p.mo - 1]);
			else if (months_after_day(language()) && has_day_number(pat))
				snprintf(b, sizeof b, "%s", months_after_day(language())[p.mo - 1]);
			else
				snprintf(b, sizeof b, "%s", language()->months[p.mo - 1]);
			break;
		case 'd':
			n = run_of(pat + i, 'd');
			if (n == 1)
				snprintf(b, sizeof b, "%lld", (long long)p.d);
			else if (n == 2)
				snprintf(b, sizeof b, "%02lld", (long long)p.d);
			else if (n == 3)
				snprintf(b, sizeof b, "%s", language()->days_short[p.wd]);
			else
				snprintf(b, sizeof b, "%s", language()->days[p.wd]);
			break;
		case 'h': {
			n = run_of(pat + i, 'h');
			int64_t h = p.h;
			if (twelve)
				h = h % 12 == 0 ? 12 : h % 12;
			snprintf(b, sizeof b, n >= 2 ? "%02lld" : "%lld", (long long)h);
			break;
		}
		case 's':
			n = run_of(pat + i, 's');
			snprintf(b, sizeof b, n >= 2 ? "%02lld" : "%lld", (long long)p.s);
			break;
		default:
			n = 1;
			snprintf(b, sizeof b, "%c", pat[i]);
			break;
		}
		put(b);
		i += n;
	}
	return out_len;
}

/* ---- reading: the same patterns. what: 0 a date, 1 a time of day, 2 a
 * date and time. On failure, the reason is kept for cby_dates_read. */

static _Thread_local char why[200];

static bool read_number(const char **s, int min_digits, int max_digits, int64_t *v)
{
	int n = 0;
	int64_t x = 0;
	while (n < max_digits && isdigit((unsigned char)(*s)[n])) {
		x = x * 10 + ((*s)[n] - '0');
		n++;
	}
	if (n < min_digits)
		return false;
	*s += n;
	*v = x;
	return true;
}

/* one letter of UTF-8 at s: its number, and how many bytes */
static uint32_t letter_at(const char *s, int *len)
{
	unsigned char c = (unsigned char)s[0];
	int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
	for (int i = 1; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80) {
			n = 1;
			break;
		}
	*len = n;
	if (n == 1)
		return c;
	uint32_t cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
	for (int i = 1; i < n; i++)
		cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
	return cp;
}

/* small letters, for comparing names without regard to capitals: Latin
 * (with accents), Greek (with its accents, and final sigma) and the
 * Old English letters */
static uint32_t small(uint32_t c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
		return c + 32;
	if (((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) && !(c & 1) && c != 0x130)
		return c + 1;   /* (Ā ā, Ē ē, Ġ ġ, Ī ī, Ō ō...) */
	if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2)
		return c + 32;   /* Greek capitals */
	switch (c) {
	case 0x386: return 0x3AC; case 0x388: return 0x3AD; case 0x389: return 0x3AE; case 0x38A: return 0x3AF;
	case 0x38C: return 0x3CC; case 0x38E: return 0x3CD; case 0x38F: return 0x3CE;
	case 0x3C2: return 0x3C3;   /* ς is σ */
	case 0x1E9E: return 0xDF;   /* ẞ */
	}
	return c;
}

/* how many bytes of s are the name (capitals aside), or 0 */
static size_t name_here(const char *s, const char *name)
{
	size_t used = 0;
	while (*name) {
		int a, b;
		uint32_t x = letter_at(s + used, &a), y = letter_at(name, &b);
		if (!s[used] || small(x) != small(y))
			return 0;
		used += (size_t)a;
		name += b;
	}
	return used;
}

/* which of the names (or their short forms) s starts with: the longest
 * that fits, so "juillet" isn't read as "juil" */
static int read_name(const char **s, const char *const *names, const char *const *shorts, int count, bool short_form)
{
	int best = -1;
	size_t best_len = 0;
	for (int i = 0; i < count; i++) {
		size_t n = name_here(*s, short_form ? shorts[i] : names[i]);
		if (n > best_len) {
			best = i;
			best_len = n;
		}
	}
	*s += best_len;
	return best;
}

static bool try_read(const char *text, const char *pat, int64_t what, int64_t *result)
{
	int64_t y = 1970, mo = 1, d = 1, h = 0, mi = 0, sec = 0;
	int pm = -1;
	bool twelve = has_ampm(pat);
	const char *s = text;
	for (int i = 0; pat[i];) {
		char c = (char)tolower((unsigned char)pat[i]);
		int n = 1;
		bool ok = true;
		if (pat[i] == '"') {
			int j = i + 1;
			while (pat[j] && pat[j] != '"')
				j++;
			int len = j - i - 1;
			if (strncmp(s, pat + i + 1, (size_t)len) != 0)
				ok = false;
			else
				s += len;
			n = (pat[j] ? j + 1 : j) - i;
		} else if (pat[i] == '\\' && pat[i + 1]) {
			ok = *s == pat[i + 1];
			s += ok;
			n = 2;
		} else if (c == 'a' && (pat[i + 1] == 'm' || pat[i + 1] == 'M') && pat[i + 2] == '/') {
			if (strncasecmp(s, "am", 2) == 0)
				pm = 0;
			else if (strncasecmp(s, "pm", 2) == 0)
				pm = 1;
			else
				ok = false;
			s += ok ? 2 : 0;
			n = 5;
		} else if (c == 'y') {
			n = run_of(pat + i, 'y');
			if (n >= 3) {
				ok = read_number(&s, 4, 4, &y);
			} else {
				ok = read_number(&s, 2, 2, &y);
				y += 2000;
			}
		} else if (c == 'm') {
			n = run_of(pat + i, 'm');
			if (n <= 2 && m_is_minutes(pat, i, n))
				ok = read_number(&s, n == 2 ? 2 : 1, 2, &mi);
			else if (n <= 2)
				ok = read_number(&s, n == 2 ? 2 : 1, 2, &mo);
			else {
				int k = read_name(&s, language()->months, language()->months_short, 12, n == 3);
				if (k < 0 && n >= 4 && months_after_day(language()))   /* (either form, read) */
					k = read_name(&s, months_after_day(language()), months_after_day(language()), 12, false);
				ok = k >= 0;
				mo = k + 1;
			}
		} else if (c == 'd') {
			n = run_of(pat + i, 'd');
			if (n <= 2)
				ok = read_number(&s, n == 2 ? 2 : 1, 2, &d);
			else
				ok = read_name(&s, language()->days, language()->days_short, 7, n == 3) >= 0;   /* the weekday is only checked for */
		} else if (c == 'h') {
			n = run_of(pat + i, 'h');
			ok = read_number(&s, n >= 2 ? 2 : 1, 2, &h);
		} else if (c == 's') {
			n = run_of(pat + i, 's');
			ok = read_number(&s, n >= 2 ? 2 : 1, 2, &sec);
		} else {
			ok = *s == pat[i];
			s += ok;
		}
		if (!ok) {
			snprintf(why, sizeof why, "'%s' doesn't fit the pattern '%s'", text, pat);
			return false;
		}
		i += n;
	}
	if (*s) {
		snprintf(why, sizeof why, "'%s' has more than the pattern '%s'", text, pat);
		return false;
	}
	if (twelve) {
		if (h < 1 || h > 12) {
			snprintf(why, sizeof why, "'%s': with am/pm, the hour goes from 1 to 12", text);
			return false;
		}
		h = h % 12 + (pm == 1 ? 12 : 0);
	}
	if (what != 1 && !cby_date_valid(y, mo, d)) {
		snprintf(why, sizeof why, "'%s' isn't a date: %s", text,
			 mo < 1 || mo > 12 ? "there is no such month" : "that month doesn't have that many days");
		return false;
	}
	if (what != 0 && (h > 23 || mi > 59 || sec > 59)) {
		snprintf(why, sizeof why, "'%s' isn't a time of day", text);
		return false;
	}
	int64_t days = cby_days_from_civil(y, mo, d);
	int64_t tod = ((h * 60 + mi) * 60 + sec) * NS_PER_S;
	*result = what == 0 ? days : what == 1 ? tod : days * NS_PER_DAY + tod;
	return true;
}

int cby_dates_can_read(const char *text, const char *pat, int64_t what)
{
	int64_t r;
	return try_read(text, pat, what, &r);
}

int64_t cby_dates_read(const char *text, const char *pat, int64_t what)
{
	int64_t r;
	if (!try_read(text, pat, what, &r))
		rt_panic(NULL, 0, 0, "can't read %s: %s", what == 0 ? "a date" : what == 1 ? "a time" : "a date and time",
			 why);
	return r;
}

/* the same, in another language ("de", "German") */
int64_t cby_dates_format_in(int64_t wall, const char *pat, const char *name)
{
	const Language *was = lang;
	lang = must_language(name);
	int64_t n = cby_dates_format(wall, pat);
	lang = was;
	return n;
}

int cby_dates_can_read_in(const char *text, const char *pat, int64_t what, const char *name)
{
	const Language *was = lang;
	lang = must_language(name);
	int64_t r;
	int ok = try_read(text, pat, what, &r);
	lang = was;
	return ok;
}

int64_t cby_dates_read_in(const char *text, const char *pat, int64_t what, const char *name)
{
	const Language *was = lang;
	lang = must_language(name);
	int64_t r;
	bool ok = try_read(text, pat, what, &r);
	lang = was;
	if (!ok)
		rt_panic(NULL, 0, 0, "can't read %s: %s", what == 0 ? "a date" : what == 1 ? "a time" : "a date and time", why);
	return r;
}

/* ---- reading looser text: "tomorrow", "next tuesday", "in 3 days",
 * "25 Dec", "friday at 5pm", relative to a date and time given (now, here,
 * usually). In English. */

static const char *const loose_units[] = { "day", "week", "fortnight", "month", "year", "hour", "minute", NULL };
static const char *const loose_numbers[] = { "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
					     "nine", "ten", "eleven", "twelve", NULL };

static int word_in(const char *w, const char *const *list)
{
	for (int i = 0; list[i]; i++)
		if (strcmp(w, list[i]) == 0)
			return i;
	return -1;
}

/* a unit, singular or plural ("days"): its place in loose_units, or -1 */
static int loose_unit(const char *w)
{
	char one[32];
	snprintf(one, sizeof one, "%s", w);
	size_t n = strlen(one);
	if (n > 1 && one[n - 1] == 's')
		one[n - 1] = 0;
	if (strcmp(one, "min") == 0 || strcmp(one, "mins") == 0)
		return 6;
	if (strcmp(one, "hr") == 0)
		return 5;
	return word_in(one, loose_units);
}

/* a whole number, in digits or words ("3", "three", "a", "an") */
static bool loose_count(const char *w, int64_t *n)
{
	if (strcmp(w, "a") == 0 || strcmp(w, "an") == 0) {
		*n = 1;
		return true;
	}
	int k = word_in(w, loose_numbers);
	if (k >= 0) {
		*n = k;
		return true;
	}
	if (!*w)
		return false;
	for (const char *p = w; *p; p++)
		if (!isdigit((unsigned char)*p))
			return false;
	*n = atoll(w);
	return true;
}

/* a month's or weekday's name, in English, whole or short ("sept" too) */
static int loose_name(const char *w, bool month)
{
	const Language *en = &languages[0];
	int count = month ? 12 : 7;
	for (int i = 0; i < count; i++) {
		const char *full = month ? en->months[i] : en->days[i];
		const char *shortn = month ? en->months_short[i] : en->days_short[i];
		if (strcasecmp(w, full) == 0 || strcasecmp(w, shortn) == 0)
			return i;
	}
	if (month && strcmp(w, "sept") == 0)
		return 8;
	if (!month && (strcmp(w, "tues") == 0 || strcmp(w, "thur") == 0 || strcmp(w, "thurs") == 0 || strcmp(w, "weds") == 0))
		return w[0] == 't' ? (w[1] == 'u' ? 2 : 4) : 3;
	return -1;
}

/* a time of day: "17:30", "5pm", "5:30pm", "noon", "midnight" (am/pm may
 * be the next word) */
static bool loose_time(char **words, int n, int *i, int64_t *secs)
{
	const char *w = words[*i];
	if (strcmp(w, "noon") == 0 || strcmp(w, "midday") == 0) {
		*secs = 12 * 3600;
		return true;
	}
	if (strcmp(w, "midnight") == 0) {
		*secs = 0;
		return true;
	}
	int h = 0, m = 0, used = 0;
	if (sscanf(w, "%d:%d%n", &h, &m, &used) != 2 && sscanf(w, "%d%n", &h, &used) != 1)
		return false;
	const char *rest = w + used;
	int ampm = 0;   /* 1 am, 2 pm */
	if (strcmp(rest, "am") == 0 || strcmp(rest, "a.m.") == 0)
		ampm = 1;
	else if (strcmp(rest, "pm") == 0 || strcmp(rest, "p.m.") == 0)
		ampm = 2;
	else if (*rest)
		return false;
	bool colon = strchr(w, ':') != NULL;
	if (!ampm && *i + 1 < n && (strcmp(words[*i + 1], "am") == 0 || strcmp(words[*i + 1], "pm") == 0)) {
		ampm = words[*i + 1][0] == 'a' ? 1 : 2;
		(*i)++;
	} else if (!ampm && *i + 1 < n && strcmp(words[*i + 1], "o'clock") == 0) {
		(*i)++;
	} else if (!ampm && !colon) {
		return false;   /* (a number alone isn't a time: "3" could be a day) */
	}
	if (m < 0 || m > 59 || h < 0 || h > (ampm ? 12 : 23) || (ampm && h == 0))
		return false;
	if (ampm)
		h = h % 12 + (ampm == 2 ? 12 : 0);
	*secs = h * 3600 + m * 60;
	return true;
}

static _Thread_local char loose_why[300];

/* what: 0 a date, 2 a date and time. now: the date and time it's read
 * from (nanoseconds since 1970 on the same clock). */
static bool loose_read(const char *text, int64_t now, int64_t what, int64_t *result)
{
	/* small letters, no commas, "3rd" as "3", and words apart */
	char buf[256];
	size_t k = 0;
	for (const char *p = text; *p && k + 1 < sizeof buf; p++) {
		char c = (char)tolower((unsigned char)*p);
		buf[k++] = c == ',' ? ' ' : c;
	}
	buf[k] = 0;
	char *words[64];
	int n = 0;
	for (char *t = strtok(buf, " \t\n"); t && n < 64; t = strtok(NULL, " \t\n")) {
		size_t len = strlen(t);
		if (len > 2 && isdigit((unsigned char)t[len - 3]) &&
		    (!strcmp(t + len - 2, "st") || !strcmp(t + len - 2, "nd") || !strcmp(t + len - 2, "rd") || !strcmp(t + len - 2, "th")))
			t[len - 2] = 0;
		if (!strcmp(t, "the") || !strcmp(t, "of") || !strcmp(t, "on") || !strcmp(t, "at") || !strcmp(t, "this"))
			continue;
		words[n++] = t;
	}
	int64_t today = floor_div(now, NS_PER_DAY);
	int64_t day = today;
	int64_t secs = 0;           /* the time of day */
	bool said_day = false, said_time = false, from_now = false;
	int64_t shift = 0;          /* "in 3 hours": seconds on from now */
	if (n == 0) {
		snprintf(loose_why, sizeof loose_why, "there's nothing in '%s' to read", text);
		return false;
	}
	for (int i = 0; i < n; i++) {
		const char *w = words[i];
		int64_t num;
		int k2;
		if (!strcmp(w, "today")) {
			said_day = true;
		} else if (!strcmp(w, "now")) {
			said_day = said_time = from_now = true;
		} else if (!strcmp(w, "tomorrow") || !strcmp(w, "tmrw")) {
			day = today + 1;
			said_day = true;
		} else if (!strcmp(w, "yesterday")) {
			day = today - 1;
			said_day = true;
		} else if (!strcmp(w, "day") && i + 2 < n && !strcmp(words[i + 1], "after") && !strcmp(words[i + 2], "tomorrow")) {
			day = today + 2;
			said_day = true;
			i += 2;
		} else if (!strcmp(w, "day") && i + 2 < n && !strcmp(words[i + 1], "before") && !strcmp(words[i + 2], "yesterday")) {
			day = today - 2;
			said_day = true;
			i += 2;
		} else if ((!strcmp(w, "next") || !strcmp(w, "last")) && i + 1 < n) {
			bool next = w[0] == 'n';
			const char *x = words[++i];
			int wd = loose_name(x, false);
			int unit = loose_unit(x);
			if (wd >= 0) {
				/* next tuesday: the first after today; last tuesday: the last before */
				int64_t now_wd = floor_mod(today + 4, 7);
				int64_t d = next ? floor_mod(wd - now_wd - 1, 7) + 1 : -(floor_mod(now_wd - wd - 1, 7) + 1);
				day = today + d;
			} else if (unit == 0 || unit == 1 || unit == 2) {
				day = today + (next ? 1 : -1) * (unit == 0 ? 1 : unit == 1 ? 7 : 14);
			} else if (unit == 3 || unit == 4) {
				day = cby_date_add_months(today, (next ? 1 : -1) * (unit == 3 ? 1 : 12));
			} else {
				snprintf(loose_why, sizeof loose_why, "'%s %s' in '%s' isn't something it knows", w, x, text);
				return false;
			}
			said_day = true;
		} else if (!strcmp(w, "in") && i + 2 < n && loose_count(words[i + 1], &num) && (k2 = loose_unit(words[i + 2])) >= 0) {
			/* in 3 days, in a week, in 2 hours */
			i += 2;
			if (k2 >= 5) {
				shift += num * (k2 == 5 ? 3600 : 60);
				from_now = said_time = said_day = true;
			} else if (k2 >= 3) {
				day = cby_date_add_months(day, num * (k2 == 3 ? 1 : 12));
				said_day = true;
			} else {
				day += num * (k2 == 0 ? 1 : k2 == 1 ? 7 : 14);
				said_day = true;
			}
		} else if (loose_count(w, &num) && i + 2 < n && (k2 = loose_unit(words[i + 1])) >= 0 &&
			   (!strcmp(words[i + 2], "ago") || !strcmp(words[i + 2], "later") || !strcmp(words[i + 2], "from"))) {
			/* 3 days ago, 2 weeks later, 2 days from now */
			int sign = !strcmp(words[i + 2], "ago") ? -1 : 1;
			i += 2;
			if (!strcmp(words[i], "from") && i + 1 < n && !strcmp(words[i + 1], "now"))
				i++;
			if (k2 >= 5) {
				shift += sign * num * (k2 == 5 ? 3600 : 60);
				from_now = said_time = said_day = true;
			} else if (k2 >= 3) {
				day = cby_date_add_months(day, sign * num * (k2 == 3 ? 1 : 12));
				said_day = true;
			} else {
				day += sign * num * (k2 == 0 ? 1 : k2 == 1 ? 7 : 14);
				said_day = true;
			}
		} else if ((k2 = loose_name(w, false)) >= 0) {
			/* tuesday: the next one, or today */
			int64_t now_wd = floor_mod(today + 4, 7);
			day = today + floor_mod(k2 - now_wd, 7);
			said_day = true;
		} else if (loose_time(words, n, &i, &secs)) {
			said_time = true;
		} else {
			/* a written date: 2026-11-03, 3/11/2026, 3.11.2026, 3 nov [2026],
			 * nov 3 [2026] */
			int64_t y = -1, mo = -1, d = -1;
			int a, b, c;
			char tail;
			if (sscanf(w, "%d-%d-%d%c", &a, &b, &c, &tail) == 3 && a > 31) {
				y = a; mo = b; d = c;
			} else if (sscanf(w, "%d/%d/%d%c", &a, &b, &c, &tail) == 3 || sscanf(w, "%d.%d.%d%c", &a, &b, &c, &tail) == 3) {
				d = a; mo = b; y = c < 100 ? 2000 + c : c;   /* (the day first) */
			} else if (sscanf(w, "%d/%d%c", &a, &b, &tail) == 2 || (sscanf(w, "%d.%d%c", &a, &b, &tail) == 2 && strchr(w, '.'))) {
				d = a; mo = b;
			} else if (loose_count(w, &num) && i + 1 < n && (k2 = loose_name(words[i + 1], true)) >= 0) {
				d = num; mo = k2 + 1;
				i++;
			} else if ((k2 = loose_name(w, true)) >= 0 && i + 1 < n && loose_count(words[i + 1], &num) && num <= 31) {
				mo = k2 + 1; d = num;
				i++;
			} else {
				snprintf(loose_why, sizeof loose_why, "can't make sense of '%s' in '%s'", w, text);
				return false;
			}
			if (y < 0 && i + 1 < n && strlen(words[i + 1]) == 4 && loose_count(words[i + 1], &num)) {
				y = num;
				i++;
			}
			int64_t yy, mm, dd;
			civil_from_days(today, &yy, &mm, &dd);
			bool no_year = y < 0;
			if (no_year)
				y = yy;
			if (!cby_date_valid(y, mo, d)) {
				snprintf(loose_why, sizeof loose_why, "'%s' isn't a date", text);
				return false;
			}
			day = cby_days_from_civil(y, mo, d);
			if (no_year && day < today && cby_date_valid(y + 1, mo, d))
				day = cby_days_from_civil(y + 1, mo, d);   /* (no year: the next one) */
			said_day = true;
		}
	}
	(void)said_day;
	if (what == 0 && said_time && !from_now && !said_day) {
		snprintf(loose_why, sizeof loose_why, "'%s' is a time, not a date", text);
		return false;
	}
	int64_t now_secs = floor_mod(now, NS_PER_DAY) / NS_PER_S;
	if (from_now && !(said_time && secs) ) {
		/* "now", "in 2 hours": from the time it is */
		int64_t t = (day - today) * 86400 + now_secs + shift;
		day = today + floor_div(t, 86400);
		secs = floor_mod(t, 86400);
	}
	*result = what == 0 ? day : day * NS_PER_DAY + secs * NS_PER_S;
	return true;
}

int cby_dates_can_read_loosely(const char *text, int64_t now, int64_t what)
{
	int64_t r;
	return loose_read(text, now, what, &r);
}

int64_t cby_dates_read_loosely(const char *text, int64_t now, int64_t what)
{
	int64_t r;
	if (!loose_read(text, now, what, &r))
		rt_panic(NULL, 0, 0, "can't read %s: %s", what == 0 ? "a date" : "a date and time", loose_why);
	return r;
}
