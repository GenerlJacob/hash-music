# Demo script (about 5 minutes)

**Browser version.** If the terminal is awkward on the projector, run the same script in
[demo/live_demo.html](../demo/live_demo.html): tab 1 for steps b and c (type `jacob`, press Enter; the
Determinism table shows matching fingerprints), tab 2 for step d, and tabs 3 and 4 if questions turn
to how SHA-256 works or whether its output is uniform. Ask the audience for inputs to type. Check that
the badge at the top right says "Self-test passed" before you start.

## Before you present

1. Build and test: `cmake -S . -B build -G Ninja`, `cmake --build build`, `ctest --test-dir build`.
2. Open PowerShell in the project folder and make the commands short and readable on the projector:

   ```powershell
   Set-Alias hashmusic .\build\hashmusic.exe   # .\build\Release\hashmusic.exe with Visual Studio
   Set-Alias play .\tools\play.ps1
   ```

   If PowerShell says running scripts is disabled, allow it for this window only:
   `Set-ExecutionPolicy -Scope Process Bypass`.

3. Turn the volume up and test it with `play examples\hardcoded.mid`.
4. If sound fails on the day, double-click the files instead, or fall back on the printed fingerprints
   and tempos, which prove the same points without audio.

## a. What is a hash? (30 seconds)

> A hash function turns any input into a fixed-size fingerprint, 32 bytes for SHA-256. The same input
> always gives the same fingerprint, and changing anything changes it completely. We wrote SHA-256
> ourselves and turned its output into music so you can hear both properties.

## b. Same input, same song (1 minute)

```powershell
hashmusic jacob -o jacob.mid
play jacob.mid
hashmusic jacob -o jacob_again.mid
play jacob_again.mid
```

> Point at the **File fingerprint** line: both runs print the same value, so the two files are identical
> byte for byte. Point at the table: each byte is two hex digits. The first picks the pitch and the
> second the rhythm, so `c3` is C6 for an eighth note. You can read the song off the hash.

## c. One character more, a different song (1 minute)

```powershell
hashmusic jacob1 -o jacob1.mid
play jacob1.mid
```

> Different notes, a different rhythm, and a different speed: `jacob` plays at 140 BPM and `jacob1` at 91.

## d. The avalanche comparison (1.5 minutes)

```powershell
hashmusic --compare jacob jacob1
```

> One extra character flipped 146 of the 256 bits, 57%. An ideal hash flips each bit with probability
> one half, so about 128. The grid shows the flips are spread everywhere, not bunched near the end where
> the change was. Only 2 of the 24 notes kept their pitch, which is what chance alone predicts.

If there is time, play `examples\jacob_band.mid` (the `--band` version) or open
`examples\distribution.html` to show that across 10,000 random inputs every pitch comes up equally often.

## e. Questions (1 minute)

Each owner answers for their module. Likely questions:

- **Why does the padding end with the message length?** (A) The 1 bit marks where the message stops.
  The 64-bit length ("Merkle-Damgard strengthening") is what lets the security proof say that any
  collision in SHA-256 would also be a collision in its 64-round compression function, and it blocks
  tricks that combine messages of different lengths.
- **Where do the constants come from?** (A) The cube roots of the first 64 primes and the square roots
  of the first 8. Our tests re-derive every one, so nothing is hidden in them.
- **How do you know your SHA-256 is right?** (A) It matches NIST's published outputs, and Python's
  `hashlib` on 1,000 random inputs.
- **Why a pentatonic scale?** (B) It has no clashing half steps, so random notes still sound musical.
- **What is a delta time?** (C) The ticks since the previous event, stored as a variable-length number
  (7 bits per byte), which keeps MIDI files small.
- **How do the tests prove determinism?** (D) They run the program twice and compare the files byte for
  byte, and they pin the demo songs' fingerprints so every computer must produce the same bytes.
- **Could two inputs give the same song?** In principle yes, since a song carries about 150 bits of the
  hash. Finding such a pair would take around 2^75 tries, far beyond any computer.
- **Is this secure?** No; it is a teaching tool. A song reveals most of its hash, which is why real
  systems store passwords with a salt and a slow hash such as bcrypt, not plain SHA-256.
