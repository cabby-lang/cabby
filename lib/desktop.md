# desktop — notifications, and opening things

```cabby
use desktop
```

What a desktop does for a program, the same way on Linux and on a Mac:
showing a notification, and opening a file, a folder or a web address in
whatever the computer opens it with. The program is the same on both;
`desktop` does each one's own way:

| | Linux | macOS |
|---|---|---|
| notifications | the desktop's notification server (mako, dunst, GNOME's, a shell's), over D-Bus | Notification Center (through `osascript`) |
| opening | `xdg-open` | `open` |

<!-- check -->
```cabby
use desktop

int main {
	if !desktop.notify("Backup", "All 2,341 files copied.") {
		println "(nothing showed it: no notification server is running)"
	}
	desktop.open "https://en.wiktionary.org/wiki/salve"
	desktop.open "notes.txt"            // in the text editor
	return 0
}
```

| Call | What it does |
|---|---|
| `desktop.notify Title, Body` | Shows a notification: `true` if it was shown, `false` if nothing showed it (no notification server running, or no bus). |
| `desktop.notify_numbered Title, Body` | The same, giving back its number (0: not shown), to change it with `notify_again`. |
| `desktop.notify_again Number, Title, Body` | The notification `Number`, changed where it is (a progress "37%"); its number. On a Mac each is a new one: Notification Center can't change one. |
| `desktop.open What` | Opens a file, a folder or a web address (`https://...`, `mailto:...`): `true` if it was. |
| `desktop.on_a_mac` | `true` on a Mac: for the rare thing that has to differ. |

On a Mac the notifications are shown as from Script Editor (it's
`osascript` that asks for them); the first one may ask whether Script
Editor may show notifications.

For more than this, on Linux, the [dbus](dbus.md) library talks to the
notification server directly: actions (buttons), icons, urgency, and
hearing when one's closed.
