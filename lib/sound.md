# sound — sound files, notes, MIDI files, recording, and MIDI keyboards

```cabby
use sound
```

Plays sound files, music, notes and MIDI files, all at once, mixed
together; writes and reads MIDI files; and records from a microphone or an
audio interface. Playing never waits: the program goes straight on while the
sound plays. It needs nothing installed. miniaudio, built into Cabby,
plays through PipeWire, PulseAudio, ALSA or (on a Mac) Core Audio.

Tutorial 72 and chapter 72 of the manual teach it. This page is the
complete reference.

* [Sound files](#sound-files)
* [sound.track](#soundtrack)
* [Notes](#notes)
* [Instruments](#instruments)
* [MIDI files](#midi-files)
* [Songs: writing MIDI files](#songs-writing-midi-files)
* [Recording](#recording)
* [All of it](#all-of-it)
* [In a program that ends](#in-a-program-that-ends)
* [Testing, and without a sound device](#testing-and-without-a-sound-device)
* [Not in sound yet](#not-in-sound-yet)

<!-- run -->
```cabby
use sound

int main {
	sound.play_notes "C4 E4 G4 C5", 300     // a tune: four notes, 300 ms each
	sound.wait                              // until it's finished
	println "done"
}
```

```text
done
```

## Sound files

WAV, MP3 and FLAC files.

| Function | What it does |
|---|---|
| `void sound.play const str &Path` | Plays it once. As often as it's called, even over itself, for sound effects. It's read into memory the first time, so later plays start at once. |
| `void sound.play_music const str &Path` | Plays it round and round, until `stop_music`; another `play_music` changes the music. A sound file, or a MIDI file (`.mid`), played with the SoundFont's instruments. |
| `void sound.stop_music` | Stops the music. |
| `void sound.fade_out_music int Ms` | The music gets quieter over that many milliseconds, and stops: going from one place to another. |
| `void sound.change_music const str &Path, int Ms` | Other music: the music playing fades out over that many milliseconds, then the new music starts, as when a game's hero walks from one place to the next. If that music is playing already, it goes on; with none playing, the new music starts at once. |
| `void sound.crossfade_music const str &Path, int Ms` | Other music, the two together for a moment: the new music gets louder over that many milliseconds while the old gets quieter. |
| `void sound.set_effects_volume double Volume`, `double sound.effects_volume` | How loud the sounds `sound.play` plays are, from 0 to 1 (1 to begin with); music and notes stay as they are. With `set_music_volume`, a game's two volume sliders. |
| `void sound.set_music_volume double Volume`, `double sound.music_volume` | How loud the music is, from 0 to 1 (1 to begin with); the effects and notes stay as they are. In a MIDI file, each note takes the volume as it starts. |
| `sound.track sound.load const str &Path` | A sound file to control, read as it plays (so long files are fine). |
| `void sound.convert_to_wav From, To, Rate, Channels` | A sound file (WAV, MP3, FLAC, Ogg Vorbis) saved again as a WAV file of 16-bit samples, so many a second in so many channels: `44100, 2` as a CD has it; `16000, 1` as speech recognition hears it. |

A file that isn't there, or isn't a sound file, is a panic at the line
that played it.

## sound.track

| Member | What it does |
|---|---|
| `void play const` | Plays it, from where it's up to. |
| `void pause const` | Stops where it is. |
| `void stop const` | Stops, and goes back to the start. |
| `void set_looping bool On const` | Round and round, or once. |
| `void set_volume double Volume const` | 1 as it is, 0.5 half as loud, 0 silent. |
| `bool playing const` | Whether it's playing. |
| `double seconds const` | How long it is. |
| `double position const` | Where it's up to, in seconds. |
| `void move_to double Seconds const` | Goes to there. |

When a track is dropped (at the end of the block it was made in), it
stops.

## Notes

Notes are named as on a stave: a letter, then `#` for a sharp or `b` for a
flat, then the octave. `"C4"` is middle C, `"A4"` the A above it (440 Hz),
`"F#3"`, `"Bb5"`. A MIDI note number (`"60"`) works too.

| Function | What it does |
|---|---|
| `void sound.play_note const str &Note, int Ms` | Plays a note for that many milliseconds. |
| `void sound.play_chord const list str &Notes, int Ms` | Plays the notes together: `sound.play_chord {"C4", "E4", "G4"}, 1000`. |
| `void sound.play_notes const str &Tune, int Ms` | Plays the notes one after another, each that long; `-` is a rest: `sound.play_notes "E4 D4 C4 - E4 D4 C4", 400`. A tune played straight after another follows on after it. |
| `void sound.start_note const str &Note` | Starts a note that goes on until `stop_note`: for a key held down. |
| `void sound.stop_note const str &Note` | Ends it. |
| `void sound.set_instrument const str &Instrument` | What the notes are played with, from now on (see below). |
| `void sound.set_note_volume double Volume` | How loud notes are, from 0 to 1 (0.8 to begin with). |

## Instruments

Notes and MIDI files are played with the instruments of a **SoundFont**
(an `.sf2` file). The one installed on the computer is found by itself:
FluidR3 (`soundfont-fluid` on Arch, `fluid-soundfont-gm` on Debian), or
any in `/usr/share/soundfonts` or `/usr/share/sounds/sf2` named
`default.sf2`, `default-GM.sf2`, `FluidR3_GM.sf2` or `TimGM6mb.sf2`.
`sound.set_soundfont "my.sf2"` uses another, and `sound.soundfont` says
which is used. Without one, a simple synthesizer of Cabby's own plays the
notes, every instrument the same way.

`set_instrument` takes these names:

| | |
|---|---|
| Keys | `"piano"`, `"bright piano"`, `"electric piano"`, `"harpsichord"`, `"organ"`, `"accordion"` |
| Tuned percussion | `"glockenspiel"`, `"music box"`, `"vibraphone"`, `"marimba"`, `"xylophone"`, `"timpani"`, `"steel drums"` |
| Strings | `"violin"`, `"viola"`, `"cello"`, `"double bass"`, `"harp"`, `"strings"` |
| Guitars | `"guitar"` (nylon), `"steel guitar"`, `"jazz guitar"`, `"electric guitar"`, `"overdriven guitar"`, `"distortion guitar"`, `"bass"`, `"electric bass"`, `"banjo"`, `"sitar"` |
| Wind | `"flute"`, `"piccolo"`, `"recorder"`, `"pan flute"`, `"oboe"`, `"clarinet"`, `"bassoon"`, `"saxophone"`, `"harmonica"` |
| Brass | `"trumpet"`, `"trombone"`, `"tuba"`, `"french horn"`, `"brass"` |
| Voices | `"choir"` |
| Drums | `"drums"`: each note is a drum (`"C2"` bass drum, `"D2"` snare, `"F#2"` closed hi-hat, `"C#3"` crash cymbal ...) |

or a General MIDI instrument's number, from `"1"` to `"128"`.

## MIDI files

| Function | What it does |
|---|---|
| `void sound.play_midi const str &Path` | Plays a MIDI file (`.mid`), with its own instruments. |
| `void sound.stop_midi` | Stops it. |
| `void sound.set_soundfont const str &Path` | The SoundFont to play notes and MIDI files with. |
| `str sound.soundfont` | The SoundFont being used (`""`: none, Cabby's own synthesizer). |

## Songs: writing MIDI files

A song is parts played together, at a tempo; a part is one instrument's
notes, with their times and lengths in beats. Tutorial 74 and chapter 74
of the manual teach it.

```cabby
sound.part Tune
Tune.set_instrument "flute"
Tune.notes "E4 D4 C4:2 - E4 D4 C4:2", 1     // a beat each, C4:2 two; - is a rest
sound.song Song
Song.set_tempo 100
Song.add Tune
Song.save "tune.mid"
```

### sound.part

| Member | What it does |
|---|---|
| `void set_instrument const str &Name`, `str instrument` | What it's played with: an instrument's name, as for `sound.set_instrument` (`"piano"` to begin with). With `"drums"`, each note is a drum. |
| `void set_volume double Volume`, `double volume` | How loud the notes added after it are, from 0 to 1 (0.8 to begin with). |
| `void set_held double Part` | How much of its time each note added after it is held: 1 to the next note (to begin with), 0.9 a little apart, 0.5 short. |
| `void note const str &Note, double Beats` | A note, after the last one. A note is a name (`"C4"`, `"F#3"`) or a MIDI number as text (`"60"`). |
| `void notes const str &Tune, double Beats` | Notes one after another, each `Beats` long, unless a note says otherwise after a colon (`"C4:2"`); `-` is a rest (`"-:0.5"`). Spaces, tabs and line ends between them are all the same. |
| `void chord const list str &Notes, double Beats` | Notes together, after the last one. |
| `void rest double Beats` | Silence. |
| `void note_at const str &Note, double Beat, double Beats` | A note at that beat (0 is the start), whatever has gone before; the next `note` still goes after the last. |
| `double beat` | The beat the next note goes on. |
| `void move_to double Beat` | Where the next note goes from now on. |
| `double beats` | How long the part is, in beats: to where the next note would go, or the end of its last note, whichever is later. |
| `void transpose int Steps` | Every note up (or, below 0, down) by that many half steps. Drums stay as they are. |
| `list sound.note Notes` | Its notes, in the order they were added. |

### sound.note

A note's `long Key` (its MIDI number: 60 is middle C), `double Beat` (when
it starts), `double Beats` (how long it's held) and `double Volume` (0 to
1); `str name` is its name, `"C4"` or `"F#3"`.

### sound.song

| Member | What it does |
|---|---|
| `void set_tempo double Beats`, `double tempo` | Beats a minute (120 to begin with). |
| `void set_time_signature int Beats, int Note`, `str time_signature` | `3, 4` is three crotchets a bar (`"3/4"`; 4/4 to begin with). It's saved in the file, for music programs that show bars. |
| `void add sound.part Part` | Moves a part into the song. Each part plays on a MIDI channel of its own; a song can have 15, and drums. |
| `list sound.part Parts` | Its parts, in the order they were added. |
| `double beats`, `double seconds` | How long it is: its longest part. |
| `void transpose int Steps` | Every part (but the drums) up or down. |
| `void save const str &Path` | Saves it as a MIDI file (`.mid`): a track for each part, named by its instrument. |
| `void play` | Plays it, as `play_midi` plays a file; `stop_midi` stops it. |

| Function | What it does |
|---|---|
| `void sound.play_song const sound.song &Song` | The same as `Song.play`. |
| `sound.song sound.read_song const str &Path` | Reads a MIDI file into a song: its tempo and time signature (the first ones), and a part for each MIDI channel with notes in it, by its instrument (`"drums"` for channel 10, a General MIDI number where sound has no name for it). |

A file that isn't there, or isn't a MIDI file, is a panic at the line that
read it.

## Recording

From a microphone or an audio interface, into memory, to save as a WAV
file or play. Tutorial 75 and chapter 75 of the manual teach it.

```cabby
sound.recording Take = sound.record     // starts at once
...
Take.stop
Take.save "take-1.wav"
```

| Function | What it does |
|---|---|
| `sound.recording sound.record` | Starts recording from the computer's usual input. One input records at a time: starting another recording stops the one before. |
| `sound.recording sound.record_from const str &Input` | From the input with that name, or part of it (`"Scarlett"`), big or small letters alike. One that isn't there is a panic. |
| `list str sound.inputs` | The inputs there are, by name. |
| `void sound.set_recording_channels int Channels` | 1 (mono, to begin with) or 2 (stereo), for the recordings started after it. |
| `void sound.set_recording_bits int Bits` | Recordings are saved with 24 bits (to begin with: what an audio interface records), or 16 (as a CD is, and half as big). |
| `void sound.set_recording_delay int Ms` | How much earlier a recording is moved, to be in time with what was playing (see below). |

### sound.recording

| Member | What it does |
|---|---|
| `void stop const` | Stops recording. It can't go on afterwards: record again for more. |
| `void pause const`, `void resume const` | Stops for a while, and goes on. |
| `bool recording const` | Whether it's recording now: not stopped, or paused. |
| `double seconds const` | How long it is. |
| `double level const` | How loud it's coming in now, from 0 to 1 (the loudest moment of the last tenth of a second): for a meter. 1 is as loud as it can be; near it, the input is too loud. |
| `void play const` | Plays it, as `sound.play` plays a file. |
| `void trim_silence const` | Cuts off the quiet before the first sound and after the last, keeping a moment of each. Quiet is judged by the hundredth of a second, on average, against the loudest part of the take and the input's own hiss (its quietest tenth), so a click doesn't count as sound, and a noisy input's hiss doesn't either. |
| `void save const str &Path const` | Saves it as a WAV file, 48000 frames a second. |

A recording is kept until it's dropped (at the end of the block it was
made in); it stops recording then too.

Each input is recorded as it comes, at its own rate and channels (a
webcam's microphone may be 16000 frames a second, in stereo), and changed
into the recording's 48000 frames a second, and its channels, while the
program goes on: never while the input is waiting to be heard, so it
can't fall behind and click.

**In time with what's playing.** A recording started while sound plays
(a click, a backing track, a song) is moved earlier by the time the sound
took to reach the speakers, and the time the input took to come in, so
it lines up with what was heard: start the backing and the recording
together, one line after the other, and in a music program they line up
from the start. The delay is worked out from the devices; if a recording
is still early or late, `sound.set_recording_delay` sets it in
milliseconds (`-1` works it out again). Listen in headphones, or the
microphone hears the backing too.

**Monitoring**: hearing the input as it's recorded, mixed with what's
playing. An audio interface usually does it itself, with no delay; a
computer's own microphone socket doesn't, and sound can:

| Function | What it does |
|---|---|
| `void sound.set_monitoring bool On`, `bool sound.monitoring` | Whether what's coming in is heard, through the speakers or headphones (`false` to begin with). It can be turned on and off as a recording goes. |
| `void sound.set_monitor_volume double Volume`, `double sound.monitor_volume` | How loud it's heard, from 0 to 1 (1 to begin with). |

It's heard a few milliseconds behind, and the system's own buffers add a
little: turn monitoring on before `sound.record`, so the input is read in
smaller pieces, sooner. (Recording without monitoring reads bigger ones,
which a slow USB microphone needs to never click.) With a microphone, use
headphones: through speakers, it hears itself and howls.

**On a Mac**, the first recording asks whether the program (or the
terminal it runs in) may use the microphone.

## MIDI keyboards and synthesizers

A MIDI keyboard (or drum pads, or a controller of knobs and sliders)
sends what's played: which key, how hard, the pedal. A MIDI synthesizer
(a hardware one, a keyboard's own sounds, a program such as FluidSynth)
plays what it's sent. Tutorial 76 and chapter 76 of the manual teach
them.

```cabby
sound.keyboard Keys = sound.open_keyboard
Keys.play_through "piano"             // heard at once, as it's played
Keys.on_note played                   // and told to the program

void played {
	println Keys.note, Keys.pressed, Keys.velocity     // C4 true 87
}
```

| Function | What it does |
|---|---|
| `list str sound.midi_inputs` | The keyboards (and other MIDI that sends) connected, by name. |
| `list str sound.midi_outputs` | The synthesizers (and other MIDI that plays) connected, by name. |
| `sound.keyboard sound.open_keyboard` | The first keyboard connected. None is a panic that says so. |
| `sound.keyboard sound.open_keyboard_named const str &Name` | The keyboard with that name, or part of it (`"Keystation"`), big or small letters alike. |
| `sound.synthesizer sound.open_synthesizer` | The first synthesizer connected. |
| `sound.synthesizer sound.open_synthesizer_named const str &Name` | The synthesizer with that name, or part of it (`"FluidSynth"`). |
| `str sound.note_name long Key` | A MIDI note number as a note's name: `60` is `"C4"`, `61` `"C#4"`. |

### sound.keyboard

| Member | What it does |
|---|---|
| `void play_through const str &Instrument` | Plays what's played through one of sound's instruments (`"piano"`, `"strings"`, a General MIDI number), heard at once: the notes go straight into the mix from a thread of sound's own, whatever the program is doing. The sustain pedal and the pitch-bend wheel work too. |
| `void stop_playing_through` | Stops it. |
| `void send_to const sound.synthesizer &Synthesizer`, `void stop_sending` | Sends everything played straight on to a synthesizer, from the keyboard's thread, as it's played. |
| `void on_note handler h` | Calls `h` for each key pressed or let go. |
| `void on_control handler h` | Calls `h` when a pedal, wheel, slider or knob moves. |
| `void on_bend handler h` | Calls `h` when the pitch-bend wheel moves. |
| `bool pressed const` | In `on_note`'s handler: `true` for a key pressed, `false` for one let go. |
| `int key const`, `str note const` | Which key: its MIDI number (60 is middle C), or its name (`"C4"`). |
| `int velocity const` | How hard it was pressed, from 1 to 127 (0 for a key let go). |
| `int control const`, `int value const` | In `on_control`'s handler: which control (1 the modulation wheel, 7 volume, 11 expression, 64 the sustain pedal, 66 the sostenuto pedal, 67 the soft pedal), and where it is now, from 0 to 127 (a pedal is down at 64 and over). |
| `int bend const` | In `on_bend`'s handler: where the wheel is, from -8192 to 8191 (0 in the middle). |
| `int channel const` | The MIDI channel it came on, from 1 to 16 (most keyboards send on 1). |
| `str name const` | Its name, as `sound.midi_inputs` has it. |
| `void close` | Stops listening, as dropping it does. Notes playing through let go. |

The handlers are called from the event loop (`events.run`, a window's
`run`), between its other events, as a process's are; `events.run` waits
while a keyboard has handlers, until it's closed. What's played isn't
lost while the program is busy: it waits, in order, for the handlers.
Messages other than these (key pressure, program changes, the clock) are
left out.

### sound.synthesizer

| Member | What it does |
|---|---|
| `void play_note const str &Note, int Ms` | Plays a note for that many milliseconds; the program goes straight on. |
| `void start_note const str &Note`, `void stop_note const str &Note` | A note held until it's stopped. |
| `void start_key int Key`, `void stop_key int Key` | The same, by MIDI note number (`Synth.start_key Keys.key + 12`: a keyboard's note, an octave up). |
| `void set_instrument const str &Instrument` | Its sound, by the names `sound.set_instrument` knows, sent as a program change. `"drums"` plays on channel 10, as MIDI's drums are. |
| `void set_channel int Number` | The channel the notes after it go on, from 1 to 16 (1 to begin with). |
| `void set_velocity int Amount` | How hard the notes after it are played, from 1 to 127 (100 to begin with). |
| `void set_control int Number, int Value` | A control: 7 volume, 10 pan, 64 the sustain pedal (127 down, 0 up)... |
| `void bend int Amount` | The pitch bend, from -8192 to 8191 (0: none). |
| `void stop` | Every note let go, on every channel, and the pedal up. |
| `str name const` | Its name, as `sound.midi_outputs` has it. |
| `void close` | Notes still to be let go are let go, and it's closed, as dropping it does. |

**Where they're found.** On Linux, through ALSA's MIDI, the "sequencer"
(libasound, which every desktop has, loaded the first time it's needed;
PipeWire's MIDI goes through it too). ALSA's own ports (Midi Through)
aren't listed. A keyboard's name is its port's, as `aconnect -l` shows
it. On a Mac, through Core MIDI: a keyboard's name is as Audio MIDI
Setup shows it, and the IAC Driver's buses, once turned on there, are
synthesizers and keyboards too, for talking to GarageBand or Logic.

## All of it

| Function | What it does |
|---|---|
| `void sound.set_volume double Volume` | How loud everything is: 1 as it is, 0.5 half, 0 silent. |
| `void sound.stop` | Stops everything. |
| `bool sound.busy` | Whether anything is playing that will end (music that loops doesn't count). |
| `void sound.wait` | Waits until everything playing has finished (looping music goes on). |
| `double sound.time` | How long sound has been playing, in seconds, since the first sound. |

## In a program that ends

Sound stops when the program ends. A program in a window (gui or game)
goes on until the window closes, so its sounds are heard. A program
that only plays sounds should end with `sound.wait`, or it ends before
anything is heard:

```cabby
sound.play "hello.wav"
sound.wait
```

## Testing, and without a sound device

With the environment variable `CABBY_SOUND=none`, nothing is sent to a
sound device, and nothing is recorded from one: a recording is silent. Instead, the sound is worked out only when `sound.wait` asks,
as fast as it can, so a test of a program that plays a long tune takes no
time. `CABBY_SOUNDFONT=none` uses Cabby's own synthesizer even where
there's a SoundFont, so every computer gives the same. Cabby's own tests
set both.

`CABBY_SOUND_IN=file.wav` records that file instead of an input, all of it
at once, the moment recording starts (its `level` is as if it were being
played in); `sound.inputs` is just its name. Cabby's tests record a
plucked guitar this way, `tests/sound-in.wav`.

`CABBY_SOUND_OUT=file.wav` saves everything played to a WAV file when the
program ends (and, like `CABBY_SOUND=none`, plays nothing), to listen to
afterwards.

`CABBY_SOUND_SYSTEM=alsa` (or `pulseaudio`, or on a Mac `coreaudio`)
plays and records through that sound system only, instead of the first
that works (PulseAudio, which PipeWire answers as, then ALSA): to find
out whether a problem is the sound system's, or a device's.

`CABBY_SOUND_DEBUG=1` says how recording went, on the terminal: which
input, through which sound system, at what rate and channels, as it
starts; and as it stops, how much sound came in over how long, and the
longest wait between the pieces it came in. Sound that's missing, or a
long wait, is heard as a click.

An input that sends far more sound than it could have (a few seconds'
worth in a second) has gone wrong: sound stops recording from it, and
says so, before it fills the computer's memory.

Without a sound device (on a server, say), a program still runs: sound
says so once, and plays nothing. Without an input, recordings are silent,
and sound says that once too.

`CABBY_MIDI_IN=song.mid` is the one keyboard there is, and plays that
MIDI file's notes as it's listened to, as fast as they can be read (and,
played through, each at its own time in the mix). `CABBY_MIDI_OUT=out.mid`
is the one synthesizer there is: what it's sent is saved to that file
when it's closed, or the program ends, a millisecond to a tick at 120
beats a minute, so `sound.read_song` can read it back. Cabby's tests set
both: the keyboard is `tests/midi-in.mid`, four notes.

## Not in sound yet

* MIDI's time messages (a clock to play in time with another program) and
  system-exclusive messages (a synthesizer's own settings)
* MIDI 2.0's finer messages
