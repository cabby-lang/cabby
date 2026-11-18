/* bluetooth_mac.h — a Mac's Bluetooth, for the bluetooth library
 * (included by bluetooth.c). A Mac's is IOBluetooth: classes, in
 * Objective-C, which C reaches through the Objective-C runtime (sending a
 * message is calling objc_msgSend). Everything is looked up when it's
 * first needed (dlopen and dlsym): the frameworks, the runtime, and the
 * few plain C functions IOBluetooth has (turning it on and off), so
 * nothing more is linked, and this compiles on Linux too (where it's
 * never used).
 *
 * Devices are known by their addresses: each call finds the device again
 * ([IOBluetoothDevice deviceWithAddressString:]). Searching and pairing
 * wait in the thread's run loop, where IOBluetooth tells what happens.
 *
 * macOS asks the person, the first time, whether the program (or the
 * Terminal it runs in) may use Bluetooth; until it's allowed, nothing is
 * found. */

#include <ctype.h>
#include <dlfcn.h>
#include <time.h>

/* (functions found by name come as plain pointers: that's what dlsym is) */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

static char handed[256];   /* text being handed to Cabby, a byte at a time */

typedef void *id;
typedef void *SEL;
typedef void *Class;
typedef signed char OBJC_BOOL;

static struct {
	int tried, ok;
	id (*get_class)(const char *);
	SEL (*sel)(const char *);
	void *send;                       /* objc_msgSend: cast to each call's own shape */
	Class (*allocate_class)(Class, const char *, size_t);
	void (*register_class)(Class);
	OBJC_BOOL (*add_method)(Class, SEL, void *, const char *);
	void *(*pool_push)(void);
	void (*pool_pop)(void *);
	int (*power_get)(void);
	void (*power_set)(int);
	int (*visible_get)(void);
	void (*visible_set)(int);
	int (*run_loop)(const void *mode, double seconds, unsigned char once);
	const void **default_mode;
} mac;

static char mac_error[200];

static void mac_say(const char *why) { snprintf(mac_error, sizeof mac_error, "%s", why); }

/* the frameworks and the runtime: 1 if they're there (a Mac) */
static int mac_ready(void)
{
	if (mac.tried)
		return mac.ok;
	mac.tried = 1;
	void *objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_LAZY | RTLD_GLOBAL);
	void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY | RTLD_GLOBAL);
	dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_LAZY | RTLD_GLOBAL);
	void *bt = dlopen("/System/Library/Frameworks/IOBluetooth.framework/IOBluetooth", RTLD_LAZY | RTLD_GLOBAL);
	if (!objc || !cf || !bt)
		return 0;
	mac.get_class = (id (*)(const char *))dlsym(objc, "objc_getClass");
	mac.sel = (SEL (*)(const char *))dlsym(objc, "sel_registerName");
	mac.send = dlsym(objc, "objc_msgSend");
	mac.allocate_class = (Class (*)(Class, const char *, size_t))dlsym(objc, "objc_allocateClassPair");
	mac.register_class = (void (*)(Class))dlsym(objc, "objc_registerClassPair");
	mac.add_method = (OBJC_BOOL (*)(Class, SEL, void *, const char *))dlsym(objc, "class_addMethod");
	mac.pool_push = (void *(*)(void))dlsym(objc, "objc_autoreleasePoolPush");
	mac.pool_pop = (void (*)(void *))dlsym(objc, "objc_autoreleasePoolPop");
	mac.power_get = (int (*)(void))dlsym(bt, "IOBluetoothPreferenceGetControllerPowerState");
	mac.power_set = (void (*)(int))dlsym(bt, "IOBluetoothPreferenceSetControllerPowerState");
	mac.visible_get = (int (*)(void))dlsym(bt, "IOBluetoothPreferenceGetDiscoverableState");
	mac.visible_set = (void (*)(int))dlsym(bt, "IOBluetoothPreferenceSetDiscoverableState");
	mac.run_loop = (int (*)(const void *, double, unsigned char))dlsym(cf, "CFRunLoopRunInMode");
	mac.default_mode = (const void **)dlsym(cf, "kCFRunLoopDefaultMode");
	mac.ok = mac.get_class && mac.sel && mac.send && mac.pool_push && mac.pool_pop && mac.run_loop && mac.default_mode;
	return mac.ok;
}

/* ---- sending messages: each shape of call its own cast (on an Apple
 * processor, a message's values are passed as the function's own, so the
 * cast must match exactly) */

static id send_id(id o, const char *s) { return ((id (*)(id, SEL))mac.send)(o, mac.sel(s)); }
static id send_id_id(id o, const char *s, id a) { return ((id (*)(id, SEL, id))mac.send)(o, mac.sel(s), a); }
static id send_id_ulong(id o, const char *s, unsigned long a) { return ((id (*)(id, SEL, unsigned long))mac.send)(o, mac.sel(s), a); }
static id send_id_str(id o, const char *s, const char *a) { return ((id (*)(id, SEL, const char *))mac.send)(o, mac.sel(s), a); }
static const char *send_cstr(id o, const char *s) { return ((const char *(*)(id, SEL))mac.send)(o, mac.sel(s)); }
static OBJC_BOOL send_bool(id o, const char *s) { return ((OBJC_BOOL (*)(id, SEL))mac.send)(o, mac.sel(s)); }
static OBJC_BOOL send_bool_sel(id o, const char *s, SEL a) { return ((OBJC_BOOL (*)(id, SEL, SEL))mac.send)(o, mac.sel(s), a); }
static int send_int(id o, const char *s) { return ((int (*)(id, SEL))mac.send)(o, mac.sel(s)); }
static unsigned long send_ulong(id o, const char *s) { return ((unsigned long (*)(id, SEL))mac.send)(o, mac.sel(s)); }
static unsigned int send_uint(id o, const char *s) { return ((unsigned int (*)(id, SEL))mac.send)(o, mac.sel(s)); }
static signed char send_schar(id o, const char *s) { return ((signed char (*)(id, SEL))mac.send)(o, mac.sel(s)); }
static void send_void(id o, const char *s) { ((void (*)(id, SEL))mac.send)(o, mac.sel(s)); }
static void send_void_id(id o, const char *s, id a) { ((void (*)(id, SEL, id))mac.send)(o, mac.sel(s), a); }
static void send_void_uchar(id o, const char *s, unsigned char a) { ((void (*)(id, SEL, unsigned char))mac.send)(o, mac.sel(s), a); }
static void send_void_bool(id o, const char *s, OBJC_BOOL a) { ((void (*)(id, SEL, OBJC_BOOL))mac.send)(o, mac.sel(s), a); }

static id cls(const char *name) { return mac.get_class(name); }

static id ns_string(const char *s) { return send_id_str(cls("NSString"), "stringWithUTF8String:", s); }

static const char *c_string(id s) { return s ? send_cstr(s, "UTF8String") : ""; }

/* waits that long, letting IOBluetooth say what happens meanwhile (its
 * news comes through the thread's run loop; a run loop with nothing in it
 * returns at once, so the time is kept here) */
static void mac_wait(double seconds)
{
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		double gone = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) / 1e9;
		if (gone >= seconds)
			break;
		int r = mac.run_loop(*mac.default_mode, seconds - gone, 0);
		if (r == 1) {   /* (kCFRunLoopRunFinished: nothing to wait for) */
			struct timespec nap = { 0, 10000000 };
			nanosleep(&nap, NULL);
		}
	}
}

/* ---- devices: what's said about each, kept from the last listing */

typedef struct MacDevice {
	char name[128], address[24], kind[24];
	int paired, connected, strength;
} MacDevice;

static MacDevice *mac_devices;
static int nmac_devices;

/* "aa-bb-cc-dd-ee-ff" to "AA:BB:CC:DD:EE:FF" */
static void mac_address(const char *in, char *out, size_t size)
{
	snprintf(out, size, "%s", in);
	for (char *c = out; *c; c++)
		*c = *c == '-' ? ':' : (char)toupper((unsigned char)*c);
}

/* what it is, from its class of device: as BlueZ's names say it */
static const char *mac_kind(id dev)
{
	unsigned int major = send_uint(dev, "deviceClassMajor"), minor = send_uint(dev, "deviceClassMinor");
	switch (major) {
	case 1: return "computer";
	case 2: return "phone";
	case 3: return "network-wireless";
	case 4:   /* audio and video */
		switch (minor) {
		case 1: case 2: return "audio-headset";
		case 6: return "audio-headphones";
		case 5: case 7: case 8: case 10: return "audio-card";
		case 11: case 12: case 15: return "camera-video";
		case 14: return "video-display";
		}
		return "audio-card";
	case 5: {   /* keyboards, mice, game controllers */
		unsigned int kb = (minor >> 4) & 3, sub = minor & 15;
		if (sub == 1 || sub == 2)
			return "input-gaming";
		if (kb == 1 || kb == 3)
			return "input-keyboard";
		if (kb == 2)
			return sub == 5 ? "input-tablet" : "input-mouse";
		return "";
	}
	case 6: return "printer";
	}
	return "";
}

static void mac_take(id dev, MacDevice *d)
{
	memset(d, 0, sizeof *d);
	const char *name = c_string(send_id(dev, "name"));
	mac_address(c_string(send_id(dev, "addressString")), d->address, sizeof d->address);
	snprintf(d->name, sizeof d->name, "%s", *name ? name : d->address);
	snprintf(d->kind, sizeof d->kind, "%s", mac_kind(dev));
	d->paired = send_bool(dev, "isPaired") != 0;
	d->connected = send_bool(dev, "isConnected") != 0;
	signed char rssi = send_schar(dev, "rawRSSI");
	d->strength = rssi == 127 || rssi >= 0 ? 0 : rssi;   /* (127: not heard) */
}

static void mac_add(id dev, int which)
{
	MacDevice d;
	mac_take(dev, &d);
	for (int i = 0; i < nmac_devices; i++)
		if (strcmp(mac_devices[i].address, d.address) == 0)
			return;
	if ((which == 1 && !d.paired) || (which == 2 && !d.connected) || (which == 3 && !d.strength))
		return;
	mac_devices = realloc(mac_devices, sizeof *mac_devices * (size_t)(nmac_devices + 1));
	mac_devices[nmac_devices++] = d;
}

static void mac_add_all(id array, int which)
{
	unsigned long n = array ? send_ulong(array, "count") : 0;
	for (unsigned long i = 0; i < n; i++)
		mac_add(send_id_ulong(array, "objectAtIndex:", i), which);
}

static id mac_device(const char *address)
{
	char dashed[24];
	snprintf(dashed, sizeof dashed, "%s", address);
	for (char *c = dashed; *c; c++)
		*c = *c == ':' ? '-' : (char)tolower((unsigned char)*c);
	return send_id_id(cls("IOBluetoothDevice"), "deviceWithAddressString:", ns_string(dashed));
}

/* ---- pairing: a delegate, made at run time, that IOBluetooth tells */

static int pair_done, pair_error;

static void pd_finished(id self, SEL s, id sender, int error)
{
	(void)self, (void)s, (void)sender;
	pair_done = 1;
	pair_error = error;
}

static void pd_pin(id self, SEL s, id sender)
{
	(void)self, (void)s;
	static const unsigned char pin[16] = "0000";
	((void (*)(id, SEL, unsigned long, const void *))mac.send)(sender, mac.sel("replyPINCode:PINCode:"), 4, pin);
}

static void pd_confirm(id self, SEL s, id sender, unsigned int number)
{
	(void)self, (void)s;
	snprintf(last_code, sizeof last_code, "%06u", number);
	send_void_bool(sender, "replyUserConfirmation:", 1);
}

static void pd_passkey(id self, SEL s, id sender, unsigned int passkey)
{
	(void)self, (void)s, (void)sender;
	snprintf(last_code, sizeof last_code, "%06u", passkey);
	fprintf(stderr, "bluetooth: to pair, type %s on it, then Enter\n", last_code);
}

static id pair_delegate(void)
{
	static id made;
	if (made)
		return made;
	Class c = mac.allocate_class(cls("NSObject"), "CabbyBluetoothPairing", 0);
	if (!c)
		return NULL;
	mac.add_method(c, mac.sel("devicePairingFinished:error:"), (void *)pd_finished, "v@:@i");
	mac.add_method(c, mac.sel("devicePairingPINCodeRequest:"), (void *)pd_pin, "v@:@");
	mac.add_method(c, mac.sel("devicePairingUserConfirmationRequest:numericValue:"), (void *)pd_confirm, "v@:@I");
	mac.add_method(c, mac.sel("devicePairingUserPasskeyNotification:passkey:"), (void *)pd_passkey, "v@:@I");
	mac.register_class(c);
	made = send_id(send_id(c, "alloc"), "init");
	return made;
}

/* ---- what bluetooth.hby calls on a Mac */

int cby_bluetooth_native(void)
{
#ifdef __APPLE__
	const char *want = getenv("CABBY_BLUETOOTH");
	if (want && strcmp(want, "bluez") == 0)
		return 0;   /* (BlueZ, over D-Bus, even on a Mac: the tests' stand-in) */
	return mac_ready();
#else
	return 0;
#endif
}

int cby_bluetooth_mac_available(void)
{
	if (!mac_ready())
		return 0;
	void *p = mac.pool_push();
	int there = send_id(cls("IOBluetoothHostController"), "defaultController") != NULL;
	mac.pool_pop(p);
	if (!there)
		mac_say("there's no Bluetooth here");
	return there;
}

int cby_bluetooth_mac_is_on(void) { return mac_ready() && mac.power_get && mac.power_get() != 0; }

int cby_bluetooth_mac_set_on(int on)
{
	if (!mac_ready() || !mac.power_set) {
		mac_say("this Mac can't be told to turn Bluetooth on or off");
		return 0;
	}
	mac.power_set(on != 0);
	for (int i = 0; i < 50 && cby_bluetooth_mac_is_on() != (on != 0); i++)
		mac_wait(0.1);   /* (it takes a moment) */
	if (cby_bluetooth_mac_is_on() != (on != 0)) {
		mac_say("Bluetooth didn't turn on or off");
		return 0;
	}
	mac_error[0] = 0;
	return 1;
}

int cby_bluetooth_mac_visible(void) { return mac_ready() && mac.visible_get && mac.visible_get() != 0; }

int cby_bluetooth_mac_set_visible(int on)
{
	if (!mac_ready() || !mac.visible_set) {
		mac_say("this Mac can't be told to be visible");
		return 0;
	}
	mac.visible_set(on != 0);
	mac_error[0] = 0;
	return 1;
}

/* the devices it knows (0 all, 1 paired, 2 connected): how many; then
 * cby_bluetooth_mac_text and the rest for each */
int cby_bluetooth_mac_list(int which)
{
	nmac_devices = 0;
	if (!mac_ready())
		return 0;
	void *p = mac.pool_push();
	mac_add_all(send_id(cls("IOBluetoothDevice"), "pairedDevices"), which);
	mac_add_all(send_id_ulong(cls("IOBluetoothDevice"), "recentDevices:", 0), which);
	mac.pool_pop(p);
	return nmac_devices;
}

/* searching for ms milliseconds: what was heard, the most strongly first */
int cby_bluetooth_mac_search(int ms)
{
	nmac_devices = 0;
	if (!mac_ready())
		return 0;
	if (!cby_bluetooth_mac_is_on()) {
		mac_say("Not Ready (Bluetooth is off)");
		return 0;
	}
	void *p = mac.pool_push();
	id inquiry = send_id_id(cls("IOBluetoothDeviceInquiry"), "inquiryWithDelegate:", NULL);
	if (!inquiry) {
		mac.pool_pop(p);
		mac_say("the search couldn't start");
		return 0;
	}
	send_void_uchar(inquiry, "setInquiryLength:", (unsigned char)(ms / 1000 > 0 && ms / 1000 < 255 ? ms / 1000 : 10));
	send_void_bool(inquiry, "setUpdateNewDeviceNames:", 1);
	int started = send_int(inquiry, "start");
	if (started != 0) {
		mac.pool_pop(p);
		snprintf(mac_error, sizeof mac_error, "the search couldn't start (%#x)", (unsigned)started);
		return 0;
	}
	mac_wait(ms / 1000.0);
	send_int(inquiry, "stop");
	mac_add_all(send_id(inquiry, "foundDevices"), 3);
	mac.pool_pop(p);
	/* the most strongly heard first */
	for (int i = 1; i < nmac_devices; i++)
		for (int j = i; j > 0 && mac_devices[j].strength > mac_devices[j - 1].strength; j--) {
			MacDevice t = mac_devices[j];
			mac_devices[j] = mac_devices[j - 1];
			mac_devices[j - 1] = t;
		}
	mac_error[0] = 0;
	return nmac_devices;
}

/* the listed device i's: 0 name, 1 address, 2 kind (the text, handed over
 * as the code is); 3 paired, 4 connected, 5 strength */
int64_t cby_bluetooth_mac_text(int64_t i, int64_t what)
{
	const char *t = "";
	if (i >= 0 && i < nmac_devices)
		t = what == 0 ? mac_devices[i].name : what == 1 ? mac_devices[i].address : mac_devices[i].kind;
	snprintf(handed, sizeof handed, "%s", t);
	return (int64_t)strlen(handed);
}

int64_t cby_bluetooth_mac_number(int64_t i, int64_t what)
{
	if (i < 0 || i >= nmac_devices)
		return 0;
	return what == 3 ? mac_devices[i].paired : what == 4 ? mac_devices[i].connected : mac_devices[i].strength;
}

/* one device, found again by its address: 0 if it isn't known */
int cby_bluetooth_mac_find(const char *address)
{
	nmac_devices = 0;
	if (!mac_ready())
		return 0;
	void *p = mac.pool_push();
	id dev = mac_device(address);
	if (dev)
		mac_add(dev, 0);
	mac.pool_pop(p);
	return nmac_devices;
}

/* pair, connect, disconnect, forget (what: 0 to 3); 1 if it worked */
int cby_bluetooth_mac_do(const char *address, int what)
{
	if (!mac_ready())
		return 0;
	void *p = mac.pool_push();
	id dev = mac_device(address);
	int ok = 0;
	if (!dev) {
		mac_say("Does Not Exist");
	} else if (what == 0) {
		id pair = send_id_id(cls("IOBluetoothDevicePair"), "pairWithDevice:", dev);
		id d = pair_delegate();
		if (!pair || !d) {
			mac_say("pairing couldn't start");
		} else {
			send_void_id(pair, "setDelegate:", d);
			pair_done = 0;
			pair_error = 0;
			int started = send_int(pair, "start");
			if (started != 0) {
				snprintf(mac_error, sizeof mac_error, "pairing couldn't start (%#x)", (unsigned)started);
			} else {
				for (int i = 0; i < 1200 && !pair_done; i++)
					mac_wait(0.1);   /* (up to two minutes, for a code to be typed) */
				if (!pair_done) {
					send_void(pair, "stop");
					mac_say("Authentication Timeout");
				} else if (pair_error) {
					snprintf(mac_error, sizeof mac_error, "Authentication Failed (%#x)", (unsigned)pair_error);
				} else {
					ok = 1;
				}
			}
		}
	} else if (what == 1) {
		int r = send_int(dev, "openConnection");
		ok = r == 0;
		if (!ok)
			snprintf(mac_error, sizeof mac_error, "Page Timeout (%#x)", (unsigned)r);
	} else if (what == 2) {
		int r = send_int(dev, "closeConnection");
		ok = r == 0;
		if (!ok)
			snprintf(mac_error, sizeof mac_error, "it didn't disconnect (%#x)", (unsigned)r);
	} else if (what == 3) {
		/* (forgetting has no public way: the one the Mac's settings use) */
		if (send_bool_sel(dev, "respondsToSelector:", mac.sel("remove"))) {
			send_void(dev, "remove");
			ok = 1;
		} else {
			mac_say("this Mac can't be told to forget a device");
		}
	}
	mac.pool_pop(p);
	if (ok)
		mac_error[0] = 0;
	return ok;
}

int64_t cby_bluetooth_mac_error(void)
{
	snprintf(handed, sizeof handed, "%s", mac_error);
	return (int64_t)strlen(handed);
}

char cby_bluetooth_handed_char(int64_t i) { return i >= 0 && i < (int64_t)strlen(handed) ? handed[i] : 0; }
