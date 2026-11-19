#!/usr/bin/env python3
"""Playback check: read .mid files with a parser written independently of our C++ writer and
confirm they are well-formed Standard MIDI Files that a standard player can open.

Checks the MThd header, every chunk length, every delta time and event (running status is
allowed, even though our writer never uses it), that each note-on has a matching note-off on the
same channel and pitch, that every track ends with exactly one End of Track event, and that a tempo
is set. Prints a one-line summary per file.

Usage: python check_midi.py song.mid [more.mid ...]
"""
import struct
import sys


class MidiError(Exception):
    pass


# Number of data bytes after each channel-message status (by its high nibble).
DATA_BYTES = {0x80: 2, 0x90: 2, 0xA0: 2, 0xB0: 2, 0xC0: 1, 0xD0: 1, 0xE0: 2}


def read_vlq(data, pos):
    value = 0
    for _ in range(4):
        if pos >= len(data):
            raise MidiError("variable-length number runs past the end of the track")
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos
    raise MidiError("variable-length number longer than 4 bytes")


def parse_track(body, index):
    pos = 0
    tick = 0
    status = None
    open_notes = {}
    notes = 0
    tempos = []
    ended = False
    while pos < len(body):
        if ended:
            raise MidiError(f"track {index}: data after End of Track")
        delta, pos = read_vlq(body, pos)
        tick += delta
        if pos >= len(body):
            raise MidiError(f"track {index}: delta time with no event after it")
        first = body[pos]
        if first == 0xFF:  # meta event: FF type length data
            if pos + 1 >= len(body):
                raise MidiError(f"track {index}: truncated meta event")
            meta_type = body[pos + 1]
            length, pos = read_vlq(body, pos + 2)
            data = body[pos:pos + length]
            if len(data) != length:
                raise MidiError(f"track {index}: meta event runs past the end of the track")
            pos += length
            if meta_type == 0x2F:
                if length != 0:
                    raise MidiError(f"track {index}: End of Track must have length 0")
                ended = True
            elif meta_type == 0x51:
                if length != 3:
                    raise MidiError(f"track {index}: tempo event must have length 3")
                tempos.append((tick, int.from_bytes(data, "big")))
            status = None
        elif first in (0xF0, 0xF7):  # system exclusive
            length, pos = read_vlq(body, pos + 1)
            pos += length
            if pos > len(body):
                raise MidiError(f"track {index}: system exclusive event runs past the end")
            status = None
        else:  # channel message, possibly using running status
            if first & 0x80:
                status = first
                pos += 1
            elif status is None:
                raise MidiError(f"track {index}: data byte {first:#04x} with no status byte before it")
            if status >= 0xF0:
                raise MidiError(f"track {index}: unexpected system message {status:#04x}")
            kind, channel = status & 0xF0, status & 0x0F
            count = DATA_BYTES[kind]
            data = body[pos:pos + count]
            if len(data) != count or any(b & 0x80 for b in data):
                raise MidiError(f"track {index}: malformed {kind:#04x} message at tick {tick}")
            pos += count
            key = (channel, data[0]) if count == 2 else None
            if kind == 0x90 and data[1] > 0:
                open_notes[key] = open_notes.get(key, 0) + 1
                notes += 1
            elif kind == 0x80 or (kind == 0x90 and data[1] == 0):
                if not open_notes.get(key):
                    raise MidiError(f"track {index}: note-off for note {data[0]} on channel {channel + 1} "
                                    f"at tick {tick} with no note-on")
                open_notes[key] -= 1
    if not ended:
        raise MidiError(f"track {index}: missing End of Track")
    stuck = sum(count for count in open_notes.values() if count)
    if stuck:
        raise MidiError(f"track {index}: {stuck} notes are never released")
    return {"ticks": tick, "notes": notes, "tempos": tempos}


def check_file(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 14 or data[:4] != b"MThd":
        raise MidiError("not a MIDI file (no MThd header)")
    (header_length,) = struct.unpack(">I", data[4:8])
    if header_length != 6:
        raise MidiError(f"header length is {header_length}, expected 6")
    fmt, track_count, division = struct.unpack(">HHH", data[8:14])
    if fmt not in (0, 1):
        raise MidiError(f"format {fmt}; expected 0 or 1")
    if fmt == 0 and track_count != 1:
        raise MidiError("a format 0 file must have exactly one track")
    if division == 0 or division & 0x8000:
        raise MidiError("expected a ticks-per-beat time division")

    pos = 14
    tracks = []
    while pos < len(data):
        if pos + 8 > len(data):
            raise MidiError("truncated chunk header at the end of the file")
        chunk_id = data[pos:pos + 4]
        (length,) = struct.unpack(">I", data[pos + 4:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise MidiError(f"chunk {chunk_id!r} claims {length} bytes but the file ends first")
        pos += 8 + length
        if chunk_id == b"MTrk":
            tracks.append(parse_track(body, len(tracks)))
        # The spec says players must skip chunk types they do not know, so we do too.
    if len(tracks) != track_count:
        raise MidiError(f"header says {track_count} tracks but the file has {len(tracks)}")

    notes = sum(track["notes"] for track in tracks)
    if notes == 0:
        raise MidiError("the file has no notes")
    tempos = sorted(t for track in tracks for t in track["tempos"])
    if not tempos:
        raise MidiError("no tempo event")
    total_ticks = max(track["ticks"] for track in tracks)
    seconds, last_tick, tempo = 0.0, 0, 500_000  # 120 BPM until the first tempo event
    for tick, new_tempo in tempos:
        seconds += (tick - last_tick) * tempo / 1e6 / division
        last_tick, tempo = tick, new_tempo
    seconds += (total_ticks - last_tick) * tempo / 1e6 / division
    bpm = 60_000_000 / tempos[0][1]
    return f"format {fmt}, {track_count} tracks, {notes} notes, {seconds:.1f} s at {bpm:.0f} BPM"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    failures = 0
    for path in sys.argv[1:]:
        try:
            print(f"OK    {path}: {check_file(path)}")
        except (MidiError, OSError) as error:
            failures += 1
            print(f"FAIL  {path}: {error}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
