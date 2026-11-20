// password.h - salted password storage on top of our SHA-256 (project idea #1 from the course list).
//
// Passwords are never stored. Each user gets a random salt, and the store keeps
//     pbkdf2-sha256$<iterations>$<salt hex>$<derived key hex>
// PBKDF2 repeats HMAC-SHA256 hundreds of thousands of times, so each guess costs an attacker as much
// as it costs us once per login. The salt makes equal passwords store differently, which defeats
// precomputed "rainbow tables". Repeated wrong guesses lock the account for longer and longer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "sha256.h"

namespace hashmusic {

// OWASP's 2023 recommendation for PBKDF2-HMAC-SHA256.
constexpr std::uint32_t kDefaultIterations = 600000;

// HMAC-SHA256 (RFC 2104): a keyed hash, the building block of PBKDF2.
Digest hmac_sha256(std::string_view key, const std::uint8_t* message, std::size_t length);

// PBKDF2-HMAC-SHA256 (RFC 8018) with a 32-byte output.
Digest pbkdf2_sha256(std::string_view password, const std::vector<std::uint8_t>& salt, std::uint32_t iterations);

std::vector<std::uint8_t> random_salt(std::size_t bytes = 16);
std::vector<std::uint8_t> from_hex(std::string_view hex);  // throws std::invalid_argument
std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);

struct PasswordRecord {
    std::uint32_t iterations = 0;
    std::vector<std::uint8_t> salt;
    Digest key{};
};

std::string format_record(const PasswordRecord& record);
PasswordRecord parse_record(const std::string& text);  // throws std::invalid_argument
PasswordRecord make_record(std::string_view password, const std::vector<std::uint8_t>& salt,
                           std::uint32_t iterations = kDefaultIterations);

// Recomputes the key and compares in constant time, so timing does not reveal how close a guess was.
bool verify_password(std::string_view password, const PasswordRecord& record);

// A tiny user database with brute-force protection, saved as a text file.
class PasswordStore {
public:
    static constexpr int kFreeAttempts = 3;            // wrong guesses allowed before a lockout
    static constexpr long long kBaseLockSeconds = 30;  // doubles with every further wrong guess

    enum class Result { kOk, kWrongPassword, kNoSuchUser, kLocked };
    struct Attempt {
        Result result = Result::kNoSuchUser;
        int failures = 0;             // consecutive wrong guesses so far
        long long wait_seconds = 0;   // how long until the next guess is allowed (0 = now)
    };

    static bool valid_username(std::string_view user);  // 1-32 of A-Z a-z 0-9 . _ -

    void add_user(const std::string& user, const PasswordRecord& record);  // throws if taken or invalid
    Attempt login(const std::string& user, std::string_view password, long long now);
    bool has_user(const std::string& user) const { return users_.count(user) != 0; }

    void load(const std::string& path);  // a missing file is an empty store
    void save(const std::string& path) const;

private:
    struct Entry {
        std::string record;
        int failures = 0;
        long long locked_until = 0;
    };
    std::map<std::string, Entry> users_;
};

}  // namespace hashmusic
