// test_mapper.cpp - Member B's tests: the scale, the rhythm table, tempo and velocity, exact mapping.
#include <cstdint>
#include <string>

#include "mapper.h"
#include "sha256.h"
#include "test_framework.h"

using namespace hashmusic;

TEST_CASE(pitch_table_is_c_major_pentatonic_from_g3_to_g6) {
    CHECK_EQ(kPitchTable.front(), 55);  // G3
    CHECK_EQ(kPitchTable.back(), 91);   // G6
    for (std::size_t i = 0; i < kPitchTable.size(); ++i) {
        const int pitch_class = kPitchTable[i] % 12;  // C=0 D=2 E=4 G=7 A=9
        CHECK(pitch_class == 0 || pitch_class == 2 || pitch_class == 4 || pitch_class == 7 || pitch_class == 9);
        if (i > 0) {
            // Consecutive entries are neighbors in the scale: 2 or 3 semitones apart, none skipped.
            const int step = kPitchTable[i] - kPitchTable[i - 1];
            CHECK(step == 2 || step == 3);
        }
    }
}

TEST_CASE(rhythm_table_averages_exactly_one_beat) {
    std::uint32_t total = 0;
    for (const std::uint32_t ticks : kRhythmTable) {
        CHECK(ticks == 240 || ticks == 480 || ticks == 720 || ticks == 960);
        total += ticks;
    }
    CHECK_EQ(total, 16 * kTicksPerBeat);
}

TEST_CASE(tempo_and_loudness_ranges) {
    CHECK_EQ(tempo_from_byte(0x00), 80);
    CHECK_EQ(tempo_from_byte(0x80), 120);
    CHECK_EQ(tempo_from_byte(0xff), 160);
    CHECK_EQ(base_velocity_from_byte(0x00), 60);
    CHECK_EQ(base_velocity_from_byte(0xff), 100);
    for (int b = 1; b < 256; ++b) {
        CHECK(tempo_from_byte(static_cast<std::uint8_t>(b)) >= tempo_from_byte(static_cast<std::uint8_t>(b - 1)));
        CHECK(base_velocity_from_byte(static_cast<std::uint8_t>(b)) >=
              base_velocity_from_byte(static_cast<std::uint8_t>(b - 1)));
    }
}

TEST_CASE(all_zero_digest_is_g3_eighth_notes_at_80_bpm) {
    const Digest zero{};
    const Song song = map_digest(zero);
    CHECK_EQ(song.bpm, 80);
    CHECK_EQ(song.tracks.size(), 1);
    const Track& melody = song.tracks[0];
    CHECK_EQ(melody.notes.size(), kMelodyNotes + 1);
    for (std::size_t i = 0; i < kMelodyNotes; ++i) {
        CHECK_EQ(melody.notes[i].pitch, 55);
        CHECK_EQ(melody.notes[i].start, i * 240);
        CHECK_EQ(melody.notes[i].duration, 216);  // 90% of an eighth note
    }
    // Base velocity 60: +12 on the downbeat, -8 between beats, 0 on beat 2, +6 on beat 3.
    CHECK_EQ(melody.notes[0].velocity, 72);
    CHECK_EQ(melody.notes[1].velocity, 52);
    CHECK_EQ(melody.notes[2].velocity, 60);
    CHECK_EQ(melody.notes[4].velocity, 66);
    // 24 eighth notes fill exactly 3 bars. The home note C5 runs to the first bar line at least
    // two beats away: tick 7680, one full bar.
    const Note& home = melody.notes.back();
    CHECK_EQ(home.pitch, 72);
    CHECK_EQ(home.start, 5760);
    CHECK_EQ(home.duration, 1920);
    CHECK_EQ(song.length_ticks, 7680 + 480);
}

TEST_CASE(each_byte_reads_as_pitch_digit_then_rhythm_digit) {
    Digest d{};
    d[0] = 0x7c;  // 7 = C5, c = dotted quarter
    d[1] = 0xf0;  // f = G6, 0 = eighth
    d[2] = 0x2e;  // 2 = C4, e = half
    d[kTempoByte] = 0xff;
    d[kLoudnessByte] = 0xff;
    const Song song = map_digest(d);
    const auto& n = song.tracks[0].notes;
    CHECK_EQ(song.bpm, 160);
    CHECK_EQ(note_name(n[0].pitch), "C5");
    CHECK_EQ(n[0].start, 0);
    CHECK_EQ(n[0].duration, 648);
    CHECK_EQ(note_name(n[1].pitch), "G6");
    CHECK_EQ(n[1].start, 720);
    CHECK_EQ(n[1].duration, 216);
    CHECK_EQ(note_name(n[2].pitch), "C4");
    CHECK_EQ(n[2].start, 960);
    CHECK_EQ(n[2].duration, 864);
    CHECK_EQ(n[3].start, 1920);
    CHECK_EQ(n[0].velocity, 112);  // base 100, downbeat
    CHECK_EQ(n[1].velocity, 92);   // between beats
    CHECK_EQ(n[2].velocity, 106);  // beat 3
    CHECK_EQ(n[3].velocity, 112);  // next downbeat
}

TEST_CASE(jacob_song_can_be_read_off_its_hash) {
    // sha256("jacob") = c35d6aaf3b38... -> c3 5d 6a af ...; byte 24 = c0, byte 25 = f9
    const Song song = map_digest(sha256("jacob"));
    const auto& n = song.tracks[0].notes;
    CHECK_EQ(note_name(n[0].pitch), "C6");  // c3: c = C6, 3 = eighth
    CHECK_EQ(n[0].duration, 216);
    CHECK_EQ(note_name(n[1].pitch), "G4");  // 5d: 5 = G4, d = dotted quarter
    CHECK_EQ(n[1].duration, 648);
    CHECK_EQ(note_name(n[2].pitch), "A4");  // 6a: 6 = A4, a = quarter
    CHECK_EQ(n[2].duration, 432);
    CHECK_EQ(note_name(n[3].pitch), "G5");  // af: a = G5, f = half
    CHECK_EQ(n[3].duration, 864);
    CHECK_EQ(song.bpm, 140);  // 0xc0 = 192 -> 80 + 192 * 80 / 255
}

TEST_CASE(every_song_is_well_formed) {
    for (int i = 0; i < 300; ++i) {
        const Song song = map_digest(sha256("song " + std::to_string(i)));
        const auto& notes = song.tracks[0].notes;
        CHECK_EQ(notes.size(), kMelodyNotes + 1);
        for (std::size_t k = 1; k < notes.size(); ++k) {
            CHECK(notes[k - 1].start + notes[k - 1].duration <= notes[k].start);  // in order, never overlapping
        }
        for (const Note& note : notes) CHECK(note.velocity >= 1 && note.velocity <= 127);
        const Note& home = notes.back();
        const std::uint32_t end = home.start + home.duration;
        CHECK_EQ(end % kTicksPerBar, 0);
        CHECK(home.duration >= 2 * kTicksPerBeat && home.duration < 6 * kTicksPerBeat);
        CHECK_EQ(song.length_ticks, end + kTicksPerBeat);
        CHECK(song.bpm >= 80 && song.bpm <= 160);
    }
}

TEST_CASE(same_digest_same_song_different_digest_different_song) {
    const Digest a = sha256("jacob");
    const Digest b = sha256("jacob1");
    CHECK(map_digest(a) == map_digest(a));
    CHECK(!(map_digest(a) == map_digest(b)));
}

TEST_CASE(note_and_rhythm_names) {
    CHECK_EQ(note_name(60), "C4");
    CHECK_EQ(note_name(72), "C5");
    CHECK_EQ(note_name(55), "G3");
    CHECK_EQ(note_name(61), "C#4");
    CHECK_EQ(note_name(0), "C-1");
    CHECK_EQ(note_name(127), "G9");
    CHECK_EQ(rhythm_symbol(240), "e");
    CHECK_EQ(rhythm_symbol(480), "q");
    CHECK_EQ(rhythm_symbol(720), "q.");
    CHECK_EQ(rhythm_symbol(960), "h");
}
