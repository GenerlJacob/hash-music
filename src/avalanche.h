// avalanche.h - the avalanche demo (stretch goal): compare two inputs that differ by one character.
// Owner: Member A (Jacob).
//
// For an ideal hash, every output bit flips with probability 1/2 when the input changes at all,
// so two digests should differ in about 128 of 256 bits (give or take 8, one standard deviation).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "sha256.h"

namespace hashmusic {

// How many of the 256 bits differ (the population count of a XOR b).
int count_bit_differences(const Digest& a, const Digest& b);

// How many of the 64 hex digits differ.
int count_hex_differences(const Digest& a, const Digest& b);

// 64 characters: '^' under each hex digit that differs, ' ' where the digits match.
std::string hex_difference_marker(const Digest& a, const Digest& b);

// The 256 bits as 8 rows of 32 (4 bytes per row, a space between bytes):
// '#' = this bit flipped, '.' = same. Each row ends with '\n'.
std::string bit_difference_grid(const Digest& a, const Digest& b);

// Levenshtein distance: the fewest single-character insertions, deletions or substitutions
// that turn one input into the other ("jacob" -> "jacob1" is 1).
std::size_t edit_distance(std::string_view a, std::string_view b);

}  // namespace hashmusic
