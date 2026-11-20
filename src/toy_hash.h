// toy_hash.h - a deliberately BAD hash, to hear what SHA-256 protects us from (--hash toy).
//
// Digest byte j is the XOR of every input byte at positions j, j+32, j+64, ... That means:
//   - no avalanche: changing one character changes one digest byte, so one note of the song;
//   - it leaks the input: anything up to 32 bytes long shows straight through as the digest;
//   - instant collisions: swap two characters 32 positions apart and the digest does not change.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "sha256.h"

namespace hashmusic {

enum class HashKind { kSha256, kToy };

// "sha256" or "toy" -> HashKind; throws std::invalid_argument for anything else.
HashKind parse_hash_kind(std::string_view name);
const char* hash_display_name(HashKind kind);  // "SHA-256" or "toy XOR hash"

class ToyHash {
public:
    void update(const void* data, std::size_t length);
    Digest finish() const { return digest_; }

private:
    Digest digest_{};
    std::size_t position_ = 0;
};

Digest toy_hash(std::string_view text);

// Either hash behind one interface, so every mode of the tool can use either one.
class AnyHasher {
public:
    explicit AnyHasher(HashKind kind) : kind_(kind) {}
    void update(const void* data, std::size_t length);
    Digest finish();

private:
    HashKind kind_;
    Sha256 sha_;
    ToyHash toy_;
};

Digest hash_text(HashKind kind, std::string_view text);

// Two different strings with the same toy hash: `text` padded with '.' to 33+ characters, and the
// same string with characters 0 and 32 swapped (made different first if they happen to match).
std::pair<std::string, std::string> make_toy_collision(std::string text);

}  // namespace hashmusic
