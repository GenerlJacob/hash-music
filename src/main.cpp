// main.cpp - the hashmusic command-line tool.
// Owner: Member D. Backup: Member C.
//
// main reads the input, calls each module in order, and writes the .mid file:
//   input bytes --sha256()--> 32-byte digest --map_digest()--> Song --write_midi()--> bytes --> song.mid
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "avalanche.h"
#include "band.h"
#include "mapper.h"
#include "midi_writer.h"
#include "sha256.h"
#include "stats.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {

using namespace hashmusic;

const char* const kUsage = R"(hashmusic - hear what a SHA-256 hash sounds like

Usage:
  hashmusic [options] <text...>          hash the text (words are joined with spaces), write its song
  hashmusic [options] --file <path>      hash a file's bytes and write its "theme song"
  hashmusic [options] --stdin            hash everything read from standard input
  hashmusic [options] --compare <a> <b>  avalanche demo: compare two inputs bit by bit, write both songs
  hashmusic --stats [count]              hash random inputs, chart how evenly bytes and pitches spread

Options:
  -o, --out <path>  output file (default song.mid). With --compare it is a prefix for
                    <prefix>_a.mid and <prefix>_b.mid (default "compare"). With --stats it names
                    the chart page (default distribution.html; a .csv is written beside it)
  --band            add hash-chosen instruments, harmony, bass and drums
  --hash-only       print the SHA-256 digest and stop
  --seed <n>        random seed for --stats (default 2710)
  -q, --quiet       with text, --file or --stdin: print only the digest and the output file name
  -h, --help        show this help

Examples:
  hashmusic jacob
  hashmusic jacob -o jacob.mid
  hashmusic --compare jacob jacob1
  hashmusic --band --file README.md -o readme_theme.mid
  hashmusic --stats 10000
)";

enum class Mode { kText, kFile, kStdin, kCompare, kStats };

struct Options {
    Mode mode = Mode::kText;
    bool mode_set = false;
    bool help = false;
    std::vector<std::string> positional;
    std::string out;
    std::string file;
    bool band = false;
    bool hash_only = false;
    bool quiet = false;
    std::uint64_t seed = 2710;
};

// Bad command-line usage: reported with a hint and exit code 2.
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::uint64_t parse_number(const std::string& text, const std::string& what) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        throw UsageError(what + " must be a whole number, not '" + text + "'");
    }
    try {
        return std::stoull(text);
    } catch (const std::exception&) {
        throw UsageError(what + " is too large");
    }
}

Options parse_args(const std::vector<std::string>& args) {
    Options o;
    const auto set_mode = [&o](Mode mode) {
        if (o.mode_set && o.mode != mode) throw UsageError("use only one of --file, --stdin, --compare and --stats");
        o.mode = mode;
        o.mode_set = true;
    };
    bool options_ended = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw UsageError(arg + " needs a value");
            return args[++i];
        };
        if (options_ended || arg.size() < 2 || arg[0] != '-') {
            o.positional.push_back(arg);
        } else if (arg == "--") {
            options_ended = true;
        } else if (arg == "-h" || arg == "--help") {
            o.help = true;
        } else if (arg == "-o" || arg == "--out") {
            o.out = value();
        } else if (arg == "--file") {
            set_mode(Mode::kFile);
            o.file = value();
        } else if (arg == "--stdin") {
            set_mode(Mode::kStdin);
        } else if (arg == "--compare") {
            set_mode(Mode::kCompare);
        } else if (arg == "--stats") {
            set_mode(Mode::kStats);
        } else if (arg == "--band") {
            o.band = true;
        } else if (arg == "--hash-only") {
            o.hash_only = true;
        } else if (arg == "--seed") {
            o.seed = parse_number(value(), "--seed");
        } else if (arg == "-q" || arg == "--quiet") {
            o.quiet = true;
        } else {
            throw UsageError("unknown option '" + arg + "' (put -- before text that starts with '-')");
        }
    }
    if (o.help) return o;

    switch (o.mode) {
        case Mode::kText:
            if (o.positional.empty()) throw UsageError("give some text to hash, for example: hashmusic jacob");
            break;
        case Mode::kFile:
        case Mode::kStdin:
            if (!o.positional.empty()) throw UsageError("unexpected text '" + o.positional[0] + "'");
            break;
        case Mode::kCompare:
            if (o.positional.size() != 2) throw UsageError("--compare needs exactly two inputs, e.g. --compare jacob jacob1");
            break;
        case Mode::kStats:
            if (o.positional.size() > 1) throw UsageError("--stats takes at most one number");
            break;
    }
    if (o.hash_only && (o.mode == Mode::kCompare || o.mode == Mode::kStats)) {
        throw UsageError("--hash-only works with text, --file or --stdin");
    }
    return o;
}

// ---- Formatting helpers -------------------------------------------------------------------------

std::string with_commas(std::uint64_t n) {
    const std::string digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string fixed1(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    return buffer;
}

std::string hex_pair(std::uint8_t byte) {
    static const char kDigits[] = "0123456789abcdef";
    return {kDigits[byte >> 4], kDigits[byte & 0x0F]};
}

std::string pad(std::string text, std::size_t width) {
    if (text.size() < width) text.resize(width, ' ');
    return text;
}

std::string rtrim(std::string text) {
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

// Input text shown on one line: control characters escaped, long text cut short.
std::string printable(const std::string& text, std::size_t max_chars = 60) {
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        const bool continues_utf8 = (c & 0xC0) == 0x80;  // never cut a multi-byte character in half
        if (!continues_utf8 && out.size() >= max_chars) {
            out += "...";
            break;
        }
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20 || c == 0x7F) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\x%02x", c);
            out += buffer;
        } else {
            out += ch;
        }
    }
    return out;
}

double song_seconds(const Song& song) {
    return static_cast<double>(song.length_ticks) / kTicksPerBeat * 60.0 / song.bpm;
}

std::string drum_options(const BandChoices& c) {
    std::string out;
    const auto add = [&out](bool on, const char* name) {
        if (!on) return;
        if (!out.empty()) out += ", ";
        out += name;
    };
    add(c.ride, "ride cymbal");
    add(c.fills, "tom fills");
    add(c.clap, "hand clap");
    add(c.tambourine, "tambourine");
    return out.empty() ? "no extras" : out;
}

// ---- The pipeline -------------------------------------------------------------------------------

Song make_song(const Digest& digest, bool band) {
    Song song = map_digest(digest);
    if (band) add_band(song, digest);
    return song;
}

// Writes the song and returns the file's bytes (so the caller can fingerprint them).
std::vector<std::uint8_t> write_song(const Song& song, const std::string& path) {
    std::vector<std::uint8_t> bytes = write_midi(song);
    save_file(path, bytes);
    return bytes;
}

void save_text(const std::string& path, const std::string& text) {
    save_file(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

struct Input {
    Digest digest{};
    std::string description;  // what was hashed, for the report
};

Input read_input(const Options& o) {
    Input input;
    if (o.mode == Mode::kText) {
        std::string text;
        for (std::size_t i = 0; i < o.positional.size(); ++i) {
            if (i > 0) text += ' ';
            text += o.positional[i];
        }
        input.digest = sha256(text);
        input.description = "\"" + printable(text) + "\" (" + with_commas(text.size()) + " bytes)";
        return input;
    }

    // File and stdin modes hash a stream in 64 KiB pieces, so any size of input works.
    Sha256 hasher;
    std::vector<char> buffer(1 << 16);
    std::uint64_t size = 0;
    std::string preview;
    if (o.mode == Mode::kFile) {
        std::ifstream in(std::filesystem::u8path(o.file), std::ios::binary);
        if (!in) throw std::runtime_error("cannot open '" + o.file + "'");
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = static_cast<std::size_t>(in.gcount());
            hasher.update(buffer.data(), got);
            size += got;
        }
        if (in.bad()) throw std::runtime_error("error while reading '" + o.file + "'");
        input.description = "file " + o.file + " (" + with_commas(size) + " bytes)";
    } else {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);  // hash the exact bytes: no CRLF translation
#endif
        std::size_t got = 0;
        while ((got = std::fread(buffer.data(), 1, buffer.size(), stdin)) > 0) {
            hasher.update(buffer.data(), got);
            if (preview.size() < 64) preview.append(buffer.data(), std::min<std::size_t>(got, 64 - preview.size()));
            size += got;
        }
        if (std::ferror(stdin)) throw std::runtime_error("error while reading standard input");
        input.description = "standard input (" + with_commas(size) + " bytes) \"" + printable(preview) + "\"";
    }
    input.digest = hasher.finish();
    return input;
}

// ---- Modes --------------------------------------------------------------------------------------

void print_band(const Digest& digest) {
    const BandChoices c = band_choices(digest);
    std::cout << "Band      byte 26 = " << hex_pair(digest[kInstrumentByte]) << " -> melody on " << c.melody_instrument
              << ", harmony on " << c.harmony_instrument << "\n"
              << "          byte 27 = " << hex_pair(digest[kChordByte]) << " -> chords " << c.progression_name
              << " (then V-I to finish), harmony plays " << c.harmony_pattern_name << "\n"
              << "          byte 28 = " << hex_pair(digest[kBassByte]) << " -> " << c.bass_instrument << ", "
              << c.bass_pattern_name << "\n"
              << "          byte 29 = " << hex_pair(digest[kDrumByte]) << " -> " << c.groove_name << " groove, "
              << drum_options(c) << "\n";
}

void print_song_report(const Digest& digest, const Song& song, bool band) {
    std::cout << "SHA-256   " << to_hex(digest) << "\n\n";
    std::cout << "Melody    each byte is two hex digits: the first picks the pitch, the second the rhythm\n"
              << "          (e = eighth, q = quarter, q. = dotted quarter, h = half)\n";
    for (std::size_t row = 0; row < kMelodyNotes / 8; ++row) {
        std::string bytes = "  byte    ";
        std::string pitches = "  pitch   ";
        std::string rhythms = "  rhythm  ";
        for (std::size_t i = row * 8; i < row * 8 + 8; ++i) {
            bytes += pad(hex_pair(digest[i]), 5);
            pitches += pad(note_name(kPitchTable[digest[i] >> 4]), 5);
            rhythms += pad(rhythm_symbol(kRhythmTable[digest[i] & 0x0F]), 5);
        }
        std::cout << rtrim(bytes) << "\n" << rtrim(pitches) << "\n" << rtrim(rhythms) << "\n\n";
    }
    std::cout << "  ...then the home note " << note_name(kHomePitch) << " to finish\n\n";
    std::cout << "Tempo     byte 24 = " << hex_pair(digest[kTempoByte]) << " -> " << song.bpm << " BPM\n";
    std::cout << "Loudness  byte 25 = " << hex_pair(digest[kLoudnessByte]) << " -> velocity "
              << static_cast<int>(base_velocity_from_byte(digest[kLoudnessByte])) << " (accents on beats 1 and 3)\n";
    if (band) print_band(digest);
}

int run_single(const Options& o) {
    const Input input = read_input(o);
    if (o.hash_only) {
        std::cout << to_hex(input.digest) << "\n";
        return 0;
    }
    const std::string path = o.out.empty() ? "song.mid" : o.out;
    const Song song = make_song(input.digest, o.band);
    const std::vector<std::uint8_t> file = write_song(song, path);

    if (o.quiet) {
        std::cout << to_hex(input.digest) << "\nwrote " << path << "\n";
        return 0;
    }
    std::cout << "Input     " << input.description << "\n";
    print_song_report(input.digest, song, o.band);
    std::cout << "\nWrote " << path << ": " << with_commas(file.size()) << " bytes, " << song.tracks.size()
              << (song.tracks.size() == 1 ? " instrument track, " : " instrument tracks, ") << fixed1(song_seconds(song))
              << " seconds\n"
              << "File fingerprint (SHA-256 of " << path << "): " << to_hex(sha256(file)) << "\n"
              << "Run it again with the same input and both lines come out identical.\n";
    return 0;
}

int run_compare(const Options& o) {
    const std::string& a = o.positional[0];
    const std::string& b = o.positional[1];
    const Digest da = sha256(a);
    const Digest db = sha256(b);
    const int bits = count_bit_differences(da, db);

    std::cout << "Avalanche demo\n"
              << "  A = \"" << printable(a) << "\"\n"
              << "  B = \"" << printable(b) << "\"\n";
    const std::size_t edits = edit_distance(a, b);
    std::cout << "  The inputs are " << edits << (edits == 1 ? " edit" : " edits")
              << " apart (counting characters inserted, deleted or replaced).\n\n";

    std::cout << "SHA-256\n"
              << "  A  " << to_hex(da) << "\n"
              << "  B  " << to_hex(db) << "\n"
              << "     " << rtrim(hex_difference_marker(da, db)) << "\n"
              << "  " << count_hex_differences(da, db) << " of 64 hex digits differ (^)\n\n";

    std::cout << "Bits that flipped (# = flipped, . = same; 4 bytes per row)\n";
    std::istringstream grid(bit_difference_grid(da, db));
    for (std::string line; std::getline(grid, line);) std::cout << "  " << line << "\n";
    std::cout << "\n  " << bits << " of 256 bits differ (" << fixed1(100.0 * bits / 256.0) << "%). "
              << "An ideal hash flips each bit with probability 1/2: about 128, give or take 8.\n\n";

    const Song sa = make_song(da, o.band);
    const Song sb = make_song(db, o.band);
    std::cout << "Melodies (pitch + rhythm of each note; * marks notes with the same pitch)\n";
    int same_pitch = 0;
    int same_rhythm = 0;
    for (std::size_t row = 0; row < kMelodyNotes / 8; ++row) {
        std::string numbers = "  note ";
        std::string line_a = "  A    ";
        std::string line_b = "  B    ";
        std::string marks = "       ";
        for (std::size_t i = row * 8; i < row * 8 + 8; ++i) {
            const std::uint8_t pa = kPitchTable[da[i] >> 4];
            const std::uint8_t pb = kPitchTable[db[i] >> 4];
            const std::uint32_t ra = kRhythmTable[da[i] & 0x0F];
            const std::uint32_t rb = kRhythmTable[db[i] & 0x0F];
            numbers += pad(std::to_string(i + 1), 6);
            line_a += pad(note_name(pa) + rhythm_symbol(ra), 6);
            line_b += pad(note_name(pb) + rhythm_symbol(rb), 6);
            marks += pad(pa == pb ? "*" : "", 6);
            if (pa == pb) ++same_pitch;
            if (ra == rb) ++same_rhythm;
        }
        std::cout << rtrim(numbers) << "\n" << rtrim(line_a) << "\n" << rtrim(line_b) << "\n";
        if (!rtrim(marks).empty()) std::cout << rtrim(marks) << "\n";
        std::cout << "\n";
    }
    std::cout << "  Same pitch in " << same_pitch << " of 24 notes, same rhythm in " << same_rhythm
              << " (chance alone gives about 1.5 and 7.5).\n"
              << "  Tempo: A = " << sa.bpm << " BPM, B = " << sb.bpm << " BPM.\n\n";

    std::string prefix = o.out.empty() ? "compare" : o.out;
    if (prefix.size() > 4 && prefix.compare(prefix.size() - 4, 4, ".mid") == 0) prefix.resize(prefix.size() - 4);
    const std::string path_a = prefix + "_a.mid";
    const std::string path_b = prefix + "_b.mid";
    write_song(sa, path_a);
    write_song(sb, path_b);
    std::cout << "Wrote " << path_a << " (A) and " << path_b << " (B). Play them back to back.\n";
    return 0;
}

int run_stats(const Options& o) {
    const std::uint64_t count = o.positional.empty() ? 10000 : parse_number(o.positional[0], "--stats count");
    if (count < 1 || count > 10000000) throw UsageError("--stats count must be between 1 and 10,000,000");
    const Distribution d = measure_distribution(count, o.seed);
    std::cout << distribution_text(d);

    const std::string html_path = o.out.empty() ? "distribution.html" : o.out;
    const std::string csv_path = std::filesystem::u8path(html_path).replace_extension(".csv").u8string();
    save_text(html_path, distribution_html(d));
    save_text(csv_path, distribution_csv(d));
    std::cout << "\nWrote " << html_path << " (charts: open it in a browser) and " << csv_path << " (raw counts).\n";
    return 0;
}

int run(const Options& o) {
    if (o.help) {
        std::cout << kUsage;
        return 0;
    }
    switch (o.mode) {
        case Mode::kCompare: return run_compare(o);
        case Mode::kStats: return run_stats(o);
        default: return run_single(o);
    }
}

// Command-line arguments as UTF-8. On Windows, argv arrives in the local code page, which would make
// non-ASCII text hash differently than on macOS or Linux, so read the UTF-16 command line instead.
std::vector<std::string> utf8_arguments(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide != nullptr) {
        for (int i = 1; i < count; ++i) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(bytes > 1 ? static_cast<std::size_t>(bytes - 1) : 0, '\0');
            if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), bytes, nullptr, nullptr);
            args.push_back(std::move(arg));
        }
        LocalFree(wide);
        return args;
    }
#endif
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return args;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // show UTF-8 input text correctly in the console
#endif
    try {
        return run(parse_args(utf8_arguments(argc, argv)));
    } catch (const UsageError& e) {
        std::cerr << "hashmusic: " << e.what() << "\nRun 'hashmusic --help' for usage.\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "hashmusic: error: " << e.what() << "\n";
        return 1;
    }
}
