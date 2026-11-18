#!/usr/bin/env python3
"""Makes the sound effects the tutorials use, into tutorials/sounds/: short
WAV files, worked out from sine and square waves and noise.

    python3 tools/make-sounds.py
"""
import math
import os
import random
import struct
import wave

RATE = 22050
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tutorials", "sounds")


def save(name, samples):
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 30000)) for s in samples))


def sweep(f0, f1, seconds, square=False, volume=0.5):
    out, phase = [], 0.0
    n = int(RATE * seconds)
    for i in range(n):
        t = i / n
        f = f0 + (f1 - f0) * t
        phase += 2 * math.pi * f / RATE
        v = math.sin(phase)
        if square:
            v = 1.0 if v > 0 else -1.0
        out.append(v * volume * (1 - t) ** 1.5)
    return out


def blip(freqs, each, volume=0.5):
    out = []
    for f in freqs:
        n = int(RATE * each)
        for i in range(n):
            t = i / n
            out.append(math.sin(2 * math.pi * f * i / RATE) * volume * min(1, (1 - t) * 4))
    return out


random.seed(57)
save("laser", sweep(1400, 300, 0.18, square=True, volume=0.25))
boom = []
low = 0.0
n = int(RATE * 0.6)
for i in range(n):
    low = low * 0.9 + random.uniform(-1, 1) * 0.1   # noise, made deeper
    boom.append(low * 4 * (1 - i / n) ** 2)
save("boom", boom)
save("catch", blip([880, 1320], 0.07))
save("miss", sweep(300, 90, 0.3, volume=0.6))
print("sounds written to", os.path.normpath(OUT))

# ---- for Cabby Quest (tutorials 77 to 86)

def noise_burst(seconds, volume=0.4, low=0.6):
    """a short thud of noise, softened (a footstep)"""
    out, last = [], 0.0
    n = int(RATE * seconds)
    for i in range(n):
        t = i / n
        last = last * low + random.uniform(-1, 1) * (1 - low)
        out.append(last * volume * (1 - t) ** 2)
    return out

save("step", noise_burst(0.06, 0.5, 0.75))
save("bump", sweep(140, 70, 0.12, volume=0.7))
save("blip", blip([1200], 0.025, 0.25))
save("menu-move", blip([660], 0.04, 0.3))
save("menu-choose", blip([660, 990], 0.05, 0.35))
save("pick-up", blip([523, 659, 784, 1047], 0.07, 0.35))
save("door", sweep(220, 330, 0.18, volume=0.4) + noise_burst(0.08, 0.3, 0.8))
save("notice", blip([988, 1319], 0.06, 0.4))      # someone has seen the hero: "!"
save("hop", sweep(260, 620, 0.09, volume=0.35) + sweep(620, 300, 0.09, volume=0.3))   # over a ledge
