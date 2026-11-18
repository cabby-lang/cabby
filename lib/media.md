# media — what's playing, and the play and pause buttons

```cabby
use media
```

What's playing in a media player (Spotify, a browser, VLC, Music), and
its buttons: play, pause, next and previous. The same on Linux and on a
Mac: for a bar, a remote control, or a program that pauses the music
when it needs quiet.

| | Linux | macOS |
|---|---|---|
| players | any that speaks MPRIS over D-Bus: Spotify, Firefox, Chromium, VLC, mpv (with mpv-mpris), Rhythmbox, Elisa... | Spotify and the Music app |
| through | the `dbus` library | AppleScript (`osascript`) |

<!-- check -->
```cabby
use media

int main {
	if media.player == "" {
		println "nothing's open to play anything"
	} else if media.playing {
		println "playing:", media.title, "by", media.artist, "in", media.player
	} else {
		println media.player, "is paused; playing it again"
		media.play
	}
	return 0
}
```

| Call | What it does |
|---|---|
| `media.player` | the player's name: `"Spotify"`, `"Firefox"`, `"VLC media player"` (`""`: none open) |
| `media.playing` | `true` while something's playing (not paused or stopped) |
| `media.title` | the song's (or video's) title |
| `media.artist` | who it's by; more than one: `"Ana, Bruno"` |
| `media.album` | the album it's from |
| `media.play_pause` | plays if it's paused, pauses if it's playing |
| `media.play` | plays |
| `media.pause` | pauses |
| `media.next` | the next song |
| `media.previous` | the one before (most players go back to the start of this one first, if it's a few seconds in) |

The buttons give `true` if there was a player to press them on. What's
playing gives `""` when nothing is, or the player doesn't say (a web page's
video may have no album).

With more than one player open, it's the one playing; with none playing,
one that's paused (the buttons are for that one: pausing and playing
again works); and with neither, the first.

On a Mac, a player that isn't open is never asked anything (asking would
open it). The first time, macOS asks whether the program (or Terminal,
for one run there) may control Spotify or Music: allow it, or change it
later in System Settings, Privacy & Security, Automation.
