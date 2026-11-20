// extras.cpp - proof of work, hash art, the avalanche sweep and WAV rendering.
#include "extras.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "avalanche.h"

namespace hashmusic {

int leading_zero_hex_digits(const Digest& digest) {
    int zeros = 0;
    for (const std::uint8_t byte : digest) {
        if (byte >> 4) return zeros;
        ++zeros;
        if (byte & 0x0F) return zeros;
        ++zeros;
    }
    return zeros;
}

MineResult mine(std::string_view text, int zeros, std::uint64_t max_attempts) {
    if (zeros < 0 || zeros > 64) throw std::invalid_argument("difficulty must be 0-64 zero digits");
    MineResult result;
    const std::string prefix = std::string(text) + "#";
    for (std::uint64_t nonce = 0; nonce < max_attempts; ++nonce) {
        const std::string candidate = prefix + std::to_string(nonce);
        const Digest digest = sha256(candidate);
        ++result.attempts;
        if (leading_zero_hex_digits(digest) >= zeros) {
            result.found = true;
            result.nonce = nonce;
            result.input = candidate;
            result.digest = digest;
            return result;
        }
    }
    return result;
}

std::string hash_art_svg(const Digest& digest, int size) {
    const int cell = size / 10;
    const int margin = (size - 8 * cell) / 2;
    const int hue = digest[31] * 360 / 256;
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << size << "\" height=\"" << size << "\" viewBox=\"0 0 "
        << size << ' ' << size << "\">\n";
    svg << "<rect width=\"" << size << "\" height=\"" << size << "\" rx=\"" << cell / 2 << "\" fill=\"hsl(" << hue
        << ", 30%, 95%)\"/>\n";
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 4; ++col) {
            const std::uint8_t byte = digest[static_cast<std::size_t>(row * 4 + col)];
            if (!(byte & 0x80)) continue;
            const int lightness = (byte & 0x40) ? 32 : 55;
            for (const int x : {col, 7 - col}) {  // mirror, so it looks like a face or a crest
                svg << "<rect x=\"" << margin + x * cell << "\" y=\"" << margin + row * cell << "\" width=\"" << cell
                    << "\" height=\"" << cell << "\" fill=\"hsl(" << hue << ", 65%, " << lightness << "%)\"/>\n";
            }
        }
    }
    svg << "</svg>\n";
    return svg.str();
}

std::vector<int> avalanche_sweep(std::string_view text, HashKind kind) {
    const Digest original = hash_text(kind, text);
    std::vector<int> flips;
    std::string changed(text);
    for (std::size_t i = 0; i < changed.size(); ++i) {
        changed[i] = static_cast<char>(changed[i] ^ 1);
        flips.push_back(count_bit_differences(original, hash_text(kind, changed)));
        changed[i] = static_cast<char>(changed[i] ^ 1);
    }
    return flips;
}

namespace {

void put_le(std::vector<std::uint8_t>& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
}

constexpr double kPi = 3.14159265358979323846;

}  // namespace

std::vector<std::uint8_t> render_wav(const Song& song, std::uint32_t sample_rate) {
    if (song.bpm == 0 || sample_rate < 8000) throw std::invalid_argument("bad tempo or sample rate");
    const double seconds_per_tick = 60.0 / (static_cast<double>(song.bpm) * kTicksPerBeat);
    const auto total = static_cast<std::size_t>((song.length_ticks * seconds_per_tick + 1.0) * sample_rate);
    std::vector<double> mix(total, 0.0);
    std::uint32_t noise = 0x2710;  // a fixed seed, so the same song always renders the same noise
    const auto next_noise = [&noise]() {
        noise = noise * 1664525u + 1013904223u;
        return static_cast<double>(noise >> 8) / 8388608.0 - 1.0;
    };

    for (const Track& track : song.tracks) {
        const bool drums = track.channel == kDrumChannel;
        const double level = drums ? 0.5 : track.name == "Melody" ? 0.30 : track.name == "Bass" ? 0.28 : 0.10;
        for (const Note& note : track.notes) {
            const double start = note.start * seconds_per_tick;
            const double length = note.duration * seconds_per_tick;
            const double velocity = note.velocity / 127.0;
            const auto first = static_cast<std::size_t>(start * sample_rate);
            if (!drums) {
                const double frequency = 440.0 * std::pow(2.0, (note.pitch - 69) / 12.0);
                const double release = 0.08;
                const auto count = static_cast<std::size_t>((length + release) * sample_rate);
                for (std::size_t i = 0; i < count && first + i < total; ++i) {
                    const double t = static_cast<double>(i) / sample_rate;
                    double envelope = std::min(1.0, t / 0.005) * std::exp(-2.5 * t);  // quick attack, slow decay
                    if (t > length) envelope *= 1.0 - (t - length) / release;
                    const double phase = 2 * kPi * frequency * t;
                    const double wave = std::sin(phase) + 0.4 * std::sin(2 * phase) + 0.15 * std::sin(3 * phase);
                    mix[first + i] += level * velocity * envelope * wave;
                }
            } else {
                const bool kick = note.pitch == 36;
                const double decay = kick ? 0.25 : note.pitch == 49 ? 0.9 : note.pitch == 38 ? 0.15 : 0.05;
                const auto count = static_cast<std::size_t>(decay * 3 * sample_rate);
                for (std::size_t i = 0; i < count && first + i < total; ++i) {
                    const double t = static_cast<double>(i) / sample_rate;
                    const double envelope = std::exp(-t / decay);
                    const double sound = kick ? std::sin(2 * kPi * (50.0 * t + 40.0 * (1 - std::exp(-t * 30)))) : next_noise();
                    mix[first + i] += level * velocity * envelope * sound * (kick ? 1.0 : 0.5);
                }
            }
        }
    }

    double peak = 0;
    for (const double s : mix) peak = std::max(peak, std::abs(s));
    const double gain = peak > 0.9 ? 0.9 / peak : 1.0;

    std::vector<std::uint8_t> wav;
    const auto data_bytes = static_cast<std::uint32_t>(total * 2);
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    put_le(wav, 36 + data_bytes, 4);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put_le(wav, 16, 4);               // size of the format block
    put_le(wav, 1, 2);                // PCM
    put_le(wav, 1, 2);                // mono
    put_le(wav, sample_rate, 4);
    put_le(wav, sample_rate * 2, 4);  // bytes per second
    put_le(wav, 2, 2);                // bytes per sample
    put_le(wav, 16, 2);               // bits per sample
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    put_le(wav, data_bytes, 4);
    for (const double s : mix) {
        const auto sample = static_cast<std::int16_t>(std::lround(std::clamp(s * gain, -1.0, 1.0) * 32767));
        put_le(wav, static_cast<std::uint16_t>(sample), 2);
    }
    return wav;
}

}  // namespace hashmusic
