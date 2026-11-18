# linux — what only Linux has

```cabby
use linux
```

What a Linux computer has that a Mac doesn't, read from the files the
kernel keeps for it:

* [Sensors](#sensors): temperatures, fans, voltages, power
* [Disks and partitions](#disks-and-partitions)
* [USB devices](#usb-devices)
* [The kernel's modules](#the-kernels-modules)
* [The kernel's settings](#the-kernels-settings)
* [Services](#services): systemd's

What every system has (disks' free space, the programs running, watching
files) is the [os](os.md) library's. On a Mac, each of these is a panic
that says so; `system.kind` tells a program which it's on.

## Sensors

<!-- check -->
```cabby
use linux

int main {
	for T in linux.temperatures {
		println T.chip, T.label + ":", T.value, T.unit
	}
	for F in linux.fans {
		println "fan", F.label + ":", F.value, F.unit
	}
	return 0
}
```

```text
coretemp Package id 0: 48.0 °C
coretemp Core 0: 45.5 °C
nvme Composite: 38.85 °C
fan fan1: 2450.0 RPM
```

| `linux.sensor` | |
|---|---|
| `str chip` | the chip it's on, as its driver names it: `"coretemp"` (an Intel processor), `"k10temp"` (an AMD one), `"nvme"`, `"amdgpu"` |
| `str label` | which it is: `"Package id 0"`, `"Core 3"`, `"Composite"` |
| `str kind` | `"temperature"`, `"fan"`, `"voltage"`, `"power"` or `"current"` |
| `double value`, `str unit` | what it read: degrees Celsius, turns a minute, volts, watts, amps; `"°C"`, `"RPM"`, `"V"`, `"W"`, `"A"` |
| `double high`, `double critical` | when it's too much, as the chip says (0 if it doesn't) |

`linux.sensors` lists them all, `linux.temperatures` and `linux.fans`
those kinds. They're the kernel's hwmon drivers' (what `sensors` shows);
a board whose hwmon has no temperatures (a Raspberry Pi) gives its
thermal zones' instead. Some chips need a driver loaded first (`sudo
sensors-detect` finds which).

## Disks and partitions

<!-- check -->
```cabby
use linux

int main {
	for D in linux.disks {
		str Kind = "spinning"
		if D.ssd {
			Kind = "SSD"
		}
		println D.path, D.model, D.size / 1000000000, "GB", Kind
		for P in linux.partitions_of D.name {
			println "  ", P.path, P.size / 1000000000, "GB", P.label, P.mounted_at
		}
	}
	return 0
}
```

```text
/dev/nvme0n1 WD Blue SN570 1TB 512 GB SSD
   /dev/nvme0n1p1 512 GB  /home
/dev/sdb SanDisk Ultra 30 GB spinning
   /dev/sdb1 30 GB MY STICK /media/tom/MY STICK
```

| `linux.block_device` | |
|---|---|
| `str name`, `str path` | `"sda1"`, and `"/dev/sda1"` |
| `str kind` | `"disk"` or `"partition"` |
| `str model` | a disk's maker and model |
| `str disk` | a partition's disk (`""` for a disk) |
| `str mounted_at`, `str label` | where it's mounted, and its name (`""` if not) |
| `long size` | in bytes |
| `bool removable`, `bool ssd`, `bool read_only` | |

`linux.block_devices` lists each disk followed by its partitions;
`linux.disks` just the disks, and `linux.partitions_of Disk` a disk's.

## USB devices

<!-- check -->
```cabby
use linux

int main {
	for U in linux.usb_devices {
		println U.vendor_id + ":" + U.product_id, U.maker, U.name, U.speed, "Mbit/s"
	}
	return 0
}
```

```text
046d:c52b Logitech USB Receiver 12 Mbit/s
1235:8210 Focusrite Scarlett 2i2 USB 480 Mbit/s
```

| `linux.usb_device` | |
|---|---|
| `str name`, `str maker` | what it says it is, and who made it |
| `str vendor_id`, `str product_id` | its numbers, as `lsusb` shows them |
| `str serial` | its serial number (`""` if it hasn't one) |
| `long bus`, `long number` | where it is (as `lsusb` shows them) |
| `long speed` | megabits a second: 12 (USB 1), 480 (USB 2), 5000 (USB 3) |

## The kernel's modules

| | |
|---|---|
| `list linux.module linux.modules` | the modules loaded, by name: each's `name`, `size`, `used` (how many use it) and `used_by` (which modules) |
| `bool linux.module_loaded Name` | whether one's loaded (`-` and `_` alike, as for `modprobe`) |
| `bool linux.load_module Name`, `bool linux.unload_module Name` | with `modprobe` (it needs the administrator) |

## The kernel's settings

The settings `sysctl` shows, named as it names them:

<!-- check -->
```cabby
use linux

int main {
	println "forwarding:", linux.setting("net.ipv4.ip_forward")
	println "swappiness:", linux.setting("vm.swappiness")
	return 0
}
```

| | |
|---|---|
| `str linux.setting Name` | its value: `"0"`, `"60"`, `"32768 60999"` (a panic if there's none: `linux.has_setting` checks) |
| `bool linux.has_setting Name` | |
| `bool linux.set_setting Name, Value` | changed until the computer starts again (it needs the administrator); for good, a file in `/etc/sysctl.d` |

## Services

The programs systemd keeps running: ssh, bluetooth, cups, a web server.

<!-- check -->
```cabby
use linux

int main {
	linux.service Ssh = linux.service_named "ssh"
	println Ssh.description + ":", Ssh.active, Ssh.enabled
	if !Ssh.running && !Ssh.start {
		println "it needs the administrator: sudo"
	}
	for S in linux.services {
		if S.active == "failed" {
			println "failed:", S.name
		}
	}
	return 0
}
```

| `linux.service` | |
|---|---|
| `str name`, `str description` | `"ssh.service"`, `"OpenBSD Secure Shell server"` |
| `str active` | `"active"`, `"inactive"`, `"failed"`, `"activating"`, `"deactivating"` |
| `bool running` | whether it's running now |
| `str enabled` | whether it starts with the computer: `"enabled"`, `"disabled"`, `"static"` (something else needs it), `"masked"` (it can't be started) |
| `bool start`, `stop`, `restart`, `enable`, `disable` | true if it was done (the system's need the administrator) |
| `refresh` | asks systemd again how it is |
| `bool users_own` | one of this person's own, not the system's |

`linux.services` lists the system's, running or not; `linux.user_services`
this person's own (`systemctl --user`: PipeWire, say).
`linux.service_named Name` is one by name (`"ssh"` or `"ssh.service"`), a
panic if systemd doesn't know it (`linux.has_service` checks), and
`linux.user_service_named` one of this person's own. They're asked of
`systemctl`, so a computer without systemd (Alpine, Void) has none.

## How it works

`runtime/linux.c` reads `/sys/class/hwmon`, `/sys/class/thermal`,
`/sys/block`, `/sys/bus/usb/devices`, `/proc/modules`, `/proc/sys` and
`/proc/self/mounts`; services are `systemctl`'s, in `lib/linux.hby`. The
tests use folders of their own instead (`CABBY_SYSFS`, `CABBY_PROCFS`,
`CABBY_DEVFS`), so they're the same on every computer.
