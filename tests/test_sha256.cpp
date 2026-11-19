// test_sha256.cpp - Member A's tests: NIST vectors, padding boundaries, streaming, the constants.
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sha256.h"
#include "test_framework.h"

using namespace hashmusic;

namespace {

std::string hex_of(std::string_view text) { return to_hex(sha256(text)); }

}  // namespace

// ---- Known vectors: NIST's published SHA-256 examples (FIPS 180-2 appendix B, NIST CSRC) ----

TEST_CASE(nist_empty_string) {
    CHECK_EQ(hex_of(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE(nist_abc_one_block) {
    CHECK_EQ(hex_of("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE(nist_448_bit_message_two_blocks) {
    // 56 bytes: too long for the padding to fit in one block, so the digest needs two.
    CHECK_EQ(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE(nist_896_bit_message) {
    CHECK_EQ(hex_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                    "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
             "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE(nist_one_million_a) {
    Sha256 hasher;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) hasher.update(chunk);
    CHECK_EQ(to_hex(hasher.finish()), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// ---- Padding boundaries. Expected values computed with Python's hashlib. ----
// 55 bytes is the longest message whose padding fits in one block; 56 spills into a second block.
// 63/64/65 straddle the first block edge, 119/120 the second padding edge, 127/128 the second block edge.
TEST_CASE(padding_boundaries) {
    const struct {
        std::size_t length;
        const char* hex;
    } cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
        {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
        {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    };
    for (const auto& c : cases) CHECK_EQ(hex_of(std::string(c.length, 'a')), std::string(c.hex));
}

TEST_CASE(binary_input_including_zero_bytes) {
    std::vector<std::uint8_t> all_bytes;
    for (int b = 0; b < 256; ++b) all_bytes.push_back(static_cast<std::uint8_t>(b));
    CHECK_EQ(to_hex(sha256(all_bytes)), "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
    const std::uint8_t zero = 0;
    CHECK_EQ(to_hex(sha256(&zero, 1)), "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d");
}

TEST_CASE(one_character_changes_everything) {
    CHECK_EQ(hex_of("The quick brown fox jumps over the lazy dog"),
             "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
    CHECK_EQ(hex_of("The quick brown fox jumps over the lazy dog."),
             "ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c");
    CHECK_EQ(hex_of("jacob"), "c35d6aaf3b3885dfc9f36cddf48a65e93a919e13165fbbcfc0f9de5636279559");
    CHECK_EQ(hex_of("jacob1"), "6740b27c9625034bdace8ed62215b93bd5994b65bfd2052025422569027506a4");
}

// ---- The incremental interface ----

TEST_CASE(streaming_matches_one_shot_at_every_split_point) {
    std::string message;
    for (int i = 0; i < 300; ++i) message += static_cast<char>('A' + (i * 7) % 26);
    const Digest whole = sha256(message);
    const std::string_view view(message);
    for (std::size_t split = 0; split <= message.size(); ++split) {
        Sha256 hasher;
        hasher.update(view.substr(0, split));
        hasher.update(view.substr(split));
        CHECK(hasher.finish() == whole);
    }
    Sha256 byte_at_a_time;
    for (const char c : message) byte_at_a_time.update(&c, 1);
    CHECK(byte_at_a_time.finish() == whole);
}

TEST_CASE(digest_prints_as_64_lowercase_hex_digits) {
    const std::string hex = hex_of("anything");
    CHECK_EQ(hex.size(), 64);
    CHECK(hex.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST_CASE(misuse_is_reported) {
    Sha256 hasher;
    hasher.update("abc");
    hasher.finish();
    CHECK_THROWS(hasher.finish());
    CHECK_THROWS(hasher.update("more"));
}

// ---- "Nothing up my sleeve": K and H(0) come from the cube and square roots of the first primes ----

TEST_CASE(constants_come_from_prime_roots) {
    std::vector<int> primes;
    for (int n = 2; primes.size() < 64; ++n) {
        bool is_prime = true;
        for (const int p : primes) {
            if (p * p > n) break;
            if (n % p == 0) {
                is_prime = false;
                break;
            }
        }
        if (is_prime) primes.push_back(n);
    }
    const auto first_32_fraction_bits = [](long double root) {
        return static_cast<std::uint32_t>(std::floor((root - std::floor(root)) * 4294967296.0L));
    };
    for (std::size_t i = 0; i < 64; ++i) {
        CHECK_EQ(kRoundConstants[i], first_32_fraction_bits(std::cbrt(static_cast<long double>(primes[i]))));
    }
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK_EQ(kInitialHash[i], first_32_fraction_bits(std::sqrt(static_cast<long double>(primes[i]))));
    }
}
