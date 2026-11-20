// extras.h - ideas from the course's project list that fit a hash-to-music tool, plus WAV export.
//
//   mine()               #16 hash puzzle / proof of work: find a nonce whose hash starts with zeros
//   hash_art_svg()       #19 hash art: a symmetric picture drawn from the digest
//   avalanche_sweep()    flip one bit at every position of the input and count the output bits
//   render_wav()         the song as audio anyone can play, no MIDI support needed
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sha256.h"
#include "song.h"
#include "toy_hash.h"

namespace hashmusic {

// ---- Proof of work ----
struct MineResult {
    bool found = false;
    std::uint64_t nonce = 0;
    std::uint64_t attempts = 0;
    std::string input;  // text + "#" + nonce
    Digest digest{};
};

int leading_zero_hex_digits(const Digest& digest);

// Tries text#0, text#1, ... until the SHA-256 hex starts with `zeros` zero digits. Each extra zero makes
// it 16 times harder on average; checking an answer takes one hash. That asymmetry is how Bitcoin
// "mining" works. A mined song starts with G3 eighth notes, because 0 picks G3 and 0 picks an eighth.
MineResult mine(std::string_view text, int zeros, std::uint64_t max_attempts);

// ---- Hash art ----
// An 8x8 mirrored grid: each of the 32 digest bytes decides one cell on the left half (on if its top
// bit is set, dark or light by the next bit), and the last byte picks the color.
std::string hash_art_svg(const Digest& digest, int size = 320);

// ---- Avalanche sweep ----
// For each byte position of `text`, flip its lowest bit and count how many of the 256 output bits
// change. SHA-256 gives about 128 everywhere; the toy hash gives 1.
std::vector<int> avalanche_sweep(std::string_view text, HashKind kind);

// ---- WAV export ----
// Renders the song with simple additive synthesis to a 16-bit mono WAV file.
std::vector<std::uint8_t> render_wav(const Song& song, std::uint32_t sample_rate = 22050);

}  // namespace hashmusic
