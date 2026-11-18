/* zones.c — named time zones ("Europe/Berlin") for the dates library, the
 * same way on Linux and macOS.
 *
 * Both keep the world's time zones as files, one for each, in
 * /usr/share/zoneinfo (the IANA time zone database, in TZif form): when
 * each change of offset happened (daylight saving on and off, a country
 * moving zone), and, at the end, a rule for the years after the last
 * change, written as POSIX's TZ variable is ("CET-1CEST,M3.5.0,M10.5.0/3":
 * an hour ahead of UTC, two in summer, from the last Sunday of March to
 * the last Sunday of October). They're read here, not through the C
 * library's own (which works only with the one zone of the whole program,
 * and not on a thread of its own).
 *
 * CABBY_ZONEINFO names another folder of them (for the tests). */
#include "rt.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

extern int64_t cby_days_from_civil(int64_t y, int64_t m, int64_t d);

static int64_t floor_div_(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

/* text back to Cabby: its length, then copied into a str that long */
static _Thread_local char *text_out;

static void text_ends(void) { free(text_out); text_out = NULL; }

static int64_t give_text(char *t)
{
	static _Thread_local bool hooked;
	if (!hooked) {
		hooked = true;
		rt_on_thread_end(text_ends);
	}
	free(text_out);
	text_out = t;
	return (int64_t)strlen(t);
}

void cby_zone_take_text(char *dst)
{
	if (text_out)
		memcpy(dst, text_out, strlen(text_out));
}
/* a POSIX TZ rule: a zone's offsets, and when summer time starts and ends */
typedef struct {
	int kind;          /* 'M': month, week, weekday; 'J': day 1-365, no 29 Feb; 'D': day 0-365 */
	int month, week, wday, day;
	int32_t time;      /* seconds after midnight, local time (may be < 0 or > 24 h) */
} Change;

typedef struct {
	int32_t std_off, dst_off;   /* seconds east of UTC */
	char std_name[16], dst_name[16];
	bool has_dst;
	Change start, end;
} Rule;

typedef struct {
	char *name;
	int64_t *at;       /* when each change happened, seconds since 1970 (UTC) */
	uint8_t *type;     /* and to which of the types */
	int n;
	int32_t *off;      /* each type's offset */
	uint8_t *dst;
	char **abbr;
	int ntypes;
	bool has_rule;
	Rule rule;
} Zone;

static Zone **zones;
static int nzones;
static pthread_mutex_t zones_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *zone_folder(void)
{
	const char *f = getenv("CABBY_ZONEINFO");
	if (f && *f)
		return f;
	if (access("/usr/share/zoneinfo/UTC", R_OK) == 0)
		return "/usr/share/zoneinfo";
	if (access("/var/db/timezone/zoneinfo/UTC", R_OK) == 0)
		return "/var/db/timezone/zoneinfo";   /* (where a Mac's really are) */
	return "/usr/share/zoneinfo";
}

/* ------------------------------------------------------------ POSIX rules */

static const char *rule_name(const char *p, char *out, size_t size)
{
	size_t n = 0;
	if (*p == '<') {
		p++;
		while (*p && *p != '>') {
			if (n + 1 < size)
				out[n++] = *p;
			p++;
		}
		if (*p == '>')
			p++;
	} else {
		while (isalpha((unsigned char)*p)) {
			if (n + 1 < size)
				out[n++] = *p;
			p++;
		}
	}
	out[n] = 0;
	return n ? p : NULL;
}

/* [+-]hh[:mm[:ss]], in seconds */
static const char *rule_time(const char *p, int32_t *secs)
{
	int sign = 1;
	if (*p == '+' || *p == '-')
		sign = *p++ == '-' ? -1 : 1;
	if (!isdigit((unsigned char)*p))
		return NULL;
	int32_t h = 0, m = 0, s = 0;
	while (isdigit((unsigned char)*p))
		h = h * 10 + (*p++ - '0');
	if (*p == ':') {
		p++;
		while (isdigit((unsigned char)*p))
			m = m * 10 + (*p++ - '0');
		if (*p == ':') {
			p++;
			while (isdigit((unsigned char)*p))
				s = s * 10 + (*p++ - '0');
		}
	}
	*secs = sign * (h * 3600 + m * 60 + s);
	return p;
}

static const char *rule_change(const char *p, Change *c)
{
	c->time = 7200;   /* (02:00, unless it says) */
	if (*p == 'M') {
		c->kind = 'M';
		if (sscanf(p + 1, "%d.%d.%d", &c->month, &c->week, &c->wday) != 3)
			return NULL;
		p++;
		while (*p && *p != '/' && *p != ',')
			p++;
	} else if (*p == 'J') {
		c->kind = 'J';
		c->day = atoi(p + 1);
		p++;
		while (isdigit((unsigned char)*p))
			p++;
	} else if (isdigit((unsigned char)*p)) {
		c->kind = 'D';
		c->day = atoi(p);
		while (isdigit((unsigned char)*p))
			p++;
	} else {
		return NULL;
	}
	if (*p == '/')
		p = rule_time(p + 1, &c->time);
	return p;
}

static bool read_rule(const char *s, Rule *r)
{
	memset(r, 0, sizeof *r);
	int32_t off;
	const char *p = rule_name(s, r->std_name, sizeof r->std_name);
	if (!p || !(p = rule_time(p, &off)))
		return false;
	r->std_off = -off;   /* (POSIX counts west of UTC) */
	if (!*p)
		return true;
	p = rule_name(p, r->dst_name, sizeof r->dst_name);
	if (!p)
		return false;
	r->has_dst = true;
	r->dst_off = r->std_off + 3600;
	if (*p && *p != ',') {
		if (!(p = rule_time(p, &off)))
			return false;
		r->dst_off = -off;
	}
	if (*p != ',') {
		/* (no rule said: the USA's, as POSIX has it) */
		r->start = (Change){ 'M', 3, 2, 0, 0, 7200 };
		r->end = (Change){ 'M', 11, 1, 0, 0, 7200 };
		return true;
	}
	if (!(p = rule_change(p + 1, &r->start)) || *p != ',' || !(p = rule_change(p + 1, &r->end)))
		return false;
	return true;
}

/* the day (since 1970) a change falls on, in a year */
static int64_t change_day(const Change *c, int64_t year)
{
	bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	if (c->kind == 'J')
		return cby_days_from_civil(year, 1, 1) + c->day - 1 + (leap && c->day >= 60);
	if (c->kind == 'D')
		return cby_days_from_civil(year, 1, 1) + c->day;
	/* the week'th (5: the last) of that weekday in the month */
	int64_t first = cby_days_from_civil(year, c->month, 1);
	int64_t wday_first = ((first % 7) + 7 + 4) % 7;   /* (1 January 1970 was a Thursday) */
	int64_t d = first + ((c->wday - wday_first) + 7) % 7 + (c->week - 1) * 7;
	int64_t next = c->month == 12 ? cby_days_from_civil(year + 1, 1, 1) : cby_days_from_civil(year, c->month + 1, 1);
	while (d >= next)
		d -= 7;
	return d;
}

/* the offset at t (UTC seconds), by the rule: the changes of the years
 * either side, in order, and the last before t says which time it is */
static int32_t rule_offset(const Rule *r, int64_t t, bool *dst, const char **abbr)
{
	*dst = false;
	*abbr = r->std_name;
	if (!r->has_dst)
		return r->std_off;
	int64_t year = 1970 + floor_div_(t + (int64_t)r->std_off, 31556952);
	int64_t when[6];
	bool to_dst[6];
	int n = 0;
	for (int64_t y = year - 1; y <= year + 1; y++) {
		/* (the start is written in standard time, the end in summer time) */
		when[n] = change_day(&r->start, y) * 86400 + r->start.time - r->std_off;
		to_dst[n++] = true;
		when[n] = change_day(&r->end, y) * 86400 + r->end.time - r->dst_off;
		to_dst[n++] = false;
	}
	int64_t best = INT64_MIN;
	bool in = false;
	for (int i = 0; i < n; i++)
		if (when[i] <= t && when[i] > best) {
			best = when[i];
			in = to_dst[i];
		}
	if (in) {
		*dst = true;
		*abbr = r->dst_name;
		return r->dst_off;
	}
	return r->std_off;
}

/* ------------------------------------------------------------ TZif files */

static int64_t be(const unsigned char *p, int n)
{
	int64_t v = 0;
	for (int i = 0; i < n; i++)
		v = (v << 8) | p[i];
	if (n == 4)
		v = (int32_t)(uint32_t)v;
	return v;
}

static Zone *read_zone(const char *name)
{
	/* (a name with .. in it could reach outside the folder) */
	if (!*name || strstr(name, "..") || name[0] == '/')
		return NULL;
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", zone_folder(), name);
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	unsigned char *d = NULL;
	size_t len = 0, cap = 0;
	for (;;) {
		if (len + 4096 > cap)
			d = realloc(d, cap = (cap + 4096) * 2);
		size_t got = fread(d + len, 1, 4096, f);
		len += got;
		if (got < 4096)
			break;
	}
	fclose(f);
	Zone *z = NULL;
	if (len < 44 || memcmp(d, "TZif", 4) != 0)
		goto done;
	/* version 1's 32-bit times first; from version 2, a second block in 64 bits */
	int v = d[4];
	size_t at = 0;
	int timesize = 4;
	for (int pass = 0; pass < 2; pass++) {
		if (at + 44 > len)
			goto done;
		const unsigned char *h = d + at;
		int64_t isut = be(h + 20, 4), isstd = be(h + 24, 4), leap = be(h + 28, 4), timecnt = be(h + 32, 4),
			typecnt = be(h + 36, 4), charcnt = be(h + 40, 4);
		if (timecnt < 0 || typecnt <= 0 || charcnt < 0 || timecnt > 100000 || typecnt > 256)
			goto done;
		size_t body = (size_t)(timecnt * timesize + timecnt + typecnt * 6 + charcnt + leap * (timesize + 4) + isstd + isut);
		if (at + 44 + body > len)
			goto done;
		if (pass == 0 && v >= '2') {
			at += 44 + body;
			timesize = 8;
			continue;
		}
		const unsigned char *p = h + 44;
		z = calloc(1, sizeof *z);
		z->name = strdup(name);
		z->n = (int)timecnt;
		z->at = malloc(sizeof *z->at * (size_t)(timecnt + 1));
		z->type = malloc((size_t)timecnt + 1);
		for (int64_t i = 0; i < timecnt; i++)
			z->at[i] = be(p + i * timesize, timesize);
		p += timecnt * timesize;
		memcpy(z->type, p, (size_t)timecnt);
		p += timecnt;
		z->ntypes = (int)typecnt;
		z->off = malloc(sizeof *z->off * (size_t)typecnt);
		z->dst = malloc((size_t)typecnt);
		z->abbr = malloc(sizeof *z->abbr * (size_t)typecnt);
		const unsigned char *chars = p + typecnt * 6;
		for (int64_t i = 0; i < typecnt; i++) {
			z->off[i] = (int32_t)be(p + i * 6, 4);
			z->dst[i] = p[i * 6 + 4];
			int ai = p[i * 6 + 5];
			z->abbr[i] = strndup(ai < charcnt ? (const char *)chars + ai : "", (size_t)(ai < charcnt ? charcnt - ai : 0));
		}
		for (int i = 0; i < z->n; i++)
			if (z->type[i] >= typecnt)
				z->type[i] = 0;
		/* the rule for after the last change: "\n<rule>\n" at the end */
		size_t foot = at + 44 + body;
		if (timesize == 8 && foot + 1 < len && d[foot] == '\n') {
			char rule[128];
			size_t k = 0;
			for (size_t i = foot + 1; i < len && d[i] != '\n' && k + 1 < sizeof rule; i++)
				rule[k++] = (char)d[i];
			rule[k] = 0;
			z->has_rule = k && read_rule(rule, &z->rule);
		}
		break;
	}
done:
	free(d);
	return z;
}

static Zone *zone_named(const char *name)
{
	pthread_mutex_lock(&zones_lock);
	for (int i = 0; i < nzones; i++)
		if (strcmp(zones[i]->name, name) == 0) {
			Zone *z = zones[i];
			pthread_mutex_unlock(&zones_lock);
			return z;
		}
	Zone *z = read_zone(name);
	if (z) {
		zones = realloc(zones, sizeof *zones * (size_t)(nzones + 1));
		zones[nzones++] = z;
	}
	pthread_mutex_unlock(&zones_lock);
	return z;
}

static Zone *must_zone(const char *name)
{
	Zone *z = zone_named(name);
	if (!z)
		rt_panic(NULL, 0, 0, "'%s' isn't a time zone (they're named by a continent and a city, as \"Europe/Berlin\" "
				     "or \"Australia/Sydney\"; dates.zones lists them all)", name);
	return z;
}

/* the offset (seconds east of UTC) at t (seconds since 1970, UTC) */
static int32_t zone_offset(const Zone *z, int64_t t, const char **abbr)
{
	bool dst;
	if (z->n == 0 || t < z->at[0]) {
		/* before the first change: the first type that isn't summer time */
		int k = 0;
		for (int i = 0; i < z->ntypes; i++)
			if (!z->dst[i]) {
				k = i;
				break;
			}
		if (z->n == 0 && z->has_rule)
			return rule_offset(&z->rule, t, &dst, abbr);
		*abbr = z->abbr[k];
		return z->off[k];
	}
	if (t >= z->at[z->n - 1] && z->has_rule)
		return rule_offset(&z->rule, t, &dst, abbr);
	int lo = 0, hi = z->n - 1;   /* the last change at or before t */
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if (z->at[mid] <= t)
			lo = mid;
		else
			hi = mid - 1;
	}
	*abbr = z->abbr[z->type[lo]];
	return z->off[z->type[lo]];
}

#define floor_div floor_div_

/* ------------------------------------------------------------ for Cabby */

/* seconds east of UTC in the zone, at a moment (nanoseconds) */
int64_t cby_zone_offset(const char *name, int64_t moment)
{
	const char *abbr;
	return zone_offset(must_zone(name), floor_div(moment, 1000000000), &abbr);
}

/* a date and time on the zone's clocks (nanoseconds since 1970 on that
 * clock) as a moment. A time the clocks skipped (moving forward an hour)
 * is taken as the one an hour on; one they showed twice (moving back), as
 * the first. */
int64_t cby_zone_to_moment(const char *name, int64_t wall)
{
	Zone *z = must_zone(name);
	const char *abbr;
	int64_t w = floor_div(wall, 1000000000), frac = wall - w * 1000000000;
	/* the offsets either side (a day away: changes are never so close) */
	int32_t before = zone_offset(z, w - 86400, &abbr), after = zone_offset(z, w + 86400, &abbr);
	bool a_ok = zone_offset(z, w - before, &abbr) == before;
	bool b_ok = zone_offset(z, w - after, &abbr) == after;
	int64_t t;
	if (a_ok && b_ok)
		t = w - (before > after ? before : after);   /* (shown twice: the first) */
	else if (a_ok)
		t = w - before;
	else if (b_ok)
		t = w - after;
	else
		t = w - before;   /* (skipped: what the clocks then showed, an hour on) */
	return t * 1000000000 + frac;
}

/* the zone's short name then ("CET", "AEDT") */
int64_t cby_zone_abbreviation(const char *name, int64_t moment)
{
	const char *abbr = "";
	zone_offset(must_zone(name), floor_div(moment, 1000000000), &abbr);
	return give_text(strdup(abbr));
}

int cby_zone_exists(const char *name) { return zone_named(name) != NULL; }

/* this computer's time zone's name ("Australia/Sydney"): from TZ, or
 * where /etc/localtime points into the zone files; "" if it can't be told */
int64_t cby_zone_here(void)
{
	const char *tz = getenv("TZ");
	if (tz && *tz) {
		if (*tz == ':')
			tz++;
		if (zone_named(tz))
			return give_text(strdup(tz));
	}
	char link[1024];
	ssize_t n = readlink("/etc/localtime", link, sizeof link - 1);
	if (n > 0) {
		link[n] = 0;
		const char *z = strstr(link, "zoneinfo/");
		if (z)
			return give_text(strdup(z + 9));
	}
	return give_text(strdup(""));
}

/* the zones there are, one per line, from the database's own list
 * (zone1970.tab: one for each place with clocks of its own since 1970) */
int64_t cby_zone_list(void)
{
	char path[1024];
	static const char *const lists[] = { "zone1970.tab", "zone.tab", NULL };
	FILE *f = NULL;
	for (int i = 0; lists[i] && !f; i++) {
		snprintf(path, sizeof path, "%s/%s", zone_folder(), lists[i]);
		f = fopen(path, "r");
	}
	char *all = malloc(1);
	size_t len = 0;
	all[0] = 0;
	if (f) {
		char line[512];
		while (fgets(line, sizeof line, f)) {
			if (line[0] == '#')
				continue;
			/* codes, coordinates, the zone's name, a comment: tabs between */
			char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
			if (!t2)
				continue;
			char *name = t2 + 1;
			name[strcspn(name, "\t\r\n")] = 0;
			size_t k = strlen(name);
			all = realloc(all, len + k + 2);
			if (len)
				all[len++] = '\n';
			memcpy(all + len, name, k + 1);
			len += k;
		}
		fclose(f);
	}
	return give_text(all);
}

__attribute__((destructor)) static void zones_end(void)
{
	for (int i = 0; i < nzones; i++) {
		Zone *z = zones[i];
		for (int k = 0; k < z->ntypes; k++)
			free(z->abbr[k]);
		free(z->abbr);
		free(z->off);
		free(z->dst);
		free(z->at);
		free(z->type);
		free(z->name);
		free(z);
	}
	free(zones);
}
