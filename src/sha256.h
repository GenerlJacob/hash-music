// sha256.h - SHA-256 written from scratch, following NIST FIPS 180-4.
// Owner: Member A (Jacob). Backup: Member B.
//
// Interface (agreed day 2): give it bytes, get back a 32-byte digest.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hashmusic {

constexpr std::size_t kDigestBytes = 32;
using Digest = std::array<std::uint8_t, kDigestBytes>;

// Incremental hasher: call update() as many times as you like, then finish() once.
// Text mode uses the one-call sha256() helpers below; file mode uses this class directly so a
// large file can be hashed a piece at a time instead of being loaded into memory.
class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t length);
    void update(std::string_view text) { update(text.data(), text.size()); }

    // Pads the message, compresses the final block(s), and returns the digest.
    Digest finish();

private:
    void compress(const std::uint8_t* block);  // the 64 rounds, applied to one 64-byte block

    std::array<std::uint32_t, 8> state_;   // running hash value H0..H7
    std::array<std::uint8_t, 64> buffer_;  // bytes waiting to fill the next 64-byte block
    std::size_t buffered_ = 0;             // how many bytes of buffer_ are in use
    std::uint64_t total_bytes_ = 0;        // message length so far
    bool finished_ = false;
};

Digest sha256(const void* data, std::size_t length);
Digest sha256(std::string_view text);
Digest sha256(const std::vector<std::uint8_t>& bytes);

// 64 lowercase hex characters: the same format sha256sum and Python's hashlib print.
std::string to_hex(const Digest& digest);

// The constants SHA-256 is built from. Exposed so the tests can re-derive them from prime numbers.
extern const std::array<std::uint32_t, 64> kRoundConstants;  // K
extern const std::array<std::uint32_t, 8> kInitialHash;      // H(0)

}  // namespace hashmusic
