// toy_hash.cpp - the deliberately bad XOR-fold hash, and the switch between it and SHA-256.
#include "toy_hash.h"

#include <stdexcept>

namespace hashmusic {

HashKind parse_hash_kind(std::string_view name) {
    if (name == "sha256" || name == "sha-256" || name == "SHA-256") return HashKind::kSha256;
    if (name == "toy") return HashKind::kToy;
    throw std::invalid_argument("unknown hash '" + std::string(name) + "' (use sha256 or toy)");
}

const char* hash_display_name(HashKind kind) { return kind == HashKind::kToy ? "toy XOR hash" : "SHA-256"; }

void ToyHash::update(const void* data, std::size_t length) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < length; ++i) {
        digest_[position_ % kDigestBytes] = static_cast<std::uint8_t>(digest_[position_ % kDigestBytes] ^ bytes[i]);
        ++position_;
    }
}

Digest toy_hash(std::string_view text) {
    ToyHash hasher;
    hasher.update(text.data(), text.size());
    return hasher.finish();
}

void AnyHasher::update(const void* data, std::size_t length) {
    if (kind_ == HashKind::kToy) {
        toy_.update(data, length);
    } else {
        sha_.update(data, length);
    }
}

Digest AnyHasher::finish() { return kind_ == HashKind::kToy ? toy_.finish() : sha_.finish(); }

Digest hash_text(HashKind kind, std::string_view text) {
    AnyHasher hasher(kind);
    hasher.update(text.data(), text.size());
    return hasher.finish();
}

std::pair<std::string, std::string> make_toy_collision(std::string text) {
    if (text.size() < 33) text.resize(33, '.');
    if (text[0] == text[32]) text[32] = text[0] == '!' ? '?' : '!';
    std::string partner = text;
    std::swap(partner[0], partner[32]);
    return {text, partner};
}

}  // namespace hashmusic
