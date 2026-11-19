// midi_writer.h - writes a Song as a Standard MIDI File, by hand (no MIDI library).
// Owner: Member C. Backup: Member D.
//
// File layout:
//   "MThd" header       format 1 (several tracks that play together), track count, 480 ticks per beat
//   "MTrk" track 0      the conductor track: song title, 4/4 time signature, tempo
//   "MTrk" track 1..n   one per instrument: name, program change, note-on/note-off pairs
// Every event inside a track is preceded by a delta time: ticks since the previous event.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "song.h"

namespace hashmusic {

// MIDI stores delta times as variable-length quantities: 7 bits per byte, most significant group
// first, with the top bit set on every byte except the last. 0x40 -> 40, 0x80 -> 81 00.
std::vector<std::uint8_t> encode_vlq(std::uint32_t value);

// The complete file contents. Throws std::invalid_argument for out-of-range values
// (pitch or program above 127, velocity 0, channel above 15, zero-length notes, tempo 0).
std::vector<std::uint8_t> write_midi(const Song& song);

// Writes bytes to a file in binary mode (path is UTF-8). Throws std::runtime_error on failure.
void save_file(const std::string& path, const std::vector<std::uint8_t>& bytes);

}  // namespace hashmusic
