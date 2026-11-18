# bluetooth — devices nearby

```cabby
use bluetooth
```

Turning Bluetooth on and off, the devices the computer knows, searching
for more, pairing with them, connecting and disconnecting, forgetting
them, and being told when they come and go.

On Linux, Bluetooth is **BlueZ** (its service is `bluetoothd`), which
every desktop has: `bluez` on Arch and Debian, started with `sudo
systemctl enable --now bluetooth`. The library talks to it over D-Bus's
system bus, so nothing more is installed, and no root is needed: a person
logged in at the computer may use its Bluetooth. On a **Mac**, it's the
Mac's own (IOBluetooth), with nothing to install: see [On a
Mac](#on-a-mac). The same program works on both.

* [A first look](#a-first-look)
* [The computer's Bluetooth](#the-computers-bluetooth)
* [Devices](#devices)
* [Searching](#searching)
* [Pairing](#pairing)
* [When it doesn't work](#when-it-doesnt-work)
* [Being told what changes](#being-told-what-changes)
* [On a Mac](#on-a-mac)
* [Not yet](#not-yet)

## A first look

<!-- check -->
```cabby
use bluetooth

int main {
	if !bluetooth.available {
		println "No Bluetooth here:", bluetooth.error
		return 1
	}
	bluetooth.turn_on
	for D in bluetooth.devices {
		str State = "known"
		if D.connected {
			State = "connected"
		} else if D.paired {
			State = "paired"
		}
		println D.name, "(" + D.kind + "):", State
	}
	return 0
}
```

```text
Ana's Headphones (audio-headset): connected
MX Keys (input-keyboard): paired
Living Room TV (video-display): known
```

## The computer's Bluetooth

| | |
|---|---|
| `bool bluetooth.available` | true if the computer has Bluetooth, and BlueZ is running |
| `bool bluetooth.is_on` | true if it's on |
| `bool bluetooth.turn_on`, `bool bluetooth.turn_off` | turns it on or off; false if it couldn't (airplane mode blocks it: `rfkill unblock bluetooth`) |
| `bool bluetooth.set_visible bool On`, `bool bluetooth.visible` | whether other devices searching can see this computer, to pair from them |
| `str bluetooth.error` | why the last thing that returned false didn't work ([below](#when-it-doesnt-work)) |

## Devices

`bluetooth.devices` is every device BlueZ knows: those paired with the
computer, and those a search has found lately. `bluetooth.paired_devices`
and `bluetooth.connected_devices` are some of them, and
`bluetooth.device_named "MX Keys"` finds one by its name or its address
(`found` is false if there's none).

A `bluetooth.device` says what it was when it was found (`refresh` reads
it again):

| | |
|---|---|
| `str name` | its name, as its owner set it: `"Ana's Headphones"` |
| `str address` | its address, which is its own: `"AA:BB:CC:DD:EE:FF"` |
| `str kind` | what it is, as it says: `"audio-headset"`, `"audio-headphones"`, `"audio-card"` (a speaker), `"input-keyboard"`, `"input-mouse"`, `"input-gaming"`, `"phone"`, `"computer"` ... (`""` if it doesn't say) |
| `bool paired`, `bool connected` | |
| `bool trusted` | it may connect by itself, without asking |
| `int strength` | how strongly it's heard, in dBm: -40 is close by, -90 at the edge (0: not heard lately) |
| `int battery` | its battery, in percent (-1: it doesn't say) |
| `bool found` | false for a `device_named` that found none |
| `void refresh` | reads what it says again |

And what can be done with one, each true if it worked:

| | |
|---|---|
| `bool pair` | pairs with it ([below](#pairing)) |
| `bool connect` | connects (paired first): headphones then play the computer's sound, a keyboard types |
| `bool disconnect` | |
| `bool set_trusted bool On` | |
| `bool forget` | unpairs it, and it's gone from the list until it's found again |

## Searching

A device can only be found while it's set to be: headphones in their
pairing mode (usually the power button held), a phone with its Bluetooth
settings open.

<!-- check -->
```cabby
use bluetooth

int main {
	bluetooth.turn_on
	println "Searching for 10 seconds..."
	for D in bluetooth.search time.seconds(10) {
		println D.strength, D.name, D.address
	}
}
```

`bluetooth.search Wait` searches for that long, and gives what it heard,
the most strongly heard (nearest) first. It waits; a window would rather
go on while it searches, with `bluetooth.start_search`, a timer that
looks at `bluetooth.devices` now and then, and `bluetooth.stop_search`.
(Searching uses the radio, and slows other Bluetooth: stop when done.)

## Pairing

Pairing is the two remembering each other, once, so that they can
connect from then on. `D.pair` pairs, waiting (up to two minutes) while
it's done:

<!-- check -->
```cabby
use bluetooth

int main {
	bluetooth.device Keys = bluetooth.device_named "MX Keys"
	if !Keys.found {
		println "not found: is it in pairing mode? (search first)"
		return 1
	}
	if Keys.pair && Keys.connect {
		println "ready to type"
	} else {
		println "it didn't work:", bluetooth.error
	}
	return 0
}
```

Most devices pair without asking anything (headphones, speakers, mice).
A **keyboard** shows a code to type on it: it's written on the terminal
(`bluetooth: to pair AA:BB:..., type 482913 on it, then Enter`), and is
`bluetooth.code`. Where both have a screen (a phone), each shows the same
number; the library says yes to it, since the program asked to pair.
Paired, a device is trusted too, so it can connect again by itself, as
the desktop's own Bluetooth settings do.

## When it doesn't work

Bluetooth fails often, for ordinary reasons, so what can fail gives false
rather than a panic, and `bluetooth.error` says why:

| `bluetooth.error` | usually means |
|---|---|
| `Page Timeout`, `Host is down` | the device didn't answer: it's off, asleep or too far away |
| `Authentication Failed`, `Authentication Rejected` | pairing was turned down, or the code typed was wrong |
| `Not Ready`, `Resource Not Ready` | Bluetooth is off (`bluetooth.turn_on`) |
| `In Progress` | it's being done already |
| `Bluetooth's service isn't running ...` | BlueZ isn't running: `sudo systemctl start bluetooth` |
| `there's no Bluetooth adapter ...` | the computer has no Bluetooth (or it's blocked) |

Using a device that wasn't found (`device_named` found none, and `found`
wasn't checked) is a panic.

## Being told what changes

A `bluetooth.watcher` tells a handler when a device is found, connects,
disconnects or is forgotten, or its battery changes, from a window's (or
a game's, or `events.run`'s) loop, between their other events:

<!-- check -->
```cabby
use bluetooth
use events

object tray {
	bluetooth.watcher Watcher

	void begin {
		Watcher.on_change changed
		events.run
	}

	void changed {
		bluetooth.device D = Watcher.device
		if Watcher.what == "connected" {
			println D.name, "connected, battery", D.battery
		} else if Watcher.what == "disconnected" {
			println D.name, "disconnected"
		}
	}
}

int main {
	tray T
	T.begin
}
```

`what` is `"found"`, `"gone"` (forgotten), `"connected"`,
`"disconnected"`, `"paired"`, `"unpaired"`, `"battery"`, `"name"` or
`"strength"` (heard more or less strongly, while searching), and
`device` is the device, as it is now.

## On a Mac

Everything above works on a Mac, through its own Bluetooth (IOBluetooth),
with these differences:

* **Permission.** The first time, macOS asks whether the program may use
  Bluetooth: for a program run in Terminal, it's Terminal that's asked
  (System Settings > Privacy & Security > Bluetooth). Until it's allowed,
  nothing is found. Over SSH, there's no one to ask, so it may find
  nothing at all.
* **Searching** waits: `bluetooth.search` works, but `start_search`
  gives false (a Mac's search needs waiting for).
* `battery` is -1 (a Mac keeps its devices' batteries to itself), and a
  paired device is always trusted (`set_trusted` changes nothing).
* `forget` uses what the Mac's Bluetooth settings use, which isn't public:
  a later macOS could stop it working.
* A **watcher** looks each second at what's changed, rather than being
  told: what's found, connected or disconnected is told up to a second
  later.

## Not yet

* Bluetooth Low Energy devices on a Mac that aren't paired (CoreBluetooth)
* Talking to a device's own services directly (Bluetooth Low Energy's
  GATT: a heart-rate strap's beats, a sensor's readings)
* Sending files (OBEX), and the computer's own audio settings for a
  headset (PipeWire chooses those)
* More than one adapter: the first is used
