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

// REQ-piano-7 (M9.1 amendment), README ## 7.1: the top ~1.5-2 octaves have no damper,
// so releasing the key leaves the string ringing. Below the cutoff the damper still
// works; at/above it, noteOff() changes nothing.
TEST(PianoVoice_top_octave_notes_have_no_damper)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    // The cutoff law itself (geometric mean of MIDI 88/89 ~ 1357 Hz).
    CHECK(PianoVoice::defaultHasDamper(1046.5));   // C6, damped
    CHECK(PianoVoice::defaultHasDamper(1318.5));   // MIDI 88, still damped (below cutoff)
    CHECK(!PianoVoice::defaultHasDamper(1396.9));  // MIDI 89, undamped (above cutoff)
    CHECK(!PianoVoice::defaultHasDamper(2093.0));  // C7, undamped

    // Behavioural: tail energy shortly after noteOff, released vs sustain-held.
    auto tailRms = [](double f0, bool held) {
        PianoVoice v;
        v.setFrequency(f0);
        v.setDamperHeld(held);
        v.noteOn(0.8);
        for (int i = 0; i < 2400; ++i) v.out(0.0, 0); // 50 ms struck
        v.noteOff();
        for (int i = 0; i < 1500; ++i) v.out(0.0, 0); // past the ~20 ms damper ramp
        std::vector<double> tail;
        for (int i = 0; i < 2400; ++i) tail.push_back(v.out(0.0, 0));
        return rms(tail);
    };

    // Below the cutoff (C6): releasing engages the damper -> much faster decay.
    CHECK(tailRms(1046.5, false) < tailRms(1046.5, true) * 0.6);

    // Above the cutoff (C7): no damper, so release is a no-op — the released tail
    // matches the sustain-held tail. Magnitude floor first (the ratio is meaningless
    // if both are silent).
    double highHeld = tailRms(2093.0, true);
    double highOff = tailRms(2093.0, false);
    CHECK(highHeld > 1e-5);              // it is actually still ringing
    CHECK(highOff > highHeld * 0.9);     // release changed nothing (no damper)
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
    // Deliberately NOT the C4 anchor value: with M6 grading the modal mass by
    // register (README ## 11.1) an off-anchor mass is what actually exercises the
    // 1/m in the drive gain — at m = 1 a missing 1/m would pass unnoticed.
    const double modalMass = 2.5;
    bank.setModalMass(modalMass);
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
    // velocity scale 1/(m*fs) (README ## 6, ## 11.1) into its drive; the references get
    // them applied to their inputs instead. Same signal either way.
    const double gBase = std::fabs(std::sin(M_PI * beta)) / (modalMass * 48000.0);
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

// ───────────────── M6: per-register voicing ─────────────────
// Plan §M6; math in src/physical/README.md ## 11.

namespace {
// Drives the coupled contact with the REGISTER-SCALED parameters PianoVoice
// actually uses, rather than the uniform defaults runContact() above uses. The
// two together are what make M6's criterion 3 measurable: the difference between
// the graded and ungraded contact is precisely M6's contribution.
struct RegisterContact
{
    double contactMs = 0.0;
    double energyIn = 0.0;   // hammer kinetic energy at impact
    double energyOut = 0.0;  // string modal energy once the felt has left
};
RegisterContact runRegisterContact(double f0, double velocity, bool graded)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    double b = 0.00056 * std::pow(100.0 / f0, 1.25);
    if (b < 0.00005) b = 0.00005;
    if (b > 0.02) b = 0.02;

    const double m = graded ? PianoVoice::defaultModalMass(f0) : StringPartialBank::kModalMassAtRef;
    const double mh = graded ? PianoVoice::defaultHammerMass(f0) : 1.0;

    StringPartialBank bank;
    bank.setFundamentalHz(f0);
    bank.setInharmonicity(b);
    bank.setBaseDecaySeconds(PianoVoice::defaultBaseDecaySeconds(f0));
    if (graded)
    {
        bank.setModalMass(m);
        bank.setStrikePosition(PianoVoice::defaultStrikePosition(f0));
    }

    HammerExciter h;
    if (graded)
    {
        h.setMass(mh);
        h.setStiffness(PianoVoice::defaultHammerStiffness(f0));
    }
    h.strike(velocity);

    RegisterContact r;
    const double v0 = 4.0 * velocity; // HammerExciter::kMaxImpactSpeed * velocity01
    r.energyIn = 0.5 * mh * v0 * v0;
    int n = 0;
    for (int i = 0; i < 48000 && h.isInContact(); ++i)
    {
        bank.out(h.out(bank.displacementAtStrike(), 0), 0);
        ++n;
    }
    r.contactMs = n / 48.0;

    // Free response after the hammer has left. The partials are mutually
    // incoherent, so mean(sum^2) ~= 0.5*sum(y_n^2) and the modal kinetic energy
    // is 0.5*m*sum(y_n^2) — a proxy good to ~10 %, which is ample for a check
    // whose failure mode is orders of magnitude (see the test below).
    const int period = (int)(48000.0 / f0) + 1;
    double sumSq = 0.0;
    for (int i = 0; i < period * 4; ++i)
    {
        const double y = bank.out(0.0, 0);
        sumSq += y * y;
    }
    r.energyOut = m * (sumSq / (period * 4));
    return r;
}
}

// THE regression guard for M6, and the one that caught the milestone's real bug.
// Grading the modal mass (## 11.1) makes a top-octave string ~3400x lighter than
// a bass string, and the contact ODE is integrated explicitly against a one-sample
// delayed string displacement. Once the contact gets short enough that the loop is
// no longer resolved, it stops conserving energy and starts MANUFACTURING it —
// measured 201x energy gain at C8 with the pre-M6 felt stiffness, which showed up
// downstream as a note 85x louder than its neighbours.
//
// ## 11.3's stability cap on K exists to prevent exactly this, so the cap is
// asserted by its consequence rather than by its formula: no key, at any velocity,
// may leave the string with more energy than the hammer arrived with.
TEST(PianoVoice_register_contact_never_creates_energy)
{
    double worst = 0.0;
    int worstMidi = 0;
    for (int midi = 21; midi <= 108; ++midi) // A0..C8, the full 88
    {
        const double f0 = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        for (double v : {0.2, 0.5, 0.8, 1.0})
        {
            const RegisterContact r = runRegisterContact(f0, v, true);
            CHECK(r.energyIn > 0.0);
            const double eff = r.energyOut / r.energyIn;
            CHECK(std::isfinite(eff));
            if (eff > worst) { worst = eff; worstMidi = midi; }
        }
    }
    (void)worstMidi;
    // Measured worst case 0.93 (MIDI 24). The threshold is deliberately loose
    // rather than exactly 1.0: the energy figure is a proxy, so a few per cent
    // over unity would be measurement noise, while the failure this guards
    // against was 201x.
    CHECK(worst < 1.5);
}

// ## 11.1/11.2 criterion 3: mass grading changes contact duration in a way M3's
// coupling alone could not, because M3 ran one hammer mass and one string mass at
// every pitch. Both the graded and ungraded contacts are measured here so the
// claim is a comparison, not an assertion about one number.
TEST(PianoVoice_register_scaling_regrades_contact_duration)
{
    const double f[] = {27.5, 65.4, 261.6, 1046.5, 4186.0}; // A0 C2 C4 C6 C8
    double gradedMin = 1e9, gradedMax = 0.0;
    for (double hz : f)
    {
        const double g = runRegisterContact(hz, 1.0, true).contactMs;
        const double u = runRegisterContact(hz, 1.0, false).contactMs;
        CHECK(g > 0.0 && u > 0.0);
        // Grading moves every register's contact — this is the "beyond what M3
        // alone produces" part of the criterion.
        CHECK(std::fabs(g - u) > 0.05);
        gradedMin = std::min(gradedMin, g);
        gradedMax = std::max(gradedMax, g);
    }
    // ...and lands the whole keyboard inside the range real pianos measure
    // (Askenfelt & Jansson: ~1-5 ms, longest in the bass). Measured 2.08-5.38 ms.
    CHECK(gradedMin > 1.0 && gradedMax < 6.0);
}

// ## 11.5: unison count follows the stringing scale, so the register boundaries
// land where a real instrument's do. Asserted on the law directly — the boundary
// notes are the whole point, and inferring them from audio would be indirect.
TEST(PianoVoice_unison_count_follows_the_stringing_scale)
{
    CHECK(PianoVoice::defaultUnisonCount(27.5) == 1);    // A0  — single wound
    CHECK(PianoVoice::defaultUnisonCount(55.0) == 1);    // A1  — still single
    CHECK(PianoVoice::defaultUnisonCount(58.27) == 2);   // A#1 — bichord starts
    CHECK(PianoVoice::defaultUnisonCount(87.31) == 2);   // F2  — still bichord
    CHECK(PianoVoice::defaultUnisonCount(92.50) == 3);   // F#2 — trichord starts
    CHECK(PianoVoice::defaultUnisonCount(4186.0) == 3);  // C8

    // And a voice built at those pitches really is strung that way.
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    for (double hz : {27.5, 65.4, 261.6})
    {
        PianoVoice v;
        v.setFrequency(hz);
        v.noteOn(0.8);
        double peak = 0.0;
        for (int i = 0; i < 4800; ++i) peak = std::max(peak, std::fabs(v.out(0)));
        CHECK(peak > 1e-4); // strung and sounding, whatever U is
    }
}

// Every §11 law is overridable, and an override must SURVIVE a later
// setFrequency() — otherwise a caller who set a value explicitly would silently
// lose it on the next note, which is exactly the bug §3's T60 latch exists to
// prevent. Asserted through behaviour: an overridden voice must not track the law.
TEST(PianoVoice_register_laws_are_overridable_and_latch)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    auto strikePeak = [](PianoVoice &v) {
        v.noteOn(0.8);
        double p = 0.0;
        for (int i = 0; i < 4800; ++i) p = std::max(p, std::fabs(v.out(0)));
        return p;
    };

    // A heavy modal mass makes the string yield less and radiate less. Set it at
    // C2, then move the voice to C6 — where the law would install a mass ~90x
    // lighter — and require the override to still be in force.
    PianoVoice overridden, tracking;
    overridden.setFrequency(65.4);
    overridden.setModalMass(50.0);
    overridden.setFrequency(1046.5); // law would set ~0.106; the latch must win
    tracking.setFrequency(1046.5);

    const double heldPeak = strikePeak(overridden);
    const double lawPeak = strikePeak(tracking);
    CHECK(heldPeak > 0.0 && lawPeak > 0.0);
    CHECK(heldPeak < lawPeak * 0.5); // a 470x heavier string is unmistakably quieter

    // The remaining four latches, same contract.
    PianoVoice v;
    v.setFrequency(261.6);
    v.setUnisonCount(1);
    v.setStrikePosition(0.02);
    v.setHammerMass(20.0);
    v.setHammerStiffness(5.0e11);
    v.setFrequency(4186.0); // every law would move; none may
    CHECK(strikePeak(v) > 0.0);
}

// ## 11.1-11.4 criterion 1: notes stop being transpositions of each other. The
// spectral centroid must rise with pitch FASTER than the pitch itself would carry
// it if every note were the same note transposed — i.e. centroid/f0 is not a
// constant, and the character genuinely differs across the keyboard.
TEST(PianoVoice_spectral_centroid_rises_across_the_keyboard)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    const double f[] = {27.5, 65.4, 130.8, 261.6, 523.3, 1046.5, 2093.0, 4186.0};

    double prev = 0.0;
    bool monotonic = true;
    double firstRatio = 0.0, lastRatio = 0.0;
    for (size_t k = 0; k < sizeof(f) / sizeof(f[0]); ++k)
    {
        PianoVoice v;
        v.setFrequency(f[k]);
        v.noteOn(1.0);
        std::vector<double> y(9600); // 200 ms — the attack, where voicing shows
        for (auto &s : y) s = v.out(0);

        double num = 0.0, den = 0.0;
        for (double probe = 50.0; probe < 10000.0; probe *= 1.06)
        {
            const double w = 2.0 * M_PI * probe / 48000.0, c = 2.0 * std::cos(w);
            double s1 = 0, s2 = 0;
            for (double s : y) { const double s0 = s + c * s1 - s2; s2 = s1; s1 = s0; }
            const double mag = std::sqrt(std::fabs(s1 * s1 + s2 * s2 - c * s1 * s2));
            num += probe * mag;
            den += mag;
        }
        CHECK(den > 0.0);
        const double centroid = num / den;
        if (centroid <= prev) monotonic = false;
        prev = centroid;
        if (k == 0) firstRatio = centroid / f[k];
        lastRatio = centroid / f[k];
    }
    CHECK(monotonic); // brightness rises with pitch, measured 193 Hz (A0) -> 395 Hz (C8)

    // The decisive part: centroid/f0 collapses from ~7.0 at A0 to ~0.09 at C8. A
    // keyboard of pure transpositions would hold this ratio CONSTANT. Two orders
    // of magnitude of variation is the numeric statement of "these are different
    // instruments in different registers", which is what M6 exists to produce.
    CHECK(firstRatio > lastRatio * 20.0);
}

// ───────────────── M7: longitudinal modes & phantom partials ─────────────────
// Plan §M7; math in src/physical/README.md ## 12. REQ-piano-15 amended first.

namespace {
// The longitudinal contribution, ISOLATED exactly: render the same note twice,
// once with the tension coupling and once with it zeroed, and subtract. Anything
// in the difference came from README ## 12 and from nothing else — which is what
// makes "this energy is not a transverse partial" provable rather than arguable.
struct PhantomRun
{
    std::vector<double> total;
    std::vector<double> longitudinalOnly;
};
PhantomRun runPhantom(double f0, double velocity, double seconds)
{
    auto render = [&](double kappa) {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setChannelCount(1);
        PianoVoice v;
        v.setFrequency(f0);
        v.setTensionCoupling(kappa);
        v.noteOn(velocity);
        const int n = (int)(seconds * 48000);
        std::vector<double> y((size_t)n);
        for (int i = 0; i < n; ++i) y[(size_t)i] = v.out(0);
        return y;
    };
    PhantomRun r;
    r.total = render(8.0e-3); // the shipped default (LongitudinalBank ctor)
    const std::vector<double> off = render(0.0);
    r.longitudinalOnly.resize(r.total.size());
    for (size_t i = 0; i < r.total.size(); ++i)
        r.longitudinalOnly[i] = r.total[i] - off[i];
    return r;
}

double bandMag(const std::vector<double> &x, double f, int len)
{
    const double w = 2.0 * M_PI * f / 48000.0, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < len && i < (int)x.size(); ++i)
    { const double s0 = x[(size_t)i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(std::fabs(s1 * s1 + s2 * s2 - c * s1 * s2));
}

// README ## 2's transverse series, replicated so the test can prove a frequency
// is NOT in it rather than assume so.
std::vector<double> transverseSeries(double f0)
{
    double b = 0.00056 * std::pow(100.0 / f0, 1.25);
    if (b < 0.00005) b = 0.00005;
    if (b > 0.02) b = 0.02;
    std::vector<double> f;
    for (int n = 1; n <= StringPartialBank::kMaxPartials; ++n)
    {
        const double fn = n * f0 * std::sqrt(1.0 + b * (double)n * n);
        if (fn >= 0.45 * 48000.0) break;
        f.push_back(fn);
    }
    return f;
}

double gapToSeries(double f, const std::vector<double> &series)
{
    double d = 1e30;
    for (double p : series) d = std::min(d, std::fabs(f - p));
    return d;
}
}

// THE M7 acceptance criterion (plan §M7): a genuine phantom partial — measurable
// energy at a frequency where the transverse series predicts NO partial at all.
// This is what a bank of extra resonators could never be, and it is why the
// milestone is about a nonlinearity rather than about more modes.
TEST(LongitudinalBank_produces_genuine_phantom_partials)
{
    const double f0 = 27.5; // A0 — where the effect lives
    const PhantomRun r = runPhantom(f0, 1.0, 1.0);
    const std::vector<double> series = transverseSeries(f0);
    const int win = (int)(0.3 * 48000);

    // Floor first, per the M1/M3 lesson: a ratio between two silent signals is
    // arithmetic, not evidence.
    double peak = 0.0;
    for (double s : r.longitudinalOnly) peak = std::max(peak, std::fabs(s));
    CHECK(peak > 1e-5);

    // Find where the isolated longitudinal signal is strongest, away from the
    // dense low end of the partial series.
    double bestF = 0.0, bestMag = 0.0;
    for (double f = 400.0; f < 6000.0; f += 2.0)
    {
        const double m = bandMag(r.longitudinalOnly, f, win);
        if (m > bestMag) { bestMag = m; bestF = f; }
    }
    CHECK(bestMag > 0.0);
    // It should land on the first longitudinal resonance, which is set by the
    // string's GEOMETRY (## 12.1) and has no relationship to the note's pitch.
    const double fLong = LongitudinalBank::firstModeHz(f0);
    CHECK(std::fabs(bestF - fLong) < 0.15 * fLong);

    // Now the criterion itself: sweep the neighbourhood of that resonance for a
    // frequency that is genuinely NOT a transverse partial, and require the
    // longitudinal signal to dominate the transverse one there.
    bool foundPhantom = false;
    for (double f = fLong * 0.9; f <= fLong * 1.1 && !foundPhantom; f += 2.0)
    {
        if (gapToSeries(f, series) < 12.0)
            continue; // too close to a real partial to attribute anything
        const double lon = bandMag(r.longitudinalOnly, f, win);
        const double tra = bandMag(r.total, f, win) - lon;
        if (lon > 1e-4 && lon > std::fabs(tra) * 3.0)
            foundPhantom = true;
    }
    CHECK(foundPhantom); // measured: 1280 Hz, longitudinal ~10x the transverse content
}

// The other half of the criterion, and the part that separates a phantom from any
// ordinary added partial: the coupling is through the SQUARE of the transverse
// motion (## 12.2), so it grows quadratically with strike velocity and vanishes at
// near-zero velocity, where a linearly-driven partial would simply be quiet.
TEST(LongitudinalBank_grows_quadratically_and_vanishes_when_barely_struck)
{
    const double f0 = 27.5;
    auto longitudinalRms = [&](double vel) {
        const PhantomRun r = runPhantom(f0, vel, 0.6);
        double e = 0.0;
        for (double s : r.longitudinalOnly) e += s * s;
        return std::sqrt(e / (double)r.longitudinalOnly.size());
    };
    auto fundamentalMag = [&](double vel) {
        const PhantomRun r = runPhantom(f0, vel, 0.6);
        return bandMag(r.total, transverseSeries(f0)[0], (int)(0.3 * 48000));
    };

    const double lLo = longitudinalRms(0.35), lHi = longitudinalRms(0.70);
    const double fLo = fundamentalMag(0.35), fHi = fundamentalMag(0.70);
    CHECK(lLo > 0.0 && fLo > 0.0);

    // Doubling velocity: the phantom must grow markedly faster than the note does.
    const double phantomExp = std::log2(lHi / lLo);      // measured 2.39
    const double linearExp = std::log2(fHi / fLo);       // measured 0.85
    CHECK(phantomExp > 1.5);
    CHECK(phantomExp > linearExp * 2.0);

    // ...and a whisper-struck note has essentially none of it. Relative, not
    // absolute: everything is quiet at velocity 0.02, so the claim is that the
    // phantom is a far SMALLER SHARE of a soft note than of a loud one.
    auto share = [&](double vel) {
        const PhantomRun r = runPhantom(f0, vel, 0.6);
        double e = 0.0, t = 0.0;
        for (size_t i = 0; i < r.total.size(); ++i)
        { e += r.longitudinalOnly[i] * r.longitudinalOnly[i]; t += r.total[i] * r.total[i]; }
        return std::sqrt(e / std::max(1e-30, t));
    };
    CHECK(share(1.0) > share(0.02) * 20.0); // measured 0.062 vs 0.0001
}

// ## 12.1: the longitudinal series is set by the string's GEOMETRY and the bar
// speed of steel — not by its tension — which is exactly why it is inharmonic
// against the note and reads as clang rather than as pitch.
TEST(LongitudinalBank_frequencies_follow_geometry_not_pitch)
{
    // Real bass strings resonate longitudinally around 1.3 kHz; the top of the
    // compass runs far higher. The ratio to f0 must therefore NOT be constant —
    // a constant ratio would make it just another harmonic series.
    const double a0 = LongitudinalBank::firstModeHz(27.5);
    const double c4 = LongitudinalBank::firstModeHz(261.6);
    CHECK_NEAR(a0, 1300.0, 30.0);
    CHECK_NEAR(c4, 4194.0, 30.0);
    const double ratioA0 = a0 / 27.5, ratioC4 = c4 / 261.6;
    CHECK(ratioA0 > ratioC4 * 2.0); // 47x vs 16x — emphatically not a fixed ratio

    // Monotonic in pitch, and every mode kept is below Nyquist.
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    double prev = 0.0;
    for (double f0 : {27.5, 55.0, 110.0, 220.0})
    {
        const double f1 = LongitudinalBank::firstModeHz(f0);
        CHECK(f1 > prev);
        prev = f1;
        LongitudinalBank b;
        b.setFundamentalHz(f0);
        CHECK(b.activeModeCount() > 0);
        CHECK(f1 * b.activeModeCount() < 0.45 * 48000.0);
    }
}

// ## 12.4: bass-weighted, and switched fully OFF in the treble — which is both the
// physics (the effect is a bass phenomenon) and what keeps M7 off the CPU budget
// that REQ-piano-17 gates. Measured: 44 longitudinal resonators across the whole
// 8-voice benchmark chord.
TEST(LongitudinalBank_is_bass_weighted_and_disabled_in_the_treble)
{
    CHECK(LongitudinalBank::registerGainFor(27.5) > 0.99);   // A0 — full strength
    CHECK(LongitudinalBank::registerGainFor(130.8) > 0.99);  // C3 — still full
    CHECK(LongitudinalBank::registerGainFor(261.6) < 0.5);   // C4 — fading
    CHECK(LongitudinalBank::registerGainFor(1046.5) < 0.06); // C6 — gone

    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    LongitudinalBank bass, treble;
    bass.setFundamentalHz(27.5);
    treble.setFundamentalHz(2093.0);
    CHECK(bass.isActive() && bass.activeModeCount() > 0);
    // Not merely quiet — no resonators run at all.
    CHECK(!treble.isActive());
    CHECK(treble.activeModeCount() == 0);
    // ...and a disabled bank is silent rather than passing its input through.
    for (int i = 0; i < 64; ++i)
        CHECK(treble.out(0.5, 0) == 0.0);
}

// The drive is a SQUARE, so it is non-negative by construction and carries a large
// DC term — physically the static tension rise (M8's subject), and electrically an
// output offset, since a two-pole resonator's DC gain is not zero. ## 12.2 removes
// it with HP(x) = x - LPF(x); this is the assertion that it stays removed.
TEST(LongitudinalBank_squared_drive_leaves_no_dc_offset)
{
    for (double f0 : {27.5, 65.4, 130.8})
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setChannelCount(1);
        PianoVoice v;
        v.setFrequency(f0);
        v.noteOn(1.0);
        double mean = 0.0, peak = 0.0;
        const int n = 48000;
        for (int i = 0; i < n; ++i)
        {
            const double s = v.out(0);
            mean += s;
            peak = std::max(peak, std::fabs(s));
        }
        mean /= n;
        CHECK(peak > 1e-3);
        CHECK(std::fabs(mean) < 0.01 * peak); // measured ~0.19 %
    }
}

// A squared signal sits inside the string->bridge->string loop (§8), so M7 can
// destabilise a path M1/M3/M5 each had to re-tune. Worst case, with a wide margin
// on the coupling constant.
TEST(LongitudinalBank_does_not_destabilise_the_bridge_loop)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    PianoBridge bridge;
    std::vector<PianoVoice> voices(8);
    const double f[8] = {27.5, 29.1, 30.9, 32.7, 34.6, 36.7, 38.9, 41.2};
    for (int i = 0; i < 8; ++i)
    {
        voices[i].setBridge(&bridge);
        voices[i].setFrequency(f[i]);
        voices[i].setTensionCoupling(8.0e-2); // 10x the shipped default
        voices[i].setDamperHeld(true);
        voices[i].noteOn(1.0);
    }
    double peak = 0.0;
    for (int i = 0; i < 48000 * 4; ++i)
    {
        double mix = 0.0;
        for (auto &v : voices) mix += v.out(0);
        bridge.tick();
        mix += bridge.radiatedOutput();
        CHECK(std::isfinite(mix));
        peak = std::max(peak, std::fabs(mix));
    }
    CHECK(peak < 100.0); // measured 6.13 at 10x; 3.14 at the shipped value
}

// ───────────────── duplex / aliquot shimmer (README ## 8.1, M9.2) ─────────────────

// REQ-piano-19 (plan §M9.2): the duplex adds a treble shimmer that (a) carries energy
// at the aliquot frequency n*f0, (b) SUSTAINS past the bridge-damped speaking partial
// there, and (c) is present in the treble but absent in the bass. Isolated by
// differencing a duplex-on and duplex-off render (README ## 12.2's lesson).
TEST(PianoVoice_duplex_adds_treble_shimmer)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    auto render = [](double f0, double drive) {
        PianoVoice v;
        v.setFrequency(f0);
        v.setDamperHeld(true);
        v.setDuplexDriveGain(drive);
        v.noteOn(0.9);
        std::vector<double> out(40000);
        for (int i = 0; i < 40000; ++i) out[i] = v.out(0.0, 0);
        return out;
    };
    auto window = [](const std::vector<double> &x, int a, int n) {
        return std::vector<double>(x.begin() + a, x.begin() + a + n);
    };

    // --- Treble note C6: duplex on vs off, difference is the shimmer ---
    double f0 = 1046.5;
    std::vector<double> on = render(f0, 90.0), off = render(f0, 0.0);
    std::vector<double> diff(on.size());
    for (size_t i = 0; i < on.size(); ++i) diff[i] = on[i] - off[i];

    double notePeak = 0.0, shimmerPeak = 0.0;
    for (double s : off) notePeak = std::max(notePeak, std::fabs(s));
    for (double s : diff) shimmerPeak = std::max(shimmerPeak, std::fabs(s));
    CHECK(notePeak > 1e-3);                       // magnitude floor: the note actually sounds
    CHECK(shimmerPeak > notePeak * 1e-3);         // the duplex contributes real energy
    CHECK(shimmerPeak < notePeak * 0.2);          // ...but it is a shimmer, not a second voice

    // (a) the shimmer sits at the aliquot 4*f0, not at an off-aliquot frequency.
    std::vector<double> dwin = window(diff, 2400, 8192);
    double atAliquot = goertzelMag(dwin, 4.0 * f0, 48000.0);
    double offAliquot = goertzelMag(dwin, 3.3 * f0, 48000.0);
    CHECK(atAliquot > offAliquot * 3.0);

    // (b) it sustains past the bridge-damped speaking partial: the aliquot bin decays
    // more slowly WITH the duplex than the string's own partial does without it.
    auto lateOverEarly = [&](const std::vector<double> &sig) {
        double e = goertzelMag(window(sig, 2400, 8192), 4.0 * f0, 48000.0);
        double l = goertzelMag(window(sig, 30000, 8192), 4.0 * f0, 48000.0);
        return l / std::max(1e-12, e);
    };
    CHECK(lateOverEarly(diff) > lateOverEarly(off) * 2.0); // slower decay = shimmer tail

    // (c) a bass note has no duplex at all (gated off below the crossover).
    std::vector<double> bassOn = render(110.0, 90.0), bassOff = render(110.0, 0.0);
    double bassDiff = 0.0;
    for (size_t i = 0; i < bassOn.size(); ++i) bassDiff = std::max(bassDiff, std::fabs(bassOn[i] - bassOff[i]));
    CHECK(bassDiff < shimmerPeak * 0.01); // bass shimmer is orders of magnitude smaller (here: zero)
}

// ───────────────── tension modulation / pitch glide (README ## 12.5, M8) ─────────────────

// Helper: the peak fractional pitch shift delta a struck voice reaches within its
// first `ms` milliseconds, and the settled shift at ~1 s. Uses the exact
// pitchModulation() introspection — f0(t) = f0*(1+delta), so delta IS the fractional
// f0 shift, measured exactly rather than estimated from a noisy bass onset. The
// integration suite measures the same glide through the rendered 16-bit audio path.
namespace {
void glideDeltas(double f0, double vel, double &peak50ms, double &settled1s)
{
    PianoVoice v;
    v.setFrequency(f0);
    v.setDamperHeld(true); // no damper so the tail is clean
    v.noteOn(vel);
    peak50ms = 0.0; settled1s = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        v.out(0.0, 0);
        double d = (double)v.pitchModulation();
        if (i < 2400 && d > peak50ms) peak50ms = d; // first 50 ms
        if (i >= 47900) settled1s = d;
    }
}
double toCents(double frac) { return 1200.0 * std::log2(1.0 + frac); }
}

// Plan §M8 acceptance, measured exactly: a hard bass blow starts >= 2 cents sharp in
// the first 50 ms relative to its settled pitch; a soft blow < 0.5 cents. The wide
// hard/soft gap comes for free from the amplitude^2 tension law (a struck string's
// tension rises as the square of its motion, ## 12.5), the same v^2 signature §12.2
// gives the phantom partials.
TEST(PianoVoice_tension_modulation_attack_pitch_glide)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    double hardPeak, hardSettled, softPeak, softSettled;
    glideDeltas(65.41, 1.0, hardPeak, hardSettled); // C2 fortissimo
    glideDeltas(65.41, 0.2, softPeak, softSettled); // C2 pianissimo

    double hardGlide = toCents(hardPeak) - toCents(hardSettled);
    double softGlide = toCents(softPeak) - toCents(softSettled);

    CHECK(hardGlide >= 2.0);                 // plan §M8: hard blow >= 2 cents sharp (measured ~2.5)
    CHECK(toCents(softPeak) < 0.5);          // plan §M8: soft blow < 0.5 cents (measured ~0.12)
    CHECK(hardPeak > 0.0);                   // it really starts sharp, not flat
    CHECK(hardSettled < hardPeak * 0.5);     // and it glides DOWN toward nominal as it decays
}

// The glide is driven by the SQUARE of the transverse amplitude (## 12.5), so it must
// grow far faster with strike velocity than a linear partial — the same test §12.2's
// phantom partials pass, applied to the tension-rise (DC) half of the same quantity.
TEST(PianoVoice_pitch_glide_grows_with_velocity_squared)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    double loudP, loudS, softP, softS;
    glideDeltas(65.41, 0.9, loudP, loudS);
    glideDeltas(65.41, 0.3, softP, softS); // 3x lower velocity

    // delta ~ amplitude^2, and amplitude ~ velocity, so a 3x velocity gap should give
    // roughly a 9x delta gap — certainly far more than the 3x a linear effect would.
    CHECK(loudP > softP * 5.0);
}

// The glide is a bass phenomenon by construction (## 12.5): kappa_t falls as the cube
// of pitch, so a treble note barely glides at all even struck at full force.
TEST(PianoVoice_pitch_glide_is_negligible_in_the_treble)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    double bassP, bassS, trebP, trebS;
    glideDeltas(65.41, 1.0, bassP, bassS);   // C2
    glideDeltas(2093.0, 1.0, trebP, trebS);  // C7, same fortissimo

    CHECK(toCents(trebP) < 0.1);             // treble glide is inaudible (measured ~0.001)
    CHECK(bassP > trebP * 50.0);             // the bass glides orders of magnitude more
}

// ───────────────── silent-voice skipping (README ## 13) ─────────────────

// The performance gate that lets an engine freeze idle voices. Its two conditions
// are not interchangeable, and the damper one is what keeps it physically safe.
TEST(PianoVoice_isSilent_requires_both_damped_and_inaudible)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);

    auto run = [](PianoVoice &v, double seconds) {
        const int n = (int)(seconds * 48000);
        double peak = 0.0;
        for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(v.out(0)));
        return peak;
    };

    // A struck voice is never silent.
    PianoVoice v;
    v.setFrequency(261.6);
    v.noteOn(0.9);
    CHECK(!v.isSilent());
    run(v, 0.05);
    CHECK(!v.isSilent());

    // Released: the damper engages and the voice goes quiet, then reports silent.
    v.noteOff();
    run(v, 4.0);
    CHECK(v.isSilent());
    // ...and it really is inaudible by then — the gate must not fire early.
    CHECK(run(v, 0.2) < 1e-4);

    // Struck again, it must come back immediately (noteOn resets the follower).
    v.noteOn(0.9);
    CHECK(!v.isSilent());
}

// THE safety property. An UNdamped string stays coupled to the bridge and can be
// re-excited by other notes (REQ-piano-6, README ## 8). If isSilent() ignored the
// damper, a sustain-pedalled note that had decayed below the threshold would be
// frozen and would never answer — silently breaking sympathetic resonance.
TEST(PianoVoice_isSilent_never_fires_on_an_undamped_string)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(261.6);
    v.setDamperHeld(true); // sustain pedal down
    v.noteOn(0.9);
    v.noteOff();           // key released, but the pedal holds the damper OFF

    // Run well past the point where the note is inaudible.
    double peak = 0.0;
    for (int i = 0; i < 48000 * 12; ++i) peak = std::max(peak, std::fabs(v.out(0)));
    (void)peak;
    // Quiet, but NOT skippable: the damper is lifted, so the bridge can still
    // drive it and it must keep running.
    CHECK(!v.isSilent());
}

// ───────────────── compute: parallel executor ─────────────────
// Design + measurements: docs/parallel-architecture.md. REQ-compute-1..6.

namespace {
// A deterministic, order-sensitive workload: each shard writes only its own slot,
// so a correct executor must run every shard exactly once.
struct ShardProbe
{
    static constexpr int kSlots = 64;
    int calls[kSlots] = {};
    long long sums[kSlots] = {};
};
bool serialFloorCheck(const ParallelExecutor &ex, int frames)
{
    return ex.shouldParallelise(frames);
}
void shardProbeTask(void *ctx, int shard)
{
    auto *p = static_cast<ShardProbe *>(ctx);
    p->calls[shard] += 1;
    long long acc = 0;
    for (int i = 0; i < 5000; ++i) // enough work that racing shards would overlap
        acc += (long long)shard * i;
    p->sums[shard] = acc;
}
}

// Every shard runs exactly once, on both executors. This is the base contract —
// a work-stealing claim cursor that double-claimed or dropped a shard would show
// up here and nowhere else in the suite.
TEST(ParallelExecutor_runs_every_shard_exactly_once)
{
    SerialExecutor serial;
    ThreadPoolExecutor pool(4);
    CHECK(serial.concurrency() == 1);
    CHECK(pool.concurrency() == pool.workerCount() + 1); // the caller participates

    for (ParallelExecutor *ex : {(ParallelExecutor *)&serial, (ParallelExecutor *)&pool})
    {
        for (int shards : {1, 2, 3, 8, 17, 64})
        {
            ShardProbe p;
            ex->run(&shardProbeTask, &p, shards);
            for (int i = 0; i < shards; ++i)
            {
                CHECK(p.calls[i] == 1);
                long long expect = 0;
                for (int k = 0; k < 5000; ++k) expect += (long long)i * k;
                CHECK(p.sums[i] == expect);
            }
            for (int i = shards; i < ShardProbe::kSlots; ++i)
                CHECK(p.calls[i] == 0); // never ran a shard it wasn't asked to
        }
    }
}

// Degenerate configurations must be harmless, not special-cased by callers: a
// pool with no workers is just a serial executor, and 0 shards is a no-op.
TEST(ParallelExecutor_degenerate_configurations)
{
    ThreadPoolExecutor none(0); // explicit 0 = no worker threads (single-core target)
    CHECK(none.workerCount() == 0);
    CHECK(none.concurrency() == 1);
    ShardProbe p;
    none.run(&shardProbeTask, &p, 4);
    for (int i = 0; i < 4; ++i) CHECK(p.calls[i] == 1);

    ShardProbe q;
    ThreadPoolExecutor pool(2);
    pool.run(&shardProbeTask, &q, 0); // no shards: must not hang or touch anything
    for (int i = 0; i < ShardProbe::kSlots; ++i) CHECK(q.calls[i] == 0);

    // Oversized requests are clamped rather than trusted.
    ThreadPoolExecutor huge(10000);
    CHECK(huge.workerCount() <= ThreadPoolExecutor::kMaxWorkers);

    // Default construction (kAutoWorkers) sizes to the hardware, leaving one
    // thread for the caller. Scoped so the destructor's join path runs here.
    {
        ThreadPoolExecutor autoPool;
        CHECK(autoPool.workerCount() >= 0);
        CHECK(autoPool.concurrency() == autoPool.workerCount() + 1);
        ShardProbe a;
        autoPool.run(&shardProbeTask, &a, 6);
        for (int i = 0; i < 6; ++i) CHECK(a.calls[i] == 1);
    }

    // The block-size floor: fanning out a tiny block costs more than it saves.
    CHECK(!serialFloorCheck(pool, 16));
    CHECK(!serialFloorCheck(pool, ParallelExecutor::kMinFramesForParallel - 1));
    CHECK(serialFloorCheck(pool, ParallelExecutor::kMinFramesForParallel));
    SerialExecutor s;
    CHECK(!serialFloorCheck(s, 4096)); // concurrency 1 never parallelises
}

// THE requirement (REQ-compute-3): rendering through a thread pool must produce
// BIT-IDENTICAL audio to rendering serially. Not "close" — identical. Anything
// less would silently invalidate every numeric acceptance criterion the piano
// milestones established, and would make failures depend on thread timing.
//
// This is what forces shard-order summation in finishBlock(): floating-point
// addition is not associative, so summing accumulators in completion order would
// fail this test intermittently, which is the worst possible way to learn it.
TEST(SynthEngine_parallel_render_is_bit_identical_to_serial)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    AudioConfig::instance().setOutputBitDepth(16);

    auto render = [](ParallelExecutor *ex) {
        ComputeConfig::instance().setExecutor(ex);
        SynthEngine e;
        std::vector<uint8_t> out, all;
        // A chord plus a change of pitch, so voices are unevenly loaded and the
        // shard split is not trivially balanced.
        for (int n : {48, 52, 55, 59, 62, 65})
            e.pushNoteOn(n, 0.8);
        for (int b = 0; b < 12; ++b)
        {
            if (b == 6) e.pushNoteOff(52);
            e.renderBlockBytesParallel(out, 128);
            all.insert(all.end(), out.begin(), out.end());
        }
        ComputeConfig::instance().setExecutor(nullptr);
        return all;
    };

    SerialExecutor serial;
    ThreadPoolExecutor pool(4);
    const std::vector<uint8_t> a = render(&serial);
    const std::vector<uint8_t> b = render(&pool);

    CHECK(a.size() == b.size());
    CHECK(!a.empty());
    bool nonSilent = false;
    for (uint8_t v : a) if (v != 0) { nonSilent = true; break; }
    CHECK(nonSilent); // a bit-identity test on two silent buffers proves nothing
    CHECK(a == b);
}

// The N-way split must agree with the two-way API it generalises, for every shard
// count — otherwise the shard boundary arithmetic is wrong for uneven divisions
// (kVoiceCount = 8 over 3 shards is 3/3/2, not 2/2/2 with two voices dropped).
TEST(SynthEngine_shard_count_does_not_change_the_output)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    AudioConfig::instance().setOutputBitDepth(16);
    SerialExecutor serial;
    ComputeConfig::instance().setExecutor(&serial);

    auto renderWithShards = [](int shards) {
        SynthEngine e;
        for (int n : {48, 55, 62, 67})
            e.pushNoteOn(n, 0.75);
        std::vector<uint8_t> out, all;
        for (int b = 0; b < 8; ++b)
        {
            e.beginBlock(128);
            for (int s = 0; s < shards; ++s)
                e.renderVoiceShard(s, shards, 128);
            e.setVoiceShardCount(shards);
            e.finishBlock(128);
            e.packBytes(out, 128);
            all.insert(all.end(), out.begin(), out.end());
        }
        return all;
    };

    const std::vector<uint8_t> ref = renderWithShards(1);
    CHECK(!ref.empty());
    for (int shards : {2, 3, 4, 5, 8})
        CHECK(renderWithShards(shards) == ref);

    ComputeConfig::instance().setExecutor(nullptr);
}

// ComputeConfig is the single authority (REQ-compute-2/5) and defaults to serial,
// so a platform with no threads works untouched.
TEST(ComputeConfig_defaults_to_serial_and_restores)
{
    CHECK(ComputeConfig::instance().executor().concurrency() == 1);
    ThreadPoolExecutor pool(2);
    ComputeConfig::instance().setExecutor(&pool);
    CHECK(ComputeConfig::instance().executor().concurrency() == 3);
    ComputeConfig::instance().setExecutor(nullptr); // nullptr restores the default
    CHECK(ComputeConfig::instance().executor().concurrency() == 1);
}

int main()
{
    return mini::runAll();
}
