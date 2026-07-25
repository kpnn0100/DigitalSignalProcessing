#!/usr/bin/env python3
"""Smoke test for the arstro_piano_demo app (passed as argv[1]).

Verifies both audio-output paths build and produce valid, non-silent audio:
  1. offline `--wav` render -> a non-silent stereo WAV
  2. live stdout streaming -> non-empty PCM, terminating on `quit`

Does NOT open a real audio device (headless-safe). Stdlib only. Mirrors
tests/test_kitchen_sink.py's shape (REQ-piano-10).
"""
import os
import struct
import subprocess
import sys
import tempfile
import wave


def fail(msg):
    print(f"[FAIL] {msg}")
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        fail("usage: test_piano_demo.py <binary>")
    binpath = sys.argv[1]

    # 1) offline WAV render (2 seconds of the canned demo)
    wav = os.path.join(tempfile.gettempdir(), "arstro_piano_demo.wav")
    r = subprocess.run([binpath, "--wav", wav, "2"], capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        fail(f"--wav run failed: {r.stderr}")
    with wave.open(wav, "rb") as w:
        ch, n = w.getnchannels(), w.getnframes()
        if ch != 2:
            fail(f"expected stereo, got {ch} channels")
        if n < 80000:
            fail(f"expected ~2s of audio, got {n} frames")
        ints = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    peak = max(abs(s) for s in ints) / 32767.0
    if peak < 0.001:
        fail(f"offline render is silent (peak {peak:.4f}) — demo script should strike C3 at t=0")
    print(f"[PASS] offline_wav (stereo, {n} frames, peak {peak:.3f})")

    # 2) live stdout streaming: strike a chord, hold sustain, then EOF plays the tail.
    r = subprocess.run([binpath], input=b"on 60 0.8\non 64 0.8\nsustain on\noff 60\noff 64\n",
                        capture_output=True, timeout=30)
    if r.returncode != 0:
        fail(f"live run failed: {r.stderr.decode(errors='replace')}")
    if len(r.stdout) < 50000:
        fail(f"live stdout produced too little PCM ({len(r.stdout)} bytes)")
    rq = subprocess.run([binpath], input=b"quit\n", capture_output=True, timeout=30)
    print(f"[PASS] live_stdout ({len(r.stdout)} bytes of PCM; quit emits {len(rq.stdout)} bytes)")
    print("\n2 checks, 0 failed")


if __name__ == "__main__":
    main()
