// test_extras.cpp - the toy hash, password storage, proof of work, hash art, the avalanche sweep and
// WAV export. Expected HMAC and PBKDF2 values come from RFC 4231 and Python's hashlib.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "avalanche.h"
#include "extras.h"
#include "mapper.h"
#include "password.h"
#include "sha256.h"
#include "test_framework.h"
#include "toy_hash.h"

using namespace hashmusic;

namespace {

std::size_t count_of(const std::string& text, const std::string& piece) {
    std::size_t count = 0;
    for (std::size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at + 1)) ++count;
    return count;
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t{b[at]} | (std::uint32_t{b[at + 1]} << 8) | (std::uint32_t{b[at + 2]} << 16) |
           (std::uint32_t{b[at + 3]} << 24);
}

}  // namespace

// ---- The toy hash ----

TEST_CASE(toy_hash_shows_short_input_straight_through) {
    CHECK_EQ(to_hex(toy_hash("jacob")), "6a61636f62000000000000000000000000000000000000000000000000000000");
}

TEST_CASE(toy_hash_has_no_avalanche) {
    // "jacob1" only adds the byte '1' (0x31, three 1 bits): three output bits change, not ~128.
    CHECK_EQ(count_bit_differences(toy_hash("jacob"), toy_hash("jacob1")), 3);
    const std::vector<int> sweep = avalanche_sweep("Bubble Sort Bandits", HashKind::kToy);
    CHECK_EQ(sweep.size(), 19);
    CHECK(std::all_of(sweep.begin(), sweep.end(), [](int bits) { return bits == 1; }));
}

TEST_CASE(toy_collision_is_instant_but_sha256_still_differs) {
    const auto pair = make_toy_collision("Bubble Sort Bandits");
    CHECK(pair.first != pair.second);
    CHECK_EQ(pair.first.size(), 33);
    CHECK(toy_hash(pair.first) == toy_hash(pair.second));
    CHECK(count_bit_differences(sha256(pair.first), sha256(pair.second)) > 64);
    const auto same_ends = make_toy_collision(std::string(40, 'x'));  // characters 0 and 32 start out equal
    CHECK(same_ends.first != same_ends.second);
    CHECK(toy_hash(same_ends.first) == toy_hash(same_ends.second));
}

TEST_CASE(hash_choice_and_streaming) {
    CHECK(hash_text(HashKind::kSha256, "abc") == sha256("abc"));
    CHECK(parse_hash_kind("sha256") == HashKind::kSha256);
    CHECK(parse_hash_kind("toy") == HashKind::kToy);
    CHECK_THROWS(parse_hash_kind("md5"));
    const std::string message(100, 'q');
    for (std::size_t split = 0; split <= message.size(); split += 7) {
        AnyHasher hasher(HashKind::kToy);
        hasher.update(message.data(), split);
        hasher.update(message.data() + split, message.size() - split);
        CHECK(hasher.finish() == toy_hash(message));
    }
}

// ---- HMAC and PBKDF2 ----

TEST_CASE(hmac_sha256_rfc4231_vectors) {
    const std::string data = "Hi There";
    CHECK_EQ(to_hex(hmac_sha256(std::string(20, '\x0b'), reinterpret_cast<const std::uint8_t*>(data.data()), data.size())),
             "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    const std::string long_key_data = "Test Using Larger Than Block-Size Key - Hash Key First";
    CHECK_EQ(to_hex(hmac_sha256(std::string(131, '\xaa'), reinterpret_cast<const std::uint8_t*>(long_key_data.data()),
                                long_key_data.size())),
             "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST_CASE(pbkdf2_sha256_matches_known_values) {
    const std::vector<std::uint8_t> salt = {'s', 'a', 'l', 't'};
    CHECK_EQ(to_hex(pbkdf2_sha256("password", salt, 1)), "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK_EQ(to_hex(pbkdf2_sha256("password", salt, 2)), "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK_EQ(to_hex(pbkdf2_sha256("password", salt, 4096)),
             "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    const std::string long_salt = "saltSALTsaltSALTsaltSALTsaltSALTsalt";
    CHECK_EQ(to_hex(pbkdf2_sha256("passwordPASSWORDpassword", std::vector<std::uint8_t>(long_salt.begin(), long_salt.end()), 4096)),
             "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1");
    CHECK_EQ(to_hex(pbkdf2_sha256("hunter2", from_hex("000102030405060708090a0b0c0d0e0f"), 1000)),
             "f5550e89119f593cd3662c6d7da5bd3f7a90e25cd2dc221f58a6166c772753d3");
    CHECK_THROWS(pbkdf2_sha256("x", salt, 0));
}

TEST_CASE(password_records_round_trip_and_verify) {
    const std::vector<std::uint8_t> salt = from_hex("000102030405060708090a0b0c0d0e0f");
    const PasswordRecord record = make_record("hunter2", salt, 1000);
    const std::string text = format_record(record);
    CHECK_EQ(text, "pbkdf2-sha256$1000$000102030405060708090a0b0c0d0e0f$"
                   "f5550e89119f593cd3662c6d7da5bd3f7a90e25cd2dc221f58a6166c772753d3");
    const PasswordRecord parsed = parse_record(text);
    CHECK(verify_password("hunter2", parsed));
    CHECK(!verify_password("hunter3", parsed));
    CHECK(!verify_password("", parsed));
    CHECK_THROWS(parse_record("sha256$1000$00$00"));
    CHECK_THROWS(parse_record("pbkdf2-sha256$0$00$" + to_hex(record.key)));
    CHECK_THROWS(parse_record("pbkdf2-sha256$1000$zz$" + to_hex(record.key)));
    CHECK_THROWS(parse_record("pbkdf2-sha256$1000$00$abcd"));
}

TEST_CASE(same_password_different_salt_stores_differently) {
    const PasswordRecord a = make_record("correct horse", random_salt(), 100);
    const PasswordRecord b = make_record("correct horse", random_salt(), 100);
    CHECK(a.salt != b.salt);
    CHECK(!(a.key == b.key));
    CHECK(count_bit_differences(a.key, b.key) > 64);
    CHECK(verify_password("correct horse", a));
    CHECK(verify_password("correct horse", b));
}

TEST_CASE(store_locks_out_repeated_wrong_guesses) {
    PasswordStore store;
    store.add_user("alice", make_record("s3cret", random_salt(), 50));
    CHECK_THROWS(store.add_user("alice", make_record("other", random_salt(), 50)));
    CHECK_THROWS(store.add_user("bad name", make_record("x", random_salt(), 50)));
    using R = PasswordStore::Result;
    long long now = 1000;

    CHECK(store.login("alice", "s3cret", now).result == R::kOk);
    CHECK(store.login("bob", "s3cret", now).result == R::kNoSuchUser);
    CHECK(store.login("alice", "guess1", now).result == R::kWrongPassword);
    CHECK_EQ(store.login("alice", "guess2", now).wait_seconds, 0);
    const PasswordStore::Attempt third = store.login("alice", "guess3", now);  // the third wrong guess locks
    CHECK(third.result == R::kWrongPassword);
    CHECK_EQ(third.failures, 3);
    CHECK_EQ(third.wait_seconds, PasswordStore::kBaseLockSeconds);
    const PasswordStore::Attempt locked = store.login("alice", "s3cret", now + 10);  // even the right password waits
    CHECK(locked.result == R::kLocked);
    CHECK_EQ(locked.wait_seconds, PasswordStore::kBaseLockSeconds - 10);

    now += PasswordStore::kBaseLockSeconds;
    CHECK_EQ(store.login("alice", "guess4", now).wait_seconds, 2 * PasswordStore::kBaseLockSeconds);  // doubles
    now += 2 * PasswordStore::kBaseLockSeconds;
    CHECK(store.login("alice", "s3cret", now).result == R::kOk);  // success resets the count
    CHECK_EQ(store.login("alice", "guess5", now).failures, 1);
}

TEST_CASE(store_survives_a_save_and_load) {
    const std::string path = "test_extras_users.txt";
    {
        PasswordStore store;
        store.add_user("carol", make_record("pa55", random_salt(), 50));
        store.login("carol", "wrong", 5000);
        store.save(path);
    }
    PasswordStore reloaded;
    reloaded.load(path);
    CHECK(reloaded.has_user("carol"));
    const PasswordStore::Attempt attempt = reloaded.login("carol", "pa55", 5000);
    CHECK(attempt.result == PasswordStore::Result::kOk);
    std::remove(path.c_str());
    PasswordStore missing;
    missing.load("no_such_store_file.txt");  // a missing file is an empty store
    CHECK(!missing.has_user("carol"));
}

TEST_CASE(usernames_are_checked) {
    CHECK(PasswordStore::valid_username("alice"));
    CHECK(PasswordStore::valid_username("Bubble_Sort.Bandits-1"));
    CHECK(!PasswordStore::valid_username(""));
    CHECK(!PasswordStore::valid_username("two words"));
    CHECK(!PasswordStore::valid_username(std::string(33, 'a')));
}

// ---- Proof of work ----

TEST_CASE(leading_zero_hex_digits_counts_nibbles) {
    Digest d{};
    CHECK_EQ(leading_zero_hex_digits(d), 64);
    d[0] = 0x10;
    CHECK_EQ(leading_zero_hex_digits(d), 0);
    d[0] = 0x0f;
    CHECK_EQ(leading_zero_hex_digits(d), 1);
    d[0] = 0x00;
    d[1] = 0x01;
    CHECK_EQ(leading_zero_hex_digits(d), 3);
}

TEST_CASE(mining_finds_a_checkable_answer) {
    const MineResult result = mine("jacob", 3, 1000000);
    CHECK(result.found);
    CHECK(leading_zero_hex_digits(result.digest) >= 3);
    CHECK_EQ(result.input, "jacob#" + std::to_string(result.nonce));
    CHECK(sha256(result.input) == result.digest);  // checking takes one hash
    CHECK_EQ(result.attempts, result.nonce + 1);
    CHECK_EQ(mine("jacob", 3, 1000000).nonce, result.nonce);  // deterministic
    CHECK_EQ(note_name(kPitchTable[result.digest[0] >> 4]), "G3");  // the work shows in the song
    CHECK_EQ(mine("jacob", 0, 10).nonce, 0);
    CHECK(!mine("jacob", 8, 10).found);
}

// ---- Hash art ----

TEST_CASE(hash_art_is_deterministic_and_mirrored) {
    const Digest d = sha256("jacob");
    const std::string art = hash_art_svg(d);
    CHECK(art.rfind("<svg", 0) == 0);
    CHECK(art == hash_art_svg(d));
    std::size_t on = 0;
    for (std::size_t i = 0; i < 32; ++i) on += (d[i] & 0x80) ? 1 : 0;
    CHECK_EQ(count_of(art, "<rect"), 1 + 2 * on);  // background + each cell and its mirror image
    CHECK(art != hash_art_svg(sha256("jacob1")));
}

// ---- Avalanche sweep ----

TEST_CASE(sha256_avalanches_at_every_position) {
    const std::string text = "The quick brown fox jumps over the lazy dog";
    const std::vector<int> sweep = avalanche_sweep(text, HashKind::kSha256);
    CHECK_EQ(sweep.size(), text.size());
    int total = 0;
    for (const int bits : sweep) {
        CHECK(bits > 90 && bits < 166);  // every single position, not just on average
        total += bits;
    }
    const double average = static_cast<double>(total) / static_cast<double>(sweep.size());
    CHECK(average > 120 && average < 136);
}

// ---- WAV export ----

TEST_CASE(wav_file_is_well_formed) {
    const Song song = map_digest(sha256("jacob"));
    const std::vector<std::uint8_t> wav = render_wav(song, 22050);
    CHECK(wav.size() > 44);
    CHECK(std::string(wav.begin(), wav.begin() + 4) == "RIFF");
    CHECK(std::string(wav.begin() + 8, wav.begin() + 16) == "WAVEfmt ");
    CHECK(std::string(wav.begin() + 36, wav.begin() + 40) == "data");
    CHECK_EQ(read_le32(wav, 4), wav.size() - 8);
    CHECK_EQ(read_le32(wav, 24), 22050);
    CHECK_EQ(read_le32(wav, 40), wav.size() - 44);
    const double seconds = song.length_ticks / 480.0 * 60.0 / song.bpm + 1.0;
    CHECK(std::abs(static_cast<double>(read_le32(wav, 40)) / 2.0 / 22050.0 - seconds) < 0.01);
    bool sound = false;
    for (std::size_t i = 44; i + 1 < wav.size() && !sound; i += 2) sound = wav[i] != 0 || wav[i + 1] != 0;
    CHECK(sound);
    CHECK(render_wav(song, 22050) == wav);
}
