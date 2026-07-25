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


def goertzel_mag(xs, freq):
    """Magnitude of `xs` at `freq` (Hz) via the Goertzel algorithm."""
    w = 2.0 * math.pi * freq / SR
    coeff = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for x in xs:
        s0 = x + coeff * s1 - s2
        s2 = s1
        s1 = s0
    return math.hypot(s1 - s2 * math.cos(w), s2 * math.sin(w))


def check_waveform_selection(binpath):
    """Selecting a waveform changes the timbre: a Sine has almost no 2nd harmonic,
    a Saw at the same pitch has a strong one. Proves OSC_WAVEFORM is wired."""
    f0 = 220.0
    sine = render(binpath, "oscwave", 0)[4800:]  # Sine
    saw = render(binpath, "oscwave", 1)[4800:]   # Saw
    sine_ratio = goertzel_mag(sine, 2 * f0) / max(1e-9, goertzel_mag(sine, f0))
    saw_ratio = goertzel_mag(saw, 2 * f0) / max(1e-9, goertzel_mag(saw, f0))
    if sine_ratio > 0.15:
        raise Failure(f"sine 2nd-harmonic ratio {sine_ratio:.3f} too high (not a sine?)")
    if saw_ratio < 0.25:
        raise Failure(f"saw 2nd-harmonic ratio {saw_ratio:.3f} too low (not a saw?)")
    if saw_ratio < sine_ratio * 2:
        raise Failure(f"saw ({saw_ratio:.3f}) not richer than sine ({sine_ratio:.3f})")
    return f"sine H2/H1={sine_ratio:.3f}  saw H2/H1={saw_ratio:.3f}"


def check_synth_deterministic_nonsilent(binpath):
    """Full 8-note engine: produces sound and renders identically twice."""
    a = render(binpath, "synth")
    b = render(binpath, "synth")
    if peak(a) < 0.001:
        raise Failure("synth produced silence")
    if a != b:
        raise Failure("synth render is non-deterministic")
    return f"peak={peak(a):.3f} deterministic over {len(a)} samples"


def check_piano_inharmonicity(binpath):
    """PianoVoice (large forced B=0.02): partial 4 sits at the stiffness-shifted
    frequency f0*4*sqrt(1+B*16), not the exact harmonic 4*f0 (REQ-piano-2/4)."""
    f0 = 110.0
    b = 0.02
    y = render(binpath, "piano", f0)
    if peak(y) >= 0.99:
        raise Failure(f"piano note clipped (peak {peak(y):.3f}) — see README ## Units headroom")
    seg = y[2000:]  # skip the strike transient
    exact4 = 4.0 * f0
    inharmonic4 = 4.0 * f0 * math.sqrt(1.0 + b * 16.0)
    mag_exact = goertzel_mag(seg, exact4)
    mag_inharmonic = goertzel_mag(seg, inharmonic4)
    mag_f0 = goertzel_mag(seg, f0)
    if mag_f0 < 1e-6:
        raise Failure("piano note produced no measurable fundamental")
    if mag_inharmonic < mag_exact * 1.5:
        raise Failure(
            f"partial 4 not inharmonically shifted: exact({exact4:.1f}Hz)={mag_exact:.4f} "
            f"vs shifted({inharmonic4:.1f}Hz)={mag_inharmonic:.4f}"
        )
    return f"peak={peak(y):.3f} shifted/exact={mag_inharmonic / max(1e-9, mag_exact):.2f}"


def check_piano_damper(binpath):
    """noteOff (damper engaging) decays much faster than sustain-held (REQ-piano-7)."""
    engaged = render(binpath, "pianodamper", 0)
    held = render(binpath, "pianodamper", 1)
    pre = SR // 10  # 100 ms struck region written by the harness before noteOff

    def early_late_ratio(y):
        just_after = y[pre + 1000: pre + 1000 + 2400]   # ~20ms past noteOff (damper ramp)
        later = y[pre + 15000: pre + 15000 + 2400]        # ~310ms after noteOff
        r_early = rms(just_after)
        return rms(later) / r_early if r_early > 1e-9 else 0.0

    ratio_engaged = early_late_ratio(engaged)
    ratio_held = early_late_ratio(held)
    if ratio_engaged > 0.3:
        raise Failure(f"damper (engaged) tail didn't decay enough: ratio {ratio_engaged:.3f}")
    if ratio_engaged > ratio_held * 0.5:
        raise Failure(
            f"damper not clearly faster than sustain-held: engaged={ratio_engaged:.3f} "
            f"held={ratio_held:.3f}"
        )
    return f"engaged_ratio={ratio_engaged:.3f} held_ratio={ratio_held:.3f}"


def check_piano_sympathetic_resonance(binpath):
    """A struck A3 (220Hz) sympathetically excites a silently-held same-pitch string
    far more than an off-pitch (233.08Hz) one — emergent via the shared PianoBridge,
    not a per-note-pair table (REQ-piano-6)."""
    same = render(binpath, "pianosympathetic", 0)
    off = render(binpath, "pianosympathetic", 1)
    mag_same = goertzel_mag(same, 220.0)
    mag_off = goertzel_mag(off, 233.08)
    if mag_same < mag_off * 2.0:
        raise Failure(
            f"sympathetic resonance not frequency-selective: same-pitch={mag_same:.4f} "
            f"off-pitch={mag_off:.4f}"
        )
    return f"same-pitch/off-pitch energy ratio={mag_same / max(1e-9, mag_off):.2f}"


CHECKS = [
    ("gain_doubles_amplitude", check_gain),
    ("oscillator_frequency", check_oscillator_frequency),
    ("waveform_selection", check_waveform_selection),
    ("lowpass_attenuation", check_lowpass_attenuation),
    ("adsr_envelope_shape", check_adsr_shape),
    ("reverb_stereo_decorrelation", check_reverb_stereo_decorrelation),
    ("synth_deterministic_nonsilent", check_synth_deterministic_nonsilent),
    ("piano_inharmonicity", check_piano_inharmonicity),
    ("piano_damper_decay", check_piano_damper),
    ("piano_sympathetic_resonance", check_piano_sympathetic_resonance),
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
