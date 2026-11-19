// hardcoded_melody.cpp - task 6: a fixed melody that uses only the MIDI writer.
// Owner: Member C.
//
// If hardcoded.mid plays in a MIDI player, the writer works, before any hashing or mapping exists.
// Usage: hardcoded_melody [output.mid]
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "midi_writer.h"

int main(int argc, char** argv) {
    using namespace hashmusic;
    const std::string path = argc > 1 ? argv[1] : "hardcoded.mid";

    // C major pentatonic up and back down in quarter notes, ending on a long C.
    const std::uint8_t pitches[] = {60, 62, 64, 67, 69, 72, 69, 67, 64, 62, 60};

    Song song;
    song.title = "Hardcoded test melody";
    song.bpm = 120;
    Track piano;
    piano.name = "Piano";
    std::uint32_t tick = 0;
    for (std::size_t i = 0; i < std::size(pitches); ++i) {
        const bool last = i + 1 == std::size(pitches);
        const std::uint32_t slot = last ? kTicksPerBar : kTicksPerBeat;
        piano.notes.push_back(Note{tick, slot - slot / 10, pitches[i], 96});
        tick += slot;
    }
    song.length_ticks = tick;
    song.tracks.push_back(piano);

    try {
        save_file(path, write_midi(song));
    } catch (const std::exception& e) {
        std::cerr << "hardcoded_melody: " << e.what() << "\n";
        return 1;
    }
    std::cout << "wrote " << path << " (" << std::size(pitches) << " notes at " << song.bpm << " BPM)\n";
    return 0;
}
