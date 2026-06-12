#!/usr/bin/env python3
"""Arstro DSP — advanced integration tests.

Compiles the C++ render harness (tests/render_harness.cpp) against the
channel-aware library (synth_dsp.h), runs scenarios that emit mono 16-bit WAVs,
loads them with the stdlib `wave` module, and asserts each DSP feature behaves
correctly — numerically, not just "non-silent".

Stdlib only (no NumPy / libsndfile). Run:  python3 tests/run_integration.py
"""
import glob
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SR = 48000


def synth_sources():
    """Library .cpp under src/ on the synth path (excludes legacy spatial/ + util/)."""
    src = glob.glob(os.path.join(ROOT, "src", "**", "*.cpp"), recursive=True)
    legacy = (os.path.join("src", "spatial"), os.path.join("src", "util"))
    return [p for p in src if not any(seg in p for seg in legacy)]


# ───────────────────────── infra ─────────────────────────

class Failure(Exception):
    pass


def build_harness(binpath):
    cmd = ["g++", "-std=c++17", "-O2", "-pthread",
           os.path.join(HERE, "render_harness.cpp")]
    cmd += synth_sources()
    cmd += ["-o", binpath]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise Failure("harness build failed:\n" + r.stderr)


def render(binpath, scenario, *args):
    out = os.path.join(tempfile.gettempdir(), f"arstro_{scenario}_{'_'.join(map(str, args))}.wav")
    cmd = [binpath, scenario, out] + [str(a) for a in args]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise Failure(f"render '{scenario}' failed:\n{r.stderr}")
    return read_wav_mono(out)


def read_wav_mono(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 1, "harness writes mono"
        assert w.getsampwidth() == 2, "harness writes 16-bit PCM"
        n = w.getnframes()
        raw = w.readframes(n)
    ints = struct.unpack("<%dh" % n, raw)
    return [s / 32767.0 for s in ints]


def read_wav_channels(path):
    """Return a list of per-channel sample lists (handles mono or stereo)."""
    with wave.open(path, "rb") as w:
        ch = w.getnchannels()
        assert w.getsampwidth() == 2, "harness writes 16-bit PCM"
        n = w.getnframes()
        raw = w.readframes(n)
    ints = struct.unpack("<%dh" % (n * ch), raw)
    return [[ints[i * ch + c] / 32767.0 for i in range(n)] for c in range(ch)]


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / len(xs)) if xs else 0.0


def peak(xs):
    return max(abs(x) for x in xs) if xs else 0.0


def rising_zero_crossings(xs):
    """Count negative→positive transitions (one per period for sine and saw)."""
    c = 0
    for a, b in zip(xs, xs[1:]):
        if a < 0.0 <= b:
            c += 1
    return c


# ───────────────────────── feature checks ─────────────────────────

def check_gain(binpath):
    """Gain(2.0) on a 0.4-amplitude sine => 0.8-amplitude output."""
    y = render(binpath, "gain")
    p, r = peak(y), rms(y)
    if not (0.78 <= p <= 0.82):
        raise Failure(f"gain peak {p:.4f}, expected ~0.80")
    expected_rms = 0.8 / math.sqrt(2)
    if abs(r - expected_rms) > 0.02:
        raise Failure(f"gain RMS {r:.4f}, expected ~{expected_rms:.4f}")
    return f"peak={p:.3f} rms={r:.3f}"


def check_oscillator_frequency(binpath):
    """Oscillator at 440 Hz should produce ~440 Hz fundamental."""
    target = 440.0
    y = render(binpath, "osc", target)
    y = y[4800:]  # skip the (fast) attack; analyse the steady tone
    dur = len(y) / SR
    freq = rising_zero_crossings(y) / dur
    if abs(freq - target) / target > 0.03:
        raise Failure(f"osc measured {freq:.1f} Hz, expected {target} Hz (>3% off)")
    if peak(y) < 0.2:
        raise Failure(f"osc too quiet (peak {peak(y):.3f})")
    return f"measured {freq:.1f} Hz"


def check_lowpass_attenuation(binpath):
    """LPF @500 Hz: 100 Hz passes, 4000 Hz is strongly attenuated."""
    lo = rms(render(binpath, "lpf", 100.0))
    hi = rms(render(binpath, "lpf", 4000.0))
    in_rms = 0.5 / math.sqrt(2)  # 0.5-amplitude input sine
    lo_ratio, hi_ratio = lo / in_rms, hi / in_rms
    if lo_ratio < 0.7:
        raise Failure(f"LPF over-attenuates passband: 100 Hz ratio {lo_ratio:.3f}")
    if hi_ratio > 0.3:
        raise Failure(f"LPF under-attenuates stopband: 4000 Hz ratio {hi_ratio:.3f}")
    if hi_ratio >= lo_ratio:
        raise Failure(f"LPF not low-pass: 4k ratio {hi_ratio:.3f} >= 100 ratio {lo_ratio:.3f}")
    return f"100Hz keeps {lo_ratio:.2f}, 4kHz keeps {hi_ratio:.2f}"


def check_adsr_shape(binpath):
    """ADSR: rises (attack), settles near sustain 0.5, decays to ~0 after release."""
    y = render(binpath, "adsr")
    on = y[: SR // 5]   # note-on segment
    off = y[SR // 5:]   # release segment
    early = rms(on[:200])
    settled = rms(on[-1000:])           # late in the on-phase = near sustain
    tail = rms(off[-1000:])             # end of release
    if early >= settled:
        raise Failure(f"ADSR did not rise: early rms {early:.3f} >= settled {settled:.3f}")
    if abs(settled - 0.5) > 0.06:
        raise Failure(f"ADSR sustain {settled:.3f}, expected ~0.5")
    if tail > 0.02:
        raise Failure(f"ADSR did not release to silence: tail rms {tail:.3f}")
    return f"sustain={settled:.3f} tail={tail:.4f}"


def check_reverb_stereo_decorrelation(binpath):
    """Mono input through the stereo reverb must yield a non-silent, decorrelated L/R tail."""
    out = os.path.join(tempfile.gettempdir(), "arstro_reverb.wav")
    r = subprocess.run([binpath, "reverb", out], capture_output=True, text=True)
    if r.returncode != 0:
        raise Failure(f"render 'reverb' failed:\n{r.stderr}")
    chans = read_wav_channels(out)
    if len(chans) != 2:
        raise Failure(f"reverb output not stereo ({len(chans)} channels)")
    L, R = chans
    if peak(L) < 0.001 or peak(R) < 0.001:
        raise Failure("reverb tail is silent")
    diff = rms([l - r for l, r in zip(L, R)])
    base = rms([l + r for l, r in zip(L, R)])
    ratio = diff / base if base else 0.0
    if ratio < 0.05:
        raise Failure(f"L/R not decorrelated (rms(L-R)/rms(L+R) = {ratio:.3f}, want >= 0.05)")
    return f"L/R decorrelation ratio = {ratio:.2f}"


def check_synth_deterministic_nonsilent(binpath):
    """Full 8-note engine: produces sound and renders identically twice."""
    a = render(binpath, "synth")
    b = render(binpath, "synth")
    if peak(a) < 0.001:
        raise Failure("synth produced silence")
    if a != b:
        raise Failure("synth render is non-deterministic")
    return f"peak={peak(a):.3f} deterministic over {len(a)} samples"


CHECKS = [
    ("gain_doubles_amplitude", check_gain),
    ("oscillator_frequency", check_oscillator_frequency),
    ("lowpass_attenuation", check_lowpass_attenuation),
    ("adsr_envelope_shape", check_adsr_shape),
    ("reverb_stereo_decorrelation", check_reverb_stereo_decorrelation),
    ("synth_deterministic_nonsilent", check_synth_deterministic_nonsilent),
]


def main():
    binpath = os.path.join(tempfile.gettempdir(), "arstro_render_harness")
    try:
        build_harness(binpath)
    except Failure as e:
        print(f"[BUILD] FAIL\n{e}")
        return 1

    failed = 0
    for name, fn in CHECKS:
        try:
            detail = fn(binpath)
            print(f"[PASS] {name} ({detail})")
        except Failure as e:
            failed += 1
            print(f"[FAIL] {name}: {e}")
        except Exception as e:  # noqa
            failed += 1
            print(f"[FAIL] {name}: unexpected {type(e).__name__}: {e}")

    print(f"\n{len(CHECKS)} checks, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
