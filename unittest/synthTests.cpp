#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <cmath>
#include <thread>

using namespace arstro;

TEST(LockFreeQueue_basic)
{
    LockFreeQueue<int> q(8);
    CHECK(q.empty());
    CHECK(q.push(1));
    CHECK(q.push(2));
    int v = 0;
    CHECK(q.pop(v)); CHECK(v == 1);
    CHECK(q.pop(v)); CHECK(v == 2);
    CHECK(!q.pop(v));
}

TEST(ADSR_shape_and_finish)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    ADSREnvelope env;
    env.setAttackMs(1.0);
    env.setDecayMs(1.0);
    env.setSustain(0.5);
    env.setReleaseMs(1.0);
    env.noteOn(1.0);
    // Feed a constant 1.0; envelope should rise above 0 quickly.
    double first = env.out(1.0, 0);
    for (int i = 0; i < 200; ++i) env.out(1.0, 0);
    double sustain = env.out(1.0, 0);
    CHECK(first >= 0.0 && first < sustain + 1e-9);
    CHECK_NEAR(sustain, 0.5, 0.05); // near sustain level
    CHECK(!env.isFinished());
    env.noteOff();
    for (int i = 0; i < 200; ++i) env.out(1.0, 0);
    CHECK(env.isFinished());
}

TEST(Oscillator_produces_bounded_signal)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    Oscillator osc;
    osc.setVoiceCount(5);
    osc.setDetuneCents(20);
    osc.setFrequency(220.0);
    osc.noteOn(1.0);
    bool sawPositive = false, sawNegative = false;
    double peak = 0.0;
    for (int i = 0; i < 4800; ++i)
    {
        double l = osc.out(0.0, 0);
        double r = osc.out(0.0, 1);
        if (l > 0.01) sawPositive = true;
        if (l < -0.01) sawNegative = true;
        peak = std::fmax(peak, std::fabs(l));
        CHECK(std::isfinite(l) && std::isfinite(r));
    }
    CHECK(sawPositive && sawNegative);  // it actually oscillates
    // 5 detuned voices normalized by sqrt(5) can momentarily peak near 2.24.
    CHECK(peak > 0.05 && peak < 2.4);   // non-trivial, not exploding
}

TEST(Oscillator_waveforms_each_oscillate)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    const Oscillator::Waveform shapes[] = {Oscillator::Sine, Oscillator::Square, Oscillator::Triangle, Oscillator::Saw};
    for (Oscillator::Waveform w : shapes)
    {
        Oscillator osc;
        osc.setVoiceCount(1);
        osc.setWaveform(w);
        osc.setFrequency(220.0);
        osc.noteOn(1.0);
        bool pos = false, neg = false;
        double peak = 0.0;
        for (int i = 0; i < 4800; ++i)
        {
            double s = osc.out(0.0, 0);
            if (s > 0.05) pos = true;
            if (s < -0.05) neg = true;
            peak = std::fmax(peak, std::fabs(s));
            CHECK(std::isfinite(s));
        }
        CHECK(pos && neg);             // every shape actually oscillates
        CHECK(peak > 0.3 && peak < 1.6);
    }
}

TEST(Block_different_chain_per_channel)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    Gain gL(2.0), gR(0.5);
    Block b;
    b.add(&gL, 0); // channel 0 gets x2
    b.add(&gR, 1); // channel 1 gets x0.5
    CHECK_NEAR(b.out(1.0, 0), 2.0, 1e-9);
    CHECK_NEAR(b.out(1.0, 1), 0.5, 1e-9);
}

TEST(SynthEngine_eight_note_scenario_nonsilent)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    SynthEngine eng;
    // a simple chord-ish set of 8 notes
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i)
        eng.noteOn(notes[i], 0.8);

    std::vector<double> buf;
    double peak = 0.0;
    bool finite = true;
    // ~0.5 s of audio in blocks of 128 frames
    for (int blk = 0; blk < 200; ++blk)
    {
        eng.renderBlockDouble(buf, 128);
        for (double s : buf)
        {
            if (!std::isfinite(s)) finite = false;
            peak = std::fmax(peak, std::fabs(s));
        }
    }
    CHECK(finite);
    CHECK(peak > 0.001);  // produced sound
    CHECK(eng.activeVoices() == 8);
    for (int i = 0; i < 8; ++i)
        eng.noteOff(notes[i]);
    // let releases finish
    for (int blk = 0; blk < 400; ++blk)
        eng.renderBlockDouble(buf, 128);
    CHECK(eng.activeVoices() == 0);
}

TEST(OutputBlock_quantization_range)
{
    AudioConfig::instance().setOutputBitDepth(16);
    OutputBlock out;
    CHECK(out.quantize(1.0) == 32767);
    CHECK(out.quantize(-1.0) == -32767);
    CHECK(out.quantize(2.0) == 32767); // clamped
    CHECK(out.quantize(0.0) == 0);
}

// Renders the 8-note scenario (sequential voice-split path = the shipped path)
// and returns the packed bytes.
static std::vector<uint8_t> renderScenario()
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    AudioConfig::instance().setOutputBitDepth(16);
    SynthEngine eng;
    eng.applyParam(GROUP_OSC1 + OSC_VOICE_COUNT, 5);
    eng.applyParam(GROUP_OSC2 + OSC_VOICE_COUNT, 5);
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i) eng.noteOn(notes[i], 0.8);

    std::vector<uint8_t> all, blk;
    const int frames = 128;
    for (int b = 0; b < 100; ++b)
    {
        eng.renderBlockBytes(blk, frames);
        all.insert(all.end(), blk.begin(), blk.end());
    }
    return all;
}

// The render must be deterministic across instances (no uninitialized state).
TEST(SynthEngine_render_is_deterministic)
{
    std::vector<uint8_t> a = renderScenario();
    std::vector<uint8_t> b = renderScenario();
    CHECK(!a.empty());
    CHECK(a == b);
}

// Voice-split parallel render (two halves on separate threads) must equal the
// sequential render byte-for-byte => the multi-core path is race-free.
static std::vector<uint8_t> renderParallel()
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    AudioConfig::instance().setOutputBitDepth(16);
    SynthEngine eng;
    eng.applyParam(GROUP_OSC1 + OSC_VOICE_COUNT, 5);
    eng.applyParam(GROUP_OSC2 + OSC_VOICE_COUNT, 5);
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i) eng.noteOn(notes[i], 0.8);
    std::vector<uint8_t> all, blk;
    const int frames = 128;
    for (int b = 0; b < 100; ++b)
    {
        eng.beginBlock(frames);
        std::thread t1([&] { eng.renderVoiceHalf(1, frames); });
        eng.renderVoiceHalf(0, frames);
        t1.join();
        eng.finishBlock(frames);
        eng.packBytes(blk, frames);
        all.insert(all.end(), blk.begin(), blk.end());
    }
    return all;
}

TEST(MultiCore_voice_split_parallel_is_byte_identical)
{
    std::vector<uint8_t> seq = renderScenario(); // sequential voice halves
    std::vector<uint8_t> par = renderParallel(); // halves on two threads
    CHECK(seq.size() == par.size());
    CHECK(!seq.empty());
    CHECK(seq == par);
}

// ─────────── Refactor (v1.0.0) coverage: SmoothedParameter / ParameterSet ───────────

TEST(SmoothedParameter_ramp_and_snap)
{
    SmoothedParameter p;
    p.init(1.0);
    CHECK_NEAR(p.current, 1.0, 1e-12);
    CHECK_NEAR(p.last, 1.0, 1e-12);
    CHECK_NEAR(p.target, 1.0, 1e-12);
    CHECK(!p.setTarget(1.0));            // unchanged target => false, no ramp needed
    CHECK(p.setTarget(3.0));             // moved => true
    p.beginRamp();                       // anchor last at current (1.0)
    p.interpolate(0.0); CHECK_NEAR(p.current, 1.0, 1e-12);
    p.interpolate(0.5); CHECK_NEAR(p.current, 2.0, 1e-12);
    p.interpolate(1.0); CHECK_NEAR(p.current, 3.0, 1e-12);
    p.current = 0.0;
    p.snap();                            // jump to target
    CHECK_NEAR(p.current, 3.0, 1e-12);
    CHECK_NEAR(p.last, 3.0, 1e-12);
}

TEST(ParameterSet_bulk_ops)
{
    ParameterSet ps;
    ps.resize(2);
    CHECK(ps.size() == 2);
    ps.init(0, 1.0);
    ps.init(1, 10.0);
    CHECK(ps.setTarget(0, 2.0));
    CHECK(ps.setTarget(1, 20.0));
    ps.beginRamp();
    ps.interpolate(0.5);
    CHECK_NEAR(ps.current(0), 1.5, 1e-12);
    CHECK_NEAR(ps.current(1), 15.0, 1e-12);
    CHECK_NEAR(ps.target(0), 2.0, 1e-12);
    ps.snapAll();
    CHECK_NEAR(ps.current(0), 2.0, 1e-12);
    CHECK_NEAR(ps.current(1), 20.0, 1e-12);
}

// Drives SignalProcessor's smoothing scheduler through a concrete subclass (Gain):
// a parameter change must ramp across the buffer, not jump.
TEST(SignalProcessor_property_smoothing_ramps)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    AudioConfig::instance().setBufferSize(8);
    Gain g(1.0);
    g.setSmoothEnable(true);
    g.setGain(3.0);                                          // target 1 -> 3 over 8 samples
    CHECK_NEAR(g.getPropertyTargetValue(Gain::gainID), 3.0, 1e-12);
    double y1 = g.out(1.0, 0);
    CHECK(y1 > 1.0 && y1 < 3.0);                             // mid-ramp, not snapped
    for (int i = 0; i < 8; ++i) g.out(1.0, 0);              // finish the ramp
    CHECK_NEAR(g.out(1.0, 0), 3.0, 1e-9);                    // arrived at target
    AudioConfig::instance().setBufferSize(128);             // restore for later tests
}

// Smoothing disabled => parameter applies immediately (snap), no ramp.
TEST(SignalProcessor_smoothing_disabled_snaps)
{
    AudioConfig::instance().setChannelCount(1);
    AudioConfig::instance().setBufferSize(64);
    Gain g(1.0);
    g.setSmoothEnable(false);
    g.setGain(5.0);
    CHECK_NEAR(g.out(1.0, 0), 5.0, 1e-9);
    AudioConfig::instance().setBufferSize(128);
}

// Bypass returns the input untouched; otherwise gain is applied.
TEST(SignalProcessor_bypass_passthrough)
{
    AudioConfig::instance().setChannelCount(1);
    Gain g(4.0);
    CHECK(!g.isBypassed());
    CHECK_NEAR(g.out(2.0, 0), 8.0, 1e-9);
    g.setBypass(true);
    CHECK(g.isBypassed());
    CHECK_NEAR(g.out(2.0, 0), 2.0, 1e-9);                    // passthrough
}

// Natural stereo: a mono input fed to both channels must yield IDENTICAL tails at
// width 0 (mono) and DECORRELATED tails at width 1 (wide). See reverb/README.md.
TEST(Reverb_stereo_decorrelation)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);

    auto runDiff = [](Sample width) {
        Reverb rv;
        rv.setWidth(width);
        rv.setDelayInMs(20.0);
        rv.setDecayInMs(300.0);
        rv.setMix(1.0);                       // fully wet so we compare the tail
        double diff = 0.0, energy = 0.0;
        for (int i = 0; i < 8000; ++i)
        {
            Sample in = std::sin(i * 0.05);   // same mono excitation on both channels
            double l = rv.out(in, 0);
            double r = rv.out(in, 1);
            diff += (l - r) * (l - r);
            energy += l * l + r * r;
        }
        return std::make_pair(diff, energy);
    };

    auto mono = runDiff(0.0);
    CHECK(mono.first < 1e-12);                // width 0 => L and R identical (mono)

    auto wide = runDiff(1.0);
    CHECK(wide.second > 1e-6);                // produced a tail
    CHECK(wide.first > 1e-4 * wide.second);   // width 1 => L and R meaningfully decorrelated
}

// ─────────────────────────── physical/ (piano) ───────────────────────────
// Verifies the math in src/physical/README.md, not just "doesn't crash".

namespace {
// Goertzel magnitude at a target frequency — same technique tests/run_integration.py
// uses (see check_waveform_selection), reimplemented locally for a C++ unit test.
double goertzelMag(const std::vector<double> &xs, double freqHz, double sr)
{
    double k = 0.5 + (xs.size() * freqHz / sr);
    double w = (2.0 * M_PI / xs.size()) * k;
    double coeff = 2.0 * std::cos(w);
    double q0 = 0, q1 = 0, q2 = 0;
    for (double x : xs)
    {
        q0 = coeff * q1 - q2 + x;
        q2 = q1;
        q1 = q0;
    }
    double real = q1 - q2 * std::cos(w);
    double imag = q2 * std::sin(w);
    return std::sqrt(real * real + imag * imag);
}
double rms(const std::vector<double> &xs)
{
    double s = 0;
    for (double x : xs) s += x * x;
    return std::sqrt(s / xs.size());
}
}

// REQ-piano-3: a driven resonator settles at its configured frequency and decays.
TEST(StringResonator_frequency_and_decay)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    StringResonator res;
    res.setFrequencyHz(440.0);
    res.setDecaySeconds(0.3);

    std::vector<double> early, late;
    for (int i = 0; i < 200; ++i) early.push_back(res.out((i == 0) ? 1.0 : 0.0, 0));
    for (int i = 0; i < 4000; ++i) res.out(0.0, 0);
    for (int i = 0; i < 4000; ++i) late.push_back(res.out(0.0, 0));

    double magAt440 = goertzelMag(late, 440.0, 48000.0);
    double magAt220 = goertzelMag(late, 220.0, 48000.0);
    CHECK(magAt440 > magAt220 * 5.0); // resonant at the configured frequency, not elsewhere
    CHECK(rms(late) < rms(early));    // decaying (T60=0.3s, ~14 periods of 0.3s by 4000+4000 samples in)
}

// REQ-piano-2/4: inharmonic partial 4 lands where B predicts, not at the exact harmonic.
// (n=4, beta=0.125 chosen so the mode-shape gain g_4=|sin(4*pi*0.125)|=1 is maximal, and
// the absolute frequency shift — which grows with n — is comfortably above Goertzel's
// bin resolution at this window length; n=2 with a small/moderate B is too close to the
// exact harmonic to resolve reliably at practical window sizes.)
TEST(StringPartialBank_partials_are_inharmonic)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    StringPartialBank bank;
    Sample f0 = 110.0, B = 0.01; // deliberately large B so the shift is easy to measure
    bank.setFundamentalHz(f0);
    bank.setInharmonicity(B);
    bank.setBaseDecaySeconds(2.0);
    bank.setStrikePosition(0.125);

    std::vector<double> out;
    for (int i = 0; i < 16000; ++i) out.push_back(bank.out((i == 0) ? 1.0 : 0.0, 0));

    Sample exactHarmonic4 = 4.0 * f0;
    Sample inharmonic4 = 4.0 * f0 * std::sqrt(1.0 + B * 16.0); // README ## 2 formula, n=4

    double magExact = goertzelMag(out, exactHarmonic4, 48000.0);
    double magInharmonic = goertzelMag(out, inharmonic4, 48000.0);
    CHECK(magInharmonic > magExact * 1.5); // energy sits at the stiffness-shifted frequency
}

// REQ-piano-1/2: a struck note is bounded (no blow-up) and decays after the strike.
TEST(PianoVoice_note_bounded_and_decaying)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(220.0);
    v.noteOn(0.9);

    double maxAbs = 0.0;
    std::vector<double> attack, sustained;
    for (int i = 0; i < 48000; ++i)
    {
        Sample s = v.out(0.0, 0);
        if (std::fabs(s) > maxAbs) maxAbs = std::fabs(s);
        if (i < 2000) attack.push_back(s);
        if (i >= 46000) sustained.push_back(s);
    }
    CHECK(maxAbs < 1.0);              // no blow-up (README ## Units: near +-1 at full velocity)
    CHECK(maxAbs > 0.01);             // actually produced sound
    CHECK(rms(sustained) < rms(attack) * 0.5); // decayed well below the strike level by 1s in
}

// REQ-piano-7: damper (noteOff, not held) measurably speeds up decay vs. sustain-held.
TEST(PianoVoice_damper_speeds_up_decay_vs_sustain_held)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    auto measure = [](bool sustainHeld) {
        PianoVoice v;
        v.setFrequency(220.0);
        v.setDamperHeld(sustainHeld);
        v.noteOn(0.8);
        for (int i = 0; i < 4800; ++i) v.out(0.0, 0); // let the note establish (100ms)
        v.noteOff();
        for (int i = 0; i < 1500; ++i) v.out(0.0, 0); // past the ~20ms damper ramp
        std::vector<double> tail;
        for (int i = 0; i < 2000; ++i) tail.push_back(v.out(0.0, 0));
        return rms(tail);
    };

    double dampedRms = measure(false);
    double heldRms = measure(true);
    CHECK(dampedRms < heldRms * 0.5); // damper (not held) decays much faster than sustain-held
}

// REQ-piano-6: sympathetic resonance is frequency-selective, emerging from the shared
// PianoBridge, not from a per-note-pair table (there is none in this codebase).
TEST(PianoVoice_sympathetic_resonance_is_frequency_selective)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    PianoBridge bridge;
    PianoVoice struckVoice, samePitchSilent, offPitchSilent;
    struckVoice.setFrequency(220.0);
    samePitchSilent.setFrequency(220.0);     // same pitch, never struck
    offPitchSilent.setFrequency(233.08);     // a half-step away, never struck

    struckVoice.setBridge(&bridge);
    samePitchSilent.setBridge(&bridge);
    offPitchSilent.setBridge(&bridge);

    samePitchSilent.noteOn(0.0); // "silently depressed": damper lifted, no hammer strike
    offPitchSilent.noteOn(0.0);
    struckVoice.noteOn(0.9);

    std::vector<double> samePitchOut, offPitchOut;
    for (int i = 0; i < 48000; ++i)
    {
        struckVoice.out(0.0, 0);
        samePitchOut.push_back(samePitchSilent.out(0.0, 0));
        offPitchOut.push_back(offPitchSilent.out(0.0, 0));
        bridge.tick();
    }

    double sympatheticEnergy = goertzelMag(samePitchOut, 220.0, 48000.0);
    double offPitchEnergy = goertzelMag(offPitchOut, 233.08, 48000.0);
    CHECK(sympatheticEnergy > offPitchEnergy * 2.0); // same-pitch string picks up far more energy
}

// REQ-piano-1/2: louder strikes produce louder output (hammer velocity drives dynamics).
TEST(PianoVoice_velocity_increases_loudness)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    auto peak = [](Sample velocity) {
        PianoVoice v;
        v.setFrequency(220.0);
        v.noteOn(velocity);
        double m = 0.0;
        for (int i = 0; i < 4800; ++i) m = std::max(m, std::fabs((double)v.out(0.0, 0)));
        return m;
    };
    CHECK(peak(0.9) > peak(0.3) * 1.5);
}

// Regression (REQ-piano-12 addendum): reusing a voice whose damper is fully
// engaged must NOT spike. PianoEngine's real allocation path calls
// setFrequency() (recomputes StringPartialBank decay coefficients from
// whatever mDamperValue currently is) BEFORE noteOn() (where reset() runs) —
// on a voice-stolen bank that was left fully damped by a previous note, that
// briefly bakes a short-T60/large-input-gain coefficient set (README ## 1's
// G=(1-r^2)sin(theta) grows as decay shrinks) into the resonators; the new
// note's hammer strike then drove that miscalibrated resonator, producing a
// ~20x amplitude spike (measured) instead of a normal struck note. Fixed by
// having StringPartialBank::reset() also recompute decay coefficients from
// the just-cleared damper value. This test exercises the exact sequence
// PianoEngine::noteOnMidi uses on a stolen voice.
TEST(PianoVoice_reused_voice_after_damper_engaged_stays_bounded)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(261.63); // C4
    v.noteOn(0.8);
    for (int i = 0; i < 4800; ++i) v.out(0.0, 0); // ring 100ms
    v.noteOff();                                  // engage damper (no pedal held)
    for (int i = 0; i < 4800; ++i) v.out(0.0, 0); // let it fully engage (>>20ms ramp)

    // Voice-stealing path: setFrequency() first, then noteOn() — matches
    // PianoEngine::noteOnMidi exactly.
    v.setFrequency(440.0); // A4
    v.noteOn(0.8);
    double maxAbs = 0.0;
    for (int i = 0; i < 4800; ++i)
        maxAbs = std::max(maxAbs, std::fabs((double)v.out(0.0, 0)));
    CHECK(maxAbs < 1.0); // bounded like any normal struck note (README ## Units), not a spike
}

// ───────────────── M1: frequency-dependent loss (spectral evolution) ─────────────────
// Plan docs/piano-physics-plan.md §M1; math in src/physical/README.md ## 3.

// M1 acceptance criterion 1, now in its ORIGINAL form: M2 made partial 20 exist
// at C4 (63 active partials), so the plan's stated target is asserted directly.
// This tilt is what makes a bright attack mellow to near-sine; its absence is why
// the pre-M1 model read as a plucked string.
TEST(StringPartialBank_high_partials_decay_far_faster)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    StringPartialBank bank;
    bank.setFundamentalHz(261.6); // C4
    bank.setInharmonicity(1.6831e-4);
    bank.setBaseDecaySeconds(PianoVoice::defaultBaseDecaySeconds(261.6));

    CHECK(bank.partialCount() > 20); // M2: C4 now carries 63 partials, not 12

    const double t60First = bank.partialDecaySeconds(0);
    const double t60P20 = bank.partialDecaySeconds(19); // partial 20
    CHECK(t60First > 0.0 && t60P20 > 0.0);
    CHECK(t60First / t60P20 >= 50.0); // plan §M1 criterion 1, measured 100.7

    const int top = bank.partialCount() - 1;
    const double t60Top = bank.partialDecaySeconds(top);
    CHECK(t60Top > 0.0);

    // Monotonic: every partial decays at least as fast as the one below it.
    bool monotonic = true;
    for (int i = 1; i <= top; ++i)
        monotonic = monotonic && bank.partialDecaySeconds(i) <= bank.partialDecaySeconds(i - 1);
    CHECK(monotonic);
}

// The model itself, asserted exactly rather than statistically: alpha(f) must be
// affine in omega^2, i.e. (alpha_n - alpha_1)/(w_n^2 - w_1^2) is the SAME c3 for
// every partial. This pins README ## 3's loss law, not just its consequences.
TEST(StringPartialBank_loss_law_is_quadratic_in_frequency)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    StringPartialBank bank;
    bank.setFundamentalHz(261.6);
    bank.setInharmonicity(1.6831e-4);
    bank.setBaseDecaySeconds(6.9);
    bank.setBrightnessDecaySeconds(0.08);

    const double kT60 = 6.907755; // ln(1000)
    const double a1 = kT60 / bank.partialDecaySeconds(0);
    const double w1 = 2.0 * M_PI * bank.partialFrequencyHz(0);
    double c3Ref = -1.0;
    bool consistent = true;
    for (int i = 1; i < bank.partialCount(); ++i)
    {
        const double an = kT60 / bank.partialDecaySeconds(i);
        const double wn = 2.0 * M_PI * bank.partialFrequencyHz(i);
        const double c3 = (an - a1) / (wn * wn - w1 * w1);
        if (c3Ref < 0.0) c3Ref = c3;
        if (std::fabs(c3 - c3Ref) > 1e-9 * std::fabs(c3Ref)) consistent = false;
    }
    CHECK(consistent);
    CHECK(c3Ref > 0.0); // highs really do lose more, not less

    // The fundamental's T60 is honoured exactly — it is a parameter, not a hint.
    CHECK_NEAR(bank.partialDecaySeconds(0), 6.9, 1e-6);
}

// Acceptance criterion 3: decay spans the keyboard. A single 3 s constant for all
// 88 notes (pre-M1) is wrong at both ends — bass sounds cut off, treble sounds
// like a music box.
TEST(PianoVoice_decay_scales_with_pitch)
{
    const double a0 = PianoVoice::defaultBaseDecaySeconds(27.5);   // A0
    const double c7 = PianoVoice::defaultBaseDecaySeconds(2093.0); // C7
    CHECK(a0 / c7 >= 10.0); // measured 50.7

    // Strictly decreasing with pitch across the whole keyboard.
    bool decreasing = true;
    double prev = PianoVoice::defaultBaseDecaySeconds(27.5);
    for (int midi = 22; midi <= 108; midi += 6)
    {
        const double f = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        const double t = PianoVoice::defaultBaseDecaySeconds(f);
        decreasing = decreasing && (t <= prev);
        prev = t;
    }
    CHECK(decreasing);
}

// ───────────────── M2: pitch-dependent partial count (bandwidth) ─────────────────
// Plan §M2; math in src/physical/README.md ## 2.

// The partial count must follow pitch, not be a constant: a string has as many
// modes as fit below Nyquist. The cutoff is evaluated on the INHARMONIC f_n,
// which stiffness stretches well beyond n*f0.
TEST(StringPartialBank_partial_count_scales_with_pitch)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    const double nyquist = 48000.0 * 0.5;

    auto bankAt = [](double f0, double b) {
        auto *bank = new StringPartialBank();
        bank->setFundamentalHz(f0);
        bank->setInharmonicity(b);
        bank->setBaseDecaySeconds(PianoVoice::defaultBaseDecaySeconds(f0));
        return bank;
    };

    // Bass fills the cap; the treble needs only a handful.
    StringPartialBank *bass = bankAt(27.5, 0.00281);    // A0
    StringPartialBank *mid = bankAt(261.6, 1.6831e-4);  // C4
    StringPartialBank *treble = bankAt(4186.0, 5e-5);   // C8

    CHECK(bass->partialCount() == StringPartialBank::kMaxPartials); // cap-limited
    CHECK(mid->partialCount() > 40 && mid->partialCount() <= StringPartialBank::kMaxPartials);
    CHECK(treble->partialCount() < 10);                            // Nyquist-limited
    CHECK(treble->partialCount() >= 1);                            // never zero

    // Strictly increasing bandwidth need as pitch falls.
    CHECK(bass->partialCount() >= mid->partialCount());
    CHECK(mid->partialCount() > treble->partialCount());

    // NOTHING may sit at or above Nyquist — aliasing would fold it back audibly.
    for (StringPartialBank *b : {bass, mid, treble})
        for (int i = 0; i < b->partialCount(); ++i)
            CHECK(b->partialFrequencyHz(i) < nyquist);

    delete bass; delete mid; delete treble;
}

// Every f_n across the WHOLE keyboard stays below Nyquist, using each note's own
// register-dependent inharmonicity (the value that actually stretches the series).
TEST(StringPartialBank_no_partial_above_nyquist_across_keyboard)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    const double nyquist = 48000.0 * 0.5;
    bool allBelow = true, allPositive = true;

    for (int midi = 21; midi <= 108; ++midi) // A0..C8
    {
        const double f0 = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        double b = 0.00056 * std::pow(100.0 / f0, 1.25);
        if (b < 0.00005) b = 0.00005;
        if (b > 0.02) b = 0.02;

        StringPartialBank bank;
        bank.setFundamentalHz(f0);
        bank.setInharmonicity(b);
        bank.setBaseDecaySeconds(PianoVoice::defaultBaseDecaySeconds(f0));
        CHECK(bank.partialCount() >= 1);
        for (int i = 0; i < bank.partialCount(); ++i)
        {
            if (!(bank.partialFrequencyHz(i) < nyquist)) allBelow = false;
            if (!(bank.partialDecaySeconds(i) > 0.0)) allPositive = false;
        }
    }
    CHECK(allBelow);
    CHECK(allPositive);
}

// M2 inlined §1's recurrence for speed. That optimisation must not change the
// math — drive a single-partial bank and a StringResonator configured identically
// and require sample-exact agreement. This also keeps StringResonator (still the
// soundboard's mode type) as the verified reference implementation.
TEST(StringPartialBank_flattened_loop_matches_reference_resonator)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    // f0 high enough that only partial 1 fits below Nyquist -> a 1-partial bank.
    const double f0 = 15000.0, beta = 0.125, t60 = 0.5;
    StringPartialBank bank;
    bank.setFundamentalHz(f0);
    bank.setInharmonicity(0.0);
    bank.setBaseDecaySeconds(t60);
    bank.setStrikePosition(beta);
    CHECK(bank.partialCount() == 1);

    // Since M4 the bank runs TWO resonators per low partial (vertical + horizontal,
    // README ## 5b), so the reference is the sum of two — which also pins the
    // polarisation split itself, not just the recurrence.
    const double root = std::sqrt(StringPartialBank::kPolarizationDecayRatio);
    const double t60n = bank.partialDecaySeconds(0);

    StringResonator refV, refH;
    refV.setImpulseNormalized(true); // strings are impulse-normalised (README ## 1)
    refV.setFrequencyHz(bank.partialFrequencyHz(0));
    refV.setDecaySeconds(t60n / root);
    refH.setImpulseNormalized(true);
    refH.setFrequencyHz(bank.partialFrequencyHz(0) * (1.0 + StringPartialBank::kPolarizationDetune));
    refH.setDecaySeconds(std::min(t60n * root, (double)StringPartialBank::kPolarizationT60Cap));

    // The bank folds the mode-shape gain, the polarisation split AND the physical
    // velocity scale 1/(kModalMass*fs) (README ## 6) into its drive; the references get
    // them applied to their inputs instead. Same signal either way.
    const double gBase = std::fabs(std::sin(M_PI * beta)) /
                         (StringPartialBank::kModalMass * 48000.0);
    const double gV = gBase * (1.0 - StringPartialBank::kPolarizationSplit);
    const double gH = gBase * StringPartialBank::kPolarizationSplit;
    double maxErr = 0.0, energy = 0.0;
    for (int i = 0; i < 2000; ++i)
    {
        const double x = (i == 0) ? 1.0 : 0.0; // impulse
        const double a = bank.out(x, 0);
        const double b = refV.out(x * gV, 0) + refH.out(x * gH, 0);
        maxErr = std::max(maxErr, std::fabs(a - b));
        energy += a * a;
    }
    CHECK(energy > 1e-9);   // it actually rang, so the comparison means something
    CHECK(maxErr < 1e-12);  // sample-exact
}

// ───────────────── M3: coupled hammer<->string interaction ─────────────────
// Plan §M3; math in src/physical/README.md ## 6.

namespace {
// Runs PianoVoice's coupled loop directly so the contact itself can be observed:
// hammer sees the string's displacement, string is driven by the reaction force.
struct ContactRun
{
    int samples = 0;
    std::vector<double> force;
};
ContactRun runContact(double f0, double velocity)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    StringPartialBank bank;
    double b = 0.00056 * std::pow(100.0 / f0, 1.25);
    if (b < 0.00005) b = 0.00005;
    if (b > 0.02) b = 0.02;
    bank.setFundamentalHz(f0);
    bank.setInharmonicity(b);
    bank.setBaseDecaySeconds(PianoVoice::defaultBaseDecaySeconds(f0));

    HammerExciter h;
    h.strike(velocity);
    ContactRun r;
    for (int i = 0; i < 48000 && h.isInContact(); ++i)
    {
        const double f = h.out(bank.displacementAtStrike(), 0);
        bank.out(f, 0);
        r.force.push_back(f);
        ++r.samples;
    }
    return r;
}
}

// Criterion 1 + 4: with the string yielding, contact duration is a RESULT of string
// impedance, so it varies with pitch — structurally impossible while the felt
// compressed against a rigid wall. And in the treble contact outlasts a whole
// string period, so the hammer stays engaged across several reflections: the same
// model produces genuinely different excitation in different registers.
TEST(HammerString_contact_duration_varies_with_pitch)
{
    const double bassMs = runContact(65.4, 0.8).samples / 48.0;   // C2
    const double midMs = runContact(261.6, 0.8).samples / 48.0;   // C4
    const double trebleMs = runContact(2093.0, 0.8).samples / 48.0; // C7

    // Bass strings yield more, so the felt stays in contact markedly longer.
    CHECK(bassMs > midMs * 1.5);
    CHECK(midMs > trebleMs);
    // All within a physically sensible window (real pianos: ~1-5 ms).
    CHECK(trebleMs > 0.5 && bassMs < 9.0);

    // Criterion 4: treble contact spans more than one period of f0.
    const double treblePeriodMs = 1000.0 / 2093.0;
    CHECK(trebleMs > treblePeriodMs);
    // ...while the bass hammer has left long before its period elapses.
    CHECK(bassMs < 1000.0 / 65.4);
}

// Criterion 2: harder strikes still compress the felt faster and leave sooner.
TEST(HammerString_contact_shortens_with_velocity)
{
    double prev = 1e9;
    bool monotonic = true;
    for (double v : {0.2, 0.4, 0.6, 0.8, 1.0})
    {
        const double ms = runContact(261.6, v).samples / 48.0;
        if (ms > prev) monotonic = false;
        prev = ms;
    }
    CHECK(monotonic);
    CHECK(runContact(261.6, 0.2).samples > runContact(261.6, 1.0).samples);
}

// Criterion 3: the force pulse is no longer a smooth one-shot bump. The wave
// launched at the strike point reflects off the near termination and returns while
// the hammer is still touching, re-modulating the contact force — a piano
// fingerprint, and only resolvable because M2 gave the bass a full partial series.
TEST(HammerString_force_pulse_shows_reflection_ripple)
{
    const ContactRun r = runContact(65.4, 0.8); // C2
    CHECK(r.samples > 50);

    size_t peak = 0;
    for (size_t i = 0; i < r.force.size(); ++i)
        if (r.force[i] > r.force[peak]) peak = i;

    int localMaxima = 0;
    for (size_t i = peak + 2; i + 2 < r.force.size(); ++i)
        if (r.force[i] > r.force[i - 1] && r.force[i] > r.force[i + 1] &&
            r.force[i] > 0.02 * r.force[peak])
            ++localMaxima;
    CHECK(localMaxima >= 1); // at least one re-excitation after the primary peak
}

// The coupling must actually be wired: driving the hammer against a displaced
// string has to change the force it produces. Guards against the input being
// silently ignored again (which is exactly the bug M3 fixed).
TEST(HammerString_coupling_is_actually_connected)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    // Advance two hammers IDENTICALLY so their states match exactly, then differ
    // only in the string displacement on the next step. (Comparing over many steps
    // would invert the sign: less force means less deceleration, so the yielding
    // hammer penetrates further and can end up producing MORE force later.)
    HammerExciter rigid, yielding;
    rigid.strike(0.8);
    yielding.strike(0.8);
    for (int i = 0; i < 40; ++i)
    {
        rigid.out(0.0, 0);
        yielding.out(0.0, 0);
    }
    const double fRigid = rigid.out(0.0, 0);       // rigid wall: string never moves
    const double fYielding = yielding.out(1e-5, 0); // string yields away from the felt

    CHECK(fRigid > 0.0);        // there is a contact force to compare
    CHECK(fRigid != fYielding); // the displacement input reaches the force law at all
    CHECK(fYielding < fRigid);  // a yielding string reduces compression, hence force
}

// ───────────────── M4: two transverse polarisations (double decay) ─────────────────
// Plan §M4; math in src/physical/README.md ## 5b.

namespace {
// Least-squares slope of log(RMS) over [t0,t1], in nepers/s. Fitting across many
// sub-windows (rather than differencing two of them) averages out the slow beating
// between the two polarisations, which otherwise makes a two-point rate unreliable.
double decayRate(const std::vector<double> &y, double t0, double t1)
{
    const int kWindows = 12;
    const double sr = 48000.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int w = 0; w < kWindows; ++w)
    {
        const double a = t0 + (t1 - t0) * w / kWindows;
        const double b = t0 + (t1 - t0) * (w + 1) / kWindows;
        const size_t i0 = (size_t)(a * sr), i1 = (size_t)(b * sr);
        if (i1 > y.size() || i1 <= i0) continue;
        double acc = 0;
        for (size_t i = i0; i < i1; ++i) acc += y[i] * y[i];
        const double rms = std::sqrt(acc / (double)(i1 - i0));
        if (rms < 1e-12) continue;
        const double x = 0.5 * (a + b), ly = std::log(rms);
        sx += x; sy += ly; sxx += x * x; sxy += x * ly; ++n;
    }
    if (n < 4) return 0.0;
    const double denom = n * sxx - sx * sx;
    if (std::fabs(denom) < 1e-18) return 0.0;
    return -(n * sxy - sx * sy) / denom; // positive = decaying
}

// The crossover time predicted by README ## 5b, from the model's own constants:
// t_cross = ln(1/eps) / (a_v * (1 - 1/R)).
double predictedCrossover(double f0)
{
    const double R = StringPartialBank::kPolarizationDecayRatio;
    const double eps = StringPartialBank::kPolarizationSplit;
    const double t60v = PianoVoice::defaultBaseDecaySeconds(f0) / std::sqrt(R);
    const double av = 6.907755278982137 / t60v;
    return std::log(1.0 / eps) / (av * (1.0 - 1.0 / R));
}

std::vector<double> renderHeld(double f0, double seconds)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(f0);
    // ONE unison string: unison detuning (README ## 5) beats at ~0.2 Hz, which
    // modulates the envelope on a timescale comparable to the measurement span and
    // would confound a test aimed at the POLARISATION mechanism. Beating is a real
    // feature tested elsewhere; here it is isolated out.
    v.setUnisonCount(1);
    v.setDamperHeld(true); // sustain: measure the string's own decay, not the damper
    v.noteOn(0.9);
    std::vector<double> y((size_t)(seconds * 48000));
    for (auto &s : y) s = v.out(0.0, 0);
    return y;
}
}

// Acceptance criterion 1: the envelope is NOT a single exponential. The prompt sound
// (vertical, strongly bridge-coupled) falls fast; once it has decayed past the
// weakly-coupled horizontal plane, the long quiet aftersound takes over. Windows are
// placed relative to each note's own predicted crossover, because that time scales
// with T60 — the plan's fixed [0,0.5]/[1.5,3] windows only straddle it correctly in
// the mid register (measured 5.5x at C4 but 2.6x at C3, where the crossover is 2.3 s).
TEST(PianoVoice_double_decay_prompt_then_aftersound)
{
    // Bass and mid, where double decay is a real and prominent phenomenon. Only the
    // BRIDGE-loss term splits between the planes (README ## 5b), so how strongly a
    // note double-decays depends on how bridge-dominated its fundamental is: C3 is
    // 89 % bridge loss, C4 77 %. Measured ratios 5.4 and 4.3.
    for (double f0 : {130.8, 261.6}) // C3, C4
    {
        const double tc = predictedCrossover(f0);
        const std::vector<double> y = renderHeld(f0, 4.5 * tc);

        const double early = decayRate(y, 0.10 * tc, 0.70 * tc);
        const double late = decayRate(y, 2.00 * tc, 4.00 * tc);

        CHECK(early > 0.0); // it really is decaying early on
        CHECK(late > 0.0);  // ...and still decaying late, just far more slowly
        CHECK(early >= late * 3.0);
    }
}

// ...and the strength of the effect must FALL with pitch, because a treble
// fundamental is increasingly internal-loss dominated (C5 is only 50 % bridge loss)
// and the two planes converge. That trend is the physics of the c1-only split, and
// it matches real pianos, where the aftersound is a bass/mid phenomenon. Demanding a
// flat >=3x across the whole keyboard would be demanding the model be WRONG.
TEST(PianoVoice_double_decay_weakens_toward_the_treble)
{
    double previous = 1e9;
    bool falling = true;
    for (double f0 : {130.8, 261.6, 523.3}) // C3, C4, C5
    {
        const double tc = predictedCrossover(f0);
        const std::vector<double> y = renderHeld(f0, 4.5 * tc);
        const double early = decayRate(y, 0.10 * tc, 0.70 * tc);
        const double late = decayRate(y, 2.00 * tc, 4.00 * tc);
        CHECK(late > 0.0);
        const double ratio = early / late;
        if (ratio > previous) falling = false;
        previous = ratio;
    }
    CHECK(falling);
    CHECK(previous > 1.2); // still present in the treble, just much weaker
}

// The aftersound must actually be the HORIZONTAL plane, not just a slower tail of
// the same thing: kill the polarisation split by asking for a single-polarisation
// bank (one partial, above kPolarizedPartials' reach is not possible, so instead
// compare decay ratios) — here we assert the late rate tracks the modelled
// horizontal decay rather than the vertical one.
TEST(PianoVoice_aftersound_rate_matches_horizontal_polarisation)
{
    const double f0 = 261.6; // C4
    const double R = StringPartialBank::kPolarizationDecayRatio;
    const double t60n = PianoVoice::defaultBaseDecaySeconds(f0);
    const double expectedHorizRate = 6.907755278982137 / (t60n * std::sqrt(R));
    const double expectedVertRate = 6.907755278982137 / (t60n / std::sqrt(R));

    const double tc = predictedCrossover(f0);
    const std::vector<double> y = renderHeld(f0, 4.5 * tc);
    const double late = decayRate(y, 2.00 * tc, 4.00 * tc);

    // Far closer to the horizontal (slow) rate than the vertical (fast) one.
    CHECK(std::fabs(late - expectedHorizRate) < std::fabs(late - expectedVertRate));
    CHECK(late < expectedVertRate * 0.5);
}

// ───────────────── M5: soundboard / bridge ─────────────────
// Plan §M5; math in src/physical/README.md ## 8.

// Acceptance: the soundboard must COLOUR the treble. Before M5 the bridge had 8
// modes spanning 80-700 Hz, so everything above 700 Hz radiated completely flat —
// naked resonators, which is music-box territory rather than an instrument.
TEST(PianoBridge_colours_the_treble)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    PianoBridge bridge;

    // Impulse the shared bus and capture the radiated response.
    std::vector<double> y(24000);
    for (size_t i = 0; i < y.size(); ++i)
    {
        bridge.accumulate(i == 0 ? 1.0 : 0.0);
        bridge.tick();
        y[i] = bridge.radiatedOutput();
    }

    auto mag = [&](double f) {
        const double w = 2.0 * M_PI * f / 48000.0, c = 2.0 * std::cos(w);
        double s1 = 0, s2 = 0;
        for (double v : y) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::hypot(s1 - s2 * std::cos(w), s2 * std::sin(w));
    };

    std::vector<double> db;
    double peak = 0.0;
    for (double f = 700.0; f <= 5000.0; f += 25.0) peak = std::max(peak, mag(f));
    CHECK(peak > 1e-6); // it radiates up there at all
    for (double f = 700.0; f <= 5000.0; f += 25.0)
        db.push_back(20.0 * std::log10(std::max(1e-12, mag(f)) / peak));
    std::sort(db.begin(), db.end());

    const double variation = db.back() - db[(size_t)(db.size() * 0.05)];
    CHECK(variation >= 6.0); // measured 10.0 dB — a real board ripples ~10 dB up here
}

// Plate physics: a soundboard's modal density is CONSTANT in Hz (bending waves give
// w ~ k^2), so the modes must be spread roughly uniformly in frequency across the
// range — not bunched at the bottom the way the pre-M5 set was, and not laid out as
// a harmonic series.
TEST(PianoBridge_modes_span_the_audible_range_uniformly)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    PianoBridge bridge;
    std::vector<double> y(24000);
    for (size_t i = 0; i < y.size(); ++i)
    {
        bridge.accumulate(i == 0 ? 1.0 : 0.0);
        bridge.tick();
        y[i] = bridge.radiatedOutput();
    }
    auto mag = [&](double f) {
        const double w = 2.0 * M_PI * f / 48000.0, c = 2.0 * std::cos(w);
        double s1 = 0, s2 = 0;
        for (double v : y) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::hypot(s1 - s2 * std::cos(w), s2 * std::sin(w));
    };

    // Every octave band from 125 Hz to 4 kHz must carry real energy — i.e. modes are
    // present across the whole range, not just the bottom.
    double loudest = 0.0;
    std::vector<double> band;
    for (double centre : {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0})
    {
        double acc = 0.0;
        for (double f = centre * 0.75; f <= centre * 1.5; f += centre * 0.05) acc += mag(f);
        band.push_back(acc);
        loudest = std::max(loudest, acc);
    }
    for (double b : band)
        CHECK(b > loudest * 0.02); // no band is effectively dead (>-34 dB of the loudest)
}

int main()
{
    return mini::runAll();
}
