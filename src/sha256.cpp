// sha256.cpp - SHA-256 from scratch (FIPS 180-4 sections 4.1.2, 4.2.2, 5.1.1 and 6.2).
// Owner: Member A (Jacob). Backup: Member B.
//
// The three stages the project plan calls out are marked below:
//   1. padding           -> Sha256::finish()
//   2. message schedule  -> Sha256::compress(), step 1
//   3. the 64 rounds     -> Sha256::compress(), step 2
#include "sha256.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace hashmusic {

// K: the first 32 bits of the fractional parts of the cube roots of the first 64 primes.
// These are "nothing up my sleeve" numbers: anyone can check where they came from, so nobody can
// have hidden a weakness in them. tests/test_sha256.cpp re-derives every one.
const std::array<std::uint32_t, 64> kRoundConstants = {{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
}};

// H(0): the first 32 bits of the fractional parts of the square roots of the first 8 primes.
const std::array<std::uint32_t, 8> kInitialHash = {{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
}};

namespace {

// Rotate right: bits that fall off the right end come back in on the left.
constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// The six logical functions of FIPS 180-4 section 4.1.2.
// ch: "choose" - each bit of x picks the matching bit from y (x=1) or z (x=0).
constexpr std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (~x & z); }
// maj: "majority" - each output bit is whichever value at least two of x, y, z have.
constexpr std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
constexpr std::uint32_t big_sigma0(std::uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
constexpr std::uint32_t big_sigma1(std::uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
constexpr std::uint32_t small_sigma0(std::uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
constexpr std::uint32_t small_sigma1(std::uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

// SHA-256 reads its input as big-endian 32-bit words, whatever the CPU's own byte order is.
std::uint32_t load_big_endian(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
           std::uint32_t{p[3]};
}

}  // namespace

Sha256::Sha256() : state_(kInitialHash), buffer_{} {}

void Sha256::update(const void* data, std::size_t length) {
    if (finished_) throw std::logic_error("Sha256::update() called after finish()");
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_bytes_ += length;
    while (length > 0) {
        const std::size_t take = std::min(length, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes, take);
        buffered_ += take;
        bytes += take;
        length -= take;
        if (buffered_ == buffer_.size()) {  // a full 64-byte block is ready
            compress(buffer_.data());
            buffered_ = 0;
        }
    }
}

Digest Sha256::finish() {
    if (finished_) throw std::logic_error("Sha256::finish() called twice");

    // Stage 1: padding (FIPS 180-4 section 5.1.1). The padded message must fill whole 64-byte blocks:
    //   a. append a single 1 bit (the byte 0x80),
    //   b. append 0 bits until the length is 8 bytes short of a block boundary,
    //   c. append the original length in bits as a 64-bit big-endian number.
    // If fewer than 9 bytes are left in the current block, the padding spills into one extra block.
    const std::uint64_t message_bits = total_bytes_ * 8;
    std::array<std::uint8_t, 64> padding{};
    padding[0] = 0x80;
    const std::size_t pad_bytes = (buffered_ < 56) ? 56 - buffered_ : 64 + 56 - buffered_;
    update(padding.data(), pad_bytes);

    std::array<std::uint8_t, 8> length_field{};
    for (std::size_t i = 0; i < length_field.size(); ++i) {
        length_field[i] = static_cast<std::uint8_t>(message_bits >> (56 - 8 * i));
    }
    update(length_field.data(), length_field.size());  // completes the final block; buffered_ is 0 again

    // The digest is H0..H7 written out big-endian.
    Digest digest{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        digest[4 * i + 0] = static_cast<std::uint8_t>(state_[i] >> 24);
        digest[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        digest[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        digest[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    finished_ = true;
    return digest;
}

void Sha256::compress(const std::uint8_t* block) {
    // Stage 2: the message schedule. The block's 16 words are stretched to 64; each new word mixes
    // four earlier ones, so every input bit ends up influencing many rounds.
    std::array<std::uint32_t, 64> w{};
    for (std::size_t t = 0; t < 16; ++t) {
        w[t] = load_big_endian(block + 4 * t);
    }
    for (std::size_t t = 16; t < 64; ++t) {
        w[t] = small_sigma1(w[t - 2]) + w[t - 7] + small_sigma0(w[t - 15]) + w[t - 16];
    }

    // Stage 3: the 64 rounds. Eight working variables start as the current hash value. Each round
    // folds in one schedule word and one constant, then shifts the variables along by one place.
    // All additions are mod 2^32: unsigned overflow simply wraps around, which is what we want.
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t t = 0; t < 64; ++t) {
        const std::uint32_t t1 = h + big_sigma1(e) + ch(e, f, g) + kRoundConstants[t] + w[t];
        const std::uint32_t t2 = big_sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // Add this block's result into the running hash value.
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

Digest sha256(const void* data, std::size_t length) {
    Sha256 hasher;
    hasher.update(data, length);
    return hasher.finish();
}

Digest sha256(std::string_view text) { return sha256(text.data(), text.size()); }

Digest sha256(const std::vector<std::uint8_t>& bytes) { return sha256(bytes.data(), bytes.size()); }

std::string to_hex(const Digest& digest) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        hex += kHexDigits[byte >> 4];
        hex += kHexDigits[byte & 0x0F];
    }
    return hex;
}

}  // namespace hashmusic
