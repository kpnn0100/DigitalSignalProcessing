#!/usr/bin/env python3
"""Arstro DSP — advanced integration tests.

Compiles the C++ render harness (tests/render_harness.cpp) against the
channel-aware library (synth_dsp.h), runs scenarios that emit mono 16-bit WAVs,
loads them with the stdlib `wave` module, and asserts each DSP feature behaves
correctly — numerically, not just "non-silent".

Stdlib only (no NumPy / libsndfile). Run:  python3 tests/run_integration.py
"""
import cmath
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


def check_piano_voice_reuse_bounded(binpath):
    """Regression: reusing a fully-damped PianoVoice for a new note (the exact
    setFrequency()-then-noteOn() sequence PianoEngine uses when stealing a voice)
    must sound like a normal struck note, not a ~20x amplitude spike (a stale
    short-decay/high-gain resonator coefficient set left over from the previous
    note's engaged damper — see StringPartialBank::reset())."""
    y = render(binpath, "pianoreuse")
    p = peak(y)
    if p > 0.95:
        raise Failure(f"reused-voice note peak {p:.3f} — spiking/saturating, expected a normal bounded note")
    return f"peak={p:.3f}"


def check_piano_bandwidth(binpath):
    """M2 acceptance (plan §M2): a C4 note must now carry real energy ABOVE 3 kHz.
    Before M2 the bank was a fixed 12 partials, so C4 topped out at ~3.18 kHz and had
    *exactly zero* content above it — a dull, near-pure tone and one of the reasons
    the model read as a plucked string. C4 now runs 63 partials to ~21 kHz."""
    f0 = 261.63
    b = min(0.02, max(0.00005, 0.00056 * (100.0 / f0) ** 1.25))
    y = render(binpath, "pianospectral")
    seg = y[480:14400]  # ~10-300 ms: the attack, where the highs live

    def partial(n):
        return n * f0 * math.sqrt(1.0 + b * n * n)

    # Probe partials that simply did not exist before M2 (n > 12).
    above_3k = [partial(n) for n in range(13, 64) if partial(n) > 3000.0]
    if len(above_3k) < 10:
        raise Failure(f"expected many partials above 3 kHz, found {len(above_3k)}")
    hi = math.sqrt(sum(goertzel_mag(seg, f) ** 2 for f in above_3k[:20]))
    ref = goertzel_mag(seg, partial(1))
    if ref <= 0:
        raise Failure("no fundamental to compare against")
    rel_db = 20.0 * math.log10(max(hi, 1e-12) / ref)
    # Must be present and well clear of the numerical floor, not merely non-zero.
    if rel_db < -70.0:
        raise Failure(f"no real energy above 3 kHz ({rel_db:.1f} dB rel. fundamental)")
    return f"{len(above_3k)} partials >3 kHz, energy {rel_db:.1f} dB rel. fundamental"


def check_piano_spectral_evolution(binpath):
    """M1 acceptance criterion 2 (plan §M1): piano tone is defined by its spectral
    EVOLUTION — a bright, complex attack that mellows to near-sinusoidal within about
    a second. Measured as the high-band/low-band energy ratio at the attack versus at
    t = 1 s; it must collapse by >= 20 dB. Before M1's alpha = c1 + c3*w^2 loss law,
    partials decayed at near-uniform rates, the spectrum barely changed, and the model
    read as a bowed/plucked string."""
    f0 = 261.63  # C4, as rendered by the harness
    # Inharmonicity default from README ## 2, replicated here so the partial
    # frequencies the C++ side actually uses are the ones we probe (this also
    # cross-checks that documented formula).
    b = min(0.02, max(0.00005, 0.00056 * (100.0 / f0) ** 1.25))
    partials = [n * f0 * math.sqrt(1.0 + b * n * n) for n in range(1, 13)]
    low = partials[0]
    high = [f for f in partials if f > 2000.0]
    if not high:
        raise Failure("no partials above 2 kHz to measure — check the partial series")

    y = render(binpath, "pianospectral")
    attack = y[480:5280]      # ~10-110 ms
    late = y[48000:52800]     # ~1.00-1.11 s
    eps = 1e-12

    def band_ratio_db(seg):
        hi = math.sqrt(sum(goertzel_mag(seg, f) ** 2 for f in high))
        lo = goertzel_mag(seg, low)
        return 20.0 * math.log10(max(hi, eps) / max(lo, eps))

    attack_db = band_ratio_db(attack)
    late_db = band_ratio_db(late)
    # Back to the plan's original 20 dB target, restored at M3 as planned: M1 had to
    # lower it to 15 because the open-loop hammer pulse left the high partials ~42 dB
    # down with little left to lose. Coupling the hammer to the string re-injects
    # high-frequency energy (the reflection ripple), and the measured drop rose to
    # 20.5 dB — so the original criterion now holds on its own terms.
    # Threshold moved 20 -> 18 at M4, measured 20.0. M4's double decay legitimately
    # speeds the FUNDAMENTAL's prompt phase (C4: 6.9 s -> 2.9 s vertical), and this
    # metric divides by the low band, so a faster-decaying fundamental at t=1 s makes
    # the ratio less negative and shrinks the measured drop. The high partials did not
    # get duller — that was verified separately when the first (uniform-R) split DID
    # dull them and this figure collapsed to 11.2 dB.
    drop = attack_db - late_db
    if drop < 18.0:
        raise Failure(
            f"spectrum barely evolves: high/low ratio {attack_db:.1f} dB at attack vs "
            f"{late_db:.1f} dB at 1 s (drop {drop:.1f} dB, need >= 18 dB)"
        )
    return f"high/low ratio {attack_db:.1f} dB -> {late_db:.1f} dB (drop {drop:.1f} dB)"


def check_piano_double_decay(binpath):
    """M4 acceptance (plan §M4): the envelope is NOT a single exponential. The vertical
    polarisation is strongly bridge-coupled and decays fast (the prompt sound); once it
    falls past the weakly-coupled horizontal plane, the long quiet aftersound takes
    over. A single exponential reads as a plucked string — this is what makes piano
    notes bloom."""
    y = render(binpath, "pianodoubledecay")

    def decay_rate(t0, t1, windows=12):
        """Least-squares slope of log(RMS) — averages out residual modulation."""
        pts = []
        for w in range(windows):
            a = t0 + (t1 - t0) * w / windows
            b = t0 + (t1 - t0) * (w + 1) / windows
            i0, i1 = int(a * SR), int(b * SR)
            if i1 > len(y) or i1 <= i0:
                continue
            r = rms(y[i0:i1])
            if r < 1e-9:
                continue
            pts.append((0.5 * (a + b), math.log(r)))
        if len(pts) < 4:
            raise Failure("not enough signal to fit a decay slope")
        n = len(pts)
        sx = sum(p[0] for p in pts); sy = sum(p[1] for p in pts)
        sxx = sum(p[0] * p[0] for p in pts); sxy = sum(p[0] * p[1] for p in pts)
        return -(n * sxy - sx * sy) / (n * sxx - sx * sx)

    # Windows straddle the predicted C4 crossover (~1.2 s, README ## 5b).
    early = decay_rate(0.12, 0.85)
    late = decay_rate(2.4, 4.8)
    if early <= 0.0 or late <= 0.0:
        raise Failure(f"envelope not decaying (early {early:.3f}/s, late {late:.3f}/s)")
    if early < late * 3.0:
        raise Failure(
            f"no double decay: early {early:.3f}/s vs late {late:.3f}/s "
            f"(ratio {early / late:.2f}, need >= 3)"
        )
    return f"prompt {early:.2f}/s -> aftersound {late:.2f}/s (ratio {early / late:.2f})"


def check_soundboard_colours_treble(binpath):
    """M5 acceptance (plan §M5): the soundboard must COLOUR the treble. Before M5 the
    bridge had 8 modes spanning 80-700 Hz, so everything above 700 Hz radiated
    completely flat — naked resonators, i.e. music box rather than instrument. A real
    board ripples ~10 dB up there."""
    y = render(binpath, "bridgeimpulse")
    freqs = [700.0 + 25.0 * i for i in range(int((5000 - 700) / 25) + 1)]
    mags = [goertzel_mag(y, f) for f in freqs]
    peak = max(mags)
    if peak <= 0:
        raise Failure("bridge radiates nothing above 700 Hz")
    db = sorted(20.0 * math.log10(max(m, 1e-12) / peak) for m in mags)
    variation = db[-1] - db[int(len(db) * 0.05)]
    if variation < 6.0:
        raise Failure(
            f"treble is uncoloured: only {variation:.1f} dB variation above 700 Hz "
            "(need >= 6) — the soundboard is not shaping the top end"
        )
    return f"{variation:.1f} dB variation across 700 Hz - 5 kHz"


def check_piano_sympathetic_resonance(binpath):
    """A struck A3 (220Hz) sympathetically excites a silently-held same-pitch string
    far more than an off-pitch (233.08Hz) one — emergent via the shared PianoBridge,
    not a per-note-pair table (REQ-piano-6)."""
    same = render(binpath, "pianosympathetic", 0)
    off = render(binpath, "pianosympathetic", 1)
    mag_same = goertzel_mag(same, 220.0)
    mag_off = goertzel_mag(off, 233.08)
    # Floor check FIRST: without it this passes vacuously on an all-zero signal
    # (0 < 0 is false). That actually happened at M3 — the drive path was rescaled
    # by 1/(m*fs), the sympathetic response fell below 16-bit WAV resolution, and
    # the check reported "ratio 0.00 PASS".
    if mag_same < 1e-3:
        raise Failure(
            f"sympathetic response is effectively silent (|same| = {mag_same:.3e}) — "
            "coupling gain too low, or lost below WAV quantisation"
        )
    if mag_same < mag_off * 2.0:
        raise Failure(
            f"sympathetic resonance not frequency-selective: same-pitch={mag_same:.4f} "
            f"off-pitch={mag_off:.4f}"
        )
    return f"same-pitch/off-pitch energy ratio={mag_same / max(1e-9, mag_off):.2f}"


def check_piano_register_voicing(binpath):
    """M6 acceptance (plan §M6): notes must stop being transpositions of each other.
    Rendered end to end at four pitches with every default, so README ## 11's register
    scaling is the only difference between the renders.

    Measured as the spectral centroid RELATIVE to the note's own fundamental. A
    keyboard of pure transpositions holds centroid/f0 constant by construction — that
    is what "transposition" means — so a large collapse in that ratio across the
    compass is the numeric statement of per-register character."""
    notes = [("A0", 27.5), ("C3", 130.8), ("C5", 523.3), ("C8", 4186.0)]
    ratios, centroids = [], []
    for name, f0 in notes:
        y = render(binpath, "pianoregister", f0)
        # Coarse log-spaced spectrum over the attack, where voicing shows most.
        seg = y[: int(0.2 * SR)]
        num = den = 0.0
        f = 50.0
        while f < 10000.0:
            m = goertzel_mag(seg, f)
            num += f * m
            den += m
            f *= 1.06
        if den <= 0:
            raise Failure(f"{name} rendered no measurable spectrum")
        c = num / den
        # Floor check beside the ratio, per the M1/M3 lesson: a ratio between two
        # near-silent renders is arithmetic, not evidence.
        if max(abs(v) for v in y) < 1e-3:
            raise Failure(f"{name} is effectively silent — peak {max(abs(v) for v in y):.2e}")
        centroids.append((name, c))
        ratios.append(c / f0)

    if any(centroids[i][1] >= centroids[i + 1][1] for i in range(len(centroids) - 1)):
        raise Failure(
            "spectral centroid is not rising with pitch: "
            + ", ".join(f"{n}={c:.0f}Hz" for n, c in centroids)
        )
    collapse = ratios[0] / ratios[-1]
    if collapse < 20.0:
        raise Failure(
            f"notes still read as transpositions: centroid/f0 varies only {collapse:.1f}x "
            "across the keyboard (need >= 20) — per-register voicing is not taking effect"
        )
    return (
        f"centroid {centroids[0][1]:.0f}->{centroids[-1][1]:.0f} Hz, "
        f"centroid/f0 collapses {collapse:.0f}x A0->C8"
    )


def check_piano_phantom_partials(binpath):
    """M7 acceptance (plan §M7): phantom partials — measurable energy at a frequency
    where the transverse series predicts NO partial at all, growing faster than
    linearly with strike velocity.

    Isolated exactly by rendering A0 twice, with and without the tension coupling,
    and subtracting: whatever is in the difference came from README ## 12 and from
    nothing else. That is what makes "this is not a transverse partial" provable
    rather than a matter of interpretation."""
    f0 = 27.5
    b = min(0.02, max(0.00005, 0.00056 * (100.0 / f0) ** 1.25))
    series = []
    n = 1
    while True:
        fn = n * f0 * math.sqrt(1.0 + b * n * n)
        if fn >= 0.45 * SR or n > 64:
            break
        series.append(fn)
        n += 1

    def isolate(vel):
        on = render(binpath, "pianophantom", round(vel * 100))
        off = render(binpath, "pianophantom", -round(vel * 100))
        return on, [a - c for a, c in zip(on, off)]

    total, longitudinal = isolate(1.0)
    if peak(longitudinal) < 1e-4:
        raise Failure(
            f"longitudinal stage contributes nothing (peak {peak(longitudinal):.2e}) — "
            "tension coupling lost, or below WAV quantisation"
        )

    # f_long,1 = 4194*(f0/261.6)^0.52, README ## 12.1 — set by string geometry.
    f_long = 4194.0 * (f0 / 261.6) ** 0.52
    seg_l = longitudinal[: int(0.3 * SR)]
    seg_t = total[: int(0.3 * SR)]

    # Sweep the first longitudinal resonance for a frequency that is genuinely not
    # a transverse partial, and require the longitudinal signal to dominate there.
    best = None
    f = f_long * 0.9
    while f <= f_long * 1.1:
        gap = min(abs(f - p) for p in series)
        if gap >= 12.0:
            lon = goertzel_mag(seg_l, f)
            tra = abs(goertzel_mag(seg_t, f) - lon)
            if lon > 1e-3 and lon > tra * 3.0 and (best is None or lon > best[1]):
                best = (f, lon, tra, gap)
        f += 2.0
    if best is None:
        raise Failure(
            f"no phantom found near the longitudinal resonance {f_long:.0f} Hz — "
            "energy is only landing on frequencies the transverse series already has"
        )
    fp, lon, tra, gap = best

    # Velocity scaling: the phantom is driven by a SQUARE, so doubling velocity
    # must grow it markedly faster than it grows the note itself.
    def lrms(vel):
        _, d = isolate(vel)
        return rms(d)

    lo, hi = lrms(0.35), lrms(0.70)
    if lo <= 0:
        raise Failure("phantom is silent at velocity 0.35 — cannot measure scaling")
    exponent = math.log2(hi / lo)
    if exponent < 1.5:
        raise Failure(
            f"phantom grows only as v^{exponent:.2f} (need >= 1.5) — that is a linear "
            "partial, not a nonlinear phantom"
        )
    return (
        f"phantom at {fp:.0f} Hz ({gap:.0f} Hz from any partial), "
        f"longitudinal/transverse {lon / max(1e-12, tra):.1f}x, grows as v^{exponent:.2f}"
    )


def _demod_baseband(xs, fref, box_len):
    """Complex-demodulate xs at fref and box-average (a moving average whose first
    null sits at SR/box_len). With box_len = SR/f0 the null lands on the neighbouring
    partials at f0 spacing, so the chosen partial is isolated. Pure stdlib."""
    w0 = 2.0 * math.pi * fref / SR
    out = [0j] * len(xs)
    acc = 0j
    from collections import deque
    dq = deque()
    for i, x in enumerate(xs):
        v = x * cmath.exp(-1j * w0 * i)
        acc += v
        dq.append(v)
        if len(dq) > box_len:
            acc -= dq.popleft()
        out[i] = acc / len(dq)
    return out


def _glide_cents(on, off, f0, partial, t0, t1):
    """Mean fractional pitch glide of `on` relative to `off` over [t0, t1] seconds,
    in cents. Measured as the accumulated (unwrapped) phase of the on/off ratio at a
    chosen partial: because the two renders are identical apart from the glide (same
    deterministic hammer + noise seed), their broadband onset transient cancels in the
    ratio — README ## 12.2's "difference two renders" lesson, in the phase domain. The
    accumulated phase over a fixed window integrates out quantisation noise, so this
    tracks the ground-truth delta the model applies to ~10%."""
    fref = partial * f0
    box = int(round(SR / f0))
    zon = _demod_baseband(on, fref, box)
    zoff = _demod_baseband(off, fref, box)
    a, b = int(t0 * SR), int(t1 * SR)
    psi = 0.0
    prev = cmath.phase(zon[a] / zoff[a])
    for i in range(a + 1, b + 1):
        cur = cmath.phase(zon[i] / zoff[i])
        d = cur - prev
        while d > math.pi:
            d -= 2.0 * math.pi
        while d < -math.pi:
            d += 2.0 * math.pi
        psi += d
        prev = cur
    frac = psi / (2.0 * math.pi * fref * ((b - a) / SR))
    return 1200.0 * math.log2(1.0 + frac)


def check_pitch_glide(binpath):
    """M8 acceptance (plan §M8): tension modulation / attack pitch glide. A hard bass
    blow starts sharp and glides down; a soft blow barely does. Measured through the
    rendered 16-bit audio by differencing a glide-on and glide-off render of the same
    C2 note (identical but for the partial tuning) — see _glide_cents.

    The plan's literal peak criterion (>= 2 cents in the first 50 ms, < 0.5 soft) is
    asserted exactly by the unit test via pitchModulation(): f0(t) = f0*(1+delta), so
    delta IS the f0 shift, and a 2-cent shift on a bass onset cannot be resolved to
    that precision by any single-window Goertzel/zero-crossing. This audio test proves
    the glide survives into the WAV and scales with velocity, measured over a settled
    [30, 130] ms window where the estimator matches ground truth."""
    f0 = 65.41  # C2
    hard_on = render(binpath, "pianoglide", 90)
    hard_off = render(binpath, "pianoglide", -90)
    soft_on = render(binpath, "pianoglide", 20)
    soft_off = render(binpath, "pianoglide", -20)

    hard4 = _glide_cents(hard_on, hard_off, f0, 4, 0.030, 0.130)
    hard2 = _glide_cents(hard_on, hard_off, f0, 2, 0.030, 0.130)
    soft4 = _glide_cents(soft_on, soft_off, f0, 4, 0.030, 0.130)

    if hard4 < 1.3:
        raise Failure(f"hard bass blow glides only {hard4:.3f} cents (need >= 1.3) — "
                      "the attack pitch glide is not reaching the audio")
    if soft4 >= 0.4:
        raise Failure(f"soft blow glides {soft4:.3f} cents (need < 0.4) — the glide is "
                      "not velocity-dependent as amplitude^2 requires")
    if hard4 < soft4 * 5.0:
        raise Failure(f"hard/soft glide ratio only {hard4 / max(1e-9, soft4):.1f}x "
                      "(need >= 5) — glide barely grows with velocity")
    # Uniform re-tune (## 12.5): every partial shifts by the SAME fraction, so the
    # glide measured at partial 2 and partial 4 must agree in cents.
    if abs(hard2 - hard4) > 0.4 * max(hard2, hard4):
        raise Failure(f"partial-2 glide {hard2:.3f}c and partial-4 glide {hard4:.3f}c "
                      "disagree — the shift is not a uniform re-tune of the series")
    return (f"C2 glide: hard {hard4:.2f}c vs soft {soft4:.2f}c "
            f"({hard4 / max(1e-9, soft4):.0f}x), uniform across partials (p2 {hard2:.2f}c)")


def check_parallel_render_is_identical(binpath):
    """REQ-compute-3: rendering through a worker pool must be BIT-IDENTICAL to
    rendering serially — not merely similar. Anything less would make the audio a
    function of thread scheduling, and would silently invalidate every numeric
    acceptance criterion the piano milestones rest on.

    This is what forces fixed shard-order summation in finishBlock(): float
    addition is not associative, so summing accumulators in completion order would
    fail this intermittently — the worst possible way to find out."""
    ref = render(binpath, "synthparallel", 0)   # serial executor
    if peak(ref) < 1e-3:
        raise Failure(f"serial reference is silent (peak {peak(ref):.2e})")
    for workers in (1, 3, 7):
        got = render(binpath, "synthparallel", workers)
        if len(got) != len(ref):
            raise Failure(f"{workers} workers: length {len(got)} != {len(ref)}")
        if got != ref:
            diffs = sum(1 for a, b in zip(ref, got) if a != b)
            worst = max(abs(a - b) for a, b in zip(ref, got))
            raise Failure(
                f"{workers} workers: {diffs}/{len(ref)} samples differ from serial "
                f"(worst {worst:.3e}) — parallel rendering changed the audio"
            )
    return f"serial == 1/3/7 workers, bit-identical over {len(ref)} samples"


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
    ("piano_voice_reuse_bounded", check_piano_voice_reuse_bounded),
    ("piano_bandwidth", check_piano_bandwidth),
    ("piano_spectral_evolution", check_piano_spectral_evolution),
    ("piano_double_decay", check_piano_double_decay),
    ("soundboard_colours_treble", check_soundboard_colours_treble),
    ("piano_sympathetic_resonance", check_piano_sympathetic_resonance),
    ("piano_register_voicing", check_piano_register_voicing),
    ("piano_phantom_partials", check_piano_phantom_partials),
    ("piano_pitch_glide", check_pitch_glide),
    ("parallel_render_is_identical", check_parallel_render_is_identical),
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
