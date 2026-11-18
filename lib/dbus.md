# dbus — talking to the desktop

```cabby
use dbus
```

Much of a Linux desktop talks over **D-Bus**: the notifications that pop
up, the battery, the network, Bluetooth, and the music player all answer
questions on it, and announce when something changes. A bar that shows
the battery, or a program that pops up a notification, talks D-Bus.

There are two buses:

| Bus | What's on it |
|---|---|
| `dbus.session` | your desktop's: notifications, media players, the screen saver |
| `dbus.system` | the computer's: the battery (UPower), the network (NetworkManager), Bluetooth (BlueZ), logins (logind) |

On each bus are **services**, each known by a name like
`org.freedesktop.Notifications`. A service has **objects**, at paths like
`/org/freedesktop/Notifications`. Each object has **interfaces**: groups
of **methods** to call, **properties** to read, and **signals** it sends.

Cabby speaks D-Bus itself, over the bus's own socket, so there's nothing
more to install.

## A notification

The quickest thing to do, and a good check that it all works:

<!-- check -->
```cabby
use dbus

int main {
	long Number = dbus.notify "Cabby", "Hello from D-Bus"
	println "shown as number", Number
	return 0
}
```

`dbus.notify_again Number, Title, Body` changes that notification where
it is (for a progress "37%", say) rather than showing another.

## Reading a property

A property is a value an object has: the battery's charge, the volume.
`Bus.property` reads one; `Bus.properties` reads all of an interface's, as
a lookup by name:

<!-- check -->
```cabby
use dbus

int main {
	dbus.bus Bus = dbus.system
	str Battery = "/org/freedesktop/UPower/devices/DisplayDevice"
	json Charge = Bus.property("org.freedesktop.UPower", Battery,
		"org.freedesktop.UPower.Device", "Percentage")
	println "battery:", Charge.double, "%"

	json All = Bus.properties("org.freedesktop.UPower", Battery,
		"org.freedesktop.UPower.Device")
	println "charging:", All["State"].long is 1
	return 0
}
```

What comes back is a **json** value (see json.md): `use dbus` brings
`json` in with it. D-Bus's values become json's:

| D-Bus | Letter | Read with |
|---|---|---|
| text, an object's path, a signature | `s`, `o`, `g` | `.str` |
| whole numbers (8 to 64 bits) | `y n q i u x t` | `.long` |
| a number with a point | `d` | `.double` |
| true or false | `b` | `.bool` |
| a list (an array) | `a` | `V[0]`, `.length`, `for X in V` |
| a struct | `( )` | `V[0]`, `V[1]`, as a list |
| a dictionary | `a{ }` | `V["name"]`, `.keys`, `.has` (keys that are numbers become text: `V["3"]`) |
| a variant | `v` | unwrapped: it's the value it holds |

## Calling a method

A call is made, given its values in order, then sent. The answer is a
list: one item for each value the method gives back.

<!-- check -->
```cabby
use dbus

int main {
	dbus.bus Bus = dbus.session
	dbus.message M = dbus.method_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "GetNameOwner")
	M.add_str "org.freedesktop.Notifications"
	json Answer = Bus.call M
	println "notifications are shown by", Answer[0].str
	return 0
}
```

`dbus.method_call Service, Path, Interface, Method` makes the call. A
method's documentation says what it takes as **a signature**: a letter
for each value. GetNameOwner takes `s`, one string. Notify (what
`dbus.notify` calls) takes `susssasa{sv}i`: a string, an unsigned number,
three strings, a list of strings, a dictionary of strings to variants,
then a number. Each value is added with its own kind:

| Add | Letter | |
|---|---|---|
| `M.add_str S` | `s` | text |
| `M.add_path P` | `o` | an object's path, "/org/..." |
| `M.add_bool B` | `b` | |
| `M.add_byte N` | `y` | 0 to 255 |
| `M.add_int N`, `M.add_uint N` | `i`, `u` | 32 bits: from about -2 billion to 2 billion; from 0 to about 4 billion |
| `M.add_long N`, `M.add_ulong N` | `x`, `t` | 64 bits |
| `M.add_double D` | `d` | |
| `M.add_strs L` | `as` | a `list str`, in one go |

`M.signature` is what's been given so far (`"susss"`), to compare with
what the method wants.

### Lists, dictionaries, variants and structs

These hold other values. Each is begun, given its values, and ended:

| Begin | End | |
|---|---|---|
| `M.begin_list "s"` | `M.end_list` | a list of that type (`"s"`, `"u"`, `"(si)"`...); begun and ended at once, it's empty |
| `M.begin_lookup "s", "v"` | `M.end_lookup` | a dictionary: then a key, its value, a key, its value... |
| `M.begin_variant "s"` | `M.end_variant` | one value of that type, which says its own type |
| `M.begin_struct` | `M.end_struct` | values side by side |

Notify's hints, `a{sv}`, are a dictionary of strings to variants. One
saying how urgent it is:

<!-- check -->
```cabby
use dbus

int main {
	dbus.bus Bus = dbus.session
	dbus.message M = dbus.method_call("org.freedesktop.Notifications",
		"/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify")
	M.add_str "my program"          // the program's name
	M.add_uint 0                    // a new notification, not one changed
	M.add_str "dialog-warning"      // an icon
	M.add_str "Battery low"
	M.add_str "10% left"
	M.begin_list "s"                // actions: none
	M.end_list
	M.begin_lookup "s", "v"         // hints
	M.add_str "urgency"
	M.begin_variant "y"
	M.add_byte 2                    // critical
	M.end_variant
	M.end_lookup
	M.add_int 5000                  // shown for 5 seconds
	println M.signature             // susssasa{sv}i
	Bus.call M
	return 0
}
```

A value of the wrong kind for its list or dictionary is a panic, at once,
that says so ("this list was begun as a list of 's', so it can't be given
a 'u'"), as is sending a message with something still begun.

### When a call doesn't work

A call that doesn't work (no such service, no such method, the wrong
values) is a panic with what D-Bus said:

```text
panic at bar.cby:12:5: the D-Bus call org.example.Thing.Go (to org.example.Thing) didn't work:
org.freedesktop.DBus.Error.ServiceUnknown: The name org.example.Thing was not provided by any .service files
```

When it might not work (a music player that may not be running),
`try_call` doesn't panic: `Bus.error` is then why, or `""` when it worked.

```cabby
json Answer = Bus.try_call M
if Bus.error != "" {
	println "no player:", Bus.error
}
```

A call waits up to 25 seconds for its answer.

## Signals: being told when something changes

A service sends a **signal** when something happens: a notification is
closed, the battery's charge changes, a song starts. `Bus.listen` asks
for the signals of an interface (a name, or `""` for all its signals), and
`Bus.on_signal` names the method that hears them. It's called from the
program's loop (a window's, a game's each frame, or `events.run`), between
its other events, as a button's handler is. In it, `Bus.signal` is the
signal's values (a json list) and `Bus.signal_name` says which signal it
is:

<!-- check -->
```cabby
use dbus
use events

object watcher {
	dbus.bus Bus                    // the bus is the watcher's

	void begin {
		Bus = dbus.session
		Bus.listen "org.freedesktop.Notifications", "NotificationClosed"
		Bus.listen "org.freedesktop.Notifications", "ActionInvoked"
		Bus.on_signal heard
		long Number = dbus.notify "Cabby", "Close me"
		println "showing", Number
	}

	void heard {
		json Values = Bus.signal
		if Bus.signal_name == "NotificationClosed" {
			println "notification", Values[0].long, "closed"
			events.stop
		}
	}
}

int main {
	watcher W
	W.begin
	events.run                      // until it's closed
	return 0
}
```

As with a window's buttons, the handler is a method of the object the bus
is part of: the bus is a field of `watcher`, so the bus, and what it calls,
go together.

| In the handler | |
|---|---|
| `Bus.signal` | its values, a json list |
| `Bus.signal_name` | which signal: `"NotificationClosed"` |
| `Bus.signal_interface` | whose: `"org.freedesktop.Notifications"` |
| `Bus.signal_path` | the object that sent it |
| `Bus.signal_sender` | the service's unique name (`":1.17"`) |

Properties that change are announced with one signal for them all,
`PropertiesChanged` in `org.freedesktop.DBus.Properties`: its values are
the interface, a lookup of what changed, and a list of names that changed
without saying to what.

## Objects of your own: answering calls

A program can be called, too: it owns a **name** others find it by, has
**objects** at paths, and answers calls to them, as a notification server
or a media player does. `Bus.own_name` takes a name (`true` if it's the
program's now, `false` if another program has it), `Bus.serve` an
object's path, and `Bus.on_call` names the method that hears each call,
from the program's loop, as `on_signal`'s does. In it:

| In the handler | |
|---|---|
| `Bus.call_name` | the method called: `"Notify"` |
| `Bus.call_values` | its values, a json list |
| `Bus.call_interface`, `Bus.call_path`, `Bus.call_sender` | which interface, which object, and who called (`":1.17"`) |
| `Bus.reply A` | the answer: `dbus.answer` makes one, given its values as a call is |
| `Bus.fail Name, Why` | it didn't work: an error's name (`"org.example.Error.NotFound"`) and why, for the caller |

A call that's neither replied to nor failed is answered with nothing (a
method that gives nothing back). `Ping` (org.freedesktop.DBus.Peer) is
answered by itself, and so is `Introspect`, with what `Bus.describe
Path, Xml` says the object has: its `<interface>` elements, in D-Bus's
XML, for tools (d-feet, `busctl introspect`) and libraries that look
before they call.

`dbus.signal_message Path, Interface, Name` makes a **signal**, given its
values as a call is, and `Bus.send` sends it to everyone listening.

A notification server, the start of one: it shows each notification as a
line, gives it a number, and says when one's closed.

<!-- check -->
```cabby
use dbus
use events

object notifications {
	dbus.bus Bus
	long Next                           // the last number given

	void begin {
		Bus = dbus.session
		if !Bus.own_name("org.freedesktop.Notifications") {
			println "another notification server is running"
			return
		}
		Bus.serve "/org/freedesktop/Notifications"
		Bus.on_call answer
		events.run
	}

	void answer {
		json Values = Bus.call_values
		dbus.message A = dbus.answer
		if Bus.call_name == "Notify" {
			// app, replaces, icon, summary, body, actions, hints, timeout
			Next++
			println Values[0].str + ":", Values[3].str, "-", Values[4].str
			A.add_uint Next
		} else if Bus.call_name == "GetCapabilities" {
			A.begin_list "s"
			A.add_str "body"
			A.end_list
		} else if Bus.call_name == "GetServerInformation" {
			A.add_str "Cabby notifications"     // its name, who made it,
			A.add_str "Cabby"                   // its version, and the
			A.add_str "1.0"                     // specification's
			A.add_str "1.2"
		} else if Bus.call_name == "CloseNotification" {
			closed Values[0].long
		} else {
			Bus.fail "org.freedesktop.DBus.Error.UnknownMethod", "no method " + Bus.call_name
			return
		}
		Bus.reply A
	}

	// "NotificationClosed": its number, and why (3: closed by a call)
	void closed long Number {
		dbus.message S = dbus.signal_message("/org/freedesktop/Notifications",
			"org.freedesktop.Notifications", "NotificationClosed")
		S.add_uint Number
		S.add_uint 3
		Bus.send S
	}
}

int main {
	notifications N
	N.begin
	return 0
}
```

Run it when nothing else is showing notifications (stop the desktop's
own first: mako, dunst, or a shell's), then `notify-send Hello there` in
another terminal.

### Properties

An object's **properties** (a media player's `PlaybackStatus`, its
`Volume`) are set with `Bus.set_property Path, Interface, Name, Value`,
and then answered by themselves: `Get` and `GetAll` (the
org.freedesktop.DBus.Properties methods every library uses) give what
was set, without the call handler hearing them. Each time one changes,
everyone listening is told (the `PropertiesChanged` signal), as a bar
showing what's playing needs.

| | |
|---|---|
| `Bus.set_property Path, Interface, Name, V` | `V` is a `dbus.value`, with one value of any type added: `V.add_strs {"Ana", "Bruno"}` |
| `Bus.set_str_property Path, Interface, Name, "Playing"` | the same, for a `str` (and `set_int_property`, `set_double_property`, `set_bool_property`) |

```cabby
Bus.serve "/org/mpris/MediaPlayer2"
Bus.set_str_property "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "PlaybackStatus", "Playing"
Bus.set_double_property "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "Volume", 0.8
```

Setting one to what it is already sends nothing. A property that hasn't
been set goes to the call handler, if there is one (to work out as it
likes), or else is answered with UnknownProperty; and `Set` (someone else
changing one) goes to the call handler, to allow or not, or else is
answered with PropertyReadOnly.

## Open files

Some services hand over an open file, or take one: logind's `Inhibit`
gives one that keeps the computer from sleeping until it's closed, and a
file chooser portal gives the files chosen. A value of type `h` comes as
the file's number, and is the program's from then on: `dbus.close_file
Fd` closes it. `M.add_file Fd` sends one (a copy goes; the program keeps
its own).

```cabby
// keeps the computer awake while something long is done
dbus.bus System = dbus.system
dbus.message Ask = dbus.method_call("org.freedesktop.login1", "/org/freedesktop/login1",
	"org.freedesktop.login1.Manager", "Inhibit")
Ask.add_str "sleep"                 // what's held off
Ask.add_str "Backup"                // who's asking
Ask.add_str "Copying files"         // why
Ask.add_str "block"
long Lock = System.call(Ask)[0].long
// ... the long work ...
dbus.close_file Lock                // and it can sleep again
```

Buses agree to pass files when a program signs in, as Linux's do;
sending one over a bus that hasn't is a panic that says so.

## Finding out what's there

`busctl` (part of systemd) lists what's on a bus, and what each object
has:

```sh
busctl --user list                                  # the session bus's services
busctl --user tree org.freedesktop.Notifications    # its objects
busctl --user introspect org.freedesktop.Notifications /org/freedesktop/Notifications
busctl introspect org.freedesktop.UPower /org/freedesktop/UPower/devices/DisplayDevice
```

Each method is listed with its signature: what to `add_` for it, in
order. Some well-known ones:

| What | Bus | Service | Path | Interface |
|---|---|---|---|---|
| notifications | session | org.freedesktop.Notifications | /org/freedesktop/Notifications | org.freedesktop.Notifications |
| the battery | system | org.freedesktop.UPower | /org/freedesktop/UPower/devices/DisplayDevice | org.freedesktop.UPower.Device |
| a music player | session | org.mpris.MediaPlayer2.*name* (spotify, vlc...) | /org/mpris/MediaPlayer2 | org.mpris.MediaPlayer2.Player |
| the network | system | org.freedesktop.NetworkManager | /org/freedesktop/NetworkManager | org.freedesktop.NetworkManager |
| Bluetooth | system | org.bluez | /org/bluez/hci0 | org.bluez.Adapter1 |
| power off, sleep | system | org.freedesktop.login1 | /org/freedesktop/login1 | org.freedesktop.login1.Manager |

## Where the bus is

`dbus.session` connects to the bus that `DBUS_SESSION_BUS_ADDRESS` names
(as every desktop sets), or else `$XDG_RUNTIME_DIR/bus`. `dbus.system`
connects to `DBUS_SYSTEM_BUS_ADDRESS`, or `/var/run/dbus/system_bus_socket`.
Either one, when there's no bus, is a panic saying so. On a Mac there's no
D-Bus unless you've installed it (`brew install dbus`).

`dbus.try_session` is for a program that can do without one: with no bus
it gives one that isn't open (`Bus.is_open` is `false`), and its
`try_call`s don't work, without a panic (`Bus.error` is
`"no session bus"`). The `media` library finds players this way.

`Bus.name` is the program's own unique name on the bus (`":1.42"`).

## Not yet

- Introspection of properties by themselves: `Bus.describe` says what an
  object has, `<property>` elements and all, as it's written.
