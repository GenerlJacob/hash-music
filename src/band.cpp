// band.cpp - hash-chosen instruments plus harmony, bass and drum layers (extra goal, --band).
#include "band.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include "mapper.h"

namespace hashmusic {
namespace {

struct Instrument {
    std::uint8_t program;  // General MIDI program number, counting from 0
    const char* name;
};

const Instrument kMelodyInstruments[16] = {
    {0, "Acoustic Grand Piano"}, {4, "Electric Piano"}, {8, "Celesta"},       {9, "Glockenspiel"},
    {10, "Music Box"},           {11, "Vibraphone"},    {12, "Marimba"},      {13, "Xylophone"},
    {24, "Nylon Guitar"},        {46, "Harp"},          {73, "Flute"},        {75, "Pan Flute"},
    {79, "Ocarina"},             {80, "Square Lead"},   {108, "Kalimba"},     {114, "Steel Drums"},
};

const Instrument kHarmonyInstruments[16] = {
    {48, "Strings"},      {49, "Slow Strings"},          {52, "Choir"},        {88, "New Age Pad"},
    {89, "Warm Pad"},     {16, "Drawbar Organ"},         {5, "Electric Piano 2"}, {6, "Harpsichord"},
    {25, "Steel Guitar"}, {27, "Clean Electric Guitar"}, {46, "Harp"},         {61, "Brass Section"},
    {71, "Clarinet"},     {68, "Oboe"},                  {90, "Polysynth Pad"}, {92, "Bowed Pad"},
};

const Instrument kBassInstruments[8] = {
    {32, "Acoustic Bass"}, {33, "Finger Bass"},  {34, "Pick Bass"},    {35, "Fretless Bass"},
    {36, "Slap Bass"},     {38, "Synth Bass 1"}, {39, "Synth Bass 2"}, {43, "Contrabass"},
};

// The six chords built from the C major scale: root (as a pitch class, C = 0) and major/minor.
struct Chord {
    int root;
    bool minor;
    const char* numeral;
};
const Chord kChords[6] = {
    {0, false, "I"}, {2, true, "ii"}, {4, true, "iii"}, {5, false, "IV"}, {7, false, "V"}, {9, true, "vi"},
};
enum Degree { kI, kII, kIII, kIV, kV, kVI };

const Degree kProgressions[16][4] = {
    {kI, kV, kVI, kIV}, {kVI, kIV, kI, kV},   {kI, kVI, kIV, kV},  {kI, kIV, kV, kIV},
    {kI, kIV, kVI, kV}, {kII, kV, kI, kVI},   {kI, kIII, kIV, kV}, {kVI, kV, kIV, kV},
    {kI, kV, kIV, kI},  {kIV, kI, kV, kVI},   {kI, kII, kIV, kV},  {kVI, kII, kV, kI},
    {kI, kIV, kI, kV},  {kIV, kV, kIII, kVI}, {kI, kVI, kII, kV},  {kI, kV, kVI, kIII},
};

// Which chord tone a hit plays. kTriad plays root, third and fifth together.
enum Tone { kRoot, kThird, kFifth, kOctave, kTriad };

struct Hit {
    int step;   // start within the bar, in sixteenth notes (0-15)
    int steps;  // length, in sixteenth notes
    Tone tone;
};

struct Pattern {
    const char* name;
    std::vector<Hit> hits;
};

const std::vector<Pattern>& bass_patterns() {
    static const std::vector<Pattern> patterns = {
        {"whole notes", {{0, 16, kRoot}}},
        {"half notes", {{0, 8, kRoot}, {8, 8, kFifth}}},
        {"steady quarters", {{0, 4, kRoot}, {4, 4, kRoot}, {8, 4, kRoot}, {12, 4, kRoot}}},
        {"root-fifth-octave", {{0, 4, kRoot}, {4, 4, kFifth}, {8, 4, kOctave}, {12, 4, kFifth}}},
        {"push", {{0, 6, kRoot}, {6, 2, kRoot}, {8, 8, kFifth}}},
        {"eighth-note pulse",
         {{0, 2, kRoot}, {2, 2, kRoot}, {4, 2, kRoot}, {6, 2, kRoot},
          {8, 2, kRoot}, {10, 2, kRoot}, {12, 2, kRoot}, {14, 2, kRoot}}},
        {"syncopated", {{0, 3, kRoot}, {3, 3, kRoot}, {6, 2, kFifth}, {8, 4, kRoot}, {12, 4, kOctave}}},
        {"walking", {{0, 4, kRoot}, {4, 4, kThird}, {8, 4, kFifth}, {12, 4, kThird}}},
    };
    return patterns;
}

const std::vector<Pattern>& harmony_patterns() {
    static const std::vector<Pattern> patterns = {
        {"sustained pad", {{0, 16, kTriad}}},
        {"half-note chords", {{0, 8, kTriad}, {8, 8, kTriad}}},
        {"rising arpeggio", {{0, 4, kRoot}, {4, 4, kThird}, {8, 4, kFifth}, {12, 4, kOctave}}},
        {"up-and-down arpeggio",
         {{0, 2, kRoot}, {2, 2, kThird}, {4, 2, kFifth}, {6, 2, kOctave},
          {8, 2, kFifth}, {10, 2, kThird}, {12, 2, kRoot}, {14, 2, kThird}}},
        {"broken chord",
         {{0, 2, kRoot}, {2, 2, kFifth}, {4, 2, kThird}, {6, 2, kFifth},
          {8, 2, kRoot}, {10, 2, kFifth}, {12, 2, kThird}, {14, 2, kFifth}}},
        {"quarter-note stabs", {{0, 2, kTriad}, {4, 2, kTriad}, {8, 2, kTriad}, {12, 2, kTriad}}},
        {"falling arpeggio", {{0, 4, kOctave}, {4, 4, kFifth}, {8, 4, kThird}, {12, 4, kRoot}}},
        {"off-beat chords", {{2, 2, kTriad}, {6, 2, kTriad}, {10, 2, kTriad}, {14, 2, kTriad}}},
    };
    return patterns;
}

// Drum grooves on a 16-step grid (one step = a sixteenth note). 'x' = hit.
struct Groove {
    const char* name;
    const char* kick;
    const char* snare;
    const char* top;           // hi-hat line (played on the ride cymbal when that option is on)
    std::uint8_t snare_note;   // snare drum or side stick
    std::uint8_t top_note;     // closed or open hi-hat
};

// General MIDI percussion notes (channel 10).
constexpr std::uint8_t kKick = 36;
constexpr std::uint8_t kSideStick = 37;
constexpr std::uint8_t kSnare = 38;
constexpr std::uint8_t kClap = 39;
constexpr std::uint8_t kClosedHat = 42;
constexpr std::uint8_t kOpenHat = 46;
constexpr std::uint8_t kCrash = 49;
constexpr std::uint8_t kRide = 51;
constexpr std::uint8_t kTambourine = 54;
constexpr std::uint8_t kToms[4] = {50, 48, 45, 41};  // high to low, for fills

const Groove kGrooves[8] = {
    {"rock",      "x.......x.......", "....x.......x...", "x.x.x.x.x.x.x.x.", kSnare, kClosedHat},
    {"pop",       "x.....x.x.......", "....x.......x...", "x.x.x.x.x.x.x.x.", kSnare, kClosedHat},
    {"disco",     "x...x...x...x...", "....x.......x...", "..x...x...x...x.", kSnare, kOpenHat},
    {"half-time", "x.........x.....", "........x.......", "x.x.x.x.x.x.x.x.", kSnare, kClosedHat},
    {"funk",      "x..x..x...x.....", "....x.......x.x.", "xxxxxxxxxxxxxxxx", kSnare, kClosedHat},
    {"one drop",  "........x.......", "........x.......", "x.x.x.x.x.x.x.x.", kSideStick, kClosedHat},
    {"march",     "x.......x.......", "....x.xx....x.xx", "x...x...x...x...", kSnare, kClosedHat},
    {"clave",     "x.......x.......", "x..x..x...x.x...", "x.x.x.x.x.x.x.x.", kSideStick, kClosedHat},
};

constexpr std::uint32_t kTicksPerStep = kTicksPerBeat / 4;
constexpr int kBassBase = 36;     // C2: bass roots sit in the octave above this
constexpr int kHarmonyBase = 48;  // C3

std::uint8_t clamp_velocity(int velocity) { return static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)); }

std::vector<int> chord_tones(const Chord& chord, int base, Tone tone) {
    const int root = base + chord.root;
    const int third = root + (chord.minor ? 3 : 4);
    const int fifth = root + 7;
    switch (tone) {
        case kRoot: return {root};
        case kThird: return {third};
        case kFifth: return {fifth};
        case kOctave: return {root + 12};
        case kTriad: return {root, third, fifth};
    }
    return {root};
}

void play_pattern(Track& track, const Pattern& pattern, const Chord& chord, int base, std::uint32_t bar_start,
                  int velocity) {
    for (const Hit& hit : pattern.hits) {
        const std::uint32_t length = static_cast<std::uint32_t>(hit.steps) * kTicksPerStep;
        for (const int pitch : chord_tones(chord, base, hit.tone)) {
            Note note;
            note.start = bar_start + static_cast<std::uint32_t>(hit.step) * kTicksPerStep;
            note.duration = length - length / 10;
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.velocity = clamp_velocity(velocity);
            track.notes.push_back(note);
        }
    }
}

void drum_hit(Track& drums, std::uint32_t tick, std::uint8_t drum, int velocity) {
    drums.notes.push_back(Note{tick, kTicksPerStep / 2, drum, clamp_velocity(velocity)});
}

void play_groove(Track& drums, const Groove& groove, const BandChoices& choices, std::uint32_t bar_start, bool fill,
                 bool crash, int base_velocity) {
    const auto scaled = [base_velocity](int velocity) { return velocity * base_velocity / 80; };
    const std::uint8_t top_note = choices.ride ? kRide : groove.top_note;
    for (std::uint32_t step = 0; step < 16; ++step) {
        const std::uint32_t tick = bar_start + step * kTicksPerStep;
        if (fill && step >= 12) {  // the last beat of the bar becomes a run down the toms
            drum_hit(drums, tick, kToms[step - 12], scaled(90));
            continue;
        }
        if (groove.kick[step] == 'x') drum_hit(drums, tick, kKick, scaled(100));
        if (groove.snare[step] == 'x') {
            drum_hit(drums, tick, groove.snare_note, scaled(groove.snare_note == kSideStick ? 84 : 94));
            if (choices.clap) drum_hit(drums, tick, kClap, scaled(76));
        }
        if (groove.top[step] == 'x') drum_hit(drums, tick, top_note, scaled(step % 4 == 0 ? 72 : 56));
        if (choices.tambourine && (step == 4 || step == 12)) drum_hit(drums, tick, kTambourine, scaled(60));
    }
    if (crash) drum_hit(drums, bar_start, kCrash, scaled(96));
}

}  // namespace

BandChoices band_choices(const Digest& digest) {
    BandChoices c;
    const Instrument& melody = kMelodyInstruments[digest[kInstrumentByte] >> 4];
    const Instrument& harmony = kHarmonyInstruments[digest[kInstrumentByte] & 0x0F];
    const Instrument& bass = kBassInstruments[(digest[kBassByte] >> 4) % 8];
    c.melody_program = melody.program;
    c.melody_instrument = melody.name;
    c.harmony_program = harmony.program;
    c.harmony_instrument = harmony.name;
    c.bass_program = bass.program;
    c.bass_instrument = bass.name;

    c.progression = digest[kChordByte] >> 4;
    for (std::size_t i = 0; i < 4; ++i) {
        if (i > 0) c.progression_name += '-';
        c.progression_name += kChords[kProgressions[c.progression][i]].numeral;
    }
    c.harmony_pattern = (digest[kChordByte] & 0x0F) % 8;
    c.harmony_pattern_name = harmony_patterns()[c.harmony_pattern].name;
    c.bass_pattern = (digest[kBassByte] & 0x0F) % 8;
    c.bass_pattern_name = bass_patterns()[c.bass_pattern].name;
    c.groove = (digest[kDrumByte] >> 4) % 8;
    c.groove_name = kGrooves[c.groove].name;

    const unsigned options = digest[kDrumByte] & 0x0Fu;
    c.ride = (options & 1u) != 0;
    c.fills = (options & 2u) != 0;
    c.clap = (options & 4u) != 0;
    c.tambourine = (options & 8u) != 0;
    return c;
}

void add_band(Song& song, const Digest& digest) {
    if (song.tracks.size() != 1 || song.tracks[0].notes.size() != kMelodyNotes + 1) {
        throw std::invalid_argument("add_band() expects a song made by map_digest()");
    }
    const BandChoices choices = band_choices(digest);
    song.tracks[0].program = choices.melody_program;

    // map_digest() ends the home note on a bar line; that bar is the final bar.
    const Note& home = song.tracks[0].notes.back();
    const std::uint32_t bars = (home.start + home.duration) / kTicksPerBar;
    const std::uint32_t final_bar = bars - 1;
    const int base_velocity = base_velocity_from_byte(digest[kLoudnessByte]);

    Track harmony{"Harmony", 2, choices.harmony_program, {}};
    Track bass{"Bass", 1, choices.bass_program, {}};
    Track drums{"Drums", kDrumChannel, 0, {}};

    const Pattern& harmony_pattern = harmony_patterns()[choices.harmony_pattern];
    const Pattern& bass_pattern = bass_patterns()[choices.bass_pattern];
    const Groove& groove = kGrooves[choices.groove];
    const int harmony_velocity = base_velocity - (choices.harmony_pattern == 0 ? 22 : 14);
    const Pattern ending_chord = {"ending chord", {{0, 16, kTriad}}};
    const Pattern ending_bass = {"ending root", {{0, 16, kRoot}}};

    for (std::uint32_t bar = 0; bar < bars; ++bar) {
        const std::uint32_t bar_start = bar * kTicksPerBar;
        if (bar == final_bar) {  // everyone lands together on C and holds it
            const Chord& tonic = kChords[kI];
            play_pattern(harmony, ending_chord, tonic, kHarmonyBase, bar_start, harmony_velocity);
            play_pattern(bass, ending_bass, tonic, kBassBase, bar_start, base_velocity + 8);
            drum_hit(drums, bar_start, kKick, 100 * base_velocity / 80);
            drum_hit(drums, bar_start, kCrash, 100 * base_velocity / 80);
            continue;
        }
        const bool cadence = bar + 1 == final_bar;
        const Chord& chord = kChords[cadence ? kV : kProgressions[choices.progression][bar % 4]];
        play_pattern(harmony, harmony_pattern, chord, kHarmonyBase, bar_start, harmony_velocity);
        play_pattern(bass, bass_pattern, chord, kBassBase, bar_start, base_velocity + 8);
        const bool fill = choices.fills && (bar % 4 == 3 || cadence);
        play_groove(drums, groove, choices, bar_start, fill, bar == 0, base_velocity);
    }

    song.tracks.push_back(std::move(harmony));
    song.tracks.push_back(std::move(bass));
    song.tracks.push_back(std::move(drums));
}

}  // namespace hashmusic
