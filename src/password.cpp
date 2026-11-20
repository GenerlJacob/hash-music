// password.cpp - HMAC-SHA256, PBKDF2 and a lockout-protected password store, all on our own SHA-256.
#include "password.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace hashmusic {
namespace {

// HMAC hashes (key XOR ipad) || message, then (key XOR opad) || that. Both key blocks are fixed for a
// given password, so we hash them once and copy the half-finished hashers for every PBKDF2 round.
struct HmacKey {
    Sha256 inner;
    Sha256 outer;
};

HmacKey prepare_key(std::string_view key) {
    std::array<std::uint8_t, 64> block{};
    if (key.size() > block.size()) {
        const Digest shortened = sha256(key);  // keys longer than a block are hashed first
        std::copy(shortened.begin(), shortened.end(), block.begin());
    } else {
        std::copy(key.begin(), key.end(), block.begin());
    }
    std::array<std::uint8_t, 64> ipad{};
    std::array<std::uint8_t, 64> opad{};
    for (std::size_t i = 0; i < block.size(); ++i) {
        ipad[i] = static_cast<std::uint8_t>(block[i] ^ 0x36);
        opad[i] = static_cast<std::uint8_t>(block[i] ^ 0x5c);
    }
    HmacKey prepared;
    prepared.inner.update(ipad.data(), ipad.size());
    prepared.outer.update(opad.data(), opad.size());
    return prepared;
}

Digest hmac_with(const HmacKey& key, const std::uint8_t* message, std::size_t length) {
    Sha256 inner = key.inner;
    inner.update(message, length);
    const Digest inner_digest = inner.finish();
    Sha256 outer = key.outer;
    outer.update(inner_digest.data(), inner_digest.size());
    return outer.finish();
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

Digest hmac_sha256(std::string_view key, const std::uint8_t* message, std::size_t length) {
    return hmac_with(prepare_key(key), message, length);
}

Digest pbkdf2_sha256(std::string_view password, const std::vector<std::uint8_t>& salt, std::uint32_t iterations) {
    if (iterations == 0) throw std::invalid_argument("PBKDF2 needs at least one iteration");
    const HmacKey key = prepare_key(password);
    // U1 = HMAC(password, salt || block number 1); each later U is HMAC(password, previous U).
    std::vector<std::uint8_t> first(salt);
    first.insert(first.end(), {0, 0, 0, 1});
    Digest u = hmac_with(key, first.data(), first.size());
    Digest result = u;
    for (std::uint32_t i = 1; i < iterations; ++i) {
        u = hmac_with(key, u.data(), u.size());
        for (std::size_t b = 0; b < result.size(); ++b) result[b] = static_cast<std::uint8_t>(result[b] ^ u[b]);
    }
    return result;
}

std::vector<std::uint8_t> random_salt(std::size_t bytes) {
    std::random_device device;  // the operating system's random source
    std::vector<std::uint8_t> salt(bytes);
    for (std::uint8_t& b : salt) b = static_cast<std::uint8_t>(device() & 0xFF);
    return salt;
}

std::vector<std::uint8_t> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) throw std::invalid_argument("hex text must have an even number of digits");
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = hex_value(hex[i]);
        const int low = hex_value(hex[i + 1]);
        if (high < 0 || low < 0) throw std::invalid_argument("not a hex digit in '" + std::string(hex) + "'");
        bytes.push_back(static_cast<std::uint8_t>(high * 16 + low));
    }
    return bytes;
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out += kDigits[b >> 4];
        out += kDigits[b & 0x0F];
    }
    return out;
}

std::string format_record(const PasswordRecord& record) {
    return "pbkdf2-sha256$" + std::to_string(record.iterations) + "$" + bytes_to_hex(record.salt) + "$" +
           to_hex(record.key);
}

PasswordRecord parse_record(const std::string& text) {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    for (std::string part; std::getline(stream, part, '$');) parts.push_back(part);
    if (parts.size() != 4 || parts[0] != "pbkdf2-sha256") throw std::invalid_argument("not a pbkdf2-sha256 record");
    if (parts[1].empty() || parts[1].size() > 8 || parts[1].find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("bad iteration count in password record");
    }
    PasswordRecord record;
    record.iterations = static_cast<std::uint32_t>(std::stoul(parts[1]));
    if (record.iterations == 0) throw std::invalid_argument("bad iteration count in password record");
    record.salt = from_hex(parts[2]);
    const std::vector<std::uint8_t> key = from_hex(parts[3]);
    if (key.size() != kDigestBytes) throw std::invalid_argument("password record key must be 32 bytes");
    std::copy(key.begin(), key.end(), record.key.begin());
    return record;
}

PasswordRecord make_record(std::string_view password, const std::vector<std::uint8_t>& salt, std::uint32_t iterations) {
    PasswordRecord record;
    record.iterations = iterations;
    record.salt = salt;
    record.key = pbkdf2_sha256(password, salt, iterations);
    return record;
}

bool verify_password(std::string_view password, const PasswordRecord& record) {
    const Digest attempt = pbkdf2_sha256(password, record.salt, record.iterations);
    unsigned difference = 0;
    for (std::size_t i = 0; i < attempt.size(); ++i) difference |= static_cast<unsigned>(attempt[i] ^ record.key[i]);
    return difference == 0;
}

bool PasswordStore::valid_username(std::string_view user) {
    if (user.empty() || user.size() > 32) return false;
    return std::all_of(user.begin(), user.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

void PasswordStore::add_user(const std::string& user, const PasswordRecord& record) {
    if (!valid_username(user)) throw std::invalid_argument("user names are 1-32 letters, digits, '.', '_' or '-'");
    if (has_user(user)) throw std::invalid_argument("user '" + user + "' already exists");
    users_[user].record = format_record(record);
}

PasswordStore::Attempt PasswordStore::login(const std::string& user, std::string_view password, long long now) {
    Attempt attempt;
    const auto found = users_.find(user);
    if (found == users_.end()) return attempt;  // kNoSuchUser
    Entry& entry = found->second;
    attempt.failures = entry.failures;
    if (now < entry.locked_until) {  // locked: the password is not even checked
        attempt.result = Result::kLocked;
        attempt.wait_seconds = entry.locked_until - now;
        return attempt;
    }
    if (verify_password(password, parse_record(entry.record))) {
        entry.failures = 0;
        entry.locked_until = 0;
        attempt.result = Result::kOk;
        attempt.failures = 0;
        return attempt;
    }
    ++entry.failures;
    if (entry.failures >= kFreeAttempts) {
        const int doublings = std::min(entry.failures - kFreeAttempts, 10);
        entry.locked_until = now + (kBaseLockSeconds << doublings);
    }
    attempt.result = Result::kWrongPassword;
    attempt.failures = entry.failures;
    attempt.wait_seconds = std::max(0LL, entry.locked_until - now);
    return attempt;
}

void PasswordStore::load(const std::string& path) {
    users_.clear();
    std::ifstream in(std::filesystem::u8path(path));
    if (!in) return;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string user;
        Entry entry;
        if (!(fields >> user >> entry.record >> entry.failures >> entry.locked_until) || !valid_username(user)) {
            throw std::runtime_error("bad line in password store '" + path + "'");
        }
        parse_record(entry.record);  // reject a damaged record now, not at login
        users_[user] = entry;
    }
}

void PasswordStore::save(const std::string& path) const {
    std::ofstream out(std::filesystem::u8path(path), std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write password store '" + path + "'");
    out << "# hashmusic password store: user, pbkdf2-sha256 record, failed attempts, locked until (unix time)\n";
    for (const auto& [user, entry] : users_) {
        out << user << ' ' << entry.record << ' ' << entry.failures << ' ' << entry.locked_until << '\n';
    }
    if (!out) throw std::runtime_error("failed while writing '" + path + "'");
}

}  // namespace hashmusic
