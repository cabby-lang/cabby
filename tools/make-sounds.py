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

# ---- for Cabby Quest in 3D (tutorial 119): water, rain, thunder, the cave


def fade_ends(samples, seconds=0.01):
    """no click at the start or the end"""
    n = int(RATE * seconds)
    for i in range(min(n, len(samples))):
        samples[i] *= i / n
        samples[-1 - i] *= i / n
    return samples


def splash():
    """a burst of bright noise, falling, with a bubble under it"""
    out, last = [], 0.0
    n = int(RATE * 0.4)
    for i in range(n):
        t = i / n
        last = last * 0.35 + random.uniform(-1, 1) * 0.65
        bubble = math.sin(2 * math.pi * (320 - 180 * t) * i / RATE) * 0.25 * (1 - t) ** 3
        out.append((last * 0.45 * (1 - t) ** 2.5 + bubble) * min(1, t * 40))
    return out


def stroke():
    """a soft swish: noise, swelling and falling"""
    out, last = [], 0.0
    n = int(RATE * 0.22)
    for i in range(n):
        t = i / n
        last = last * 0.7 + random.uniform(-1, 1) * 0.3
        out.append(last * 0.5 * math.sin(math.pi * t))
    return out


def drip():
    """a drop: a quick falling pitch, and its echo"""
    one = sweep(1900, 700, 0.05, volume=0.45)
    echo = [v * 0.35 for v in one]
    return one + [0.0] * int(RATE * 0.09) + echo


def rain():
    """three seconds of soft noise, its end blended into its start, to go
    round and round with no seam"""
    seconds, blend = 3.0, 0.25
    n, b = int(RATE * seconds), int(RATE * blend)
    raw, last, hiss = [], 0.0, 0.0
    for i in range(n + b):
        last = last * 0.55 + random.uniform(-1, 1) * 0.45
        hiss = hiss * 0.92 + last * 0.08
        drop = random.uniform(-1, 1) * 0.6 if random.random() < 0.002 else 0.0
        raw.append((last * 0.22 + hiss * 0.5 + drop))
    out = raw[:n]
    for i in range(b):
        t = i / b
        out[i] = raw[n + i] * (1 - t) + raw[i] * t
    return out


def thunder():
    """a crack, then a long low rumble, rolling away"""
    out, low, lower = [], 0.0, 0.0
    n = int(RATE * 3.0)
    for i in range(n):
        t = i / n
        low = low * 0.97 + random.uniform(-1, 1) * 0.03
        lower = lower * 0.995 + low * 0.005
        crack = random.uniform(-1, 1) * 0.5 * max(0, 1 - t * 20)
        roll = 1 + 0.4 * math.sin(2 * math.pi * 1.3 * t * 3)
        out.append(crack + (low * 9 + lower * 40) * (1 - t) ** 1.5 * roll)
    return out


def squeak():
    """a bat: two tiny high chirps"""
    one = sweep(3200, 4300, 0.025, volume=0.18)
    return one + [0.0] * int(RATE * 0.03) + one


save("splash", fade_ends(splash()))
save("stroke", fade_ends(stroke()))
save("drip", fade_ends(drip()))
save("rain", rain())
save("thunder", fade_ends(thunder()))
save("squeak", fade_ends(squeak()))

# ---- for the coast (tutorial 120): the surf, and gulls


def surf():
    """six seconds of the sea: noise, swelling as each wave comes in and
    falling as it goes out, its end blended into its start"""
    seconds, blend = 6.0, 0.5
    n, b = int(RATE * seconds), int(RATE * blend)
    raw, last, deep = [], 0.0, 0.0
    for i in range(n + b):
        t = i / RATE
        last = last * 0.6 + random.uniform(-1, 1) * 0.4
        deep = deep * 0.985 + last * 0.015
        swell = 0.35 + 0.65 * max(0, math.sin(2 * math.pi * t / 3.0)) ** 2   # (a wave every three seconds)
        raw.append((last * 0.18 + deep * 2.2) * swell)
    out = raw[:n]
    for i in range(b):
        k = i / b
        out[i] = raw[n + i] * (1 - k) + raw[i] * k
    return out


def gull():
    """a gull's cry: a high note falling, wavering, twice"""
    out = []
    for cry in range(2):
        n = int(RATE * 0.28)
        phase = 0.0
        for i in range(n):
            t = i / n
            f = 1500 - 650 * t + 60 * math.sin(2 * math.pi * 28 * i / RATE)
            phase += 2 * math.pi * f / RATE
            v = math.sin(phase) + 0.3 * math.sin(2 * phase)
            out.append(v * 0.16 * math.sin(math.pi * t))
        out += [0.0] * int(RATE * 0.08)
    return out


save("surf", surf())
save("gull", fade_ends(gull()))


# ---- for Cabby Quest in 3D (tutorial 122): the train


def whistle():
    """a steam whistle: three notes together, a chord, swelling in, held,
    and dying away, with a breath of steam through it"""
    out, last = [], 0.0
    n = int(RATE * 1.3)
    phases = [0.0, 0.0, 0.0]
    for i in range(n):
        t = i / n
        swell = min(1, t * 12) * (1 - max(0, t - 0.75) / 0.25)
        last = last * 0.5 + random.uniform(-1, 1) * 0.5
        v = 0.0
        for k, f in enumerate((587, 740, 880)):    # (D, F sharp, A)
            phases[k] += 2 * math.pi * f * (1 + 0.004 * math.sin(2 * math.pi * 5 * i / RATE)) / RATE
            v += math.sin(phases[k])
        out.append((v * 0.11 + last * 0.06) * swell)
    return out


save("whistle", fade_ends(whistle()))
