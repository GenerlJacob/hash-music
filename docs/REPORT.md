<!-- DRAFT. The plan says each owner writes the section for their own module: rewrite yours in your
     own words, check the numbers against your own run, and swap "Member B/C/D" for names. -->

# Hash Music Generator: Report

**Bubble Sort Bandits:** Jacob Breeden, Mabel Boateng, Mikey Greco, Ysabella Coker · CSC 2710 · November 2026

We built a C++17 command-line tool that hashes any text or file with a SHA-256 we wrote from scratch and
turns the 32-byte digest into a short melody saved as a MIDI file. Typing `jacob` twice gives the same
song byte for byte; typing `jacob1` gives a completely different one. Hashing's two key properties,
determinism and the avalanche effect, become something you can hear.

## SHA-256 (Member A, Jacob)

The hasher follows FIPS 180-4. It pads the message with a 1 bit, zeros, and the 64-bit message length;
stretches each 64-byte block into a 64-word message schedule; and runs 64 rounds of the choose,
majority and rotation functions. It is incremental, so file mode can hash large files 64 KiB at a time.
It matches NIST's published outputs for the empty string, `abc`, the two-block 448-bit message, the
896-bit message and one million `a`s, plus messages at every padding boundary (55-57, 63-65, 119-120
and 127-128 bytes). It also matches Python's `hashlib` on 1,000 random inputs of 0-9,998 bytes, with
0 mismatches. A test re-derives all 72 constants from the square and cube roots of the first primes.

## Note mapper (Member B)

Each digest byte is two hex digits, and the mapper reads them in order. In bytes 0-23 the first digit
picks one of 16 pitches (C major pentatonic, G3 to G6) and the second picks a length (eighth, quarter,
dotted quarter or half, averaging one beat). Byte 24 sets the tempo (80-160 BPM) and byte 25 the
loudness (velocity 60-100, accented on beats 1 and 3). The song ends on C5. Using 16-entry tables makes
every pitch equally likely, and it means the song can be read straight off the printed hash.
The pentatonic scale was our planned fix for melodies that sound harsh. A separate Python model of these
rules (the `mapping_cross_check` test) decodes the `.mid` files for 30 inputs and matches them note for note.

## MIDI writer (Member C)

The writer emits a format-1 Standard MIDI File at 480 ticks per beat: a conductor track (title, 4/4,
tempo) plus one track per instrument. Delta times are variable-length quantities. When notes meet, the
note-off is written before the note-on, so a repeated pitch is not cut short. A unit test checks the
exact 77 bytes of a one-note file computed by hand. Every generated file parses with an independent
reader we wrote in Python and opens in Windows' built-in MIDI sequencer.

## CLI, build and tests (Member D)

`hashmusic` takes text, a file or standard input; CMake builds it, and `ctest` runs ten checks:
55 unit tests in five test programs plus five end-to-end checks. GitHub Actions runs them with GCC,
Clang and Visual Studio on every push, and a browser version of the tool (`demo/live_demo.html`, a
JavaScript port) checks itself against the C++ build when it loads. All of the plan's correctness
checks pass: known vectors, the 1,000-input cross-check, determinism (two runs of the CLI write
byte-identical files, and the demo songs' fingerprints are pinned so every machine must agree) and
playback.

## Avalanche result

| `jacob` vs `jacob1` (1 edit apart) | Result | Ideal hash |
|---|---|---|
| Digest bits that differ | 146 of 256 (57.0%) | 128 ± 8 |
| Hex digits that differ | 62 of 64 | about 60 |
| Melody notes with the same pitch | 2 of 24 | 1.5 by chance |
| Tempo | 140 vs 91 BPM | unrelated |

146 bits is 2.3 standard deviations above 128, which is unremarkable for a single pair. Across
10,000 random one-bit input changes, an average of 127.9 bits flipped (standard deviation 8.0), which
is the ideal binomial distribution.

## Beyond the plan

We also built the proposal's extras. `--band` uses bytes 26-29 to choose instruments, a chord
progression, a bass line and drums. File mode gives any file a theme song. `--stats` hashes 10,000 random
inputs and charts the results: pitch chi-square 11.3 (a uniform source gives 15 ± 5.5) and byte
chi-square 237.3 (255 ± 22.6). The first version of `--stats` scored 378, which looked like a biased
hash. The cause was that it allowed 1-letter inputs, and 5% of inputs repeated; since hashing is
deterministic, each repeat added identical bytes. Requiring 8+ letters fixed it: determinism, seen from
the other side.

Four more ideas came from the course's project list. Passwords (#1) are stored as a random salt plus
600,000 rounds of PBKDF2-HMAC-SHA256 built on our SHA-256 (it matches RFC 4231 and published PBKDF2
values), and three wrong guesses lock an account. In the demo, a table of 40 common passwords cracks
4 of 6 plain SHA-256 users and none of the salted ones. `--verify` checks a file against a known hash
(#5), `--mine` solves a proof-of-work puzzle (#16: five zero digits took 1.4 million tries), and `--art`
draws a hash as a picture (#19). For contrast, `--hash toy` swaps in a deliberately bad XOR hash: one
extra character flips 3 bits instead of 146, two swapped letters collide instantly, and its byte
chi-square is about 21 million.

**Limits.** The tool is for understanding, not security. A song reveals about 150 of the digest's 256
bits, so it should never be made from a real password.
