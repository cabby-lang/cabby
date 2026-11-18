/* bluetooth.c — the bluetooth library's C side (lib/bluetooth.hby does the
 * rest, in Cabby, with the dbus library's messages).
 *
 * On Linux, Bluetooth is BlueZ, a service on the system bus: org.bluez,
 * with an object for each adapter (/org/bluez/hci0) and each device it
 * knows (/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF). Here is the one connection
 * to it that the library's calls share, and the agent: what BlueZ asks,
 * while a device is being paired, to show a code ("type 123456 on the
 * keyboard") or to confirm one. The agent answers from C, at once, even
 * while the program waits in its own call to Pair, which is when BlueZ
 * asks. It agrees to what's asked: pairing is something the program
 * started.
 *
 * A Mac's Bluetooth is IOBluetooth, reached in bluetooth_mac.h. */

#include "rt.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int64_t cby_dbus_try_address(const char *address);
extern void cby_dbus_serve_tree(int64_t id, const char *prefix,
				void (*fn)(void *ctx, int64_t bus, const char *path, const char *iface, const char *member,
					   int64_t args),
				void *ctx);
extern int64_t cby_dbus_message_new(const char *dest, const char *path, const char *iface, const char *member);
extern int64_t cby_dbus_answer_new(void);
extern void cby_dbus_message_free(int64_t id);
extern void cby_dbus_add_str(int64_t id, const char *s);
extern void cby_dbus_add_path(int64_t id, const char *s);
extern void cby_dbus_add_uint(int64_t id, int64_t v);
extern int64_t cby_dbus_try_call(int64_t id, int64_t msg);
extern void cby_dbus_reply(int64_t id, int64_t msg);
extern void cby_dbus_fail(int64_t id, const char *name, const char *text);
extern void cby_dbus_set_wait(int64_t id, int64_t ms);
extern int64_t cby_dbus_text(int64_t id, int64_t what);
extern int64_t cby_json_at(int64_t h, int64_t i);
extern int64_t cby_json_long(int64_t h);
extern int64_t cby_json_length(int64_t h);
extern void cby_json_release(int64_t h);
extern const char *cby_json_str_of(int64_t h);

#define AGENT_PATH "/org/cabby/bluetooth/agent"

static int64_t shared;      /* the connection (0: none yet; -1: there's no system bus) */
static int agent_ready;
static char last_code[16];   /* the last code shown for pairing */

/* the system bus, where BlueZ is; 0 if there isn't one (a Mac, a container) */
int64_t cby_bluetooth_bus(void)
{
	if (shared == -1)
		return 0;
	if (!shared) {
		const char *a = getenv("DBUS_SYSTEM_BUS_ADDRESS");
		shared = cby_dbus_try_address(a && *a ? a : "unix:path=/var/run/dbus/system_bus_socket");
		if (!shared) {
			shared = -1;
			return 0;
		}
	}
	return shared;
}

/* the device's name, for what's shown: its path's last part, as
 * AA:BB:CC:DD:EE:FF */
static void address_of(const char *path, char *out, size_t size)
{
	const char *d = strstr(path, "dev_");
	snprintf(out, size, "%s", d ? d + 4 : path);
	for (char *c = out; *c; c++)
		if (*c == '_')
			*c = ':';
}

static void answer_empty(int64_t bus)
{
	int64_t m = cby_dbus_answer_new();
	cby_dbus_reply(bus, m);
	cby_dbus_message_free(m);
}

static void agent(void *ctx, int64_t bus, const char *path, const char *iface, const char *member, int64_t args)
{
	(void)ctx;
	(void)path;
	if (strcmp(iface, "org.bluez.Agent1") != 0) {
		cby_dbus_fail(bus, "org.freedesktop.DBus.Error.UnknownMethod", "only org.bluez.Agent1 is here");
		return;
	}
	int64_t a0 = cby_json_length(args) > 0 ? cby_json_at(args, 0) : 0;
	int64_t a1 = cby_json_length(args) > 1 ? cby_json_at(args, 1) : 0;
	char addr[64] = "";
	if (a0)
		address_of(cby_json_str_of(a0), addr, sizeof addr);
	if (strcmp(member, "RequestPinCode") == 0) {
		/* an old device with a fixed PIN: most are 0000 */
		int64_t m = cby_dbus_answer_new();
		cby_dbus_add_str(m, "0000");
		cby_dbus_reply(bus, m);
		cby_dbus_message_free(m);
	} else if (strcmp(member, "DisplayPinCode") == 0 || strcmp(member, "DisplayPasskey") == 0) {
		/* a keyboard: the code is typed on it */
		if (member[7] == 'P' && member[8] == 'i')
			snprintf(last_code, sizeof last_code, "%s", a1 ? cby_json_str_of(a1) : "");
		else
			snprintf(last_code, sizeof last_code, "%06lld", a1 ? (long long)cby_json_long(a1) : 0LL);
		fprintf(stderr, "bluetooth: to pair %s, type %s on it, then Enter\n", addr, last_code);
		answer_empty(bus);
	} else if (strcmp(member, "RequestPasskey") == 0) {
		/* (a code shown on the device, to be typed here: no one to type it) */
		cby_dbus_fail(bus, "org.bluez.Error.Rejected", "there's no one to type the code shown on the device");
	} else if (strcmp(member, "RequestConfirmation") == 0) {
		/* the same code on both: the program asked to pair, so yes */
		snprintf(last_code, sizeof last_code, "%06lld", a1 ? (long long)cby_json_long(a1) : 0LL);
		answer_empty(bus);
	} else if (strcmp(member, "RequestAuthorization") == 0 || strcmp(member, "AuthorizeService") == 0 ||
		   strcmp(member, "Release") == 0 || strcmp(member, "Cancel") == 0) {
		answer_empty(bus);
	} else {
		cby_dbus_fail(bus, "org.freedesktop.DBus.Error.UnknownMethod", "the agent has no such method");
	}
	if (a0)
		cby_json_release(a0);
	if (a1)
		cby_json_release(a1);
}

/* the agent, made and told to BlueZ (once); 1 if BlueZ took it */
int cby_bluetooth_agent(void)
{
	int64_t bus = cby_bluetooth_bus();
	if (!bus)
		return 0;
	if (agent_ready)
		return 1;
	cby_dbus_serve_tree(bus, AGENT_PATH, agent, NULL);
	int64_t m = cby_dbus_message_new("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent");
	cby_dbus_add_path(m, AGENT_PATH);
	cby_dbus_add_str(m, "KeyboardDisplay");
	int64_t got = cby_dbus_try_call(bus, m);
	cby_dbus_message_free(m);
	int ok = cby_dbus_text(bus, 1) == 0;   /* (the error: "" when it worked) */
	cby_json_release(got);
	/* (the desktop's own agent may be the default: ours is asked then
	 * only for what this program pairs, which is all it needs) */
	m = cby_dbus_message_new("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent");
	cby_dbus_add_path(m, AGENT_PATH);
	got = cby_dbus_try_call(bus, m);
	cby_dbus_message_free(m);
	cby_json_release(got);
	agent_ready = ok;
	return ok;
}

/* pairing waits for a person (typing a code on a keyboard): two minutes */
void cby_bluetooth_wait_long(int on)
{
	int64_t bus = cby_bluetooth_bus();
	if (bus)
		cby_dbus_set_wait(bus, on ? 120000 : 0);
}

int64_t cby_bluetooth_code_length(void) { return (int64_t)strlen(last_code); }
char cby_bluetooth_code_char(int64_t i) { return i >= 0 && i < (int64_t)strlen(last_code) ? last_code[i] : 0; }

/* a device that wasn't found (device_named found none), used as one */
void cby_bluetooth_no_device(void)
{
	rt_panic(NULL, 0, 0, "%s", "this device wasn't found (check `found` after device_named)");
}

#include "bluetooth_mac.h"
