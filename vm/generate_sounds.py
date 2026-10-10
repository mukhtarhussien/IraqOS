#!/usr/bin/env python3
"""Create IraqOS's small, original UI sound set with the Python standard library."""
import math
import os
import struct
import wave

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(PROJECT, "assets", "sounds")
RATE = 22050


def tone_sequence(tones, length, volume=0.20):
    frames = int(RATE * length)
    samples = []
    for frame in range(frames):
        t = frame / RATE
        freq = tones[0][1]
        for start, hz in tones:
            if t >= start:
                freq = hz
        fade_in = min(1.0, t / 0.012)
        fade_out = min(1.0, max(0.0, (length - t) / 0.045))
        envelope = fade_in * fade_out
        value = math.sin(2.0 * math.pi * freq * t) * volume * envelope
        # Add a very quiet upper harmonic so the cues feel warm on phone speakers.
        value += math.sin(4.0 * math.pi * freq * t) * volume * 0.12 * envelope
        samples.append(max(-32767, min(32767, int(value * 32767))))
    return samples


def write_wav(name, samples):
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))


def main():
    os.makedirs(OUT, exist_ok=True)
    sounds = {
        "startup": ([(0.00, 523.25), (0.16, 659.25), (0.33, 783.99), (0.51, 1046.50)], 0.78),
        "login": ([(0.00, 659.25), (0.14, 783.99)], 0.31),
        "open": ([(0.00, 587.33), (0.07, 783.99)], 0.22),
        "close": ([(0.00, 783.99), (0.08, 587.33)], 0.20),
        "click": ([(0.00, 1046.50)], 0.07),
    }
    for name, (tones, length) in sounds.items():
        write_wav(name, tone_sequence(tones, length))


if __name__ == "__main__":
    main()
