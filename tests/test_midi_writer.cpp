// test_midi_writer.cpp - Member C's tests: VLQ encoding, the exact bytes of a tiny file, event order,
// chunk lengths, and rejection of out-of-range values.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "midi_writer.h"
#include "test_framework.h"

using namespace hashmusic;
using Bytes = std::vector<std::uint8_t>;

namespace {

std::string hex(const Bytes& bytes) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        if (!out.empty()) out += ' ';
        out += kDigits[b >> 4];
        out += kDigits[b & 0x0F];
    }
    return out;
}

Song one_track_song(const Track& track, std::uint32_t length_ticks) {
    Song song;
    song.length_ticks = length_ticks;
    song.tracks.push_back(track);
    return song;
}

}  // namespace

TEST_CASE(vlq_examples_from_the_midi_spec) {
    const struct {
        std::uint32_t value;
        Bytes bytes;
    } cases[] = {
        {0x00000000, {0x00}},
        {0x00000040, {0x40}},
        {0x0000007F, {0x7F}},
        {0x00000080, {0x81, 0x00}},
        {0x00002000, {0xC0, 0x00}},
        {0x00003FFF, {0xFF, 0x7F}},
        {0x00004000, {0x81, 0x80, 0x00}},
        {0x00100000, {0xC0, 0x80, 0x00}},
        {0x001FFFFF, {0xFF, 0xFF, 0x7F}},
        {0x00200000, {0x81, 0x80, 0x80, 0x00}},
        {0x08000000, {0xC0, 0x80, 0x80, 0x00}},
        {0x0FFFFFFF, {0xFF, 0xFF, 0xFF, 0x7F}},
    };
    for (const auto& c : cases) CHECK_EQ(hex(encode_vlq(c.value)), hex(c.bytes));
    CHECK_THROWS(encode_vlq(0x10000000));
}

TEST_CASE(one_note_song_exact_bytes) {
    Song song;
    song.title = "T";
    song.bpm = 120;
    song.length_ticks = 480;
    Track track;
    track.name = "M";
    track.notes.push_back(Note{0, 240, 60, 100});
    song.tracks.push_back(track);

    const Bytes expected = {
        // MThd: length 6, format 1, 2 tracks, 480 ticks per beat
        0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x02, 0x01, 0xE0,
        // MTrk 0, the conductor track: 25 bytes
        0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x19,
        0x00, 0xFF, 0x03, 0x01, 'T',                     // track name "T"
        0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08,  // time signature 4/4
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,        // tempo: 500,000 microseconds per beat = 120 BPM
        0x83, 0x60, 0xFF, 0x2F, 0x00,                    // 480 ticks later: End of Track
        // MTrk 1, the melody: 22 bytes
        0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x16,
        0x00, 0xFF, 0x03, 0x01, 'M',  // track name "M"
        0x00, 0xC0, 0x00,             // program change: Acoustic Grand Piano
        0x00, 0x90, 0x3C, 0x64,       // note on: middle C, velocity 100
        0x81, 0x70, 0x80, 0x3C, 0x40, // 240 ticks later: note off
        0x81, 0x70, 0xFF, 0x2F, 0x00, // 240 ticks later: End of Track
    };
    CHECK_EQ(hex(write_midi(song)), hex(expected));
}

TEST_CASE(note_off_comes_before_note_on_at_the_same_tick) {
    // Two middle Cs back to back, listed out of order. The first must end before the second starts,
    // or a player would cut the second note off.
    Track track;
    track.name = "x";
    track.notes.push_back(Note{480, 480, 60, 100});
    track.notes.push_back(Note{0, 480, 60, 100});
    const Bytes file = write_midi(one_track_song(track, 960));
    const Bytes tail = {
        0x00, 0x90, 0x3C, 0x64,        // tick 0: on
        0x83, 0x60, 0x80, 0x3C, 0x40,  // tick 480: off
        0x00, 0x90, 0x3C, 0x64,        // tick 480: on
        0x83, 0x60, 0x80, 0x3C, 0x40,  // tick 960: off
        0x00, 0xFF, 0x2F, 0x00,        // End of Track
    };
    CHECK(file.size() >= tail.size());
    CHECK_EQ(hex(Bytes(file.end() - static_cast<std::ptrdiff_t>(tail.size()), file.end())), hex(tail));
}

TEST_CASE(drum_channel_gets_no_program_change) {
    Track drums;
    drums.name = "D";
    drums.channel = kDrumChannel;
    drums.notes.push_back(Note{0, 60, 36, 100});
    const Bytes file = write_midi(one_track_song(drums, 480));
    CHECK(std::find(file.begin(), file.end(), 0xC9) == file.end());  // no program change on channel 10
    CHECK(std::find(file.begin(), file.end(), 0x99) != file.end());  // note on, channel 10
}

TEST_CASE(chunk_lengths_add_up_and_every_track_ends) {
    Song song;
    song.length_ticks = 4000;
    for (std::uint8_t channel = 0; channel < 3; ++channel) {
        Track track;
        track.name = "track " + std::to_string(channel);
        track.channel = channel;
        for (std::uint32_t i = 0; i < 20; ++i) {
            track.notes.push_back(Note{i * 150, 140, static_cast<std::uint8_t>(50 + i + channel), 90});
        }
        song.tracks.push_back(track);
    }
    const Bytes file = write_midi(song);
    CHECK_EQ((std::uint32_t{file[10]} << 8) | file[11], song.tracks.size() + 1);  // track count in MThd
    std::size_t pos = 14;
    std::size_t chunks = 0;
    while (pos + 8 <= file.size()) {
        CHECK(file[pos] == 'M' && file[pos + 1] == 'T' && file[pos + 2] == 'r' && file[pos + 3] == 'k');
        const std::uint32_t length = (std::uint32_t{file[pos + 4]} << 24) | (std::uint32_t{file[pos + 5]} << 16) |
                                     (std::uint32_t{file[pos + 6]} << 8) | std::uint32_t{file[pos + 7]};
        pos += 8 + length;
        CHECK(pos <= file.size());
        if (pos > file.size()) break;
        CHECK(file[pos - 3] == 0xFF && file[pos - 2] == 0x2F && file[pos - 1] == 0x00);
        ++chunks;
    }
    CHECK_EQ(pos, file.size());
    CHECK_EQ(chunks, song.tracks.size() + 1);
}

TEST_CASE(out_of_range_values_are_rejected) {
    const auto with_note = [](Note note) {
        Track track;
        track.name = "x";
        track.notes.push_back(note);
        return one_track_song(track, 480);
    };
    CHECK_THROWS(write_midi(with_note(Note{0, 100, 128, 100})));  // pitch
    CHECK_THROWS(write_midi(with_note(Note{0, 100, 60, 0})));     // velocity 0 would mean note off
    CHECK_THROWS(write_midi(with_note(Note{0, 100, 60, 128})));   // velocity
    CHECK_THROWS(write_midi(with_note(Note{0, 0, 60, 100})));     // zero-length note

    Song bad_channel = with_note(Note{0, 100, 60, 100});
    bad_channel.tracks[0].channel = 16;
    CHECK_THROWS(write_midi(bad_channel));
    Song bad_program = with_note(Note{0, 100, 60, 100});
    bad_program.tracks[0].program = 128;
    CHECK_THROWS(write_midi(bad_program));
    Song no_tempo = with_note(Note{0, 100, 60, 100});
    no_tempo.bpm = 0;
    CHECK_THROWS(write_midi(no_tempo));
    Song too_slow = with_note(Note{0, 100, 60, 100});
    too_slow.bpm = 3;  // 20 seconds per beat does not fit in the 24-bit tempo field
    CHECK_THROWS(write_midi(too_slow));
}

TEST_CASE(save_file_writes_exact_bytes) {
    // Includes bytes that a text-mode stream would change on Windows (\r, \n, 0x1a).
    const Bytes data = {0x4D, 0x54, 0x00, 0xFF, '\r', '\n', 0x1A, 0x80};
    const std::string path = "test_midi_writer_roundtrip.bin";
    save_file(path, data);
    Bytes back;
    {
        std::ifstream in(path, std::ios::binary);
        back.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK_EQ(hex(back), hex(data));
    std::remove(path.c_str());
    CHECK_THROWS(save_file("no/such/folder/song.mid", data));
}
