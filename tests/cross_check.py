#!/usr/bin/env python3
"""Cross-check: our SHA-256 must agree with Python's hashlib on 1,000 random inputs.

Usage: python cross_check.py <path to sha256_batch> [count]

Every length from 0 to 200 bytes is included, which covers each padding case (55 vs 56 bytes and
63/64/65 bytes are the tricky ones). The remaining inputs have random lengths up to 10,000 bytes.
"""
import hashlib
import random
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 1000

    rng = random.Random(2710)
    lengths = list(range(min(count, 201)))
    lengths += [rng.randrange(10_000) for _ in range(count - len(lengths))]
    inputs = [rng.randbytes(n) for n in lengths]

    payload = "".join(data.hex() + "\n" for data in inputs).encode("ascii")
    result = subprocess.run([exe], input=payload, capture_output=True, check=False)
    if result.returncode != 0:
        print(f"sha256_batch failed (exit {result.returncode}): {result.stderr.decode(errors='replace')}")
        return 1
    ours = result.stdout.decode("ascii").split()
    if len(ours) != len(inputs):
        print(f"expected {len(inputs)} digests, got {len(ours)}")
        return 1

    mismatches = 0
    for data, digest in zip(inputs, ours):
        expected = hashlib.sha256(data).hexdigest()
        if digest != expected:
            mismatches += 1
            if mismatches <= 5:
                print(f"MISMATCH for a {len(data)}-byte input: ours {digest}, hashlib {expected}")

    print(f"cross-check: {len(inputs)} inputs of {min(lengths)}-{max(lengths)} bytes, {mismatches} mismatches")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
