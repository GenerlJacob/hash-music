// main.cpp - the hashmusic command-line tool.
// Owner: Member D. Backup: Member C.
//
// main reads the input, calls each module in order, and writes the .mid file:
//   input bytes --sha256()--> 32-byte digest --map_digest()--> Song --write_midi()--> bytes --> song.mid
// The other modes reuse that pipeline: a password's derived key, a mined hash or a toy hash also
// becomes a song.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
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
#include "extras.h"
#include "mapper.h"
#include "midi_writer.h"
#include "password.h"
#include "sha256.h"
#include "stats.h"
#include "toy_hash.h"

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

Songs:
  hashmusic [options] <text...>          hash the text (words are joined with spaces), write its song
  hashmusic [options] --file <path>      hash a file's bytes and write its "theme song"
  hashmusic [options] --stdin            hash everything read from standard input

Seeing what makes a good hash:
  hashmusic --compare <a> <b>            avalanche demo: compare two inputs bit by bit, write both songs
  hashmusic --sweep <text...>            flip one bit at every input position, count output bits that change
  hashmusic --collide [text...]          two different inputs with the same toy hash (same song)
  hashmusic --stats [count]              hash random inputs, chart how evenly bytes and pitches spread

Passwords, integrity and proof of work:
  hashmusic --password <password|->      salted PBKDF2-SHA256 record for a password, and its song
  hashmusic --register <user> <password|->   add a user to the password store
  hashmusic --login <user> <password|->      check a login; wrong guesses lock the account
  hashmusic --file <path> --verify <hex> check a file against a known SHA-256 (tamper detection)
  hashmusic --mine <text...>             find a nonce whose hash starts with zeros (--difficulty)

Options:
  -o, --out <path>     output file (default song.mid). With --compare and --collide it is a prefix
                       for <prefix>_a.mid and <prefix>_b.mid. With --stats it names the chart page
                       (default distribution.html; a .csv is written beside it)
  --hash <sha256|toy>  the hash to use (default sha256); toy is a deliberately bad XOR hash
  --band               add hash-chosen instruments, harmony, bass and drums
  --art <file.svg>     also draw the hash as a picture
  --wav <file.wav>     also render the song as audio
  --hash-only          print the digest and stop
  --seed <n>           random seed for --stats (default 2710)
  --salt <hex>         salt for --password (default: 16 random bytes)
  --iterations <n>     PBKDF2 iterations for --password and --register (default 600000)
  --store <file>       password store for --register and --login (default users.txt)
  --difficulty <n>     zero hex digits for --mine, 0-7 (default 4)
  -q, --quiet          with text, --file or --stdin: print only the digest and the output file name
  -h, --help           show this help

A password given as - is read from standard input, which keeps it out of your shell history.

Examples:
  hashmusic jacob
  hashmusic jacob -o jacob.mid --art jacob.svg --wav jacob.wav
  hashmusic --compare jacob jacob1
  hashmusic --compare jacob jacob1 --hash toy
  hashmusic --band --file README.md -o readme_theme.mid
  hashmusic --register alice - --store users.txt
  hashmusic --mine jacob --difficulty 5
)";

enum class Mode { kText, kFile, kStdin, kCompare, kStats, kSweep, kCollide, kPassword, kRegister, kLogin, kMine };

struct Options {
    Mode mode = Mode::kText;
    bool mode_set = false;
    bool help = false;
    std::vector<std::string> positional;
    std::string out;
    std::string file;
    HashKind hash = HashKind::kSha256;
    bool hash_set = false;
    bool band = false;
    bool hash_only = false;
    bool quiet = false;
    std::uint64_t seed = 2710;
    std::string art;
    std::string wav;
    std::string verify;  // expected SHA-256, lowercase hex
    std::string salt;    // hex
    std::uint32_t iterations = kDefaultIterations;
    std::string store = "users.txt";
    int difficulty = 4;
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

bool is_hex(const std::string& text) {
    return !text.empty() && text.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

Options parse_args(const std::vector<std::string>& args) {
    Options o;
    const auto set_mode = [&o](Mode mode) {
        if (o.mode_set && o.mode != mode) {
            throw UsageError("use only one of --file, --stdin, --compare, --stats, --sweep, --collide, --password, "
                             "--register, --login and --mine");
        }
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
        } else if (arg == "--sweep") {
            set_mode(Mode::kSweep);
        } else if (arg == "--collide") {
            set_mode(Mode::kCollide);
        } else if (arg == "--password") {
            set_mode(Mode::kPassword);
        } else if (arg == "--register") {
            set_mode(Mode::kRegister);
        } else if (arg == "--login") {
            set_mode(Mode::kLogin);
        } else if (arg == "--mine") {
            set_mode(Mode::kMine);
        } else if (arg == "--hash") {
            const std::string& name = value();
            try {
                o.hash = parse_hash_kind(name);
            } catch (const std::invalid_argument& e) {
                throw UsageError(e.what());
            }
            o.hash_set = true;
        } else if (arg == "--band") {
            o.band = true;
        } else if (arg == "--art") {
            o.art = value();
        } else if (arg == "--wav") {
            o.wav = value();
        } else if (arg == "--hash-only") {
            o.hash_only = true;
        } else if (arg == "--verify") {
            o.verify = value();
        } else if (arg == "--seed") {
            o.seed = parse_number(value(), "--seed");
        } else if (arg == "--salt") {
            o.salt = value();
        } else if (arg == "--iterations") {
            const std::uint64_t n = parse_number(value(), "--iterations");
            if (n < 1 || n > 10000000) throw UsageError("--iterations must be between 1 and 10,000,000");
            o.iterations = static_cast<std::uint32_t>(n);
        } else if (arg == "--store") {
            o.store = value();
        } else if (arg == "--difficulty") {
            const std::uint64_t n = parse_number(value(), "--difficulty");
            if (n > 7) throw UsageError("--difficulty must be 0-7 (each step is 16 times more work)");
            o.difficulty = static_cast<int>(n);
        } else if (arg == "-q" || arg == "--quiet") {
            o.quiet = true;
        } else {
            throw UsageError("unknown option '" + arg + "' (put -- before text that starts with '-')");
        }
    }
    if (o.help) return o;

    const bool single = o.mode == Mode::kText || o.mode == Mode::kFile || o.mode == Mode::kStdin;
    const bool one_song = single || o.mode == Mode::kPassword || o.mode == Mode::kMine;
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
        case Mode::kSweep:
        case Mode::kMine:
            if (o.positional.empty()) throw UsageError("this mode needs some text, e.g. --mine jacob");
            break;
        case Mode::kCollide:
            break;
        case Mode::kPassword:
            if (o.positional.size() != 1) throw UsageError("--password needs one password (or - to read it from standard input)");
            break;
        case Mode::kRegister:
        case Mode::kLogin:
            if (o.positional.size() != 2) throw UsageError("--register and --login need a user name and a password (or -)");
            break;
    }
    if (o.hash_only && !single) throw UsageError("--hash-only works with text, --file or --stdin");
    if (o.hash == HashKind::kToy && !(single || o.mode == Mode::kCompare || o.mode == Mode::kStats || o.mode == Mode::kSweep)) {
        throw UsageError("--hash toy works with text, --file, --stdin, --compare, --stats and --sweep");
    }
    if (!o.verify.empty()) {
        if (!single) throw UsageError("--verify works with text, --file or --stdin");
        if (o.hash != HashKind::kSha256) throw UsageError("--verify checks SHA-256 digests; leave out --hash toy");
        if (o.verify.size() != 64 || !is_hex(o.verify)) throw UsageError("--verify needs a 64-digit hex SHA-256 digest");
        std::transform(o.verify.begin(), o.verify.end(), o.verify.begin(),
                       [](char c) { return static_cast<char>(c >= 'A' && c <= 'F' ? c - 'A' + 'a' : c); });
    }
    if ((!o.art.empty() || !o.wav.empty()) && !one_song) {
        throw UsageError("--art and --wav work in modes that write one song: text, --file, --stdin, --password, --mine");
    }
    if (!o.salt.empty()) {
        if (o.mode != Mode::kPassword) throw UsageError("--salt works with --password");
        if (o.salt.size() % 2 != 0 || !is_hex(o.salt)) throw UsageError("--salt must be hex, e.g. 00112233445566778899aabbccddeeff");
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

std::string fixed(double value, int places) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
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

std::string join_words(const std::vector<std::string>& words) {
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) text += ' ';
        text += words[i];
    }
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

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
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

const char* digest_label(HashKind kind) { return kind == HashKind::kToy ? "Toy hash" : "SHA-256"; }

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

struct Written {
    Song song;
    std::vector<std::uint8_t> midi;
};

// The .mid file, plus the picture and the audio when --art and --wav ask for them.
Written write_outputs(const Digest& digest, const Options& o, const std::string& path) {
    Written written;
    written.song = make_song(digest, o.band);
    written.midi = write_song(written.song, path);
    if (!o.art.empty()) save_text(o.art, hash_art_svg(digest));
    if (!o.wav.empty()) save_file(o.wav, render_wav(written.song));
    return written;
}

struct Input {
    Digest digest{};
    std::string description;  // what was hashed, for the report
};

Input read_input(const Options& o) {
    Input input;
    if (o.mode == Mode::kText) {
        const std::string text = join_words(o.positional);
        input.digest = hash_text(o.hash, text);
        input.description = "\"" + printable(text) + "\" (" + with_commas(text.size()) + " bytes)";
        return input;
    }

    // File and stdin modes hash a stream in 64 KiB pieces, so any size of input works.
    AnyHasher hasher(o.hash);
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

// ---- Reports ------------------------------------------------------------------------------------

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

void print_song_report(const Digest& digest, const Song& song, bool band, const std::string& label) {
    std::cout << pad(label, 10) << to_hex(digest) << "\n\n";
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

void print_written(const std::string& path, const Written& written, const Options& o) {
    std::cout << "\nWrote " << path << ": " << with_commas(written.midi.size()) << " bytes, " << written.song.tracks.size()
              << (written.song.tracks.size() == 1 ? " instrument track, " : " instrument tracks, ")
              << fixed(song_seconds(written.song), 1) << " seconds\n";
    if (!o.art.empty()) std::cout << "Wrote " << o.art << ": hash art, a picture drawn from the same 32 bytes\n";
    if (!o.wav.empty()) std::cout << "Wrote " << o.wav << ": the song as audio\n";
    std::cout << "File fingerprint (SHA-256 of " << path << "): " << to_hex(sha256(written.midi)) << "\n"
              << "Run it again with the same input and both lines come out identical.\n";
}

// Prints whether the digest matches the expected one; true when it does.
bool check_integrity(const Digest& digest, const std::string& expected_hex) {
    const std::vector<std::uint8_t> bytes = from_hex(expected_hex);
    Digest expected{};
    std::copy(bytes.begin(), bytes.end(), expected.begin());
    const int bits = count_bit_differences(digest, expected);
    if (bits == 0) {
        std::cout << "Integrity MATCH: the SHA-256 equals the expected value, so the input is unchanged.\n";
        return true;
    }
    std::cout << "Integrity MISMATCH: " << bits << " of 256 bits differ from the expected SHA-256.\n"
              << "  expected " << expected_hex << "\n"
              << "  actual   " << to_hex(digest) << "\n"
              << "  The input was changed or corrupted.\n";
    return false;
}

// ---- Modes --------------------------------------------------------------------------------------

int run_single(const Options& o) {
    const Input input = read_input(o);
    const auto verified = [&]() { return o.verify.empty() || check_integrity(input.digest, o.verify); };
    if (o.hash_only) {
        std::cout << to_hex(input.digest) << "\n";
        return verified() ? 0 : 1;
    }
    const std::string path = o.out.empty() ? "song.mid" : o.out;
    const Written written = write_outputs(input.digest, o, path);
    if (o.quiet) {
        std::cout << to_hex(input.digest) << "\nwrote " << path << "\n";
        return verified() ? 0 : 1;
    }
    std::cout << "Input     " << input.description << "\n";
    if (o.hash == HashKind::kToy) {
        std::cout << "Hash      the toy XOR hash: short inputs show straight through, and one changed character\n"
                  << "          changes one note. Compare with the SHA-256 song of the same input.\n";
    }
    print_song_report(input.digest, written.song, o.band, digest_label(o.hash));
    print_written(path, written, o);
    if (o.verify.empty()) return 0;
    std::cout << "\n";
    return verified() ? 0 : 1;
}

int run_compare(const Options& o) {
    const std::string& a = o.positional[0];
    const std::string& b = o.positional[1];
    const Digest da = hash_text(o.hash, a);
    const Digest db = hash_text(o.hash, b);
    const int bits = count_bit_differences(da, db);

    std::cout << "Avalanche demo" << (o.hash == HashKind::kToy ? " with the toy XOR hash\n" : "\n")
              << "  A = \"" << printable(a) << "\"\n"
              << "  B = \"" << printable(b) << "\"\n";
    const std::size_t edits = edit_distance(a, b);
    std::cout << "  The inputs are " << edits << (edits == 1 ? " edit" : " edits")
              << " apart (counting characters inserted, deleted or replaced).\n\n";

    std::cout << digest_label(o.hash) << "\n"
              << "  A  " << to_hex(da) << "\n"
              << "  B  " << to_hex(db) << "\n"
              << "     " << rtrim(hex_difference_marker(da, db)) << "\n"
              << "  " << count_hex_differences(da, db) << " of 64 hex digits differ (^)\n\n";

    std::cout << "Bits that flipped (# = flipped, . = same; 4 bytes per row)\n";
    std::istringstream grid(bit_difference_grid(da, db));
    for (std::string line; std::getline(grid, line);) std::cout << "  " << line << "\n";
    std::cout << "\n  " << bits << " of 256 bits differ (" << fixed(100.0 * bits / 256.0, 1) << "%). "
              << "An ideal hash flips each bit with probability 1/2: about 128, give or take 8.\n";
    if (o.hash == HashKind::kToy) {
        std::cout << "  The toy hash has no avalanche effect: the change stays where it happened.\n";
    }
    std::cout << "\n";

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

int run_sweep(const Options& o) {
    const std::string text = join_words(o.positional);
    if (text.size() > 200) throw UsageError("--sweep takes at most 200 bytes of text");
    const std::vector<int> flips = avalanche_sweep(text, o.hash);
    std::cout << "Avalanche sweep (" << hash_display_name(o.hash) << "): flip the lowest bit of each input byte in turn\n"
              << "and count the output bits that change. An ideal hash changes about 128 of 256, wherever the\n"
              << "change is.\n\n"
              << "  byte  char  bits\n";
    int total = 0;
    int lowest = 256;
    int highest = 0;
    for (std::size_t i = 0; i < flips.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::string shown = (c >= 0x21 && c < 0x7F) ? std::string(1, static_cast<char>(c)) : "0x" + hex_pair(c);
        std::cout << "  " << pad(std::to_string(i + 1), 6) << pad(shown, 6) << pad(std::to_string(flips[i]), 5)
                  << std::string(static_cast<std::size_t>((flips[i] + 3) / 4), '#') << "\n";
        total += flips[i];
        lowest = std::min(lowest, flips[i]);
        highest = std::max(highest, flips[i]);
    }
    std::cout << "\n  Average " << fixed(static_cast<double>(total) / static_cast<double>(flips.size()), 1)
              << " bits (lowest " << lowest << ", highest " << highest << ") over " << flips.size() << " positions.\n";
    return 0;
}

int run_collide(const Options& o) {
    const std::string base = o.positional.empty() ? std::string("Bubble Sort Bandits") : join_words(o.positional);
    const std::pair<std::string, std::string> pair = make_toy_collision(base);
    const std::string& a = pair.first;
    const std::string& b = pair.second;
    const Digest toy_a = toy_hash(a);
    const Digest toy_b = toy_hash(b);
    const Digest sha_a = sha256(a);
    const Digest sha_b = sha256(b);

    std::cout << "A hash collision: two different inputs, one toy hash\n"
              << "  A = \"" << printable(a, 80) << "\"\n"
              << "  B = \"" << printable(b, 80) << "\"   (characters 1 and 33 swapped)\n\n"
              << "Toy hash\n"
              << "  A  " << to_hex(toy_a) << "\n"
              << "  B  " << to_hex(toy_b) << "\n"
              << (toy_a == toy_b ? "  Identical. Found instantly, because each digest byte only XORs input bytes\n"
                                   "  32 apart, and XOR does not care about order.\n\n"
                                 : "  Different (unexpected).\n\n")
              << "SHA-256\n"
              << "  A  " << to_hex(sha_a) << "\n"
              << "  B  " << to_hex(sha_b) << "\n"
              << "  " << count_bit_differences(sha_a, sha_b) << " of 256 bits differ. Finding a SHA-256 collision by\n"
              << "  brute force would take about 2^128 tries.\n\n";

    std::string prefix = o.out.empty() ? "collision" : o.out;
    if (prefix.size() > 4 && prefix.compare(prefix.size() - 4, 4, ".mid") == 0) prefix.resize(prefix.size() - 4);
    const std::string path_a = prefix + "_a.mid";
    const std::string path_b = prefix + "_b.mid";
    const std::vector<std::uint8_t> file_a = write_song(make_song(toy_a, o.band), path_a);
    const std::vector<std::uint8_t> file_b = write_song(make_song(toy_b, o.band), path_b);
    std::cout << "Wrote " << path_a << " and " << path_b << " from the toy hash: "
              << (file_a == file_b ? "byte-identical files, the same song for two different inputs.\n"
                                   : "different files.\n");
    return 0;
}

// The password argument, or a line from standard input when it is "-".
std::string password_argument(const std::string& argument) {
    std::string password = argument;
    if (argument == "-") {
        if (!std::getline(std::cin, password)) password.clear();
        if (!password.empty() && password.back() == '\r') password.pop_back();
    }
    if (password.empty()) throw UsageError("the password must not be empty");
    return password;
}

int run_password(const Options& o) {
    const std::string password = password_argument(o.positional[0]);
    const std::vector<std::uint8_t> salt = o.salt.empty() ? random_salt() : from_hex(o.salt);
    const auto start = std::chrono::steady_clock::now();
    const PasswordRecord record = make_record(password, salt, o.iterations);
    const double seconds = seconds_since(start);

    const std::string path = o.out.empty() ? "song.mid" : o.out;
    const Written written = write_outputs(record.key, o, path);
    if (o.quiet) {
        std::cout << format_record(record) << "\nwrote " << path << "\n";
        return 0;
    }
    std::cout << "Password  (hidden; it is never stored)\n"
              << "Salt      " << bytes_to_hex(salt) << (o.salt.empty() ? "  (16 random bytes)\n" : "  (given)\n")
              << "PBKDF2    " << with_commas(o.iterations) << " rounds of HMAC-SHA256 in " << fixed(seconds, 2)
              << " s: every guess costs an attacker the same\n"
              << "Record    " << format_record(record) << "\n\n"
              << "The song comes from the stored key. Run this again without --salt and the same password gets a\n"
              << "new salt, a new record and a different song, so equal passwords never look equal.\n\n";
    print_song_report(record.key, written.song, o.band, "Key");
    print_written(path, written, o);
    return 0;
}

int run_register(const Options& o) {
    const std::string& user = o.positional[0];
    const std::string password = password_argument(o.positional[1]);
    PasswordStore store;
    store.load(o.store);
    store.add_user(user, make_record(password, random_salt(), o.iterations));
    store.save(o.store);
    std::cout << "Registered " << user << " in " << o.store << ": a random salt and " << with_commas(o.iterations)
              << " rounds of PBKDF2-SHA256.\nThe store keeps only the salt and the derived key, never the password.\n";
    return 0;
}

int run_login(const Options& o) {
    const std::string& user = o.positional[0];
    const std::string password = password_argument(o.positional[1]);
    PasswordStore store;
    store.load(o.store);
    const PasswordStore::Attempt attempt = store.login(user, password, static_cast<long long>(std::time(nullptr)));
    store.save(o.store);
    switch (attempt.result) {
        case PasswordStore::Result::kOk:
            std::cout << "Login OK: welcome, " << user << ".\n";
            return 0;
        case PasswordStore::Result::kLocked:
            std::cout << "Account locked: try again in " << attempt.wait_seconds << " s. The password was not even checked.\n";
            return 1;
        case PasswordStore::Result::kNoSuchUser:
            std::cout << "Login failed: wrong user name or password.\n";
            return 1;
        case PasswordStore::Result::kWrongPassword:
            // A real site would not say which part was wrong; the counter is shown so the lockout can be demonstrated.
            std::cout << "Login failed: wrong user name or password (" << attempt.failures << " wrong in a row).\n";
            if (attempt.wait_seconds > 0) {
                std::cout << "Too many wrong guesses: the account is locked for " << attempt.wait_seconds
                          << " s, and each further wrong guess doubles that.\n";
            } else {
                std::cout << (PasswordStore::kFreeAttempts - attempt.failures)
                          << " more wrong guesses before the account locks.\n";
            }
            return 1;
    }
    return 1;
}

int run_mine(const Options& o) {
    const std::string text = join_words(o.positional);
    const auto start = std::chrono::steady_clock::now();
    const MineResult result = mine(text, o.difficulty, std::uint64_t{1} << 40);
    const double seconds = seconds_since(start);
    if (!result.found) throw std::runtime_error("gave up before finding a nonce");

    std::uint64_t expected = 1;
    for (int i = 0; i < o.difficulty; ++i) expected *= 16;
    const std::string path = o.out.empty() ? "song.mid" : o.out;
    const Written written = write_outputs(result.digest, o, path);
    std::cout << "Mined     \"" << printable(result.input) << "\" after " << with_commas(result.attempts) << " tries in "
              << fixed(seconds, 2) << " s (" << with_commas(static_cast<std::uint64_t>(result.attempts / std::max(seconds, 1e-6)))
              << " hashes per second)\n"
              << "Target    the first " << o.difficulty << " hex digits of the SHA-256 must be 0: about "
              << with_commas(expected) << " tries on average, while checking the answer takes one.\n"
              << "          Blockchains use the same puzzle as proof of work.\n\n";
    print_song_report(result.digest, written.song, o.band, "SHA-256");
    print_written(path, written, o);
    return 0;
}

int run_stats(const Options& o) {
    const std::uint64_t count = o.positional.empty() ? 10000 : parse_number(o.positional[0], "--stats count");
    if (count < 1 || count > 10000000) throw UsageError("--stats count must be between 1 and 10,000,000");
    const Distribution d = measure_distribution(count, o.seed, o.hash);
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
        case Mode::kSweep: return run_sweep(o);
        case Mode::kCollide: return run_collide(o);
        case Mode::kPassword: return run_password(o);
        case Mode::kRegister: return run_register(o);
        case Mode::kLogin: return run_login(o);
        case Mode::kMine: return run_mine(o);
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
