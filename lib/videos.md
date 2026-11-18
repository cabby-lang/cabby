# videos — video files, and playing them

```cabby
use videos
```

Video files opened, and asked what's in them: their size, length and
frame rate, and how their picture and sound are kept. A picture from any
moment in one (a thumbnail, or a frame to look at), its sound saved on
its own, and, with `gui`, playing it in a window.

It's built on FFmpeg's libraries, as most video players are, so it reads
nearly anything: MP4 and MOV (from phones and cameras), MKV, WebM, AVI,
and sound files too. The same on Linux and on a Mac.

* [Playing one in a window](#playing-one-in-a-window)
* [What's in it](#whats-in-it)
* [Playing](#playing)
* [Pictures and sound from it](#pictures-and-sound-from-it)
* [How it plays](#how-it-plays)
* [Installing FFmpeg](#installing-ffmpeg)
* [Not yet](#not-yet)

## Playing one in a window

<!-- check -->
```cabby
use gui
use videos

object player : gui.window {
	videos.video Film
	gui.video Screen

	void build {
		set_title "Lesson"
		Film = videos.open "lesson.mp4"
		Screen = add_video Film.source
		Screen.show_controls
		Screen.on_end finished
		Film.play
	}

	void finished {
		println "watched it all"
	}
}

int main {
	player P
	P.run
	return 0
}
```

The video is what plays, pauses and seeks; the window's
[`gui.video`](gui.md#guivideo) shows it and plays its sound. A click on
the picture plays or pauses it, and its controls (`show_controls`) are a
play button, a track to go to any place along, and the time. Tutorial 67
is a whole player.

## What's in it

`videos.open Path` opens one: a panic if it isn't there, or FFmpeg can't
read it (`videos.can_open Path` checks first).

| `videos.video` | |
|---|---|
| `int width`, `int height` | its picture's size, in pixels (0 for a sound file) |
| `double length` | how long it is, in seconds |
| `double frame_rate` | pictures a second: 25, 29.97, 30, 60 |
| `bool has_picture`, `bool has_sound` | whether it has each |
| `str picture_codec` | how its picture is kept: `"h264"`, `"hevc"`, `"vp9"`, `"av1"` |
| `str sound_codec` | and its sound: `"aac"`, `"opus"`, `"mp3"` (`""` for none) |
| `str title` | its title, if the file has one |
| `str kind` | the kind of file, as FFmpeg names it: `"mov,mp4,m4a,3gp,3g2,mj2"` |
| `str path` | the file it was opened from |

<!-- check -->
```cabby
use videos

int main {
	videos.video Film = videos.open "lesson.mp4"
	println Film.width, "by", Film.height, "for", Film.length, "seconds"
	println Film.picture_codec, "with", Film.sound_codec, "sound"
	return 0
}
```

`videos.ffmpeg_version` gives the version of FFmpeg's libraries it's
using.

## Playing

| `videos.video` | |
|---|---|
| `play`, `pause`, `play_pause` | |
| `bool playing` | |
| `double position` | where it's got to, in seconds |
| `seek Seconds` | to there (playing on from there, if it was playing) |
| `rewind` | back to the start |
| `bool ended` | true once it's played to its end, and stopped there |
| `set_looping Yes`, `bool looping` | played again from the start each time it ends |
| `set_volume Level`, `int volume` | how loud, from 0 to 100 (100, as it is, to begin with) |
| `long source` | what a window shows: for gui's `add_video` |

Played after it's ended, it starts again from the start.

## Pictures and sound from it

| `videos.video` | |
|---|---|
| `images.picture picture_at Seconds` | the picture at that time, as an [images](images.md) picture: to save, or look at |
| `bool save_sound Path` | all of its sound, saved as a WAV file (48,000 samples a second, two channels): `false` if it has none |

Thumbnails, a picture from every tenth of the way through:

<!-- check -->
```cabby
use videos

int main {
	videos.video Film = videos.open "lesson.mp4"
	for i in 10 {
		images.picture Thumb = Film.picture_at Film.length * i / 10
		Thumb.save "thumb-" + i.text + ".png"
	}
	return 0
}
```

`picture_at` moves the video to that time, as `seek` does.

## How it plays

A video plays by a clock: its position is where it was started from,
plus the time since. Its picture is decoded as it's needed, as far ahead
as the next picture, and its sound half a second ahead. The window shows
the picture for now each time it's drawn, so a slow computer shows fewer
pictures rather than falling behind, and the sound is kept to the clock.
The picture is turned into RGBA (and scaled to fit the window by the
graphics card); the sound into 48,000 samples a second, in two channels,
which the computer's sound plays.

Under a gui test script, the clock is the script's, and the sound isn't
played: `wait 1500` is a second and a half of the video, and the pictures
shown at each moment are the same every time.

If a video's sound can't be heard, run the program with the environment
variable `CABBY_SOUND_REPORT` set (`CABBY_SOUND_REPORT=1 cabby film.cby`).
On stderr it says which sound system and device it's playing to, and
every two seconds how much sound it has given and how loud it was. If
the sound was given but not heard, look at the computer's own sound
settings. If the device is missing, or nothing loud was given, it's
Cabby's to fix.

## Installing FFmpeg

The library is built when Cabby is, if FFmpeg's libraries are there:

| | |
|---|---|
| Debian | `sudo apt install libavformat-dev libavcodec-dev libswscale-dev libswresample-dev` |
| Arch | `sudo pacman -S ffmpeg` |
| a Mac | `brew install ffmpeg` |

Then build and install Cabby again. Without them, `use videos` says
what's missing.

## Not yet

* Videos in a window of the gtk library (`use gui` shows them)
* Playing faster or slower, and subtitles
* Turning a phone's video the right way up (one recorded with the phone
  on its side shows as it was recorded)
* Making videos (from pictures and sound), and cutting them
* Streams from the web (a file downloaded first plays)
