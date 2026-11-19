#!/usr/bin/env python3
"""Mapping cross-check: an independent model of the byte map, written in Python from the rules in
README.md, must agree note for note with the .mid files hashmusic writes.

For each test input it runs hashmusic, decodes the melody track of the file it wrote, and compares the
tempo, every note's start, end, pitch and velocity, and the End of Track time with what the rules
predict from hashlib's digest of the same input.

Usage: python check_mapping.py <path to hashmusic> <scratch folder>
"""
import hashlib
import os
import random
import struct
import subprocess
import sys

PITCHES = [55, 57, 60, 62, 64, 67, 69, 72, 74, 76, 79, 81, 84, 86, 88, 91]  # first hex digit
LENGTHS = [240] * 6 + [480] * 6 + [720] * 2 + [960] * 2                    # second hex digit
BAR = 1920


def expected_song(text):
    digest = hashlib.sha256(text.encode("utf-8")).digest()
    bpm = 80 + digest[24] * 80 // 255
    base = 60 + digest[25] * 40 // 255

    def accent(tick):
        position = tick % BAR
        return 12 if position == 0 else 6 if position == 960 else 0 if position % 480 == 0 else -8

    notes, tick = [], 0
    for byte in digest[:24]:
        slot = LENGTHS[byte & 0x0F]
        notes.append((tick, tick + slot - slot // 10, PITCHES[byte >> 4], max(1, min(127, base + accent(tick)))))
        tick += slot
    end = -(-(tick + 960) // BAR) * BAR  # the first bar line at least two beats after the last note
    notes.append((tick, end, 72, max(1, min(127, base + 6))))
    return bpm, notes, end + 480


def read_vlq(data, pos):
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos


def track_events(body):
    pos, tick = 0, 0
    while pos < len(body):
        delta, pos = read_vlq(body, pos)
        tick += delta
        status = body[pos]
        if status == 0xFF:
            kind = body[pos + 1]
            length, pos = read_vlq(body, pos + 2)
            yield tick, "meta", kind, body[pos:pos + length]
            pos += length
        else:
            size = 1 if status & 0xF0 in (0xC0, 0xD0) else 2
            yield tick, "channel", status, body[pos + 1:pos + 1 + size]
            pos += 1 + size


def decoded_song(path):
    with open(path, "rb") as f:
        data = f.read()
    chunks, pos = [], 14
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos + 4:pos + 8])
        chunks.append(data[pos + 8:pos + 8 + length])
        pos += 8 + length
    conductor, melody = chunks[0], chunks[1]
    tempo = next(int.from_bytes(payload, "big") for _, kind, meta, payload in track_events(conductor)
                 if kind == "meta" and meta == 0x51)
    started, notes, end = {}, [], 0
    for tick, kind, status, payload in track_events(melody):
        end = tick
        if kind == "channel" and status & 0xF0 == 0x90:
            started[payload[0]] = (tick, payload[1])
        elif kind == "channel" and status & 0xF0 == 0x80:
            start, velocity = started.pop(payload[0])
            notes.append((start, tick, payload[0], velocity))
    return round(60_000_000 / tempo), sorted(notes), end


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    exe, folder = sys.argv[1], sys.argv[2]
    os.makedirs(folder, exist_ok=True)
    rng = random.Random(2710)
    alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?-é♪"
    inputs = ["jacob", "jacob1", "", "The quick brown fox jumps over the lazy dog", "Bubble Sort Bandits"]
    inputs += ["".join(rng.choice(alphabet) for _ in range(rng.randrange(1, 40))) for _ in range(25)]

    failures = 0
    for index, text in enumerate(inputs):
        path = os.path.join(folder, f"mapping_{index}.mid")
        subprocess.run([exe, "-q", "-o", path, "--", text], check=True, capture_output=True)
        if decoded_song(path) != expected_song(text):
            failures += 1
            print(f"MISMATCH for input {text!r}: the file does not follow the byte map")
    print(f"mapping cross-check: {len(inputs)} songs, {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
