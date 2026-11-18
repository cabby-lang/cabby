/* desktop.c — what a desktop does, the same way on Linux and macOS
 * (use desktop, use power).
 *
 * A program says what it wants (a notification, a file opened, the
 * battery's charge) and this file does it each system's own way, so the
 * program needs no #ifs of its own:
 *
 *                      Linux                               macOS
 *   notifications      org.freedesktop.Notifications       osascript's
 *                      (D-Bus: dbus.c)                     "display notification"
 *   opening            xdg-open                            open
 *   the battery        /sys/class/power_supply             pmset -g batt
 *   the volume         wpctl, pactl or amixer              osascript's "volume settings"
 *   Wi-Fi's name       iwgetid or nmcli                    networksetup or ipconfig
 *   the system         /etc/os-release, uname              sw_vers, uname
 *   (and, the same on both: the network's addresses, users and groups,
 *   and what kind of file a file is)
 *
 * For Cabby's tests, the Mac's ways can be tried on Linux too:
 * CABBY_PMSET_OUTPUT names a file of what pmset says, and CABBY_OPENER /
 * CABBY_OSASCRIPT a program to run instead of xdg-open or osascript;
 * CABBY_POWER_SUPPLY a folder to read instead of /sys/class/power_supply;
 * and CABBY_DESKTOP_AS (linux or macos) which system's ways to use, on
 * either. CABBY_TOOLS names a folder of stand-ins for the programs asked
 * (wpctl, networksetup...): with it set, only those are run, and one
 * that isn't there counts as not installed. CABBY_OS_RELEASE is a file to
 * read instead of /etc/os-release.
 */
#include "rt.h"
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* dbus.c's, for notifications on Linux */
extern int64_t cby_dbus_try_session(void);
extern void cby_dbus_close(int64_t bus);
extern int64_t cby_dbus_message_new(const char *dest, const char *path, const char *iface, const char *member);
extern void cby_dbus_message_free(int64_t msg);
extern void cby_dbus_add_str(int64_t msg, const char *s);
extern void cby_dbus_add_uint(int64_t msg, int64_t v);
extern void cby_dbus_add_int(int64_t msg, int64_t v);
extern void cby_dbus_begin_list(int64_t msg, const char *t);
extern void cby_dbus_end_list(int64_t msg);
extern void cby_dbus_begin_lookup(int64_t msg, const char *k, const char *v);
extern void cby_dbus_end_lookup(int64_t msg);
extern int64_t cby_dbus_try_call(int64_t bus, int64_t msg);
extern int64_t cby_dbus_text(int64_t id, int64_t what);
extern void cby_json_release(int64_t h);
extern int64_t cby_json_length(int64_t h);

#ifdef __APPLE__
#include <sys/sysctl.h>
#define ON_A_MAC 1
#else
#define ON_A_MAC 0
#endif

/* the Mac's ways, or Linux's: this system's, unless the tests say */
static bool as_mac(void)
{
	const char *as = getenv("CABBY_DESKTOP_AS");
	if (as && strcmp(as, "macos") == 0)
		return true;
	if (as && strcmp(as, "linux") == 0)
		return false;
	return ON_A_MAC;
}

/* 1 on a Mac, 0 on Linux */
int cby_desktop_on_a_mac(void) { return as_mac(); }

/* runs a program with its arguments, waiting for it: true if it worked
 * (exit status 0). The lock is let go of meanwhile. */
static bool run(const char *const *argv)
{
	pid_t pid;
	bool was = rt_gil_drop();
	int got = posix_spawnp(&pid, argv[0], NULL, NULL, (char *const *)argv, environ);
	int status = 0;
	if (got == 0)
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		}
	rt_gil_take(was);
	return got == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static const char *instead(const char *var, const char *usual)
{
	const char *v = getenv(var);
	return v && *v ? v : usual;
}

/* ------------------------------------------------------------ notifications */

/* text in an AppleScript string: " and \ escaped */
static char *apple_quoted(const char *s)
{
	char *q = rt_alloc(strlen(s) * 2 + 3), *p = q;
	*p++ = '"';
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			*p++ = '\\';
		*p++ = *s;
	}
	*p++ = '"';
	*p = 0;
	return q;
}

static bool notify_mac(const char *app, const char *title, const char *body)
{
	(void)app;   /* (shown as from Script Editor, whatever it's given) */
	char *t = apple_quoted(title), *b = apple_quoted(body);
	char *script = rt_alloc(strlen(t) + strlen(b) + 64);
	sprintf(script, "display notification %s with title %s", b, t);
	const char *argv[] = { instead("CABBY_OSASCRIPT", "osascript"), "-e", script, NULL };
	bool ok = run(argv);
	free(script);
	free(t);
	free(b);
	return ok;
}

/* the desktop's notification server (mako, dunst, a shell's...); its
 * number for it, or 0 if there's no server */
static int64_t notify_linux(const char *app, const char *title, const char *body, int64_t replaces)
{
	int64_t bus = cby_dbus_try_session();
	if (!bus)
		return 0;
	int64_t m = cby_dbus_message_new("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
					 "org.freedesktop.Notifications", "Notify");
	cby_dbus_add_str(m, app);
	cby_dbus_add_uint(m, replaces);
	cby_dbus_add_str(m, "");      /* an icon */
	cby_dbus_add_str(m, title);
	cby_dbus_add_str(m, body);
	cby_dbus_begin_list(m, "s");  /* actions */
	cby_dbus_end_list(m);
	cby_dbus_begin_lookup(m, "s", "v");   /* hints */
	cby_dbus_end_lookup(m);
	cby_dbus_add_int(m, -1);      /* how long: the server's choice */
	int64_t answer = cby_dbus_try_call(bus, m);
	int64_t number = 0;
	if (cby_json_length(answer) > 0) {
		extern int64_t cby_json_at(int64_t h, int64_t i);
		extern double cby_json_double(int64_t h);
		int64_t first = cby_json_at(answer, 0);
		number = (int64_t)cby_json_double(first);
		cby_json_release(first);
		if (number == 0)
			number = 1;   /* (shown, whatever its number) */
	}
	cby_json_release(answer);
	cby_dbus_message_free(m);
	cby_dbus_close(bus);
	return number;
}

/* shown: its number (Linux), or 1 (macOS, where they have none); 0: it
 * couldn't be shown */
int64_t cby_desktop_notify(const char *app, const char *title, const char *body, int64_t replaces)
{
	if (as_mac())
		return notify_mac(app, title, body) ? 1 : 0;
	return notify_linux(app, title, body, replaces);
}

/* ------------------------------------------------------------ opening things */

/* a file, a folder or a web address, in what the desktop opens it with */
int cby_desktop_open(const char *what)
{
	const char *argv[] = { instead("CABBY_OPENER", as_mac() ? "open" : "xdg-open"), what, NULL };
	return run(argv);
}

/* ------------------------------------------------------------ the battery */

typedef struct {
	bool has_battery;
	int percent;          /* -1: no battery */
	bool charging;        /* being charged now */
	bool plugged_in;      /* on mains power (charging, or full) */
	bool full;
} Power;

/* pmset -g batt says, for example:
 *   Now drawing from 'AC Power'
 *    -InternalBattery-0 (id=4653155)	87%; charging; 0:43 remaining present: true
 * or 'Battery Power', and "discharging", "charged", "finishing charge",
 * "AC attached; not charging". A Mac mini says only the first line. */
static Power read_pmset(const char *text)
{
	Power p = { false, -1, false, false, false };
	p.plugged_in = strstr(text, "'AC Power'") != NULL;
	const char *bat = strstr(text, "InternalBattery");
	if (!bat)
		return p;
	const char *pct = strchr(bat, '%');
	const char *nl = strchr(bat, '\n');
	if (!pct || (nl && pct > nl))
		return p;
	const char *d = pct;
	while (d > bat && d[-1] >= '0' && d[-1] <= '9')
		d--;
	p.has_battery = true;
	p.percent = atoi(d);
	char state[64] = "";
	const char *semi = strchr(pct, ';');
	if (semi) {
		semi++;
		while (*semi == ' ')
			semi++;
		size_t n = strcspn(semi, ";\n");
		snprintf(state, sizeof state, "%.*s", (int)(n < 63 ? n : 63), semi);
	}
	p.charging = strcmp(state, "charging") == 0 || strcmp(state, "finishing charge") == 0;
	p.full = strcmp(state, "charged") == 0 || p.percent >= 100;
	return p;
}

static Power power_mac(void)
{
	const char *file = getenv("CABBY_PMSET_OUTPUT");
	char text[4096] = "";
	size_t n = 0;
	FILE *f = file ? fopen(file, "r") : popen("pmset -g batt 2>/dev/null", "r");
	if (f) {
		n = fread(text, 1, sizeof text - 1, f);
		file ? fclose(f) : pclose(f);
	}
	text[n] = 0;
	return read_pmset(text);
}

/* a small file's first line, or "" */
static void first_line(const char *dir, const char *name, const char *file, char *out, size_t size)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s/%s", dir, name, file);
	out[0] = 0;
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	if (fgets(out, (int)size, f))
		out[strcspn(out, "\r\n")] = 0;
	fclose(f);
}

/* each supply in /sys/class/power_supply: a battery (type Battery: its
 * capacity, in percent, and status: Charging, Discharging, Full, Not
 * charging), or the mains (Mains: online 1 when plugged in). With more
 * than one battery, the charge is their average. */
static Power power_linux(void)
{
	Power p = { false, -1, false, false, false };
	const char *dir = instead("CABBY_POWER_SUPPLY", "/sys/class/power_supply");
	DIR *d = opendir(dir);
	if (!d)
		return p;
	int batteries = 0, total = 0, full = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		char type[64], value[64];
		first_line(dir, e->d_name, "type", type, sizeof type);
		if (strcmp(type, "Mains") == 0 || strcmp(type, "USB") == 0) {
			first_line(dir, e->d_name, "online", value, sizeof value);
			if (strcmp(value, "1") == 0)
				p.plugged_in = true;
		} else if (strcmp(type, "Battery") == 0) {
			first_line(dir, e->d_name, "scope", value, sizeof value);
			if (strcmp(value, "Device") == 0)
				continue;   /* (a mouse's or a keyboard's, not the computer's) */
			first_line(dir, e->d_name, "capacity", value, sizeof value);
			if (!value[0])
				continue;
			batteries++;
			total += atoi(value);
			first_line(dir, e->d_name, "status", value, sizeof value);
			if (strcmp(value, "Charging") == 0)
				p.charging = true;
			if (strcmp(value, "Full") == 0)
				full++;
		}
	}
	closedir(d);
	if (batteries) {
		p.has_battery = true;
		p.percent = total / batteries;
		p.full = full == batteries || p.percent >= 100;
		if (p.charging)
			p.plugged_in = true;
	}
	return p;
}

/* 0 whether there's a battery, 1 its charge in percent (-1: none), 2
 * whether it's charging, 3 whether the computer's plugged in, 4 whether
 * the battery's full */
int64_t cby_power_read(int64_t what)
{
	Power p = as_mac() ? power_mac() : power_linux();
	switch (what) {
	case 0: return p.has_battery;
	case 1: return p.percent;
	case 2: return p.charging;
	case 3: return p.plugged_in || !p.has_battery;   /* (no battery: it must be) */
	default: return p.full;
	}
}

/* ------------------------------------------------------------ text back to Cabby */

/* Text goes back as dbus.c's does: its length first, then it's copied
 * into a str that long (cby_desktop_take_text). */
static _Thread_local char *text_out;

static void text_ends(void) { free(text_out); text_out = NULL; }

/* t becomes the text to take (t is taken over: freed later) */
static int64_t give_text(char *t)
{
	static _Thread_local bool hooked;
	if (!hooked) {
		hooked = true;
		rt_on_thread_end(text_ends);
	}
	free(text_out);
	text_out = t ? t : strdup("");
	return (int64_t)strlen(text_out);
}

static int64_t give_copy(const char *t) { return give_text(strdup(t ? t : "")); }

void cby_desktop_take_text(char *dst)
{
	if (text_out)
		memcpy(dst, text_out, strlen(text_out));
}

/* ------------------------------------------------------------ running programs */

/* a program by its name: from CABBY_TOOLS when the tests say, else
 * found on the PATH as usual */
static const char *tool(const char *name, char *buf, size_t size)
{
	const char *dir = getenv("CABBY_TOOLS");
	if (!dir || !*dir || strchr(name, '/'))
		return name;
	snprintf(buf, size, "%s/%s", dir, name);
	return buf;
}

/* Runs a program and keeps what it prints (stderr is thrown away), as
 * text without the line ends at the very end: NULL if it couldn't be
 * run or didn't work (an exit status other than 0). Freed by the caller.
 * The lock is let go of meanwhile. */
static char *capture_from(const char *const *argv, const char *input);
static char *capture(const char *const *argv) { return capture_from(argv, NULL); }

/* the same, with a file as what it reads (stdin), if input isn't NULL */
static char *capture_from(const char *const *argv, const char *input)
{
	char buf[1024];
	const char *prog = tool(argv[0], buf, sizeof buf);
	int fds[2];
	if (pipe(fds) != 0)
		return NULL;
	posix_spawn_file_actions_t acts;
	posix_spawn_file_actions_init(&acts);
	posix_spawn_file_actions_adddup2(&acts, fds[1], 1);
	if (input)
		posix_spawn_file_actions_addopen(&acts, 0, input, O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&acts, 2, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addclose(&acts, fds[0]);
	posix_spawn_file_actions_addclose(&acts, fds[1]);
	pid_t pid;
	bool was = rt_gil_drop();
	int got = posix_spawnp(&pid, prog, &acts, NULL, (char *const *)argv, environ);
	posix_spawn_file_actions_destroy(&acts);
	close(fds[1]);
	size_t len = 0, cap = 256;
	char *out = malloc(cap);
	ssize_t n;
	while ((n = read(fds[0], out + len, cap - len - 1)) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		len += (size_t)n;
		if (cap - len < 64)
			out = realloc(out, cap *= 2);
	}
	close(fds[0]);
	int status = 0;
	if (got == 0)
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		}
	rt_gil_take(was);
	if (got != 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		free(out);
		return NULL;
	}
	while (len && (out[len - 1] == '\n' || out[len - 1] == '\r'))
		len--;
	out[len] = 0;
	return out;
}

/* runs a program (from CABBY_TOOLS when the tests say), without keeping
 * what it prints: true if it worked */
static bool run_quietly(const char *const *argv)
{
	char *out = capture(argv);
	free(out);
	return out != NULL;
}

/* AppleScript's answer to a script, or NULL */
static char *apple(const char *script)
{
	const char *argv[] = { instead("CABBY_OSASCRIPT", "osascript"), "-e", script, NULL };
	return capture(argv);
}

/* what osascript says to Script ("" when it didn't work): for the
 * media library, and anything else only a Mac's AppleScript can do */
int64_t cby_desktop_apple(const char *script)
{
	return give_text(apple(script));
}

/* ------------------------------------------------------------ the volume */

/* The default speakers' volume (0 to 100) and whether they're muted, by
 * whichever of these the computer has:
 *   wpctl get-volume @DEFAULT_AUDIO_SINK@      "Volume: 0.45 [MUTED]"   (PipeWire)
 *   pactl get-sink-volume @DEFAULT_SINK@       "Volume: front-left: 29491 /  45% / ..."
 *   pactl get-sink-mute @DEFAULT_SINK@         "Mute: yes"              (PulseAudio)
 *   amixer get Master                          "... [45%] [-20.00dB] [off]"   (ALSA)
 * macOS: "output volume of (get volume settings)", and "output muted of". */
typedef struct {
	int level;     /* -1: not known */
	int muted;     /* -1: not known */
} Volume;

static Volume volume_linux(void)
{
	Volume v = { -1, -1 };
	const char *wp[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };
	char *out = capture(wp);
	if (out) {
		const char *at = strstr(out, "Volume:");
		if (at) {
			v.level = (int)(strtod(at + 7, NULL) * 100 + 0.5);
			v.muted = strstr(out, "[MUTED]") != NULL;
		}
		free(out);
		if (v.level >= 0)
			return v;
	}
	const char *pa[] = { "pactl", "get-sink-volume", "@DEFAULT_SINK@", NULL };
	out = capture(pa);
	if (out) {
		const char *pct = strchr(out, '%');
		if (pct) {
			const char *d = pct;
			while (d > out && d[-1] >= '0' && d[-1] <= '9')
				d--;
			v.level = atoi(d);
		}
		free(out);
		const char *pm[] = { "pactl", "get-sink-mute", "@DEFAULT_SINK@", NULL };
		out = capture(pm);
		if (out) {
			v.muted = strstr(out, "yes") != NULL;
			free(out);
		}
		if (v.level >= 0)
			return v;
	}
	const char *am[] = { "amixer", "get", "Master", NULL };
	out = capture(am);
	if (out) {
		/* the first line with "[NN%]": a channel's */
		const char *pct = strstr(out, "%]");
		if (pct) {
			const char *d = pct;
			while (d > out && d[-1] >= '0' && d[-1] <= '9')
				d--;
			v.level = atoi(d);
			const char *nl = strchr(pct, '\n');
			const char *off = strstr(pct, "[off]");
			v.muted = off && (!nl || off < nl);
		}
		free(out);
	}
	return v;
}

static Volume volume_mac(void)
{
	Volume v = { -1, -1 };
	char *out = apple("output volume of (get volume settings)");
	if (out && out[0] >= '0' && out[0] <= '9')
		v.level = atoi(out);
	free(out);
	out = apple("output muted of (get volume settings)");
	if (out && (strcmp(out, "true") == 0 || strcmp(out, "false") == 0))
		v.muted = strcmp(out, "true") == 0;
	free(out);
	return v;
}

/* 0 the level (0 to 100; -1: not known), 1 whether it's muted (-1: not
 * known) */
int64_t cby_volume_read(int64_t what)
{
	Volume v = as_mac() ? volume_mac() : volume_linux();
	return what == 0 ? v.level : v.muted;
}

/* the level, from 0 to 100: true if it was set */
int cby_volume_set(int64_t level)
{
	if (level < 0)
		level = 0;
	if (level > 100)
		level = 100;
	char n[32];
	if (as_mac()) {
		char script[64];
		snprintf(script, sizeof script, "set volume output volume %d", (int)level);
		char *out = apple(script);
		free(out);
		return out != NULL;
	}
	snprintf(n, sizeof n, "%d.%02d", (int)level / 100, (int)level % 100);
	const char *wp[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", n, NULL };
	if (run_quietly(wp))
		return 1;
	snprintf(n, sizeof n, "%d%%", (int)level);
	const char *pa[] = { "pactl", "set-sink-volume", "@DEFAULT_SINK@", n, NULL };
	if (run_quietly(pa))
		return 1;
	const char *am[] = { "amixer", "-q", "set", "Master", n, NULL };
	return run_quietly(am);
}

/* muted (1) or not (0): true if it was done */
int cby_volume_set_muted(int64_t yes)
{
	if (as_mac()) {
		char *out = apple(yes ? "set volume output muted true" : "set volume output muted false");
		free(out);
		return out != NULL;
	}
	const char *wp[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", yes ? "1" : "0", NULL };
	if (run_quietly(wp))
		return 1;
	const char *pa[] = { "pactl", "set-sink-mute", "@DEFAULT_SINK@", yes ? "1" : "0", NULL };
	if (run_quietly(pa))
		return 1;
	const char *am[] = { "amixer", "-q", "set", "Master", yes ? "mute" : "unmute", NULL };
	return run_quietly(am);
}

/* ------------------------------------------------------------ the network */

/* the first address of an interface that's up and isn't the computer's
 * own loopback: IPv4 if there is one (Family 4), else IPv6 (6) */
static bool local_address(char *out, size_t size)
{
	out[0] = 0;
	const char *given = getenv("CABBY_LOCAL_ADDRESS");
	if (given) {
		snprintf(out, size, "%s", given);
		return *out != 0;
	}
	struct ifaddrs *all;
	if (getifaddrs(&all) != 0)
		return false;
	char six[INET6_ADDRSTRLEN] = "";
	for (struct ifaddrs *i = all; i; i = i->ifa_next) {
		if (!i->ifa_addr || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK))
			continue;
		if (i->ifa_addr->sa_family == AF_INET && !out[0]) {
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)size);
		} else if (i->ifa_addr->sa_family == AF_INET6 && !six[0]) {
			struct in6_addr *a = &((struct sockaddr_in6 *)i->ifa_addr)->sin6_addr;
			if (IN6_IS_ADDR_LINKLOCAL(a))
				continue;   /* (fe80::, which every interface has, connected or not) */
			inet_ntop(AF_INET6, a, six, sizeof six);
		}
	}
	freeifaddrs(all);
	if (!out[0])
		snprintf(out, size, "%s", six);
	return out[0] != 0;
}

/* Wi-Fi's network name, or NULL. Linux: iwgetid -r, or nmcli's list of
 * networks ("yes:Home" for the one in use). macOS: networksetup ("Current
 * Wi-Fi Network: Home"), or ipconfig's summary ("  SSID : Home"), on en0
 * (a Mac's Wi-Fi); macOS 14 and later can hide it ("<redacted>") from
 * programs that haven't been allowed Location Services. */
static char *wifi_name(void)
{
	if (as_mac()) {
		const char *ns[] = { "networksetup", "-getairportnetwork", "en0", NULL };
		char *out = capture(ns);
		const char *at = out ? strstr(out, "Network: ") : NULL;
		if (at) {
			char *name = strdup(at + 9);
			free(out);
			return name;
		}
		free(out);
		const char *ip[] = { "ipconfig", "getsummary", "en0", NULL };
		out = capture(ip);
		at = out ? strstr(out, " SSID : ") : NULL;
		char *name = NULL;
		if (at) {
			at += 8;
			size_t n = strcspn(at, "\r\n");
			if (n && strncmp(at, "<redacted>", n) != 0)
				name = strndup(at, n);
		}
		free(out);
		return name;
	}
	const char *iw[] = { "iwgetid", "-r", NULL };
	char *out = capture(iw);
	if (out && *out)
		return out;
	free(out);
	const char *nm[] = { "nmcli", "-t", "-f", "active,ssid", "device", "wifi", NULL };
	out = capture(nm);
	char *name = NULL;
	for (char *line = out; line && *line; ) {
		size_t n = strcspn(line, "\n");
		if (strncmp(line, "yes:", 4) == 0 && n > 4) {
			name = strndup(line + 4, n - 4);
			/* (nmcli's -t writes a ':' in a name as "\:") */
			char *r = name, *w = name;
			for (; *r; r++) {
				if (*r == '\\' && r[1])
					r++;
				*w++ = *r;
			}
			*w = 0;
			break;
		}
		line += n;
		if (*line)
			line++;
	}
	free(out);
	return name;
}

/* 0 the computer's address on the network ("" when it isn't on one), 1
 * Wi-Fi's network name ("": not on Wi-Fi) */
int64_t cby_networks_here(int64_t what)
{
	if (what == 0) {
		char addr[INET6_ADDRSTRLEN];
		local_address(addr, sizeof addr);
		return give_copy(addr);
	}
	return give_text(wifi_name());
}

/* ------------------------------------------------------------ the system */

/* a value from /etc/os-release (NAME="Debian GNU/Linux"), or "" */
static char *os_release(const char *key)
{
	FILE *f = fopen(instead("CABBY_OS_RELEASE", "/etc/os-release"), "r");
	if (!f)
		f = fopen("/usr/lib/os-release", "r");
	if (!f)
		return strdup("");
	char line[512], *found = NULL;
	size_t k = strlen(key);
	while (!found && fgets(line, sizeof line, f)) {
		if (strncmp(line, key, k) != 0 || line[k] != '=')
			continue;
		char *v = line + k + 1;
		v[strcspn(v, "\r\n")] = 0;
		size_t n = strlen(v);
		if (n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0]) {
			v[n - 1] = 0;
			v++;
		}
		found = strdup(v);
	}
	fclose(f);
	return found ? found : strdup("");
}

/* macOS's version ("15.1"), from sw_vers */
static char *mac_version(void)
{
	const char *sw[] = { "sw_vers", "-productVersion", NULL };
	char *v = capture(sw);
	return v ? v : strdup("");
}

/* 0 its kind ("linux" or "macos"), 1 its name on the network, 2 the
 * system's name and version ("Debian GNU/Linux 13 (trixie)", "macOS
 * 15.1"), 3 just the version ("13", "15.1"), 4 the kernel's version
 * ("6.12.48-amd64", "24.1.0"), 5 the processor's kind ("x86_64",
 * "arm64") */
int64_t cby_system_text(int64_t what)
{
	struct utsname u;
	if (uname(&u) != 0)
		memset(&u, 0, sizeof u);
	switch (what) {
	case 0: return give_copy(as_mac() ? "macos" : "linux");
	case 1: {
		char host[256] = "";
		gethostname(host, sizeof host - 1);
		host[sizeof host - 1] = 0;
		/* (a Mac's: "mini.local", whichever system's ways are being used) */
		size_t n = strlen(host);
		if (n > 6 && strcmp(host + n - 6, ".local") == 0)
			host[n - 6] = 0;
		return give_copy(host);
	}
	case 2:
		if (as_mac()) {
			char *v = mac_version();
			char *t = malloc(strlen(v) + 8);
			sprintf(t, "macOS %s", v);
			free(v);
			return give_text(t);
		} else {
			char *t = os_release("PRETTY_NAME");
			if (!*t) {
				free(t);
				t = os_release("NAME");
			}
			if (!*t) {
				free(t);
				t = strdup("Linux");
			}
			return give_text(t);
		}
	case 3:
		return give_text(as_mac() ? mac_version() : os_release("VERSION_ID"));
	case 4: return give_copy(u.release);
	default: return give_copy(u.machine);
	}
}

/* 0 how many processors (cores) it has, 1 its memory in bytes, 2 how
 * long it's been on, in seconds */
int64_t cby_system_number(int64_t what)
{
	if (what == 0) {
		long n = sysconf(_SC_NPROCESSORS_ONLN);
		return n > 0 ? n : 1;
	}
#ifdef __APPLE__
	if (what == 1) {
		int64_t mem = 0;
		size_t size = sizeof mem;
		sysctlbyname("hw.memsize", &mem, &size, NULL, 0);
		return mem;
	}
	struct timeval boot;
	size_t size = sizeof boot;
	int mib[2] = { CTL_KERN, KERN_BOOTTIME };
	if (sysctl(mib, 2, &boot, &size, NULL, 0) != 0)
		return 0;
	return (int64_t)time(NULL) - boot.tv_sec;
#else
	if (what == 1)
		return (int64_t)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
	FILE *f = fopen("/proc/uptime", "r");
	double up = 0;
	if (f) {
		if (fscanf(f, "%lf", &up) != 1)
			up = 0;
		fclose(f);
	}
	return (int64_t)up;
#endif
}

/* ------------------------------------------------------------ users */

/* someone by their login name, or (Who "") whoever runs the program */
static struct passwd *user_of(const char *who)
{
	return *who ? getpwnam(who) : getpwuid(getuid());
}

/* the groups someone's in, by name, one per line */
static char *groups_of(struct passwd *pw)
{
	int n = 64;
	for (;;) {
#ifdef __APPLE__
		int *ids = malloc(sizeof *ids * (size_t)n);
		int base = (int)pw->pw_gid;
#else
		gid_t *ids = malloc(sizeof *ids * (size_t)n);
		gid_t base = pw->pw_gid;
#endif
		int got = n;
		if (getgrouplist(pw->pw_name, base, ids, &got) < 0) {
			free(ids);
			n = got > n ? got : n * 2;
			continue;
		}
		size_t len = 0, cap = 256;
		char *out = malloc(cap);
		out[0] = 0;
		for (int i = 0; i < got; i++) {
			bool seen = false;
			for (int j = 0; j < i; j++)
				seen |= ids[j] == ids[i];
			if (seen)
				continue;   /* (a Mac lists the first one twice) */
			struct group *g = getgrgid((gid_t)ids[i]);
			char number[32];
			snprintf(number, sizeof number, "%d", (int)ids[i]);
			const char *name = g ? g->gr_name : number;
			size_t k = strlen(name);
			if (len + k + 2 > cap)
				out = realloc(out, cap = (len + k + 2) * 2);
			if (len)
				out[len++] = '\n';
			memcpy(out + len, name, k + 1);
			len += k;
		}
		free(ids);
		return out;
	}
}

/* about Who (or, "", whoever runs the program): 0 their login name, 1
 * their full name ("" if none was given), 2 their home folder, 3 their
 * shell, 4 their groups, one per line. All "" when there's no Who. */
int64_t cby_users_text(const char *who, int64_t what)
{
	struct passwd *pw = user_of(who);
	if (!pw)
		return give_copy("");
	switch (what) {
	case 0: return give_copy(pw->pw_name);
	case 1: {
		/* the GECOS field: "Ada Lovelace,Room 4,,"; the full name's first */
		const char *g = pw->pw_gecos ? pw->pw_gecos : "";
		return give_text(strndup(g, strcspn(g, ",")));
	}
	case 2: return give_copy(pw->pw_dir);
	case 3: return give_copy(pw->pw_shell);
	default: return give_text(groups_of(pw));
	}
}

/* Who's number (their uid), or -1 when there's no Who */
int64_t cby_users_id(const char *who)
{
	struct passwd *pw = user_of(who);
	return pw ? (int64_t)pw->pw_uid : -1;
}

/* ------------------------------------------------------------ making users
 *
 * Making users and groups, and changing them, needs the administrator
 * (root: `sudo cabby prog.cby`), and each system does it its own way:
 * Linux with useradd, chpasswd, userdel, groupadd, groupdel and gpasswd
 * (the same on Debian and Arch); a Mac with sysadminctl and dseditgroup.
 * What they say when they won't is in the panic.
 *
 * (The tests: CABBY_TEST_USERS=1 lets them go on without root, and
 * CABBY_TOOLS gives stand-ins for the programs, which say what they were
 * asked.) */

/* runs one of them, giving it `input` (or nothing) to read, and keeping
 * what it says (both ways out): true if it worked */
static bool run_admin(const char *const *argv, const char *input, char **said)
{
	char buf[1024];
	const char *prog = tool(argv[0], buf, sizeof buf);
	int out[2], in[2];
	if (pipe(out) != 0)
		return false;
	if (pipe(in) != 0) {
		close(out[0]);
		close(out[1]);
		return false;
	}
	fcntl(out[0], F_SETFD, FD_CLOEXEC);
	fcntl(in[1], F_SETFD, FD_CLOEXEC);
	posix_spawn_file_actions_t acts;
	posix_spawn_file_actions_init(&acts);
	posix_spawn_file_actions_adddup2(&acts, in[0], 0);
	posix_spawn_file_actions_adddup2(&acts, out[1], 1);
	posix_spawn_file_actions_adddup2(&acts, out[1], 2);
	posix_spawn_file_actions_addclose(&acts, in[0]);
	posix_spawn_file_actions_addclose(&acts, out[1]);
	pid_t pid;
	bool was = rt_gil_drop();
	int got = posix_spawnp(&pid, prog, &acts, NULL, (char *const *)argv, environ);
	posix_spawn_file_actions_destroy(&acts);
	close(in[0]);
	close(out[1]);
	if (got == 0 && input) {
		size_t n = strlen(input), done = 0;
		while (done < n) {
			ssize_t w = write(in[1], input + done, n - done);
			if (w <= 0)
				break;
			done += (size_t)w;
		}
	}
	close(in[1]);
	size_t len = 0, cap = 256;
	char *text = malloc(cap);
	ssize_t n;
	while ((n = read(out[0], text + len, cap - len - 1)) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		len += (size_t)n;
		if (cap - len < 64)
			text = realloc(text, cap *= 2);
	}
	close(out[0]);
	int status = 0;
	if (got == 0)
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		}
	rt_gil_take(was);
	while (len && (text[len - 1] == '\n' || text[len - 1] == '\r'))
		len--;
	text[len] = 0;
	if (got != 0) {
		free(text);
		text = strdup("it isn't there to run");
	}
	*said = text;
	return got == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void need_root(const char *doing)
{
	if (geteuid() != 0 && !getenv("CABBY_TEST_USERS"))
		rt_panic(NULL, 0, 0, "%s needs the administrator: run the program with sudo (sudo cabby prog.cby, or "
				     "sudo ./prog)", doing);
}

/* a name a user or group can have: small letters, digits, _ and -, not
 * starting with a digit or -, at most 32 */
static void check_name(const char *name, const char *what)
{
	size_t n = strlen(name);
	bool ok = n >= 1 && n <= 32 && !(name[0] >= '0' && name[0] <= '9') && name[0] != '-';
	for (size_t i = 0; ok && i < n; i++) {
		char c = name[i];
		ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
	}
	if (!ok)
		rt_panic(NULL, 0, 0, "'%s' can't be a %s's name: small letters, digits, _ and - (not starting with a digit "
				     "or -), 32 at most", name, what);
}

/* the program's own words, after what Cabby says */
static _Noreturn void admin_failed(const char *what, const char *name, char *said)
{
	char words[600];
	snprintf(words, sizeof words, "%s", said && *said ? said : "it didn't say why");
	free(said);
	rt_panic(NULL, 0, 0, "can't %s '%s': %s", what, name, words);
}

static void admin(const char *const *argv, const char *input, const char *what, const char *name)
{
	char *said = NULL;
	if (!run_admin(argv, input, &said))
		admin_failed(what, name, said);
	free(said);
}

/* A Mac's directory service keeps what it knows for a while, so a user
 * just made or removed still looks as they were: its cache is cleared, and
 * Cabby waits (up to ten seconds) until the change shows, so the program's
 * next line sees it. (Linux's files show it at once.) */
static void settle_mac(const char *name, bool is_group, bool should_exist)
{
	if (!as_mac() || getenv("CABBY_TEST_USERS"))
		return;
	const char *flush[] = { "dscacheutil", "-flushcache", NULL };
	run_quietly(flush);
	for (int i = 0; i < 200; i++) {
		bool there = is_group ? getgrnam(name) != NULL : getpwnam(name) != NULL;
		if (there == should_exist)
			return;
		bool was = rt_gil_drop();
		struct timespec pause = { 0, 50000000 };   /* (a twentieth of a second) */
		nanosleep(&pause, NULL);
		rt_gil_take(was);
	}
}

static const char *sudo_group(void)
{
	return getgrnam("sudo") ? "sudo" : "wheel";   /* (Debian's, or Arch's) */
}

/* a user: their login name, full name and password; an administrator (who
 * may use sudo) or not */
void cby_users_make(const char *name, const char *full, const char *password, int is_admin)
{
	need_root("making a user");
	check_name(name, "user");
	if (getpwnam(name))
		rt_panic(NULL, 0, 0, "there's a user called '%s' already", name);
	if (strchr(full, ':') || strchr(full, '\n') || strchr(full, ','))
		rt_panic(NULL, 0, 0, "a full name can't have ':', ',' or a line end in it: '%s'", full);
	if (strchr(password, '\n') || !*password)
		rt_panic(NULL, 0, 0, "a password can't be empty, or have a line end in it%s", "");
	if (as_mac()) {
		const char *argv[] = { "sysadminctl", "-addUser", name, "-fullName", full, "-password", password,
				       is_admin ? "-admin" : NULL, NULL };
		admin(argv, NULL, "make the user", name);
		settle_mac(name, false, true);
		return;
	}
	const char *shell = access("/bin/bash", X_OK) == 0 ? "/bin/bash" : "/bin/sh";
	const char *argv[] = { "useradd", "-m", "-c", full, "-s", shell, is_admin ? "-G" : NULL, is_admin ? sudo_group() : NULL,
			       name, NULL };
	if (!is_admin) {
		argv[6] = name;
		argv[7] = NULL;
	}
	admin(argv, NULL, "make the user", name);
	char *line = malloc(strlen(name) + strlen(password) + 3);
	sprintf(line, "%s:%s\n", name, password);
	const char *pass[] = { "chpasswd", NULL };
	char *said = NULL;
	bool ok = run_admin(pass, line, &said);
	memset(line, 0, strlen(line));
	free(line);
	if (!ok) {
		/* (no user without a password left behind) */
		const char *undo[] = { "userdel", "-r", name, NULL };
		char *ignore = NULL;
		run_admin(undo, NULL, &ignore);
		free(ignore);
		admin_failed("set the password for", name, said);
	}
	free(said);
}

void cby_users_set_password(const char *name, const char *password)
{
	need_root("changing a password");
	if (!getpwnam(name) && !getenv("CABBY_TEST_USERS"))
		rt_panic(NULL, 0, 0, "there's no user called '%s'", name);
	if (strchr(password, '\n') || !*password)
		rt_panic(NULL, 0, 0, "a password can't be empty, or have a line end in it%s", "");
	if (as_mac()) {
		const char *argv[] = { "sysadminctl", "-resetPasswordFor", name, "-newPassword", password, NULL };
		admin(argv, NULL, "set the password for", name);
		return;
	}
	char *line = malloc(strlen(name) + strlen(password) + 3);
	sprintf(line, "%s:%s\n", name, password);
	const char *argv[] = { "chpasswd", NULL };
	char *said = NULL;
	bool ok = run_admin(argv, line, &said);
	memset(line, 0, strlen(line));
	free(line);
	if (!ok)
		admin_failed("set the password for", name, said);
	free(said);
}

/* a user gone, their home folder with them (or kept) */
void cby_users_remove(const char *name, int keep_home)
{
	need_root("removing a user");
	struct passwd *pw = getpwnam(name);
	if (!pw && !getenv("CABBY_TEST_USERS"))
		rt_panic(NULL, 0, 0, "there's no user called '%s'", name);
	if (pw && (pw->pw_uid == 0 || pw->pw_uid == getuid() || (getenv("SUDO_USER") && strcmp(getenv("SUDO_USER"), name) == 0)))
		rt_panic(NULL, 0, 0, "won't remove '%s': %s", name, pw->pw_uid == 0 ? "it's the administrator" : "it's who's running the program");
	if (as_mac()) {
		const char *argv[] = { "sysadminctl", "-deleteUser", name, keep_home ? "-keepHome" : NULL, NULL };
		admin(argv, NULL, "remove the user", name);
		settle_mac(name, false, false);
		return;
	}
	const char *argv[] = { "userdel", keep_home ? name : "-r", keep_home ? NULL : name, NULL };
	admin(argv, NULL, "remove the user", name);
}

/* groups: made, removed, and people put in and taken out */
void cby_users_make_group(const char *group)
{
	need_root("making a group");
	check_name(group, "group");
	if (getgrnam(group))
		rt_panic(NULL, 0, 0, "there's a group called '%s' already", group);
	if (as_mac()) {
		const char *argv[] = { "dseditgroup", "-o", "create", group, NULL };
		admin(argv, NULL, "make the group", group);
		settle_mac(group, true, true);
		return;
	}
	const char *argv[] = { "groupadd", group, NULL };
	admin(argv, NULL, "make the group", group);
}

void cby_users_remove_group(const char *group)
{
	need_root("removing a group");
	if (as_mac()) {
		const char *argv[] = { "dseditgroup", "-o", "delete", group, NULL };
		admin(argv, NULL, "remove the group", group);
		settle_mac(group, true, false);
		return;
	}
	const char *argv[] = { "groupdel", group, NULL };
	admin(argv, NULL, "remove the group", group);
}

void cby_users_join(const char *name, const char *group, int join)
{
	need_root(join ? "putting someone in a group" : "taking someone out of a group");
	if (as_mac()) {
		const char *argv[] = { "dseditgroup", "-o", "edit", join ? "-a" : "-d", name, "-t", "user", group, NULL };
		admin(argv, NULL, join ? "put in the group" : "take out of the group", name);
		if (!getenv("CABBY_TEST_USERS")) {
			const char *flush[] = { "dscacheutil", "-flushcache", NULL };
			run_quietly(flush);   /* (who's in a group: seen at once, the cache cleared) */
		}
		return;
	}
	const char *argv[] = { "gpasswd", join ? "-a" : "-d", name, group, NULL };
	admin(argv, NULL, join ? "put in the group" : "take out of the group", name);
}

/* everyone who logs in (0: people, from the first ordinary user's number:
 * 1000 on Linux, 501 on a Mac), or (1) every group, one name to a line */
int64_t cby_users_all(int64_t what)
{
	size_t len = 0, cap = 256;
	char *out = malloc(cap);
	out[0] = 0;
	unsigned first = as_mac() ? 501 : 1000;
	if (what == 0) {
		setpwent();
		struct passwd *pw;
		while ((pw = getpwent())) {
			if (pw->pw_uid < first || pw->pw_uid >= 60000 || pw->pw_name[0] == '_')
				continue;   /* (the system's own, and nobody) */
			size_t k = strlen(pw->pw_name);
			if (len + k + 2 > cap)
				out = realloc(out, cap = (len + k + 2) * 2);
			if (len)
				out[len++] = '\n';
			memcpy(out + len, pw->pw_name, k + 1);
			len += k;
		}
		endpwent();
	} else {
		setgrent();
		struct group *g;
		while ((g = getgrent())) {
			size_t k = strlen(g->gr_name);
			if (len + k + 2 > cap)
				out = realloc(out, cap = (len + k + 2) * 2);
			if (len)
				out[len++] = '\n';
			memcpy(out + len, g->gr_name, k + 1);
			len += k;
		}
		endgrent();
	}
	return give_text(out);
}

/* ------------------------------------------------------------ kinds of file */

/* What's in a file, by how it starts: the usual "magic numbers". */
typedef struct {
	int at;
	const char *bytes;
	int len;
	const char *kind;
} Magic;

static const Magic magics[] = {
	{ 0, "\x89PNG\r\n\x1a\n", 8, "image/png" },
	{ 0, "\xff\xd8\xff", 3, "image/jpeg" },
	{ 0, "GIF87a", 6, "image/gif" },
	{ 0, "GIF89a", 6, "image/gif" },
	{ 0, "BM", 2, "image/bmp" },
	{ 0, "II*\0", 4, "image/tiff" },
	{ 0, "MM\0*", 4, "image/tiff" },
	{ 0, "\0\0\1\0", 4, "image/vnd.microsoft.icon" },
	{ 0, "%PDF-", 5, "application/pdf" },
	{ 0, "PK\3\4", 4, "application/zip" },
	{ 0, "PK\5\6", 4, "application/zip" },
	{ 0, "\x1f\x8b", 2, "application/gzip" },
	{ 0, "BZh", 3, "application/x-bzip2" },
	{ 0, "\xfd" "7zXZ\0", 6, "application/x-xz" },
	{ 0, "\x28\xb5\x2f\xfd", 4, "application/zstd" },
	{ 0, "7z\xbc\xaf\x27\x1c", 6, "application/x-7z-compressed" },
	{ 257, "ustar", 5, "application/x-tar" },
	{ 0, "SQLite format 3\0", 16, "application/vnd.sqlite3" },
	{ 0, "\x7f" "ELF", 4, "application/x-executable" },
	{ 0, "\xcf\xfa\xed\xfe", 4, "application/x-mach-binary" },
	{ 0, "\xca\xfe\xba\xbe", 4, "application/x-mach-binary" },
	{ 0, "ID3", 3, "audio/mpeg" },
	{ 0, "\xff\xfb", 2, "audio/mpeg" },
	{ 0, "\xff\xf3", 2, "audio/mpeg" },
	{ 0, "fLaC", 4, "audio/flac" },
	{ 0, "OggS", 4, "audio/ogg" },
	{ 0, "MThd", 4, "audio/midi" },
	{ 0, "\x1a\x45\xdf\xa3", 4, "video/webm" },
	{ 0, "wOFF", 4, "font/woff" },
	{ 0, "wOF2", 4, "font/woff2" },
	{ 0, "\0\1\0\0\0", 5, "font/ttf" },
	{ 0, "OTTO", 4, "font/otf" },
	{ 0, "{\\rtf", 5, "application/rtf" },
	{ 0, "#!", 2, "text/x-shellscript" },
};

/* by name: the usual kind for each ending */
static const char *const endings[][2] = {
	{ "txt", "text/plain" }, { "md", "text/markdown" }, { "html", "text/html" }, { "htm", "text/html" },
	{ "css", "text/css" }, { "csv", "text/csv" }, { "tsv", "text/tab-separated-values" },
	{ "xml", "application/xml" }, { "json", "application/json" }, { "js", "text/javascript" },
	{ "mjs", "text/javascript" }, { "cby", "text/x-cabby" }, { "hby", "text/x-cabby" },
	{ "c", "text/x-c" }, { "h", "text/x-c" }, { "py", "text/x-python" }, { "sh", "text/x-shellscript" },
	{ "yaml", "application/yaml" }, { "yml", "application/yaml" }, { "toml", "application/toml" },
	{ "ini", "text/plain" }, { "log", "text/plain" },
	{ "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "gif", "image/gif" },
	{ "bmp", "image/bmp" }, { "webp", "image/webp" }, { "svg", "image/svg+xml" }, { "ico", "image/vnd.microsoft.icon" },
	{ "tif", "image/tiff" }, { "tiff", "image/tiff" }, { "heic", "image/heic" }, { "avif", "image/avif" },
	{ "pdf", "application/pdf" }, { "zip", "application/zip" }, { "gz", "application/gzip" },
	{ "tgz", "application/gzip" }, { "tar", "application/x-tar" }, { "bz2", "application/x-bzip2" },
	{ "xz", "application/x-xz" }, { "zst", "application/zstd" }, { "7z", "application/x-7z-compressed" },
	{ "db", "application/vnd.sqlite3" }, { "sqlite", "application/vnd.sqlite3" },
	{ "mp3", "audio/mpeg" }, { "wav", "audio/wav" }, { "flac", "audio/flac" }, { "ogg", "audio/ogg" },
	{ "opus", "audio/opus" }, { "m4a", "audio/mp4" }, { "aac", "audio/aac" }, { "mid", "audio/midi" },
	{ "midi", "audio/midi" }, { "mp4", "video/mp4" }, { "m4v", "video/mp4" }, { "mov", "video/quicktime" },
	{ "mkv", "video/x-matroska" }, { "webm", "video/webm" }, { "avi", "video/x-msvideo" },
	{ "ttf", "font/ttf" }, { "otf", "font/otf" }, { "woff", "font/woff" }, { "woff2", "font/woff2" },
	{ "rtf", "application/rtf" }, { "epub", "application/epub+zip" },
	{ "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
	{ "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
	{ "pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation" },
	{ "odt", "application/vnd.oasis.opendocument.text" }, { "ods", "application/vnd.oasis.opendocument.spreadsheet" },
	{ "jar", "application/java-archive" }, { "wasm", "application/wasm" },
	{ NULL, NULL },
};

static const char *by_ending(const char *name)
{
	const char *slash = strrchr(name, '/');
	const char *dot = strrchr(slash ? slash + 1 : name, '.');
	if (!dot || dot == (slash ? slash + 1 : name))
		return NULL;   /* (no ending, or a name like ".profile") */
	char low[16];
	size_t n = strlen(dot + 1);
	if (n == 0 || n >= sizeof low)
		return NULL;
	for (size_t i = 0; i <= n; i++)
		low[i] = (char)(dot[1 + i] >= 'A' && dot[1 + i] <= 'Z' ? dot[1 + i] + 32 : dot[1 + i]);
	for (int i = 0; endings[i][0]; i++)
		if (strcmp(endings[i][0], low) == 0)
			return endings[i][1];
	return NULL;
}

/* true if these bytes could be text: UTF-8, with no NULs or other
 * control characters but tabs, line ends and form feeds */
static bool looks_like_text(const unsigned char *b, size_t n, bool cut_short)
{
	for (size_t i = 0; i < n; ) {
		unsigned char c = b[i];
		if (c < 0x80) {
			if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 0x1b)
				return false;
			i++;
			continue;
		}
		int more = c >= 0xf0 && c < 0xf5 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : -1;
		if (more < 0 || c >= 0xf5)
			return false;
		if (i + (size_t)more >= n)
			return cut_short;   /* (a character cut in two at the end of what was read) */
		for (int k = 1; k <= more; k++)
			if ((b[i + k] & 0xc0) != 0x80)
				return false;
		i += (size_t)more + 1;
	}
	return true;
}

/* Name's kind ("image/png"), by its ending alone: "" when it's not one
 * known */
int64_t cby_mime_of_name(const char *name)
{
	const char *k = by_ending(name);
	return give_copy(k ? k : "");
}

/* Path's kind, by what's in it; then, for kinds of text (and zip files,
 * which a .docx or .epub is too), its ending says which. "" when there's
 * no file there; "inode/directory" for a folder. */
int64_t cby_mime_of_file(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return give_copy("");
	if (S_ISDIR(st.st_mode))
		return give_copy("inode/directory");
	FILE *f = fopen(path, "rb");
	if (!f)
		return give_copy("");
	unsigned char head[1024];
	size_t n = fread(head, 1, sizeof head, f);
	fclose(f);
	const char *named = by_ending(path);
	if (n == 0)
		return give_copy(named ? named : "text/plain");   /* (empty: what its name says) */
	if (n >= 12 && memcmp(head, "RIFF", 4) == 0) {
		if (memcmp(head + 8, "WAVE", 4) == 0)
			return give_copy("audio/wav");
		if (memcmp(head + 8, "WEBP", 4) == 0)
			return give_copy("image/webp");
		if (memcmp(head + 8, "AVI ", 4) == 0)
			return give_copy("video/x-msvideo");
	}
	if (n >= 12 && memcmp(head + 4, "ftyp", 4) == 0) {
		const unsigned char *brand = head + 8;
		if (memcmp(brand, "heic", 4) == 0 || memcmp(brand, "heix", 4) == 0 || memcmp(brand, "mif1", 4) == 0)
			return give_copy("image/heic");
		if (memcmp(brand, "avif", 4) == 0)
			return give_copy("image/avif");
		if (memcmp(brand, "qt  ", 4) == 0)
			return give_copy("video/quicktime");
		if (memcmp(brand, "M4A ", 4) == 0)
			return give_copy("audio/mp4");
		return give_copy("video/mp4");
	}
	for (size_t i = 0; i < sizeof magics / sizeof magics[0]; i++) {
		const Magic *m = &magics[i];
		if (n >= (size_t)(m->at + m->len) && memcmp(head + m->at, m->bytes, (size_t)m->len) == 0) {
			/* a zip file may be a .docx, .xlsx, .epub, .jar...: its name says */
			if (strcmp(m->kind, "application/zip") == 0 && named)
				return give_copy(named);
			if (strcmp(m->kind, "text/x-shellscript") == 0) {
				/* "#!/usr/bin/env python3": its first line says which */
				char first[128];
				const unsigned char *nl = memchr(head, '\n', n);
				size_t k = nl ? (size_t)(nl - head) : n;
				snprintf(first, sizeof first, "%.*s", (int)(k < 127 ? k : 127), (const char *)head);
				if (strstr(first, "python"))
					return give_copy("text/x-python");
				if (strstr(first, "cabby"))
					return give_copy("text/x-cabby");
			}
			return give_copy(m->kind);
		}
	}
	if (looks_like_text(head, n, n == sizeof head)) {
		/* text: its name says which kind, if it says one that's text */
		if (named && (strncmp(named, "text/", 5) == 0 || strstr(named, "json") || strstr(named, "xml") ||
			      strstr(named, "yaml") || strstr(named, "toml")))
			return give_copy(named);
		size_t s = 0;
		while (s < n && (head[s] == ' ' || head[s] == '\n' || head[s] == '\t' || head[s] == '\r'))
			s++;
		if (n - s >= 5 && (strncasecmp((const char *)head + s, "<!doc", 5) == 0 || strncasecmp((const char *)head + s, "<html", 5) == 0))
			return give_copy("text/html");
		if (n - s >= 5 && strncmp((const char *)head + s, "<?xml", 5) == 0) {
			bool svg = false;
			for (size_t i = s; i + 4 <= n && !svg; i++)
				svg = memcmp(head + i, "<svg", 4) == 0;
			return give_copy(svg ? "image/svg+xml" : "application/xml");
		}
		if (n - s >= 4 && strncmp((const char *)head + s, "<svg", 4) == 0)
			return give_copy("image/svg+xml");
		return give_copy("text/plain");
	}
	return give_copy("application/octet-stream");
}

/* ------------------------------------------------------------ running any program,
 * for the libraries written in Cabby itself (printing, speech) */

/* the program and its arguments, given as one text with \x1f between
 * them; input, if has_input, is given to it (its stdin). What it prints
 * (stdout) is kept, as capture does: its length, or -1 if it couldn't be
 * run or didn't work. */
int64_t cby_desktop_capture(const char *line, const char *input, int has_input)
{
	if (!has_input)
		input = NULL;
	char *copy = strdup(line);
	const char *argv[64];
	int n = 0;
	for (char *p = copy; n < 63;) {
		argv[n++] = p;
		char *sep = strchr(p, '\x1f');
		if (!sep)
			break;
		*sep = 0;
		p = sep + 1;
	}
	argv[n] = NULL;
	char *out;
	if (!input) {
		out = capture(argv);
	} else {
		/* (the input from a file of its own, as stdin: no pipe to fill and
		 * deadlock on) */
		char tmpl[] = "/tmp/cabby-input-XXXXXX";
		int fd = mkstemp(tmpl);
		out = NULL;
		if (fd >= 0) {
			size_t len = strlen(input), done = 0;
			while (done < len) {
				ssize_t w = write(fd, input + done, len - done);
				if (w <= 0)
					break;
				done += (size_t)w;
			}
			close(fd);
			out = capture_from(argv, tmpl);
			unlink(tmpl);
		}
	}
	free(copy);
	if (!out)
		return -1;
	return give_text(out);
}

/* ------------------------------------------------------------ the screen's brightness
 *
 * Linux, a laptop's screen: /sys/class/backlight/<one>/brightness (and
 * max_brightness), set by writing it where the program may, or else by
 * brightnessctl, or else by logind (which lets the person at the computer
 * set it, without root). A desktop's monitor, which has no backlight
 * there: ddcutil, which speaks to the monitor down its cable (DDC/CI;
 * feature 10 is its brightness). A Mac's own screen: DisplayServices, the
 * private framework the brightness keys use (a Mac mini's monitor isn't
 * the Mac's to set). CABBY_BACKLIGHT names a folder to use instead of
 * /sys/class/backlight (the tests'). */

static bool backlight(char *dir, size_t size)
{
	const char *base = instead("CABBY_BACKLIGHT", "/sys/class/backlight");
	DIR *d = opendir(base);
	if (!d)
		return false;
	struct dirent *e;
	bool found = false;
	while ((e = readdir(d)) && !found)
		if (e->d_name[0] != '.') {
			snprintf(dir, size, "%s/%s", base, e->d_name);
			found = true;
		}
	closedir(d);
	return found;
}

static long read_number(const char *dir, const char *name)
{
	char path[600];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	long v = -1;
	if (fscanf(f, "%ld", &v) != 1)
		v = -1;
	fclose(f);
	return v;
}

#ifdef __APPLE__
#include <dlfcn.h>
static int mac_display_brightness(float *get, const float *set)
{
	static void *ds, *cg;
	if (!ds)
		ds = dlopen("/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices", RTLD_LAZY);
	if (!cg)
		cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_LAZY);
	if (!ds || !cg)
		return 0;
	uint32_t (*main_display)(void);
	int (*get_b)(uint32_t, float *);
	int (*set_b)(uint32_t, float);
	void *p = dlsym(cg, "CGMainDisplayID");
	memcpy(&main_display, &p, sizeof p);
	p = dlsym(ds, "DisplayServicesGetBrightness");
	memcpy(&get_b, &p, sizeof p);
	p = dlsym(ds, "DisplayServicesSetBrightness");
	memcpy(&set_b, &p, sizeof p);
	if (!main_display || !get_b || !set_b)
		return 0;
	uint32_t display = main_display();
	if (set)
		return set_b(display, *set) == 0;
	return get_b(display, get) == 0;
}
#endif

/* ddcutil's "VCP code 0x10 (Brightness): current value = 50, max value = 100" */
static int ddc_brightness(void)
{
	const char *argv[] = { "ddcutil", "--brief", "getvcp", "10", NULL };
	char *out = capture(argv);
	if (!out)
		return -1;
	/* brief: "VCP 10 C 50 100" */
	int level = -1;
	long cur, max;
	const char *c = strstr(out, " C ");
	if (c && sscanf(c + 3, "%ld %ld", &cur, &max) == 2 && max > 0)
		level = (int)((cur * 100 + max / 2) / max);
	free(out);
	return level;
}

/* from 0 to 100; -1 if it can't be found out */
int64_t cby_brightness_level(void)
{
	if (as_mac()) {
#ifdef __APPLE__
		float b;
		if (mac_display_brightness(&b, NULL))
			return (int64_t)(b * 100 + 0.5f);
#endif
		return -1;
	}
	char dir[600];
	if (backlight(dir, sizeof dir)) {
		long cur = read_number(dir, "brightness"), max = read_number(dir, "max_brightness");
		if (cur >= 0 && max > 0)
			return (cur * 100 + max / 2) / max;
	}
	return ddc_brightness();
}

/* to the level, from 0 to 100: true if it was set */
int cby_brightness_set(int64_t level)
{
	if (level < 0)
		level = 0;
	if (level > 100)
		level = 100;
	if (as_mac()) {
#ifdef __APPLE__
		float b = (float)level / 100;
		return mac_display_brightness(NULL, &b);
#else
		return 0;
#endif
	}
	char dir[600], n[32];
	if (backlight(dir, sizeof dir)) {
		long max = read_number(dir, "max_brightness");
		if (max <= 0)
			return 0;
		long want = (level * max + 50) / 100;
		if (want == 0 && level > 0)
			want = 1;   /* (dim, but never black unless asked) */
		snprintf(n, sizeof n, "%ld", want);
		/* where the program may write it (a udev rule, or root) */
		char path[640];
		snprintf(path, sizeof path, "%s/brightness", dir);
		/* (CABBY_BACKLIGHT_LOCKED: as if it can't be, for the tests, which
		 * run as root) */
		FILE *f = getenv("CABBY_BACKLIGHT_LOCKED") ? NULL : fopen(path, "w");
		if (f) {
			bool ok = fprintf(f, "%s\n", n) > 0;
			ok = fclose(f) == 0 && ok;
			if (ok)
				return 1;
		}
		const char *bc[] = { "brightnessctl", "--quiet", "set", n, NULL };
		if (run_quietly(bc))
			return 1;
		/* logind: the person at the computer may */
		const char *slash = strrchr(dir, '/');
		const char *name = slash ? slash + 1 : dir;
		const char *gd[] = { "busctl", "call", "org.freedesktop.login1", "/org/freedesktop/login1/session/auto",
				     "org.freedesktop.login1.Session", "SetBrightness", "ssu", "backlight", name, n, NULL };
		return run_quietly(gd);
	}
	snprintf(n, sizeof n, "%d", (int)level);
	const char *ddc[] = { "ddcutil", "setvcp", "10", n, NULL };
	return run_quietly(ddc);
}

/* ------------------------------------------------------------ the computer's name */

/* changes it (it needs the administrator): true if it was changed. Linux:
 * hostnamectl (systemd's), else /etc/hostname and sethostname. A Mac:
 * scutil, its three names (HostName, LocalHostName, ComputerName). */
int cby_system_set_hostname(const char *name)
{
	if (as_mac()) {
		const char *names[] = { "HostName", "LocalHostName", "ComputerName" };
		for (int i = 0; i < 3; i++) {
			const char *argv[] = { "scutil", "--set", names[i], name, NULL };
			if (!run_quietly(argv))
				return 0;
		}
		return 1;
	}
	const char *argv[] = { "hostnamectl", "set-hostname", name, NULL };
	if (run_quietly(argv))
		return 1;
	if (getenv("CABBY_TOOLS"))
		return 0;   /* (the tests': no other way) */
	FILE *f = fopen("/etc/hostname", "w");
	if (!f)
		return 0;
	fprintf(f, "%s\n", name);
	if (fclose(f) != 0)
		return 0;
	return sethostname(name, strlen(name)) == 0;
}

/* speech.hear's: why it can't */
void cby_speech_panic(const char *why) { rt_panic(NULL, 0, 0, "%s", why); }
