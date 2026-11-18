# speech — text read aloud, and speech heard

```cabby
use speech
```

Text said aloud by the computer, in a voice, a language and a speed of
the program's choosing, or saved into a sound file. The same on Linux
and on a Mac.

| | Linux | macOS |
|---|---|---|
| said by | `espeak-ng` (or the older `espeak`, or Speech Dispatcher's `spd-say`): the first the computer has | its own voices, with `say` |

<!-- check -->
```cabby
use speech

int main {
	if !speech.can_speak {
		println "nothing here to speak with"
		return 1
	}
	speech.say "The lesson is at four."

	speech.speaker Latin
	Latin.set_language "la"
	Latin.set_rate 140
	Latin.say "Gallia est omnis divisa in partes tres."
	Latin.save "Arma virumque cano.", "aeneid.wav"
	return 0
}
```

| Call | What it does |
|---|---|
| `bool speech.say Text` | says it aloud in the usual voice, waiting until it's said: `true` if it was |
| `bool speech.save Text, Path` | saves it said into a sound file (WAV) |
| `bool speech.can_speak` | `true` if there's something here to say it with |
| `list str speech.voices` | the voices there are, for `set_voice` |

## speech.speaker

How it's said: a voice, a language, a speed.

| `speech.speaker` | |
|---|---|
| `set_voice Name` | one of `speech.voices` |
| `set_language Code` | `"en"`, `"en-gb"`, `"fr"`, `"de"`, `"la"` (Latin), `"grc"` (Ancient Greek): on a Mac, the first voice for it |
| `set_rate Words` | how fast, in words a minute (175 or so is usual; 80 is slow, 300 fast) |
| `bool say Text` | says it, waiting until it's said |
| `bool save Text, Path` | saves it said into a sound file: WAV (and on a Mac, AIFF for a `.aiff`) |

A voice is named as each system names it: a Mac's by its name
(`"Samantha"`, `"Daniel"`), espeak-ng's by its language (`"en-gb"`,
`"la"`), Speech Dispatcher's by its own names.

espeak-ng speaks over a hundred languages, Latin and Ancient Greek
among them, in a voice that's clear rather than natural. A Mac's voices
sound more natural, but a Mac has none for Latin: it reads Latin in the
usual voice. On Linux, `espeak-ng` is a package of its own on Debian and
Arch; `spd-say` comes with GNOME's screen reader, and can't save to a
file.

## Hearing: speech to text

What's said in a sound file, or to the microphone, as text: by Whisper,
OpenAI's speech recognition, running on the computer itself (whisper.cpp's
`whisper-cli`), with nothing sent anywhere.

<!-- check -->
```cabby
use speech
use time

int main {
	if !speech.can_hear {
		speech.get_model "base.en"          // once: 148 MB, from the web
	}
	println speech.hear("lesson.mp3")
	println "Say something:"
	println speech.hear_for(time.seconds(5))
	return 0
}
```

| | |
|---|---|
| `str speech.hear Path` | what's said in a sound file (WAV, MP3, FLAC, Ogg Vorbis) |
| `str speech.hear_for Wait` | what's said to the microphone, for so long (it waits, recording) |
| `bool speech.can_hear` | whether there's whisper.cpp to hear with, and a model |
| `str speech.get_model Name` | fetches one of Whisper's models from the web, for `speech.model` to find (once): `"base.en"` (148 MB, English: quick, and good), `"base"` (any language), `"small"` (466 MB, better), `"medium"` (1.5 GB, better still) |
| `str speech.model` | the model it hears with: `CABBY_WHISPER_MODEL`, or the first in `speech.model_folder`, or where whisper.cpp's packages put them (`""` for none) |
| `str speech.model_folder` | where Cabby keeps the models it fetches |

| `speech.listener` | |
|---|---|
| `set_model Path` | a model's file (`ggml-small.bin`), rather than the one found |
| `set_language Code` | `"en"`, `"de"`, `"fr"`, `"la"` (Latin), `"el"` (Greek): what's said (to begin with, `"auto"`: Whisper works it out) |
| `str hear Path`, `str hear_for Wait` | as above, as it says |

A model ending `.en` hears English alone, and does it better; the others
hear about a hundred languages, Latin among them. The bigger the model,
the better it hears, and the longer it takes: `base` hears a minute of
speech in a few seconds on most computers.

Getting whisper.cpp:

| | |
|---|---|
| Arch | `whisper.cpp` (in the AUR: `yay -S whisper.cpp`) |
| Debian | built from its source: `git clone https://github.com/ggml-org/whisper.cpp`, then `cmake -B build && cmake --build build -j`, and `build/bin/whisper-cli` put on the `PATH` |
| a Mac | `brew install whisper-cpp` |
