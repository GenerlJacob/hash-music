// stats.h - extra goal from the proposal: "graphs of pitch and byte frequencies from randomly
// generated inputs to show the output is uniformly distributed". Switched on with --stats.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hashmusic {

struct Distribution {
    std::uint64_t inputs = 0;  // how many random inputs were hashed
    std::uint64_t seed = 0;
    std::array<std::uint64_t, 256> byte_counts{};   // every digest byte (32 per input)
    std::array<std::uint64_t, 16> pitch_counts{};   // first hex digit of bytes 0-23: the melody pitches
    std::array<std::uint64_t, 16> rhythm_counts{};  // second hex digit of bytes 0-23: the rhythms
};

// Hashes `inputs` random lowercase strings of 8-24 letters and tallies the digests. The strings come
// from std::mt19937_64, whose output the C++ standard fixes, so a seed gives the same counts everywhere.
Distribution measure_distribution(std::uint64_t inputs, std::uint64_t seed);

// Pearson's chi-square statistic of the counts against a uniform distribution. For uniform data it
// lands near (bins - 1), give or take sqrt(2 * (bins - 1)); far above that means some bins are favored.
template <std::size_t Bins>
double chi_square(const std::array<std::uint64_t, Bins>& counts) {
    double total = 0;
    for (const std::uint64_t count : counts) total += static_cast<double>(count);
    const double expected = total / static_cast<double>(Bins);
    if (expected <= 0) return 0;
    double statistic = 0;
    for (const std::uint64_t count : counts) {
        const double difference = static_cast<double>(count) - expected;
        statistic += difference * difference / expected;
    }
    return statistic;
}

std::string distribution_text(const Distribution& d);  // terminal summary with an ASCII histogram
std::string distribution_html(const Distribution& d);  // standalone page: SVG charts and table views
std::string distribution_csv(const Distribution& d);   // raw counts for a spreadsheet

}  // namespace hashmusic
