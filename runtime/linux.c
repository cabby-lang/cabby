/* linux.c — the linux library's C side: what only Linux has, read from
 * the files the kernel keeps for it. Sensors (temperatures, fans, voltages,
 * power) from /sys/class/hwmon and /sys/class/thermal; the disks and their
 * partitions from /sys/block; USB devices from /sys/bus/usb/devices; the
 * kernel's modules from /proc/modules, and its settings in /proc/sys.
 * (Services are systemd's, asked with systemctl: written in Cabby, in
 * linux.hby.)
 *
 * CABBY_SYSFS and CABBY_PROCFS name folders to read instead of /sys and
 * /proc: the tests' own, the same everywhere. On a Mac, which has none of
 * these, each is a panic that says so (unless the tests have said where
 * they are). */
#include "rt.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *sysfs(void)
{
	const char *s = getenv("CABBY_SYSFS");
	return s && *s ? s : "/sys";
}

static const char *procfs(void)
{
	const char *s = getenv("CABBY_PROCFS");
	return s && *s ? s : "/proc";
}

static void linux_only(void)
{
#ifndef __linux__
	if (!getenv("CABBY_SYSFS"))
		rt_panic(NULL, 0, 0, "%s", "the linux library is for Linux: a Mac has none of this (use os, or system.kind to tell which)");
#endif
}

/* ------------------------------------------------------------ text, to Cabby */

static _Thread_local char *given;

static void given_ends(void)
{
	free(given);
	given = NULL;
}

static int64_t give_copy(const char *t)
{
	static _Thread_local int hooked;
	if (!hooked) {
		hooked = 1;
		rt_on_thread_end(given_ends);
	}
	free(given);
	given = strdup(t ? t : "");
	return (int64_t)strlen(given);
}

void cby_linux_take(char *dst)
{
	if (given)
		memcpy(dst, given, strlen(given));
}

/* a small file of the kernel's, without its line end: NULL if it isn't there */
static char *read_line_of(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;
	char buf[4096];
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '\t'))
		buf[--n] = 0;
	return strdup(buf);
}

static char *read_in(const char *dir, const char *name)
{
	size_t n = strlen(dir) + strlen(name) + 2;
	char *path = malloc(n);
	snprintf(path, n, "%s/%s", dir, name);
	char *got = read_line_of(path);
	free(path);
	return got;
}

static long long number_in(const char *dir, const char *name, long long none)
{
	char *s = read_in(dir, name);
	if (!s)
		return none;
	char *end;
	long long v = strtoll(s, &end, 10);
	if (end == s)
		v = none;
	free(s);
	return v;
}

static int by_text(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* a folder's names, sorted, as the kernel numbers them (hwmon2 before
 * hwmon10 needs no more than this: they're compared as text) */
static char **names_in(const char *dir, int *n)
{
	*n = 0;
	DIR *d = opendir(dir);
	if (!d)
		return NULL;
	char **names = NULL;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		names = realloc(names, sizeof *names * (size_t)(*n + 1));
		names[(*n)++] = strdup(de->d_name);
	}
	closedir(d);
	qsort(names, (size_t)*n, sizeof *names, by_text);
	return names;
}

static void names_free(char **names, int n)
{
	for (int i = 0; i < n; i++)
		free(names[i]);
	free(names);
}

/* ============================================================ sensors */

typedef struct {
	char *chip, *label, *kind;
	double value, high, critical;
} Sensor;

static Sensor *sensors;
static int nsensors;

static void sensors_forget(void)
{
	for (int i = 0; i < nsensors; i++) {
		free(sensors[i].chip);
		free(sensors[i].label);
		free(sensors[i].kind);
	}
	free(sensors);
	sensors = NULL;
	nsensors = 0;
}

static void add_sensor(const char *chip, const char *label, const char *kind, double value, double high, double critical)
{
	sensors = realloc(sensors, sizeof *sensors * (size_t)(nsensors + 1));
	sensors[nsensors++] = (Sensor){ strdup(chip), strdup(label), strdup(kind), value, high, critical };
}

/* one hwmon chip's readings: temp1_input (thousandths of a degree),
 * fan1_input (turns a minute), in0_input (thousandths of a volt),
 * power1_input (millionths of a watt), each perhaps with a _label */
static void read_chip(const char *dir, const char *chip)
{
	static const struct {
		const char *prefix, *kind;
		double scale;
	} kinds[] = {
		{ "temp", "temperature", 1000 }, { "fan", "fan", 1 }, { "in", "voltage", 1000 }, { "power", "power", 1000000 },
		{ "curr", "current", 1000 },
	};
	int n;
	char **names = names_in(dir, &n);
	for (size_t k = 0; k < sizeof kinds / sizeof *kinds; k++) {
		size_t pl = strlen(kinds[k].prefix);
		for (int i = 0; i < n; i++) {
			const char *nm = names[i];
			size_t len = strlen(nm);
			if (strncmp(nm, kinds[k].prefix, pl) != 0 || len < pl + 7 || strcmp(nm + len - 6, "_input") != 0)
				continue;
			/* (only digits between: "temp1_input", not "temperature_input") */
			int digits = 1;
			for (size_t c = pl; c < len - 6; c++)
				digits &= nm[c] >= '0' && nm[c] <= '9';
			if (!digits)
				continue;
			char base[64];
			snprintf(base, sizeof base, "%.*s", (int)(len - 6), nm);
			long long raw = number_in(dir, nm, LLONG_MIN);
			if (raw == LLONG_MIN)
				continue;   /* (a sensor that can't be read now: a disk asleep) */
			char f[96];
			snprintf(f, sizeof f, "%s_label", base);
			char *label = read_in(dir, f);
			snprintf(f, sizeof f, "%s_max", base);
			long long high = number_in(dir, f, 0);
			snprintf(f, sizeof f, "%s_crit", base);
			long long crit = number_in(dir, f, 0);
			add_sensor(chip, label ? label : base, kinds[k].kind, (double)raw / kinds[k].scale, (double)high / kinds[k].scale,
				   (double)crit / kinds[k].scale);
			free(label);
		}
	}
	names_free(names, n);
}

/* the sensors: a snapshot, its count */
int64_t cby_linux_sensors(void)
{
	linux_only();
	sensors_forget();
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/class/hwmon", sysfs());
	int n;
	char **chips = names_in(dir, &n);
	int temps = 0;
	for (int i = 0; i < n; i++) {
		char at[PATH_MAX + 512];
		snprintf(at, sizeof at, "%s/%s", dir, chips[i]);
		char *name = read_in(at, "name");
		int before = nsensors;
		read_chip(at, name ? name : chips[i]);
		if (nsensors == before) {
			/* (older kernels keep them in the device's folder) */
			char dev[PATH_MAX + 600];
			snprintf(dev, sizeof dev, "%s/device", at);
			read_chip(dev, name ? name : chips[i]);
		}
		for (int k = before; k < nsensors; k++)
			temps += strcmp(sensors[k].kind, "temperature") == 0;
		free(name);
	}
	names_free(chips, n);
	/* thermal zones, where hwmon has no temperatures (some ARM boards) */
	if (!temps) {
		snprintf(dir, sizeof dir, "%s/class/thermal", sysfs());
		char **zones = names_in(dir, &n);
		for (int i = 0; i < n; i++) {
			if (strncmp(zones[i], "thermal_zone", 12) != 0)
				continue;
			char at[PATH_MAX + 512];
			snprintf(at, sizeof at, "%s/%s", dir, zones[i]);
			long long t = number_in(at, "temp", LLONG_MIN);
			char *type = read_in(at, "type");
			if (t != LLONG_MIN)
				add_sensor(type ? type : zones[i], zones[i], "temperature", (double)t / 1000, 0, 0);
			free(type);
		}
		names_free(zones, n);
	}
	return nsensors;
}

static Sensor *sensor_at(int64_t i)
{
	if (i < 0 || i >= nsensors)
		rt_panic(NULL, 0, 0, "%s", "there's no sensor there");
	return &sensors[i];
}

/* 0 its chip, 1 its label, 2 its kind */
int64_t cby_linux_sensor_text(int64_t i, int64_t what)
{
	Sensor *s = sensor_at(i);
	return give_copy(what == 0 ? s->chip : what == 1 ? s->label : s->kind);
}

/* 0 what it reads, 1 high, 2 critical */
double cby_linux_sensor_value(int64_t i, int64_t what)
{
	Sensor *s = sensor_at(i);
	return what == 0 ? s->value : what == 1 ? s->high : s->critical;
}

/* ============================================================ block devices */

typedef struct {
	char *name, *kind, *model, *parent, *mounted, *label;
	long long size;
	int removable, ssd, read_only;
} Block;

static Block *blocks;
static int nblocks;

static void blocks_forget(void)
{
	for (int i = 0; i < nblocks; i++) {
		free(blocks[i].name);
		free(blocks[i].kind);
		free(blocks[i].model);
		free(blocks[i].parent);
		free(blocks[i].mounted);
		free(blocks[i].label);
	}
	free(blocks);
	blocks = NULL;
	nblocks = 0;
}

/* where /dev/<name> is mounted ("" if it isn't) */
static char *mounted_at(const char *name)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/self/mounts", procfs());
	FILE *f = fopen(path, "r");
	if (!f)
		return strdup("");
	char dev[4096], want[PATH_MAX], line[4096], *found = NULL;
	snprintf(want, sizeof want, "/dev/%s", name);
	while (!found && fgets(line, sizeof line, f)) {
		char mnt[4096];
		if (sscanf(line, "%4095s %4095s", dev, mnt) != 2)
			continue;
		char real[PATH_MAX];
		if (strcmp(dev, want) == 0 || (realpath(dev, real) && strcmp(real, want) == 0)) {
			/* (\040 for a space) */
			char *o = mnt;
			for (char *p = mnt; *p; p++)
				if (p[0] == '\\' && p[1] && p[2] && p[3]) {
					*o++ = (char)((p[1] - '0') * 64 + (p[2] - '0') * 8 + (p[3] - '0'));
					p += 3;
				} else {
					*o++ = *p;
				}
			*o = 0;
			found = strdup(mnt);
		}
	}
	fclose(f);
	return found ? found : strdup("");
}

/* its label (/dev/disk/by-label, or the tests' CABBY_DEVFS's) */
static char *label_for(const char *name)
{
	const char *dev = getenv("CABBY_DEVFS");
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/disk/by-label", dev && *dev ? dev : "/dev");
	int n;
	char **labels = names_in(dir, &n);
	char *found = NULL;
	for (int i = 0; i < n && !found; i++) {
		char link[PATH_MAX + 300], to[PATH_MAX];
		snprintf(link, sizeof link, "%s/%s", dir, labels[i]);
		ssize_t len = readlink(link, to, sizeof to - 1);
		if (len <= 0)
			continue;
		to[len] = 0;
		const char *base = strrchr(to, '/');
		if (strcmp(base ? base + 1 : to, name) == 0) {
			found = strdup(labels[i]);
			char *o = found;
			for (char *p = found; *p; p++)
				if (p[0] == '\\' && p[1] == 'x' && p[2] && p[3]) {
					char hex[3] = { p[2], p[3], 0 };
					*o++ = (char)strtol(hex, NULL, 16);
					p += 3;
				} else {
					*o++ = *p;
				}
			*o = 0;
		}
	}
	names_free(labels, n);
	return found ? found : strdup("");
}

static void add_block(const char *dir, const char *name, const char *kind, const char *parent, int removable, int ssd,
		      const char *model)
{
	blocks = realloc(blocks, sizeof *blocks * (size_t)(nblocks + 1));
	Block *b = &blocks[nblocks++];
	b->name = strdup(name);
	b->kind = strdup(kind);
	b->model = strdup(model ? model : "");
	b->parent = strdup(parent);
	b->size = number_in(dir, "size", 0) * 512;   /* (always counted in 512-byte sectors) */
	b->removable = removable;
	b->ssd = ssd;
	b->read_only = number_in(dir, "ro", 0) != 0;
	b->mounted = mounted_at(name);
	b->label = label_for(name);
}

/* the disks and their partitions: a snapshot, its count */
int64_t cby_linux_block_devices(void)
{
	linux_only();
	blocks_forget();
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/block", sysfs());
	int n;
	char **disks = names_in(dir, &n);
	for (int i = 0; i < n; i++) {
		const char *name = disks[i];
		if (strncmp(name, "loop", 4) == 0 || strncmp(name, "ram", 3) == 0 || strncmp(name, "zram", 4) == 0)
			continue;   /* (not disks: files and memory, pretending) */
		char at[PATH_MAX + 512];
		snprintf(at, sizeof at, "%s/%s", dir, name);
		char *model = read_in(at, "device/model");
		char *vendor = read_in(at, "device/vendor");
		char whole[512];
		snprintf(whole, sizeof whole, "%s%s%s", vendor && *vendor ? vendor : "", vendor && *vendor && model ? " " : "",
			 model ? model : "");
		int removable = number_in(at, "removable", 0) != 0;
		/* (a USB disk or an SD card can be taken out, whatever it says) */
		char real[PATH_MAX];
		if (realpath(at, real) && (strstr(real, "/usb") || strstr(real, "/mmc")))
			removable = 1;
		int ssd = number_in(at, "queue/rotational", 1) == 0;
		add_block(at, name, "disk", "", removable, ssd, whole);
		free(model);
		free(vendor);
		int np;
		char **parts = names_in(at, &np);
		for (int k = 0; k < np; k++) {
			char pat[PATH_MAX + 1024];
			snprintf(pat, sizeof pat, "%s/%s", at, parts[k]);
			char partition[PATH_MAX + 1100];
			snprintf(partition, sizeof partition, "%s/partition", pat);
			struct stat st;
			if (stat(partition, &st) == 0)
				add_block(pat, parts[k], "partition", name, removable, ssd, "");
		}
		names_free(parts, np);
	}
	names_free(disks, n);
	return nblocks;
}

static Block *block_at(int64_t i)
{
	if (i < 0 || i >= nblocks)
		rt_panic(NULL, 0, 0, "%s", "there's no disk there");
	return &blocks[i];
}

/* 0 name, 1 kind, 2 model, 3 the disk it's part of, 4 where it's mounted,
 * 5 its label */
int64_t cby_linux_block_text(int64_t i, int64_t what)
{
	Block *b = block_at(i);
	const char *t[] = { b->name, b->kind, b->model, b->parent, b->mounted, b->label };
	return give_copy(what >= 0 && what < 6 ? t[what] : "");
}

/* 0 size, 1 removable, 2 an SSD (not spinning), 3 read only */
int64_t cby_linux_block_number(int64_t i, int64_t what)
{
	Block *b = block_at(i);
	return what == 0 ? b->size : what == 1 ? b->removable : what == 2 ? b->ssd : b->read_only;
}

/* ============================================================ USB devices */

typedef struct {
	char *name, *maker, *vendor, *product, *serial;
	long long bus, number, speed;
} Usb;

static Usb *usbs;
static int nusbs;

static void usbs_forget(void)
{
	for (int i = 0; i < nusbs; i++) {
		free(usbs[i].name);
		free(usbs[i].maker);
		free(usbs[i].vendor);
		free(usbs[i].product);
		free(usbs[i].serial);
	}
	free(usbs);
	usbs = NULL;
	nusbs = 0;
}

/* the USB devices plugged in (not the computer's own hubs): a snapshot */
int64_t cby_linux_usb_devices(void)
{
	linux_only();
	usbs_forget();
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/bus/usb/devices", sysfs());
	int n;
	char **devs = names_in(dir, &n);
	for (int i = 0; i < n; i++) {
		/* ("1-2", "1-2.4": devices; "usb1": the computer's own; "1-2:1.0": a part of one) */
		if (strncmp(devs[i], "usb", 3) == 0 || strchr(devs[i], ':'))
			continue;
		char at[PATH_MAX + 512];
		snprintf(at, sizeof at, "%s/%s", dir, devs[i]);
		char *vendor = read_in(at, "idVendor");
		if (!vendor)
			continue;
		char *product = read_in(at, "idProduct");
		char *name = read_in(at, "product");
		char *maker = read_in(at, "manufacturer");
		char *serial = read_in(at, "serial");
		usbs = realloc(usbs, sizeof *usbs * (size_t)(nusbs + 1));
		usbs[nusbs++] = (Usb){ name ? name : strdup(""), maker ? maker : strdup(""), vendor, product ? product : strdup(""),
				       serial ? serial : strdup(""), number_in(at, "busnum", 0), number_in(at, "devnum", 0),
				       number_in(at, "speed", 0) };
	}
	names_free(devs, n);
	return nusbs;
}

static Usb *usb_at(int64_t i)
{
	if (i < 0 || i >= nusbs)
		rt_panic(NULL, 0, 0, "%s", "there's no USB device there");
	return &usbs[i];
}

/* 0 name, 1 maker, 2 vendor id, 3 product id, 4 serial */
int64_t cby_linux_usb_text(int64_t i, int64_t what)
{
	Usb *u = usb_at(i);
	const char *t[] = { u->name, u->maker, u->vendor, u->product, u->serial };
	return give_copy(what >= 0 && what < 5 ? t[what] : "");
}

/* 0 bus, 1 its number on it, 2 speed (Mbit/s) */
int64_t cby_linux_usb_number(int64_t i, int64_t what)
{
	Usb *u = usb_at(i);
	return what == 0 ? u->bus : what == 1 ? u->number : u->speed;
}

/* ============================================================ modules */

typedef struct {
	char *name, *users;
	long long size, used;
} Module;

static Module *modules;
static int nmodules;

static void modules_forget(void)
{
	for (int i = 0; i < nmodules; i++) {
		free(modules[i].name);
		free(modules[i].users);
	}
	free(modules);
	modules = NULL;
	nmodules = 0;
}

/* the kernel's modules loaded (/proc/modules: "name size used users state
 * address"), sorted by name */
static int by_module(const void *a, const void *b) { return strcmp(((const Module *)a)->name, ((const Module *)b)->name); }

int64_t cby_linux_modules(void)
{
	linux_only();
	modules_forget();
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/modules", procfs());
	FILE *f = fopen(path, "r");
	char line[4096];
	while (f && fgets(line, sizeof line, f)) {
		char name[256], users[3000];
		long long size, used;
		if (sscanf(line, "%255s %lld %lld %2999s", name, &size, &used, users) != 4)
			continue;
		/* ("a,b," or "-": who uses it) */
		size_t ul = strlen(users);
		if (strcmp(users, "-") == 0)
			users[0] = 0;
		else if (ul && users[ul - 1] == ',')
			users[ul - 1] = 0;
		modules = realloc(modules, sizeof *modules * (size_t)(nmodules + 1));
		modules[nmodules++] = (Module){ strdup(name), strdup(users), size, used };
	}
	if (f)
		fclose(f);
	qsort(modules, (size_t)nmodules, sizeof *modules, by_module);
	return nmodules;
}

static Module *module_at(int64_t i)
{
	if (i < 0 || i >= nmodules)
		rt_panic(NULL, 0, 0, "%s", "there's no module there");
	return &modules[i];
}

/* 0 name, 1 those using it ("a,b") */
int64_t cby_linux_module_text(int64_t i, int64_t what)
{
	Module *m = module_at(i);
	return give_copy(what == 0 ? m->name : m->users);
}

/* 0 size, 1 how many use it */
int64_t cby_linux_module_number(int64_t i, int64_t what)
{
	Module *m = module_at(i);
	return what == 0 ? m->size : m->used;
}

/* ============================================================ the kernel's settings */

/* "net.ipv4.ip_forward" to /proc/sys/net/ipv4/ip_forward */
static char *setting_path(const char *name)
{
	if (!*name || strstr(name, "..") || name[0] == '/' || name[0] == '.')
		rt_panic(NULL, 0, 0, "'%s' isn't a setting's name: they're written as sysctl writes them, \"net.ipv4.ip_forward\"", name);
	size_t n = strlen(procfs()) + strlen(name) + 8;
	char *p = malloc(n);
	snprintf(p, n, "%s/sys/", procfs());
	size_t at = strlen(p);
	for (const char *c = name; *c; c++)
		p[at++] = *c == '.' ? '/' : *c;
	p[at] = 0;
	return p;
}

/* its value ("1"); a panic if there's no such setting */
int64_t cby_linux_setting(const char *name)
{
	linux_only();
	char *path = setting_path(name);
	char *v = read_line_of(path);
	free(path);
	if (!v)
		rt_panic(NULL, 0, 0, "there's no setting called '%s' (or it can't be read)", name);
	/* (tabs between a setting's several values, as sysctl shows them: spaces) */
	for (char *c = v; *c; c++)
		if (*c == '\t')
			*c = ' ';
	int64_t len = give_copy(v);
	free(v);
	return len;
}

int cby_linux_has_setting(const char *name)
{
	linux_only();
	char *path = setting_path(name);
	struct stat st;
	int there = stat(path, &st) == 0 && S_ISREG(st.st_mode);
	free(path);
	return there;
}

/* changed until the computer starts again (it needs the administrator):
 * true if it was */
int cby_linux_set_setting(const char *name, const char *value)
{
	linux_only();
	char *path = setting_path(name);
	FILE *f = fopen(path, "w");
	free(path);
	if (!f)
		return 0;
	int ok = fputs(value, f) >= 0;
	ok = fclose(f) == 0 && ok;
	return ok;
}

void cby_linux_no_service(const char *name)
{
	rt_panic(NULL, 0, 0, "there's no service called '%s' (systemctl list-units --type=service lists them)", name);
}

__attribute__((destructor)) static void linux_end(void)
{
	sensors_forget();
	blocks_forget();
	usbs_forget();
	modules_forget();
}
