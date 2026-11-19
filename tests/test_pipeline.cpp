// test_pipeline.cpp - end-to-end tests across modules (Member D), the avalanche demo (Member A),
// and the extras from the proposal (--band and --stats).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "avalanche.h"
#include "band.h"
#include "mapper.h"
#include "midi_writer.h"
#include "sha256.h"
#include "stats.h"
#include "test_framework.h"

using namespace hashmusic;

namespace {

// The same three calls main() makes: hash, map, write.
std::vector<std::uint8_t> song_file(std::string_view text, bool band = false) {
    const Digest digest = sha256(text);
    Song song = map_digest(digest);
    if (band) add_band(song, digest);
    return write_midi(song);
}

}  // namespace

// ---- Determinism ----

TEST_CASE(same_input_gives_byte_identical_midi) {
    for (const char* text : {"jacob", "", "The quick brown fox jumps over the lazy dog"}) {
        CHECK(song_file(text) == song_file(text));
        CHECK(song_file(text, true) == song_file(text, true));
    }
}

TEST_CASE(one_character_change_gives_a_different_file) {
    CHECK(song_file("jacob") != song_file("jacob1"));
    CHECK(song_file("jacob", true) != song_file("jacob1", true));
}

// Pinned fingerprints of the demo songs, so every machine and compiler must produce the same bytes.
// They change only if the mapper, band or MIDI writer changes. If you changed one on purpose, update
// these values and tell the group: the demo songs change too.
TEST_CASE(demo_song_fingerprints_are_pinned) {
    CHECK_EQ(to_hex(sha256(song_file("jacob"))), "d5033c0e1d2b157b08373fa4c5668d70d04cdc1533883c23a0f065e61955ba6a");
    CHECK_EQ(to_hex(sha256(song_file("jacob", true))),
             "61229de5adb326b54ec7f452cc96f5c2286fdd4c45dcafa787e20a766047a2e2");
}

// ---- Avalanche demo (stretch goal) ----

TEST_CASE(avalanche_jacob_vs_jacob1) {
    const Digest a = sha256("jacob");
    const Digest b = sha256("jacob1");
    CHECK_EQ(count_bit_differences(a, b), 146);  // checked with Python: bin(int(a) ^ int(b)).count("1")
    CHECK_EQ(count_bit_differences(a, a), 0);
    CHECK_EQ(count_hex_differences(a, b), 62);

    const std::string grid = bit_difference_grid(a, b);
    CHECK_EQ(std::count(grid.begin(), grid.end(), '#'), 146);
    CHECK_EQ(std::count(grid.begin(), grid.end(), '\n'), 8);
    CHECK_EQ(grid.size(), 8 * (4 * 8 + 3 + 1));  // 4 bytes of 8 bits, 3 spaces, newline

    const std::string marker = hex_difference_marker(a, b);
    CHECK_EQ(marker.size(), 64);
    CHECK_EQ(std::count(marker.begin(), marker.end(), '^'), 62);
}

TEST_CASE(single_bit_flips_change_about_half_the_output_bits) {
    int total = 0;
    const int trials = 256;
    for (int i = 0; i < trials; ++i) {
        std::string message = "avalanche test message " + std::to_string(i);
        const Digest before = sha256(message);
        const std::size_t position = static_cast<std::size_t>(i) % message.size();
        message[position] = static_cast<char>(message[position] ^ (1 << (i % 8)));
        total += count_bit_differences(before, sha256(message));
    }
    const double average = static_cast<double>(total) / trials;
    CHECK(average > 124 && average < 132);  // ideal: 128, standard error 8 / sqrt(256) = 0.5
}

TEST_CASE(edit_distance_examples) {
    CHECK_EQ(edit_distance("jacob", "jacob1"), 1);
    CHECK_EQ(edit_distance("jacob", "jacob"), 0);
    CHECK_EQ(edit_distance("", "abc"), 3);
    CHECK_EQ(edit_distance("kitten", "sitting"), 3);
}

// ---- --band (extra) ----

TEST_CASE(band_choices_read_bytes_26_to_29) {
    Digest d{};
    d[kInstrumentByte] = 0x5b;  // melody 5 = Vibraphone, harmony b = Brass Section
    d[kChordByte] = 0x0a;       // progression 0, harmony pattern a mod 8 = 2
    d[kBassByte] = 0x97;        // bass 9 mod 8 = 1, pattern 7
    d[kDrumByte] = 0x4b;        // groove 4, options b = 1011: ride, fills, tambourine
    const BandChoices c = band_choices(d);
    CHECK_EQ(c.melody_instrument, "Vibraphone");
    CHECK_EQ(c.melody_program, 11);
    CHECK_EQ(c.harmony_instrument, "Brass Section");
    CHECK_EQ(c.harmony_program, 61);
    CHECK_EQ(c.progression_name, "I-V-vi-IV");
    CHECK_EQ(c.harmony_pattern_name, "rising arpeggio");
    CHECK_EQ(c.bass_instrument, "Finger Bass");
    CHECK_EQ(c.bass_program, 33);
    CHECK_EQ(c.bass_pattern_name, "walking");
    CHECK_EQ(c.groove_name, "funk");
    CHECK(c.ride);
    CHECK(c.fills);
    CHECK(!c.clap);
    CHECK(c.tambourine);
}

TEST_CASE(band_adds_three_well_formed_tracks_and_ends_on_c) {
    for (const char* text : {"jacob", "jacob1", "", "band test", "Bubble Sort Bandits"}) {
        const Digest digest = sha256(text);
        Song song = map_digest(digest);
        const Track melody_before = song.tracks[0];
        add_band(song, digest);

        CHECK_EQ(song.tracks.size(), 4);
        CHECK_EQ(song.tracks[1].name, "Harmony");
        CHECK_EQ(song.tracks[2].name, "Bass");
        CHECK_EQ(song.tracks[3].name, "Drums");
        CHECK_EQ(song.tracks[3].channel, kDrumChannel);
        CHECK_EQ(song.tracks[0].program, band_choices(digest).melody_program);
        CHECK(song.tracks[0].notes == melody_before.notes);  // the melody itself is untouched

        for (const Track& track : song.tracks) {
            CHECK(!track.notes.empty());
            for (const Note& note : track.notes) {
                CHECK(note.start + note.duration <= song.length_ticks);
                CHECK(note.duration > 0);
                CHECK(note.pitch <= 127);
                CHECK(note.velocity >= 1 && note.velocity <= 127);
            }
        }

        // The final bar: a C major chord (C, E, G) over a C in the bass.
        const std::uint32_t final_bar = song.length_ticks - kTicksPerBeat - kTicksPerBar;
        std::set<int> last_chord;
        for (const Note& note : song.tracks[1].notes) {
            if (note.start == final_bar) last_chord.insert(note.pitch % 12);
        }
        CHECK(last_chord == std::set<int>({0, 4, 7}));
        CHECK_EQ(song.tracks[2].notes.back().pitch % 12, 0);
        CHECK_EQ(song.tracks[2].notes.back().start, final_bar);

        CHECK_THROWS(add_band(song, digest));  // only a fresh map_digest() song is accepted
    }
}

// ---- --stats (extra) ----

TEST_CASE(distribution_counts_add_up_and_look_uniform) {
    // The same sample `hashmusic --stats` uses by default.
    const Distribution d = measure_distribution(10000, 2710);
    std::uint64_t bytes = 0;
    std::uint64_t pitches = 0;
    std::uint64_t rhythms = 0;
    for (const std::uint64_t c : d.byte_counts) bytes += c;
    for (const std::uint64_t c : d.pitch_counts) pitches += c;
    for (const std::uint64_t c : d.rhythm_counts) rhythms += c;
    CHECK_EQ(bytes, 10000 * 32);
    CHECK_EQ(pitches, 10000 * 24);
    CHECK_EQ(rhythms, 10000 * 24);
    // For uniform data chi-square is about (bins - 1) +/- sqrt(2 (bins - 1)). Within 3 standard deviations
    // is what the tool reports as uniform. (Repeated inputs once pushed the byte statistic to +5.4.)
    CHECK(std::abs(chi_square(d.byte_counts) - 255) < 3 * std::sqrt(510.0));
    CHECK(std::abs(chi_square(d.pitch_counts) - 15) < 3 * std::sqrt(30.0));
    CHECK(measure_distribution(300, 7).byte_counts == measure_distribution(300, 7).byte_counts);
}

TEST_CASE(chi_square_of_even_and_lopsided_counts) {
    const std::array<std::uint64_t, 4> even = {10, 10, 10, 10};
    CHECK(chi_square(even) == 0.0);
    const std::array<std::uint64_t, 4> lopsided = {40, 0, 0, 0};
    CHECK(std::abs(chi_square(lopsided) - 120.0) < 1e-9);  // 30^2/10 + 3 * 10^2/10
}

TEST_CASE(distribution_reports_render) {
    const Distribution d = measure_distribution(200, 1);
    const std::string html = distribution_html(d);
    CHECK(html.rfind("<!doctype html>", 0) == 0);
    CHECK(html.find("Melody pitches") != std::string::npos);
    CHECK(html.find("Digest byte values") != std::string::npos);
    CHECK_EQ(std::count(html.begin(), html.end(), '\0'), 0);
    const std::string csv = distribution_csv(d);
    CHECK_EQ(std::count(csv.begin(), csv.end(), '\n'), 1 + 16 + 16 + 256);
    CHECK(distribution_text(d).find("chi-square") != std::string::npos);
}
