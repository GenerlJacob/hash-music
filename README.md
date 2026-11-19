# Hash Music Generator

**Bubble Sort Bandits** (CSC 2710): Jacob Breeden, Mabel Boateng, Mikey Greco, Ysabella Coker

A C++ command-line tool that turns any text or file into a short melody. A SHA-256 hash we wrote
from scratch decides every note, so you can *hear* the two properties that make hashes useful:

- **Deterministic:** the same input always gives the same song, byte for byte.
- **Avalanche effect:** change one character and you get a completely different song.

```text
hashmusic jacob -o jacob.mid           # play it
hashmusic jacob -o jacob_again.mid     # identical file, same song
hashmusic jacob1 -o jacob1.mid         # one character more: a different song
hashmusic --compare jacob jacob1       # see which of the 256 hash bits flipped
```

## Build

You need a C++17 compiler and CMake 3.16 or newer. Python 3 is optional; without it, three of the checks
(the hashlib and mapping cross-checks and the MIDI playback check) are skipped.

**Windows.** The simplest setup is WinLibs, one package with GCC, CMake and Ninja. After installing it,
open a new terminal so it is on your PATH:

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

With Visual Studio 2022 instead (workload "Desktop development with C++"), run the same commands from a
Developer PowerShell, without `-G Ninja`, and add `--config Release` to the build and `-C Release` to ctest.

**macOS / Linux:**

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The executables land in `build/` (`build/Release/` with Visual Studio): `hashmusic`, `hardcoded_melody`
(task 6: the MIDI writer on its own), and the test programs.

**Live demo page.** [demo/live_demo.html](demo/live_demo.html) runs every feature in a browser with no
build or internet: type any text (or drop a file) and hear its song, compare two inputs bit by bit, step
through padding and the 64 rounds, and chart the distribution. Its JavaScript is a port of the C++
modules, and it checks itself on load against NIST's vectors and the C++ build's demo songs.

To hear the results before building anything, open [examples/](examples/): the demo songs for `jacob`
and `jacob1` (plain and `--band`), the task-6 test melody, the avalanche printout, and the
distribution charts (`distribution.html`).

## Usage

```text
hashmusic [options] <text...>          hash the text (words are joined with spaces), write its song
hashmusic [options] --file <path>      hash a file's bytes and write its "theme song"
hashmusic [options] --stdin            hash everything read from standard input
hashmusic [options] --compare <a> <b>  avalanche demo: compare two inputs bit by bit, write both songs
hashmusic --stats [count]              hash random inputs, chart how evenly bytes and pitches spread

-o, --out <path>   output file (default song.mid; a prefix with --compare; the chart page with --stats)
--band             add hash-chosen instruments, harmony, bass and drums
--hash-only        print the SHA-256 digest and stop
--seed <n>         random seed for --stats (default 2710)
-q, --quiet        with text, --file or --stdin: print only the digest and the output file name
```

Text is hashed as UTF-8 on every platform, so `hashmusic --hash-only abc` prints the same digest as
`printf abc | sha256sum`. Put `--` before text that starts with a dash.

### Playing the songs

- **Windows:** double-click the `.mid` file, or play it from the terminal with
  `.\tools\play.ps1 jacob.mid` (several files play back to back). If PowerShell refuses to run
  scripts, first run `Set-ExecutionPolicy -Scope Process Bypass`, which lasts only for that window.
- **Any platform:** [MuseScore](https://musescore.org) opens `.mid` files and also shows the melody as
  sheet music. On Linux, `timidity song.mid` works too.

## How a hash becomes a song

A SHA-256 digest is 32 bytes, printed as 64 hex digits. Each byte is two hex digits, and the mapper
reads them in order:

| Bytes | Hex digits | Controls |
|---|---|---|
| 0-23 | 1-48 | 24 melody notes: the **first** digit of each byte picks the pitch, the **second** picks the rhythm |
| 24 | 49-50 | tempo: 80 + byte × 80 / 255, so 80-160 BPM |
| 25 | 51-52 | loudness: base velocity 60 + byte × 40 / 255, so 60-100, with accents on beats 1 and 3 |
| 26-29 | 53-60 | `--band` only: instruments, chords, bass and drums (see [src/band.h](src/band.h)) |
| 30-31 | 61-64 | spare |

| Hex digit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | a | b | c | d | e | f |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Pitch** (first digit) | G3 | A3 | C4 | D4 | E4 | G4 | A4 | C5 | D5 | E5 | G5 | A5 | C6 | D6 | E6 | G6 |
| **Rhythm** (second digit) | e | e | e | e | e | e | q | q | q | q | q | q | q. | q. | h | h |

Rhythms use the same letters the tool prints: e = eighth note, q = quarter, q. = dotted quarter,
h = half. The pitches are the C major pentatonic scale (C D E G A), which has no clashing half steps, so any
order of notes sounds consonant. Both tables have one entry per hex digit, so with random-looking
bytes every pitch and rhythm is equally likely. After the 24 notes the melody lands on C5 and holds it
until a bar line at least two beats later, so every song sounds finished.

**Worked example.** `sha256("jacob")` starts `c3 5d 6a af`: C6 eighth, G4 dotted quarter, A4 quarter,
G5 half. Byte 24 is `c0` (192), so the tempo is 80 + 192 × 80 / 255 = 140 BPM. The tool prints this
table for every song it writes.

## The four checks

The project counts as correct when these pass. `ctest` runs all of them.

| Check | What it proves | Test |
|---|---|---|
| Known vectors | Our SHA-256 matches NIST's published outputs for `""`, `"abc"`, the two-block 448-bit message, the 896-bit message and one million `a`s, plus every padding boundary | `unit_sha256` |
| Cross-check | Our hash matches Python's `hashlib` on 1,000 random inputs, including every length from 0 to 200 bytes | `sha256_cross_check` |
| Determinism | The same input gives a byte-identical `.mid` every run; a one-character change gives a different file. The demo songs' fingerprints are pinned, so every machine and compiler must agree | `cli_determinism`, `unit_pipeline` |
| Playback | Every generated file parses cleanly with an independent MIDI reader ([tests/check_midi.py](tests/check_midi.py)); on Windows, `tools\play.ps1 -CheckOnly song.mid` confirms the built-in player opens it | `midi_playback_check` |

The other unit tests check the mapper (`unit_mapper`), the exact bytes the MIDI writer produces
(`unit_midi_writer`), and the avalanche, band and stats code (`unit_pipeline`). One more check goes past
the plan: `mapping_cross_check` rebuilds each song from the byte map rules in Python
([tests/check_mapping.py](tests/check_mapping.py)), working from `hashlib`'s digest, and requires the
`.mid` the CLI wrote to match it note for note on 30 inputs.

## Beyond the plan

The plan's one stretch goal and the proposal's extras are all built. The extras are opt-in, so
the core song is exactly what the plan describes.

- **Avalanche demo** (`--compare`): both digests with the differing hex digits marked, a grid of which
  of the 256 bits flipped, the bit count against the ideal 128 ± 8, and both melodies side by side.
- **Band** (`--band`): bytes 26-29 pick the melody and harmony instruments, one of 16 chord progressions,
  a bass instrument and pattern, and a drum groove with options. The last two bars are always V → I.
- **File mode** (`--file`): hashes any file in 64 KiB pieces, so even large files get a theme song.
- **Distribution charts** (`--stats`): hashes 10,000 random inputs and writes `distribution.html`, with
  charts of pitch and byte-value frequencies and their chi-square statistics, plus the raw counts as CSV.

## Project layout and owners

| Path | What it is | Owner | Backup |
|---|---|---|---|
| [src/sha256.h](src/sha256.h), [src/sha256.cpp](src/sha256.cpp) | SHA-256 from scratch: padding, message schedule, 64 rounds | A (Jacob) | B |
| [src/avalanche.h](src/avalanche.h), [src/avalanche.cpp](src/avalanche.cpp) | Stretch goal: bit-difference counts, grid, edit distance | A (Jacob) | B |
| [src/mapper.h](src/mapper.h), [src/mapper.cpp](src/mapper.cpp) | Digest → notes, tempo, velocity | B | A |
| [src/song.h](src/song.h) | The `Song`/`Track`/`Note` contract between the mapper and the writer | B and C | |
| [src/midi_writer.h](src/midi_writer.h), [src/midi_writer.cpp](src/midi_writer.cpp) | Standard MIDI File writer: header, tracks, delta times | C | D |
| [tools/hardcoded_melody.cpp](tools/hardcoded_melody.cpp) | Task 6: a fixed melody through the writer alone | C | D |
| [src/main.cpp](src/main.cpp), [CMakeLists.txt](CMakeLists.txt), [tests/](tests/) | CLI, build, unit tests | D | C |
| [src/band.cpp](src/band.cpp), [src/stats.cpp](src/stats.cpp) | Proposal extras: band layers, distribution charts | unassigned | |
| [docs/REPORT.md](docs/REPORT.md), [docs/DEMO.md](docs/DEMO.md) | Report draft and 5-minute demo script | All | |

Replace B, C and D with names once roles are settled.

## Team workflow

- Each module lives on its own branch (`sha256`, `mapper`, `midi-writer`, `cli`) and merges through a
  pull request that one other member reviews.
- Run `ctest` before opening a pull request. A failing `demo_song_fingerprints_are_pinned` test means
  the demo songs changed; that is fine if you meant it, but update the pinned values and tell the group.
- The interfaces in [src/sha256.h](src/sha256.h), [src/song.h](src/song.h), [src/mapper.h](src/mapper.h) and
  [src/midi_writer.h](src/midi_writer.h) were agreed on day 2. Change them only after telling everyone.

## A security note

Do not feed this tool a real password. The song gives away roughly 150 of the hash's 256 bits (4 per
pitch, about 2 per rhythm, plus most of the tempo and loudness bytes), which is plenty for anyone holding
the `.mid` file to confirm a guessed password offline: hash the guess and compare songs. That is
exactly why real systems store passwords with a salt and a deliberately slow hash such as bcrypt or
Argon2, never plain SHA-256.
