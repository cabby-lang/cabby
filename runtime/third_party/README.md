# Code from elsewhere, used by Cabby's libraries

Each is kept as it came, with its license beside it.

| File | What it is | From | License |
|---|---|---|---|
| `miniaudio.h` | plays sound through PipeWire, PulseAudio, ALSA or Core Audio, and reads WAV, MP3 and FLAC files (version 0.11.26) | https://github.com/mackron/miniaudio | public domain or MIT No Attribution (`miniaudio-LICENSE`) |
| `tsf.h` | TinySoundFont: plays notes with the instruments of a SoundFont (`.sf2`) | https://github.com/schellingb/TinySoundFont | MIT (`TinySoundFont-LICENSE`) |
| `tml.h` | TinyMidiLoader: reads MIDI files | the same | the same |

| `stb_image.h` | reads PNG, JPEG, BMP and GIF pictures (version 2.30) | https://github.com/nothings/stb | public domain or MIT (at the end of the file) |
| `stb_image_write.h` | writes PNG files (version 1.16) | the same | the same |
| `stb_truetype.h` | draws text with TrueType fonts (version 1.26) | the same | the same |

| `cgltf.h` | reads glTF 2.0 models (`.glb` and `.gltf`, as Blender exports them): their meshes, materials, skins and animations (version 1.14) | https://github.com/jkuhlmann/cgltf | MIT (`cgltf-LICENSE`) |

The sound library (`runtime/sound.c`) is built from the first three, and
the game library (`runtime/game.c`) from the stb files; the world library
(`runtime/world.c`) from the stb files and cgltf.
