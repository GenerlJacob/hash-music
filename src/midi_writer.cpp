// midi_writer.cpp - Song -> Standard MIDI File bytes.
// Owner: Member C. Backup: Member D.
// Reference: "Standard MIDI-File Format Spec. 1.1" (MIDI Manufacturers Association).
#include "midi_writer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace hashmusic {
namespace {

constexpr std::uint8_t u8(std::uint32_t value) { return static_cast<std::uint8_t>(value & 0xFF); }

void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& more) {
    out.insert(out.end(), more.begin(), more.end());
}

// MIDI files store multi-byte numbers big-endian (most significant byte first).
void put_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(u8(value >> 8));
    out.push_back(u8(value));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(u8(value >> 24));
    out.push_back(u8(value >> 16));
    out.push_back(u8(value >> 8));
    out.push_back(u8(value));
}

// One event in a track, stamped with its absolute time before delta times are worked out.
struct Event {
    std::uint32_t tick;
    int order;  // breaks ties between events at the same tick: lower goes first
    std::vector<std::uint8_t> bytes;
};

// At the same tick: setup events, then note-offs, then note-ons. Ending old notes before starting
// new ones stops a repeated pitch from being cut short by its own previous note-off.
constexpr int kSetup = 0;
constexpr int kNoteOff = 1;
constexpr int kNoteOn = 2;

// Meta event FF <type> <length> <text>, e.g. type 0x03 = track name.
Event text_event(std::uint8_t type, const std::string& text) {
    Event event{0, kSetup, {0xFF, type}};
    append(event.bytes, encode_vlq(static_cast<std::uint32_t>(text.size())));
    for (const char c : text) event.bytes.push_back(static_cast<std::uint8_t>(c));
    return event;
}

std::vector<std::uint8_t> track_chunk(std::vector<Event> events, std::uint32_t end_tick) {
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    std::vector<std::uint8_t> body;
    std::uint32_t now = 0;
    for (const Event& event : events) {
        append(body, encode_vlq(event.tick - now));  // delta time: ticks since the previous event
        append(body, event.bytes);
        now = event.tick;
    }
    // Every track must finish with the End of Track meta event, FF 2F 00.
    append(body, encode_vlq(std::max(end_tick, now) - now));
    body.insert(body.end(), {0xFF, 0x2F, 0x00});

    std::vector<std::uint8_t> chunk = {'M', 'T', 'r', 'k'};
    put_u32(chunk, static_cast<std::uint32_t>(body.size()));
    append(chunk, body);
    return chunk;
}

}  // namespace

std::vector<std::uint8_t> encode_vlq(std::uint32_t value) {
    if (value > 0x0FFFFFFF) throw std::invalid_argument("MIDI delta times are limited to 28 bits");
    std::vector<std::uint8_t> out;
    out.push_back(u8(value & 0x7F));  // last byte: top bit clear
    value >>= 7;
    while (value > 0) {
        out.push_back(u8((value & 0x7F) | 0x80));  // earlier bytes: top bit set = "more follows"
        value >>= 7;
    }
    std::reverse(out.begin(), out.end());  // we built it least significant group first
    return out;
}

std::vector<std::uint8_t> write_midi(const Song& song) {
    if (song.bpm == 0) throw std::invalid_argument("tempo must be at least 1 BPM");
    const std::uint32_t microseconds_per_beat = 60000000u / song.bpm;
    if (microseconds_per_beat > 0xFFFFFF) throw std::invalid_argument("tempo too slow for a MIDI tempo event");
    if (song.tracks.size() > 0xFFFE) throw std::invalid_argument("too many tracks");

    // Header chunk.
    std::vector<std::uint8_t> file = {'M', 'T', 'h', 'd'};
    put_u32(file, 6);                                                   // header length is always 6
    put_u16(file, 1);                                                   // format 1
    put_u16(file, static_cast<std::uint32_t>(song.tracks.size() + 1));  // + the conductor track
    put_u16(file, kTicksPerBeat);                                       // timing division

    // Track 0, the conductor track: song-wide settings.
    std::vector<Event> conductor;
    conductor.push_back(text_event(0x03, song.title));
    conductor.push_back({0, kSetup, {0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08}});  // time signature 4/4
    conductor.push_back({0, kSetup,
                         {0xFF, 0x51, 0x03, u8(microseconds_per_beat >> 16), u8(microseconds_per_beat >> 8),
                          u8(microseconds_per_beat)}});  // tempo, in microseconds per beat
    append(file, track_chunk(std::move(conductor), song.length_ticks));

    // One track per instrument.
    for (const Track& track : song.tracks) {
        if (track.channel > 15) throw std::invalid_argument("MIDI channel must be 0-15");
        if (track.program > 127) throw std::invalid_argument("General MIDI program must be 0-127");

        std::vector<Event> events;
        events.push_back(text_event(0x03, track.name));
        if (track.channel != kDrumChannel) {  // the drum channel ignores program changes
            events.push_back({0, kSetup, {u8(0xC0u | track.channel), track.program}});
        }
        for (const Note& note : track.notes) {
            if (note.pitch > 127) throw std::invalid_argument("pitch must be 0-127");
            if (note.velocity < 1 || note.velocity > 127) throw std::invalid_argument("velocity must be 1-127");
            if (note.duration == 0) throw std::invalid_argument("notes must last at least one tick");
            events.push_back({note.start, kNoteOn, {u8(0x90u | track.channel), note.pitch, note.velocity}});
            events.push_back({note.start + note.duration, kNoteOff, {u8(0x80u | track.channel), note.pitch, 0x40}});
        }
        append(file, track_chunk(std::move(events), song.length_ticks));
    }
    return file;
}

void save_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open '" + path + "' for writing");
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("failed while writing '" + path + "'");
}

}  // namespace hashmusic
