#!/usr/bin/env python3
"""Synthesises mods/poe2/sfx/poe2/glacial_spike.wav: ice bursting out of the ground, then shattering.

The sound is made from scratch (filtered noise, clicks and bell-like partials), so it carries no game audio.
It lines up with the effect in Source/poe2/vfx.cpp: the burst at 0 and the shatter 650 ms later.

Usage: python3 tools/make_glacial_spike_sfx.py [output.wav]
"""
import math
import sys
import wave

import numpy as np

RATE = 44100
LENGTH = 1.6
SHATTER_AT = 0.65

rng = np.random.default_rng(1357)
t = np.arange(int(RATE * LENGTH)) / RATE


def biquad(x, kind, freq, q):
    """A plain RBJ biquad filter."""
    w = 2 * math.pi * freq / RATE
    alpha = math.sin(w) / (2 * q)
    cos = math.cos(w)
    if kind == 'lp':
        b = [(1 - cos) / 2, 1 - cos, (1 - cos) / 2]
    elif kind == 'hp':
        b = [(1 + cos) / 2, -(1 + cos), (1 + cos) / 2]
    else:  # band pass, constant peak gain
        b = [alpha, 0, -alpha]
    a = [1 + alpha, -2 * cos, 1 - alpha]
    b = [v / a[0] for v in b]
    a = [v / a[0] for v in a]
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        out = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1 = x1, v
        y2, y1 = y1, out
        y[i] = out
    return y


def env(start, attack, decay):
    """An envelope: a short rise at start, then an exponential fall."""
    e = np.zeros_like(t)
    after = t >= start
    local = t[after] - start
    e[after] = np.minimum(local / max(attack, 1e-4), 1) * np.exp(-local / decay)
    return e


def ping(start, freq, decay, level):
    """A bright glassy ring: a few inharmonic partials, like struck ice."""
    out = np.zeros_like(t)
    for ratio, gain in ((1.0, 1.0), (2.76, 0.5), (5.40, 0.25), (8.93, 0.12)):
        f = freq * ratio
        if f > RATE * 0.45:
            continue
        out += gain * np.sin(2 * math.pi * f * (t - start)) * env(start, 0.0008, decay / ratio ** 0.5)
    return out * level


noise = rng.standard_normal(len(t))
sound = np.zeros_like(t)

# The ground gives: a deep thump that drops in pitch.
pitch = 70 * np.exp(-t / 0.08) + 38
phase = 2 * math.pi * np.cumsum(pitch) / RATE
sound += 0.9 * np.sin(phase) * env(0, 0.004, 0.16)

# The crack of the surface splitting.
sound += 0.8 * biquad(noise, 'hp', 2500, 0.7) * env(0, 0.0005, 0.018)

# Ice grinding up out of the earth: a rising crunchy roar of filtered noise and crackle.
roar = biquad(noise, 'bp', 900, 0.6) * env(0, 0.03, 0.22)
sound += 0.55 * roar
crackle = np.zeros_like(t)
for _ in range(220):
    at = rng.uniform(0, 0.45) ** 1.6
    i = int(at * RATE)
    crackle[i:i + 3] += rng.uniform(-1, 1) * math.exp(-at / 0.18)
sound += 0.5 * biquad(crackle, 'hp', 1800, 0.7)

# The crystals ring as they lock into place.
for _ in range(9):
    sound += ping(rng.uniform(0.0, 0.12), rng.uniform(1400, 3200), rng.uniform(0.12, 0.3), rng.uniform(0.05, 0.11))

# A cold hiss of mist while they stand.
sound += 0.12 * biquad(noise, 'hp', 5000, 0.7) * env(0.05, 0.15, 0.35)

# The shatter: a crack, a burst of glassy splinters and a tinkling fall.
sound += 0.7 * biquad(noise, 'hp', 3000, 0.7) * env(SHATTER_AT, 0.0005, 0.025)
sound += 0.35 * biquad(noise, 'bp', 4500, 0.8) * env(SHATTER_AT, 0.002, 0.12)
for _ in range(60):
    at = SHATTER_AT + rng.uniform(0, 0.55) ** 1.8
    sound += ping(at, rng.uniform(2200, 7000), rng.uniform(0.04, 0.16), rng.uniform(0.04, 0.12) * math.exp(-(at - SHATTER_AT) / 0.35))
splinters = np.zeros_like(t)
for _ in range(160):
    at = SHATTER_AT + rng.uniform(0, 0.6) ** 1.7
    i = int(at * RATE)
    if i + 3 < len(t):
        splinters[i:i + 2] += rng.uniform(-1, 1) * math.exp(-(at - SHATTER_AT) / 0.25)
sound += 0.45 * biquad(splinters, 'hp', 2500, 0.7)

# Fade the tail and level it a little under full scale.
sound *= np.clip((LENGTH - t) / 0.15, 0, 1)
sound /= np.max(np.abs(sound))
sound *= 0.89

out = sys.argv[1] if len(sys.argv) > 1 else 'mods/poe2/sfx/poe2/glacial_spike.wav'
with wave.open(out, 'wb') as f:
    f.setnchannels(1)
    f.setsampwidth(2)
    f.setframerate(RATE)
    f.writeframes((sound * 32767).astype('<i2').tobytes())
print(out)
