// mapper.h - turns a 32-byte SHA-256 digest into a melody.
// Owner: Member B. Backup: Member A.
//
// Byte map. Each byte is two hex digits of the printed hash, so you can read the song off the hash:
//   bytes 0-23   24 melody notes: first hex digit = pitch, second hex digit = rhythm
//   byte 24      tempo, 80-160 BPM
//   byte 25      loudness (base velocity), 60-100
//   bytes 26-29  used only by --band (see band.h); bytes 30-31 are spare
// After the 24 notes the melody lands on the home note C5, so every song sounds finished.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "sha256.h"
#include "song.h"

namespace hashmusic {

constexpr std::size_t kMelodyNotes = 24;
constexpr std::size_t kTempoByte = 24;
constexpr std::size_t kLoudnessByte = 25;
constexpr std::uint8_t kHomePitch = 72;  // C5

// What each hex digit means. Both tables have 16 entries, one per hex digit 0-f, so when the digest
// bytes are uniformly random every entry is picked equally often.
extern const std::array<std::uint8_t, 16> kPitchTable;    // C major pentatonic, G3 up to G6
extern const std::array<std::uint32_t, 16> kRhythmTable;  // note lengths in ticks

std::uint32_t tempo_from_byte(std::uint8_t byte);         // 0x00 -> 80 BPM ... 0xff -> 160 BPM
std::uint8_t base_velocity_from_byte(std::uint8_t byte);  // 0x00 -> 60 ... 0xff -> 100

// The core song: one piano track with the 24 hash notes and the final home note.
Song map_digest(const Digest& digest);

// Labels for printing: note_name(72) == "C5", rhythm_symbol(480) == "q".
std::string note_name(std::uint8_t pitch);
std::string rhythm_symbol(std::uint32_t ticks);  // e = eighth, q = quarter, q. = dotted quarter, h = half

}  // namespace hashmusic
