// band.h - extra goal from the proposal: "additional instruments chosen by the hash bytes, layered
// sounds such as bass and drums for a fuller song, a melody built from the extra instruments".
// The project plan cut this to fit two weeks; it is switched on with --band and leaves the core
// melody unchanged.
//
// It reads bytes 26-29, which the core melody does not use:
//   byte 26  first digit: melody instrument (16)    second digit: harmony instrument (16)
//   byte 27  first digit: chord progression (16)    second digit: harmony pattern (8, digit mod 8)
//   byte 28  first digit: bass instrument (8)       second digit: bass pattern (8)
//   byte 29  first digit: drum groove (8)           second digit: 4 on/off drum options, one per bit
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "sha256.h"
#include "song.h"

namespace hashmusic {

constexpr std::size_t kInstrumentByte = 26;
constexpr std::size_t kChordByte = 27;
constexpr std::size_t kBassByte = 28;
constexpr std::size_t kDrumByte = 29;

struct BandChoices {
    std::uint8_t melody_program = 0;
    std::string melody_instrument;
    std::uint8_t harmony_program = 0;
    std::string harmony_instrument;
    std::uint8_t bass_program = 0;
    std::string bass_instrument;

    std::size_t progression = 0;  // index into the 16 chord progressions
    std::string progression_name;  // e.g. "I-V-vi-IV"
    std::size_t harmony_pattern = 0;
    std::string harmony_pattern_name;
    std::size_t bass_pattern = 0;
    std::string bass_pattern_name;
    std::size_t groove = 0;
    std::string groove_name;

    bool ride = false;        // bit 0: ride cymbal instead of hi-hat
    bool fills = false;       // bit 1: tom fill at the end of every fourth bar and before the ending
    bool clap = false;        // bit 2: hand clap doubles the snare
    bool tambourine = false;  // bit 3: tambourine on beats 2 and 4
};

// Reads the band settings from bytes 26-29 of the digest.
BandChoices band_choices(const Digest& digest);

// Switches the melody to the hash-chosen instrument and adds Harmony, Bass and Drums tracks.
// Each bar gets one chord; the second-to-last bar is always V and the last is I (a "the end" cadence).
// Throws std::invalid_argument if `song` was not made by map_digest().
void add_band(Song& song, const Digest& digest);

}  // namespace hashmusic
