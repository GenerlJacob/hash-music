// song.h - the plain-data contract between the mapper (Member B) and the MIDI writer (Member C).
// Agreed on day 2. Change it only after telling the whole group.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hashmusic {

// MIDI counts time in "ticks". 480 ticks per beat (quarter note) is a common resolution.
constexpr std::uint32_t kTicksPerBeat = 480;
constexpr std::uint32_t kBeatsPerBar = 4;  // every song is in 4/4 time
constexpr std::uint32_t kTicksPerBar = kTicksPerBeat * kBeatsPerBar;
constexpr std::uint8_t kDrumChannel = 9;  // General MIDI plays channel 10 (index 9) as a drum kit

struct Note {
    std::uint32_t start = 0;      // ticks from the beginning of the song
    std::uint32_t duration = 0;   // ticks the note sounds for (must be at least 1)
    std::uint8_t pitch = 60;      // MIDI note number: 60 = middle C (C4); +1 = one semitone higher
    std::uint8_t velocity = 100;  // how hard the key is struck: 1 (soft) to 127 (loud)
};

struct Track {
    std::string name;          // shown by most MIDI players and editors
    std::uint8_t channel = 0;  // 0-15
    std::uint8_t program = 0;  // General MIDI instrument 0-127 (0 = Acoustic Grand Piano)
    std::vector<Note> notes;
};

struct Song {
    std::string title = "Hash Music";
    std::uint32_t bpm = 120;           // tempo in beats per minute
    std::uint32_t length_ticks = 0;    // where every track ends (the End of Track event)
    std::vector<Track> tracks;
};

inline bool operator==(const Note& a, const Note& b) {
    return a.start == b.start && a.duration == b.duration && a.pitch == b.pitch && a.velocity == b.velocity;
}
inline bool operator==(const Track& a, const Track& b) {
    return a.name == b.name && a.channel == b.channel && a.program == b.program && a.notes == b.notes;
}
inline bool operator==(const Song& a, const Song& b) {
    return a.title == b.title && a.bpm == b.bpm && a.length_ticks == b.length_ticks && a.tracks == b.tracks;
}

}  // namespace hashmusic
