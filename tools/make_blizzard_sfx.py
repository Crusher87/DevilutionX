#!/usr/bin/env python3
"""Synthesises the Blizzard sounds in mods/poe2/sfx/poe2/: blizzard_cast.wav and blizzard_shard1-4.wav.

The cast is a howling gust of icy wind that rises and falls, with frost crackling and crystals glittering in it.
The shards are the little storms landing: a soft muffled crunch of snow with a faint glassy tick on top. A Blizzard
drops many of them, so they are short, quiet and varied, and the game plays only some of them (Curtis 2026-10-19:
"there are many of them so I don't want it to be an annoying sound").

The sounds are made from scratch (filtered noise, clicks and bell-like partials), so they carry no game audio.

Usage: python3 tools/make_blizzard_sfx.py [output directory]
"""
import math
import os
import sys
import wave

import numpy as np

RATE = 44100


def biquad(x, kind, freq, q):
    """A plain RBJ biquad filter; freq may be a number or an array (one per sample) for a sweep."""
    freqs = np.broadcast_to(np.asarray(freq, dtype=float), x.shape)
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    last = None
    for i, v in enumerate(x):
        f = freqs[i]
        if f != last:
            w = 2 * math.pi * f / RATE
            alpha = math.sin(w) / (2 * q)
            cos = math.cos(w)
            if kind == 'lp':
                b = [(1 - cos) / 2, 1 - cos, (1 - cos) / 2]
            elif kind == 'hp':
                b = [(1 + cos) / 2, -(1 + cos), (1 + cos) / 2]
            else:  # band pass, constant peak gain
                b = [alpha, 0, -alpha]
            a0 = 1 + alpha
            b0, b1, b2 = b[0] / a0, b[1] / a0, b[2] / a0
            a1, a2 = -2 * cos / a0, (1 - alpha) / a0
            last = f
        out = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, v
        y2, y1 = y1, out
        y[i] = out
    return y


def env(t, start, attack, decay):
    """An envelope: a short rise at start, then an exponential fall."""
    e = np.zeros_like(t)
    after = t >= start
    local = t[after] - start
    e[after] = np.minimum(local / max(attack, 1e-4), 1) * np.exp(-local / decay)
    return e


def ping(t, start, freq, decay, level):
    """A bright glassy ring: a few inharmonic partials, like struck ice."""
    out = np.zeros_like(t)
    for ratio, gain in ((1.0, 1.0), (2.76, 0.5), (5.40, 0.25)):
        f = freq * ratio
        if f > 12000:
            continue
        out += gain * np.sin(2 * math.pi * f * (t - start)) * env(t, start, 0.0008, decay / ratio ** 0.5)
    return out * level


def write(path, sound, peak):
    sound = sound / np.max(np.abs(sound)) * peak
    with wave.open(path, 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes((sound * 32767).astype('<i2').tobytes())
    print(path)


def cast(rng):
    length = 1.5
    t = np.arange(int(RATE * length)) / RATE
    noise = rng.standard_normal(len(t))
    sound = np.zeros_like(t)

    # Cold air drawn in: a soft low whump as the storm gathers.
    pitch = 120 * np.exp(-t / 0.12) + 45
    sound += 0.45 * np.sin(2 * math.pi * np.cumsum(pitch) / RATE) * env(t, 0, 0.02, 0.2)

    # The gust: wind howling up in pitch and dying away, two layers a little apart so it whistles.
    swell = np.clip(t / 0.35, 0, 1) ** 1.5 * np.exp(-np.maximum(t - 0.35, 0) / 0.42)
    sweep = 350 + 1000 * np.sin(np.clip(t / 0.9, 0, 1) * math.pi * 0.75)
    sound += 1.6 * biquad(noise, 'bp', sweep, 2.5) * swell
    sound += 0.7 * biquad(rng.standard_normal(len(t)), 'bp', sweep * 1.6, 3.0) * swell
    # The hiss of driven snow under it.
    sound += 0.1 * biquad(biquad(noise, 'hp', 3500, 0.7), 'lp', 9000, 0.7) * swell

    # Frost crackling as the air freezes.
    crackle = np.zeros_like(t)
    for _ in range(140):
        at = rng.uniform(0.08, 0.9)
        i = int(at * RATE)
        crackle[i:i + 2] += rng.uniform(-1, 1)
    sound += 0.35 * biquad(biquad(crackle, 'hp', 2500, 0.7), 'lp', 8000, 0.7) * swell

    # Crystals glittering in the gust, thickest at its peak.
    for _ in range(36):
        at = rng.uniform(0.1, 1.0)
        sound += ping(t, at, rng.uniform(2500, 6500), rng.uniform(0.05, 0.16), rng.uniform(0.03, 0.08) * (0.4 + swell[int(at * RATE)]))

    sound *= np.clip((length - t) / 0.2, 0, 1)
    return sound


def shard(rng):
    length = 0.32
    t = np.arange(int(RATE * length)) / RATE
    noise = rng.standard_normal(len(t))
    sound = np.zeros_like(t)

    # A soft muffled crunch of packed snow: the body of the sound, kept low so it sits under the fight.
    sound += 0.9 * biquad(noise, 'lp', rng.uniform(700, 1000), 0.8) * env(t, 0, 0.003, rng.uniform(0.03, 0.045))
    grit = np.zeros_like(t)
    for _ in range(18):
        at = rng.uniform(0, 0.06)
        i = int(at * RATE)
        grit[i:i + 2] += rng.uniform(-1, 1) * math.exp(-at / 0.03)
    sound += 0.35 * biquad(grit, 'bp', rng.uniform(1500, 2200), 0.9)

    # A faint glassy tick and one small ring on top.
    sound += 0.12 * biquad(noise, 'hp', 4000, 0.7) * env(t, 0, 0.0005, 0.006)
    sound += ping(t, rng.uniform(0.0, 0.015), rng.uniform(2200, 3800), rng.uniform(0.05, 0.09), 0.12)

    sound *= np.clip((length - t) / 0.05, 0, 1)
    return sound


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else 'mods/poe2/sfx/poe2'
    rng = np.random.default_rng(2468)
    write(os.path.join(folder, 'blizzard_cast.wav'), cast(rng), 0.85)
    # Quieter than the cast: there are many of them.
    for n in range(1, 5):
        write(os.path.join(folder, f'blizzard_shard{n}.wav'), shard(rng), 0.32)


main()
