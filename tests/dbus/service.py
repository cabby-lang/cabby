# A small D-Bus service for run_dbus_tests.sh (needs dbus-python and
# PyGObject): says exactly what it was sent, answers with values of many
# types, and sends signals when asked.
import dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

def show(v):
    if isinstance(v, dbus.Dictionary):
        return "{" + ", ".join(f"{show(k)}: {show(x)}" for k, x in v.items()) + "}" + f"<{v.signature}>"
    if isinstance(v, (dbus.Array,)):
        return "[" + ", ".join(show(x) for x in v) + "]" + f"<{v.signature}>"
    if isinstance(v, dbus.Struct):
        return "(" + ", ".join(show(x) for x in v) + ")"
    return f"{type(v).__name__}({v!r})" + (f"@{v.variant_level}" if getattr(v, 'variant_level', 0) else "")

class T(dbus.service.Object):
    @dbus.service.method('org.cabby.Test', in_signature=None, out_signature='s')
    def Describe(self, *args):
        return " | ".join(show(a) for a in args)

    @dbus.service.method('org.cabby.Test', in_signature='', out_signature='ia{sv}a(si)ayxtdbo')
    def Give(self):
        return (-7, {"name": dbus.String("Ana", variant_level=1), "age": dbus.Int32(30, variant_level=1),
                     "tags": dbus.Array(["a", "b"], signature="s", variant_level=1),
                     "nested": dbus.Dictionary({"x": dbus.Double(1.5)}, signature="sv", variant_level=1)},
                [("one", 1), ("two", 2)], dbus.ByteArray(b"hi"), -9000000000, 18000000000, 2.25, True, "/org/x")

    @dbus.service.method('org.cabby.Test', in_signature='s', out_signature='')
    def Ring(self, what):
        self.Rang(what, dbus.Dictionary({"n": dbus.UInt32(3)}, signature="sv"))
        self.Other(5)

    @dbus.service.signal('org.cabby.Test', signature='sa{sv}')
    def Rang(self, what, extra):
        pass

    @dbus.service.signal('org.cabby.Other', signature='i')
    def Other(self, n):
        pass

    # open files passed over the bus (h): one given, one read
    @dbus.service.method('org.cabby.Test', in_signature='', out_signature='h')
    def GiveFile(self):
        import os, tempfile
        f = tempfile.TemporaryFile()
        f.write(b"from python")
        f.flush()
        f.seek(0)
        fd = os.dup(f.fileno())
        f.close()
        return dbus.types.UnixFd(fd)

    @dbus.service.method('org.cabby.Test', in_signature='h', out_signature='s')
    def ReadFile(self, fd):
        import os
        n = fd.take()
        os.lseek(n, 0, 0)
        text = os.read(n, 100).decode()
        os.close(n)
        return text

    @dbus.service.method('org.cabby.Test', in_signature='', out_signature='')
    def Quit(self):
        loop.quit()

# media players, as MPRIS has them (for media.cby): one paused, with a
# song, and one stopped, on a connection of its own, with no Identity
PROPS = 'org.freedesktop.DBus.Properties'
PLAYER = 'org.mpris.MediaPlayer2.Player'
SONGS = [("First Song", ["Ana", "Bruno"], "The Album"), ("Second Song", ["Cleo"], "The Album")]

class Player(dbus.service.Object):
    def __init__(self, conn, status, identity):
        super().__init__(conn, "/org/mpris/MediaPlayer2")
        self.status, self.identity, self.song = status, identity, 0

    def props(self, iface):
        if iface == PLAYER:
            title, artists, album = SONGS[self.song]
            return {"PlaybackStatus": dbus.String(self.status),
                    "Metadata": dbus.Dictionary({"xesam:title": dbus.String(title, variant_level=1),
                                                 "xesam:artist": dbus.Array(artists, signature="s", variant_level=1),
                                                 "xesam:album": dbus.String(album, variant_level=1),
                                                 "mpris:length": dbus.Int64(180000000, variant_level=1)}, signature="sv")}
        if iface == 'org.mpris.MediaPlayer2' and self.identity:
            return {"Identity": dbus.String(self.identity)}
        return {}

    @dbus.service.method(PROPS, in_signature='ss', out_signature='v')
    def Get(self, iface, name):
        p = self.props(iface)
        if name not in p:
            raise dbus.exceptions.DBusException("no " + name, name="org.freedesktop.DBus.Error.UnknownProperty")
        return p[name]

    @dbus.service.method(PROPS, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        return self.props(iface)

    @dbus.service.method(PLAYER, in_signature='', out_signature='')
    def PlayPause(self):
        self.status = "Paused" if self.status == "Playing" else "Playing"

    @dbus.service.method(PLAYER, in_signature='', out_signature='')
    def Next(self):
        self.song = (self.song + 1) % len(SONGS)

    @dbus.service.method(PLAYER, in_signature='', out_signature='')
    def Previous(self):
        self.song = (self.song - 1) % len(SONGS)

    @dbus.service.method(PLAYER, in_signature='', out_signature='')
    def Play(self):
        self.status = "Playing"

    @dbus.service.method(PLAYER, in_signature='', out_signature='')
    def Pause(self):
        self.status = "Paused"

bus = dbus.SessionBus()
name = dbus.service.BusName("org.cabby.Test", bus)
T(bus, "/org/cabby/Test")
player_name = dbus.service.BusName("org.mpris.MediaPlayer2.cabbytest", bus)
Player(bus, "Paused", "Cabby Test Player")
other = dbus.bus.BusConnection(dbus.bus.BUS_SESSION)
other_name = dbus.service.BusName("org.mpris.MediaPlayer2.aaa.instance_2", other)
Player(other, "Stopped", None)
loop = GLib.MainLoop()
print("ready", flush=True)
loop.run()
