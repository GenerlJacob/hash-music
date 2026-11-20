# Hash Music Generator

[![build-and-test](https://github.com/GenerlJacob/hash-music/actions/workflows/ci.yml/badge.svg)](https://github.com/GenerlJacob/hash-music/actions/workflows/ci.yml)

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

**Try it in a browser:** [generljacob.github.io/hash-music](https://generljacob.github.io/hash-music/)
(the live demo page, [demo/live_demo.html](demo/live_demo.html); it also works offline from a copy of
this folder). Its **Try it on your phone** button shows a QR code to that address for an audience to
scan; `live_demo.html#qr` opens the page with the QR code already showing.

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
(task 6: the MIDI writer on its own), and the test programs. GitHub Actions runs the same build and
tests with GCC, Clang and Visual Studio on every push ([.github/workflows/ci.yml](.github/workflows/ci.yml)).

To hear and see results before building anything, open [examples/](examples/): the demo songs (plain,
`--band` and as `jacob.wav` audio), hash art, the avalanche printouts for SHA-256 and the toy hash, a toy
collision, a mined song, a password record, and the distribution charts.

## Usage

```text
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
  -o, --out <path>     output file (default song.mid); a prefix with --compare and --collide;
                       the chart page with --stats
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
```

Text is hashed as UTF-8 on every platform, so `hashmusic --hash-only abc` prints the same digest as
`printf abc | sha256sum`. Put `--` before text that starts with a dash. A password given as `-` is read
from the keyboard, which keeps it out of your shell history.

### Playing the songs

- **Windows:** double-click the `.mid` file, or play it from the terminal with
  `.\tools\play.ps1 jacob.mid` (several files play back to back). If PowerShell refuses to run
  scripts, first run `Set-ExecutionPolicy -Scope Process Bypass`, which lasts only for that window.
- **Any platform:** `--wav song.wav` writes audio any player can open. [MuseScore](https://musescore.org)
  opens `.mid` files and shows the melody as sheet music; the demo page shows sheet music too.

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

## A bad hash, for contrast

`--hash toy` swaps SHA-256 for a deliberately bad hash ([src/toy_hash.cpp](src/toy_hash.cpp)): each digest
byte is just the XOR of the input bytes 32 positions apart. It works in every song mode, `--compare`,
`--sweep` and `--stats`, and the demo page has a switch for it.

| Test | SHA-256 | Toy hash |
|---|---|---|
| `jacob` vs `jacob1`: bits that flip | 146 of 256 | 3 of 256 |
| Notes with a new pitch | 22 of 24, and the tempo | 1 of 24 |
| Two inputs with the same song | about 2^128 tries | instant (`--collide`): swap two letters |
| Byte chi-square over 10,000 inputs (uniform: 255 ± 23) | 237 | about 21 million |

## Ideas from the course's project list

| Course idea | What we built |
|---|---|
| #1 Password storage and validation | `--password`, `--register`, `--login`: a random salt per user, PBKDF2-HMAC-SHA256 (600,000 rounds) on our own SHA-256, constant-time checks, and a lockout after 3 wrong guesses that doubles each time ([src/password.cpp](src/password.cpp)). The demo page adds a stolen-database attack: a table of 40 common passwords cracks 4 of 6 plain SHA-256 users instantly and none of the salted ones. |
| #5 File integrity checker | `--file <path> --verify <hex>` reports MATCH or MISMATCH (exit code 1), and a changed file gets a new theme song. The demo page has a "change one byte" button. |
| #16 Hash-based puzzle | `--mine` searches for a nonce whose SHA-256 starts with zeros, the proof of work behind blockchains. The zeros show in the song as low G3 notes. |
| #19 Hash art | `--art` draws the 32 digest bytes as a mirrored 8×8 picture ([src/extras.cpp](src/extras.cpp)). |
| #20 Interactive hash visualization | The demo page: live hashing as you type, a bit-flip grid, a slider through all 64 rounds, and charts. |

## Tests

The project counts as correct when the plan's four checks pass. `ctest` runs them and more: 10 checks
made of 55 unit tests in five programs and five end-to-end checks.

| Check | What it proves | Test |
|---|---|---|
| Known vectors | Our SHA-256 matches NIST's published outputs for `""`, `"abc"`, the two-block 448-bit message, the 896-bit message and one million `a`s, plus every padding boundary | `unit_sha256` |
| Cross-check | Our hash matches Python's `hashlib` on 1,000 random inputs, including every length from 0 to 200 bytes | `sha256_cross_check` |
| Determinism | The same input gives a byte-identical `.mid` every run; a one-character change gives a different file. The demo songs' fingerprints are pinned, so every machine and compiler must agree | `cli_determinism`, `unit_pipeline` |
| Playback | Every generated file parses cleanly with an independent MIDI reader ([tests/check_midi.py](tests/check_midi.py)); on Windows, `tools\play.ps1 -CheckOnly song.mid` confirms the built-in player opens it | `midi_playback_check` |

The rest: `unit_mapper` and `unit_midi_writer` check those modules (including the exact bytes of a
one-note file), `unit_pipeline` covers the avalanche, band and stats code, and `unit_extras` covers the
toy hash, HMAC (RFC 4231), PBKDF2 (published values), the lockout timeline, mining, hash art, the sweep
and WAV output. `cli_features` runs the newer modes from the command line, and `mapping_cross_check`
rebuilds each song from the byte map in Python ([tests/check_mapping.py](tests/check_mapping.py)) and
requires the `.mid` the CLI wrote to match it note for note on 30 inputs.

## Project layout and owners

| Path | What it is | Owner | Backup |
|---|---|---|---|
| [src/sha256.h](src/sha256.h), [src/sha256.cpp](src/sha256.cpp) | SHA-256 from scratch: padding, message schedule, 64 rounds | A (Jacob) | B |
| [src/avalanche.h](src/avalanche.h), [src/avalanche.cpp](src/avalanche.cpp) | Stretch goal: bit-difference counts, grid, edit distance | A (Jacob) | B |
| [src/mapper.h](src/mapper.h), [src/mapper.cpp](src/mapper.cpp) | Digest → notes, tempo, velocity | B | A |
| [src/song.h](src/song.h) | The `Song`/`Track`/`Note` contract between the mapper and the writer | B and C | |
| [src/midi_writer.h](src/midi_writer.h), [src/midi_writer.cpp](src/midi_writer.cpp) | Standard MIDI File writer: header, tracks, delta times | C | D |
| [tools/hardcoded_melody.cpp](tools/hardcoded_melody.cpp) | Task 6: a fixed melody through the writer alone | C | D |
| [src/main.cpp](src/main.cpp), [CMakeLists.txt](CMakeLists.txt), [tests/](tests/), [.github/workflows/ci.yml](.github/workflows/ci.yml) | CLI, build, tests, automatic testing | D | C |
| [src/band.cpp](src/band.cpp), [src/stats.cpp](src/stats.cpp) | Proposal extras: band layers, distribution charts | unassigned | |
| [src/toy_hash.cpp](src/toy_hash.cpp), [src/password.cpp](src/password.cpp), [src/extras.cpp](src/extras.cpp) | The bad hash, password storage, mining, hash art, sweep, WAV | unassigned | |
| [demo/live_demo.html](demo/live_demo.html), [index.html](index.html) | The browser demo (a JavaScript port with a self-test) and its GitHub Pages entry | unassigned | |
| [docs/REPORT.md](docs/REPORT.md), [docs/DEMO.md](docs/DEMO.md) | Report draft and 5-minute demo script | All | |

Replace B, C and D with names once roles are settled.

## Team workflow

- Each module lives on its own branch (`sha256`, `mapper`, `midi-writer`, `cli`) and merges through a
  pull request that one other member reviews. GitHub Actions marks a pull request with a red X if the
  build or any test fails on any of the four compilers.
- Run `ctest` before opening a pull request. A failing `demo_song_fingerprints_are_pinned` test means
  the demo songs changed; that is fine if you meant it, but update the pinned values (and the demo
  page's self-test) and tell the group.
- The interfaces in [src/sha256.h](src/sha256.h), [src/song.h](src/song.h), [src/mapper.h](src/mapper.h) and
  [src/midi_writer.h](src/midi_writer.h) were agreed on day 2. Change them only after telling everyone.

## A security note

Do not feed the song modes a real password. A song gives away roughly 150 of the hash's 256 bits (4 per
pitch, about 2 per rhythm, plus most of the tempo and loudness bytes), which is plenty for anyone holding
the `.mid` file to confirm a guessed password offline: hash the guess and compare songs. That is
exactly why real systems store passwords the way `--register` does, with a salt and a deliberately slow
hash such as PBKDF2, bcrypt or Argon2, never plain SHA-256.
