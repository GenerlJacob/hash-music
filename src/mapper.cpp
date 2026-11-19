// mapper.cpp - digest bytes -> notes, tempo and velocity.
// Owner: Member B. Backup: Member A.
#include "mapper.h"

#include <algorithm>
#include <utility>

namespace hashmusic {

// C major pentatonic (C D E G A) has no half-step clashes, so notes in any order sound consonant
// (the plan's fix for "melody sounds random or harsh"). 16 notes run from G3 to G6, one per hex digit.
const std::array<std::uint8_t, 16> kPitchTable = {{
    55, 57, 60, 62, 64,  // 0-4: G3 A3 C4 D4 E4
    67, 69, 72, 74, 76,  // 5-9: G4 A4 C5 D5 E5
    79, 81, 84, 86, 88,  // a-e: G5 A5 C6 D6 E6
    91,                  // f:   G6
}};

// Weighted toward short notes so the melody keeps moving. The average is exactly one beat.
//   0-5 eighth   6-b quarter   c-d dotted quarter   e-f half
const std::array<std::uint32_t, 16> kRhythmTable = {{
    240, 240, 240, 240, 240, 240,
    480, 480, 480, 480, 480, 480,
    720, 720,
    960, 960,
}};

std::uint32_t tempo_from_byte(std::uint8_t byte) { return 80 + (std::uint32_t{byte} * 80) / 255; }

std::uint8_t base_velocity_from_byte(std::uint8_t byte) {
    return static_cast<std::uint8_t>(60 + (std::uint32_t{byte} * 40) / 255);
}

namespace {

// Accent by position in the 4/4 bar so the melody has a pulse: strongest on beat 1, then beat 3.
int accent_for(std::uint32_t start_tick) {
    const std::uint32_t position = start_tick % kTicksPerBar;
    if (position == 0) return 12;                  // downbeat
    if (position == 2 * kTicksPerBeat) return 6;   // beat 3
    if (position % kTicksPerBeat == 0) return 0;   // beats 2 and 4
    return -8;                                     // between beats
}

std::uint8_t clamp_velocity(int velocity) { return static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)); }

}  // namespace

Song map_digest(const Digest& digest) {
    Song song;
    song.title = "Hash Music " + to_hex(digest).substr(0, 8);
    song.bpm = tempo_from_byte(digest[kTempoByte]);
    const int base_velocity = base_velocity_from_byte(digest[kLoudnessByte]);

    Track melody;
    melody.name = "Melody";
    melody.channel = 0;
    melody.program = 0;  // Acoustic Grand Piano

    std::uint32_t tick = 0;
    for (std::size_t i = 0; i < kMelodyNotes; ++i) {
        const std::uint8_t byte = digest[i];
        const std::uint32_t slot = kRhythmTable[byte & 0x0F];  // second hex digit
        Note note;
        note.start = tick;
        note.duration = slot - slot / 10;  // sound for 90% of the slot so repeated notes stay distinct
        note.pitch = kPitchTable[byte >> 4];  // first hex digit
        note.velocity = clamp_velocity(base_velocity + accent_for(tick));
        melody.notes.push_back(note);
        tick += slot;
    }

    // Ending: hold the home note until a bar line at least two beats away, so the song resolves.
    const std::uint32_t end = ((tick + 2 * kTicksPerBeat + kTicksPerBar - 1) / kTicksPerBar) * kTicksPerBar;
    Note home;
    home.start = tick;
    home.duration = end - tick;
    home.pitch = kHomePitch;
    home.velocity = clamp_velocity(base_velocity + 6);
    melody.notes.push_back(home);

    song.tracks.push_back(std::move(melody));
    song.length_ticks = end + kTicksPerBeat;  // one beat of silence so the last note can ring out
    return song;
}

std::string note_name(std::uint8_t pitch) {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(kNames[pitch % 12]) + std::to_string(pitch / 12 - 1);
}

std::string rhythm_symbol(std::uint32_t ticks) {
    switch (ticks) {
        case kTicksPerBeat / 2: return "e";
        case kTicksPerBeat: return "q";
        case kTicksPerBeat * 3 / 2: return "q.";
        case kTicksPerBeat * 2: return "h";
        default: return std::to_string(ticks) + "t";
    }
}

}  // namespace hashmusic
