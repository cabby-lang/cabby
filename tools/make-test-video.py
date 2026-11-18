#!/usr/bin/env python3
"""Makes the test videos (tests/videos/), with ffmpeg: small, and lossless,
so what's decoded is the same on every computer.

colors.mkv: 160x90, 10 frames a second, 3 seconds. Each frame's left half
says which frame it is (its red is 8 times the frame's number; green
0x40, blue 0x80); its right half is red in the first second, green in the
second, blue in the third. Its sound: 48000 a second, two channels, a
beep as each second starts (a quarter of a second of an 880 Hz square
wave, at a quarter of full loudness on the left and an eighth on the
right), and nothing for the rest of the second. (Both channels go the
same way: a Mac mini has one speaker, which adds the two together, and
when the right was the left upside down, the beeps it had then made no
sound at all.) FFV1 (bgr0) and FLAC, in Matroska.

silent.mkv: the same picture for a second, without sound.

tone.mka: the same sound for a second, without a picture.

Usage: tools/make-test-video.py [folder]   (tests/videos to begin with)
"""
import os, struct, subprocess, sys, tempfile

W, H, FPS, SECONDS, RATE = 160, 90, 10, 3, 48000
RIGHTS = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]

def frames(count):
    out = bytearray()
    for n in range(count):
        left = (8 * n, 0x40, 0x80)
        right = RIGHTS[n // FPS]
        row = bytes(left) * (W // 2) + bytes(right) * (W // 2)
        out += row * H
    return bytes(out)

def sound(seconds):
    out = bytearray()
    for i in range(RATE * seconds):
        if i % RATE < RATE // 4:
            v = 8192 if (i * 880 * 2 // RATE) % 2 == 0 else -8192
        else:
            v = 0
        out += struct.pack("<hh", v, v // 2)
    return bytes(out)

def make_sound_only(path):
    with tempfile.TemporaryDirectory() as tmp:
        snd = os.path.join(tmp, "sound.raw")
        with open(snd, "wb") as f:
            f.write(sound(1))
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar", str(RATE), "-ac", "2", "-i", snd,
                        "-c:a", "flac", "-compression_level", "12", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                        path], check=True)

def make(path, count, with_sound):
    with tempfile.TemporaryDirectory() as tmp:
        pic = os.path.join(tmp, "pic.rgb")
        with open(pic, "wb") as f:
            f.write(frames(count))
        args = ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                "-r", str(FPS), "-i", pic]
        if with_sound:
            snd = os.path.join(tmp, "sound.raw")
            with open(snd, "wb") as f:
                f.write(sound(count // FPS))
            args += ["-f", "s16le", "-ar", str(RATE), "-ac", "2", "-i", snd]
        args += ["-c:v", "ffv1", "-pix_fmt", "bgr0", "-g", "1"]
        if with_sound:
            args += ["-c:a", "flac", "-compression_level", "12"]
        args += ["-metadata", "title=Cabby's test colors", "-fflags", "+bitexact", "-flags:v", "+bitexact",
                 "-flags:a", "+bitexact", path]
        subprocess.run(args, check=True)

here = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "tests", "videos")
make(os.path.join(here, "colors.mkv"), FPS * SECONDS, True)
make(os.path.join(here, "silent.mkv"), FPS, False)
make_sound_only(os.path.join(here, "tone.mka"))
