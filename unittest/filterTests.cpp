/*
 *  Arstro DSP — the D1 primitives: Biquad, ParametricEQ, StateVariableFilter, Noise,
 *  DecayEnvelope (REQ-eq-*, REQ-svf-*, REQ-noise-1, REQ-decay-1).
 *
 *  Filters are judged by what they DO to a steady sine — the RMS gain measured on rendered
 *  samples, compared with the closed-form response — not by reading their coefficients back.
 *  No main(): registration is shared with synthTests.cpp through MiniTest.
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <cmath>
#include <vector>

using namespace arstro;

namespace
{
    void configure()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }

    // The steady-state gain, in dB, of `p` on a unit sine at `hz` (channel 0), measured after a
    // settling second so the transient and any parameter ramp are long gone.
    template <class P> double measuredGainDb(P &p, double hz)
    {
        const double fs = 48000.0;
        double sIn = 0, sOut = 0;
        for (int n = 0; n < 72000; ++n)
        {
            const double x = std::sin(2 * M_PI * hz * n / fs);
            const double y = p.out(x, 0);
            if (n >= 48000) { sIn += x * x; sOut += y * y; }
        }
        return 10.0 * std::log10(sOut / sIn);
    }
}

TEST(Biquad_lowpass_is_3dB_down_at_cutoff_and_falls_12dB_per_octave)
{
    configure();
    Biquad lp(Biquad::LowPass, 1000.0, 0.70710678);
    CHECK_NEAR(Biquad::magnitudeDb(lp.coeffs(), 1000.0, 48000.0), -3.0103, 0.01);
    CHECK_NEAR(measuredGainDb(lp, 1000.0), -3.0103, 0.05);
    Biquad a(Biquad::LowPass, 1000.0, 0.70710678), b(Biquad::LowPass, 1000.0, 0.70710678);
    const double g8k = measuredGainDb(a, 8000.0), g16k = measuredGainDb(b, 16000.0);
    // a 2-pole roll-off: ~12 dB per octave well above the cutoff (bilinear warping steepens it near Nyquist)
    CHECK(g8k - g16k > 11.0);
    CHECK(g8k < -34.0);
}

TEST(Biquad_highpass_bandpass_notch_shapes)
{
    configure();
    Biquad hp(Biquad::HighPass, 1000.0, 0.70710678), hp2(Biquad::HighPass, 1000.0, 0.70710678);
    CHECK(measuredGainDb(hp, 100.0) < -38.0);
    CHECK_NEAR(measuredGainDb(hp2, 10000.0), 0.0, 0.3);
    Biquad bp(Biquad::BandPass, 1000.0, 2.0), bp2(Biquad::BandPass, 1000.0, 2.0);
    CHECK_NEAR(measuredGainDb(bp, 1000.0), 0.0, 0.05); // constant 0 dB peak gain
    CHECK(measuredGainDb(bp2, 100.0) < -20.0);
    Biquad notch(Biquad::Notch, 1000.0, 2.0);
    CHECK(measuredGainDb(notch, 1000.0) < -40.0);
}

TEST(Biquad_peak_and_shelves_hit_their_gain)
{
    configure();
    Biquad pk(Biquad::Peak, 1000.0, 1.0, 6.0), pkFar(Biquad::Peak, 1000.0, 1.0, 6.0);
    CHECK_NEAR(measuredGainDb(pk, 1000.0), 6.0, 0.05);
    CHECK_NEAR(measuredGainDb(pkFar, 20000.0), 0.0, 0.3);
    Biquad ls(Biquad::LowShelf, 200.0, 0.70710678, -9.0), ls2(Biquad::LowShelf, 200.0, 0.70710678, -9.0);
    CHECK_NEAR(measuredGainDb(ls, 20.0), -9.0, 0.2);
    CHECK_NEAR(measuredGainDb(ls2, 10000.0), 0.0, 0.1);
    Biquad hs(Biquad::HighShelf, 4000.0, 0.70710678, 4.5), hs2(Biquad::HighShelf, 4000.0, 0.70710678, 4.5);
    CHECK_NEAR(measuredGainDb(hs, 20000.0), 4.5, 0.3);
    CHECK_NEAR(measuredGainDb(hs2, 100.0), 0.0, 0.05);
    // the closed form agrees with the measurement
    CHECK_NEAR(Biquad::magnitudeDb(pk.coeffs(), 1000.0, 48000.0), 6.0, 1e-6);
}

TEST(Biquad_setters_type_reset_and_clamps)
{
    configure();
    Biquad b;
    b.setType(Biquad::LowPass);
    CHECK(b.type() == Biquad::LowPass);
    b.setFrequency(1.0e6);           // clamped to 0.49·fs, still a valid filter
    b.setQ(0.0);                     // clamped to 0.1
    b.setGainDb(3.0);
    CHECK(std::isfinite(b.magnitudeDbAt(1000.0)));
    for (int i = 0; i < 400; ++i) CHECK(std::isfinite(b.out(i % 2 ? 1.0 : -1.0, 0)));
    b.reset();
    b.setFrequency(1000.0);
    for (int i = 0; i < 400; ++i) b.out(0.0, 0);
    CHECK(b.out(0.0, 0) == 0.0);     // reset memory + silence in = silence out
    CHECK(b.out(0.5, 7) == 0.5);     // an out-of-range channel passes through
    AudioConfig::instance().setChannelCount(3);
    CHECK(std::isfinite(b.out(0.5, 2)));
    configure();
}

TEST(Biquad_frequency_change_ramps_without_a_click)
{
    configure();
    // The largest sample-to-sample step when the cutoff leaps 200 Hz → 8 kHz under a 150 Hz tone,
    // with the property smoothing on (the default) and off. Off is the click this guards against.
    auto worstStep = [](bool smooth) {
        Biquad lp(Biquad::LowPass, 200.0, 0.70710678);
        lp.setSmoothEnable(smooth);
        double prev = 0, worst = 0;
        for (int n = 0; n < 9600; ++n)
        {
            if (n == 4800) lp.setFrequency(8000.0);
            const double y = lp.out(0.5 * std::sin(2 * M_PI * 150.0 * n / 48000.0), 0);
            if (n > 100) worst = std::max(worst, std::fabs(y - prev));
            prev = y;
        }
        return worst;
    };
    const double ramped = worstStep(true), snapped = worstStep(false);
    CHECK(snapped > 0.3);            // the un-smoothed jump is a click (measured 0.55)
    CHECK(ramped < 0.05);            // the ramp spreads it over a block (measured 0.034)
    CHECK(ramped * 10.0 < snapped);
}

TEST(ParametricEQ_fresh_is_transparent)
{
    configure();
    ParametricEQ eq;
    double worst = 0;
    for (int n = 0; n < 4800; ++n)
    {
        const double x = 0.7 * std::sin(2 * M_PI * 437.0 * n / 48000.0);
        worst = std::max(worst, std::fabs(eq.out(x, 0) - x));
    }
    CHECK(worst < 1e-9);
    CHECK_NEAR(eq.magnitudeDbAt(1000.0), 0.0, 1e-9);
    CHECK(!eq.bandEnabled(ParametricEQ::LowCut));
    CHECK(eq.bandEnabled(ParametricEQ::Peak2));
}

TEST(ParametricEQ_bands_add_and_switch)
{
    configure();
    ParametricEQ eq;
    eq.band(ParametricEQ::Peak2).setGainDb(6.0);   // 1 kHz
    eq.band(ParametricEQ::Peak3).setGainDb(-6.0);  // 4 kHz
    eq.setBandEnabled(ParametricEQ::HighCut, true);
    eq.band(ParametricEQ::HighCut).setFrequency(12000.0);
    CHECK_NEAR(measuredGainDb(eq, 1000.0), eq.magnitudeDbAt(1000.0), 0.1);
    CHECK(eq.magnitudeDbAt(1000.0) > 5.0);
    eq.reset();
    CHECK_NEAR(measuredGainDb(eq, 4000.0), eq.magnitudeDbAt(4000.0), 0.1);
    eq.setBandEnabled(ParametricEQ::Peak2, false);
    CHECK(!eq.bandEnabled(ParametricEQ::Peak2));
    eq.setBandEnabled(99, true); // ignored
    CHECK(eq.magnitudeDbAt(1000.0) < 1.0);
}

TEST(SVF_lowpass_highpass_bandpass_notch)
{
    configure();
    StateVariableFilter lp(StateVariableFilter::LowPass, 1000.0, 0.0), lp2(StateVariableFilter::LowPass, 1000.0, 0.0);
    CHECK_NEAR(measuredGainDb(lp, 100.0), 0.0, 0.1);
    CHECK(measuredGainDb(lp2, 8000.0) < -34.0);
    StateVariableFilter cut(StateVariableFilter::LowPass, 1000.0, 0.0);
    CHECK_NEAR(measuredGainDb(cut, 1000.0), -3.01, 0.1); // Q = 0.707 at resonance 0: Butterworth
    StateVariableFilter hp(StateVariableFilter::HighPass, 1000.0, 0.0), hp2(StateVariableFilter::HighPass, 1000.0, 0.0);
    CHECK(measuredGainDb(hp, 125.0) < -34.0);
    CHECK_NEAR(measuredGainDb(hp2, 12000.0), 0.0, 0.3);
    StateVariableFilter bp(StateVariableFilter::BandPass, 1000.0, 0.5), bp2(StateVariableFilter::BandPass, 1000.0, 0.5);
    // the band output peaks at Q·(1/Q)… i.e. at k·v1 = 0 dB → v1 alone is Q times that: 20·log10(Q)
    CHECK_NEAR(measuredGainDb(bp, 1000.0), 20.0 * std::log10(StateVariableFilter::qFor(0.5)), 0.1);
    CHECK(measuredGainDb(bp2, 100.0) < measuredGainDb(bp, 1000.0) - 20.0);
    StateVariableFilter n(StateVariableFilter::Notch, 1000.0, 0.0);
    CHECK(measuredGainDb(n, 1000.0) < -40.0);
}

TEST(SVF_resonance_peaks_and_stays_stable_at_full)
{
    configure();
    CHECK_NEAR(StateVariableFilter::qFor(0.0), 0.70710678, 1e-6);
    CHECK_NEAR(StateVariableFilter::qFor(1.0), 0.70710678 * std::pow(2.0, 5.5), 1e-6);
    StateVariableFilter r(StateVariableFilter::LowPass, 1000.0, 1.0);
    CHECK(measuredGainDb(r, 1000.0) > 25.0); // a resonant peak of ~20·log10(32)
    // a full-scale impulse still decays to nothing — resonant, not unstable
    StateVariableFilter imp(StateVariableFilter::LowPass, 1000.0, 1.0);
    double tail = 0;
    imp.out(1.0, 0);
    for (int i = 0; i < 96000; ++i) { const double y = imp.out(0.0, 0); if (i > 90000) tail = std::max(tail, std::fabs(y)); }
    CHECK(tail < 1e-6);
    r.setResonance(7.0);   // clamped
    r.setMode(StateVariableFilter::HighPass);
    CHECK(r.mode() == StateVariableFilter::HighPass);
}

TEST(SVF_tick_sweeps_every_sample)
{
    configure();
    StateVariableFilter f(StateVariableFilter::LowPass, 20000.0, 0.0);
    // tick() ignores the stored 20 kHz cutoff: a 200 Hz tick cutoff kills an 8 kHz tone
    double s = 0;
    for (int n = 0; n < 48000; ++n)
    {
        const double y = f.tick(std::sin(2 * M_PI * 8000.0 * n / 48000.0), 0, 200.0);
        if (n > 24000) s += y * y;
    }
    CHECK(10.0 * std::log10(s / 12000.0) < -60.0);
    CHECK(f.tick(0.3, 9, 100.0) == 0.3);  // out-of-range channel passes through
    CHECK(f.out(0.3, 9) == 0.3);
    f.setCutoff(500.0);
    f.reset();
    CHECK(f.tick(0.0, 0, 500.0) == 0.0);
}

TEST(Noise_is_deterministic_uniform_and_bounded)
{
    Noise a(1234), b(1234), c(99);
    bool same = true, differ = false;
    double sum = 0, sum2 = 0, lo = 1, hi = -1;
    const int N = 200000;
    for (int i = 0; i < N; ++i)
    {
        const double x = a.next(), y = b.next(), z = c.next();
        same = same && x == y;
        differ = differ || x != z;
        sum += x; sum2 += x * x;
        lo = std::min(lo, x); hi = std::max(hi, x);
    }
    CHECK(same);
    CHECK(differ);
    CHECK(lo >= -1.0 && hi < 1.0);
    CHECK_NEAR(sum / N, 0.0, 0.01);
    CHECK_NEAR(std::sqrt(sum2 / N), 1.0 / std::sqrt(3.0), 0.005); // uniform on [-1,1)
    Noise zero(0);                 // a zero seed is replaced, not stuck at 0
    CHECK(zero.next() != zero.next());
}

TEST(DecayEnvelope_falls_60dB_in_its_decay_time)
{
    configure();
    DecayEnvelope e;
    e.setDecayMs(250.0);
    CHECK(e.decayMs() == 250.0);
    CHECK(e.isFinished());
    CHECK(e.next() == 0.0);
    e.trigger(1.0);
    CHECK(!e.isFinished());
    double at = 0;
    for (int n = 0; n <= 12000; ++n) { const double v = e.next(); if (n == 12000) at = v; }
    CHECK_NEAR(20.0 * std::log10(at), -60.0, 0.05); // 250 ms = 12000 samples
    for (int n = 0; n < 48000 && !e.isFinished(); ++n) e.next();
    CHECK(e.isFinished());         // gone below −80 dB of its peak
    CHECK(e.level() == 0.0);
}

TEST(DecayEnvelope_attack_retrigger_choke_and_guards)
{
    configure();
    DecayEnvelope e;
    e.setAttackMs(1.0);            // 48 samples, linear
    e.setDecayMs(100.0);
    e.trigger(0.8);
    CHECK(e.next() == 0.0);
    for (int i = 0; i < 47; ++i) e.next();
    CHECK_NEAR(e.level(), 0.8, 1e-9);
    e.trigger(0.5);                // retrigger BELOW the current level: straight to decay at 0.5
    CHECK_NEAR(e.next(), 0.5, 1e-9);
    e.choke(5.0);                  // 5 ms to −60 dB
    for (int i = 0; i < 240; ++i) e.next();
    CHECK(e.level() < 0.5 * 0.0011);
    e.setAttackMs(-3.0);           // clamps to 0 → no attack stage
    e.setDecayMs(0.0);             // clamps to 1 ms
    e.trigger(0.0);                // a zero hit is no hit
    CHECK(e.isFinished());
    e.choke(1.0);                  // choking silence is a no-op
    CHECK(e.isFinished());
}
