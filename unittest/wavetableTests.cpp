// A wavetable: frames read at any pitch with nothing above Nyquist, moved through by a position (REQ-wavetable-1…3) —
// src/generator/README.md ## Wavetable (W1)–(W7).
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace arstro;

namespace
{
    constexpr double kTau = 6.283185307179586;
    constexpr int kN = 65536; // a power of two: a tone of m cycles in it lands on bin m exactly

    void configure48()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }

    /** |X[k]|², k < N/2, of `x` (N points). */
    std::vector<double> power(const std::vector<double> &x)
    {
        const int N = (int)x.size();
        FFT f(N);
        std::vector<std::complex<double>> X(x.begin(), x.end());
        f.forward(X.data());
        std::vector<double> p((size_t)N / 2);
        for (int k = 0; k < N / 2; ++k) p[(size_t)k] = std::norm(X[(size_t)k]);
        return p;
    }

    /** One table oscillator (its envelope a gate, as BasicSynth's), m cycles in kN at 48 kHz: kN samples after a warm-up. */
    std::vector<double> tone(const Wavetable &t, double position, int m)
    {
        configure48();
        Oscillator o;
        o.setAttackMs(0.0);
        o.setDecayMs(0.0);
        o.setSustain(1.0);
        o.setReleaseMs(0.0);
        o.setWaveform(Oscillator::Table);
        o.setTable(&t);
        o.setVoiceCount(1);
        o.setFrequency(48000.0 * m / kN);
        o.setPosition(position, 0);
        o.noteOn(1.0);
        std::vector<Sample> buf(1024);
        for (int b = 0; b < 4; ++b) { std::fill(buf.begin(), buf.end(), 0.0); o.addBlock(buf.data(), 1024, 0, 1.0); } // warm-up
        std::vector<double> y;
        while ((int)y.size() < kN)
        {
            std::fill(buf.begin(), buf.end(), 0.0);
            o.addBlock(buf.data(), 1024, 0, 1.0);
            y.insert(y.end(), buf.begin(), buf.end());
        }
        y.resize(kN);
        return y;
    }

    /** What lies off the tone's harmonics (bins not a multiple of m, DC aside), against what lies on them — dB. */
    double offHarmonics(const std::vector<double> &y, int m)
    {
        const auto p = power(y);
        double on = 0, off = 0;
        for (int k = 1; k < (int)p.size(); ++k) (k % m == 0 ? on : off) += p[(size_t)k];
        return 10 * std::log10(off / on);
    }
    /** The amplitude of harmonic h of a tone of m cycles, from its spectrum. */
    double harmonic(const std::vector<double> &p, int m, int h) { return h * m < (int)p.size() ? std::sqrt(p[(size_t)(h * m)]) : 0.0; }
}

TEST(Wavetable_levels_keep_their_harmonics)
{
    // (W1): Harmonics' last frame is Σ sin(2πhx)/h for h = 1…32 — each level holds exactly harmonics 1 … H_l of it
    // (at 1/h, to 1e-6) and nothing above; level 0 peaks at 1
    const Wavetable &t = Wavetable::builtIn(2);
    CHECK(t.frames() == 32);
    double worstRatio = 0, worstAbove = 0;
    for (int l = 0; l < Wavetable::kLevels; ++l)
    {
        int N = 0;
        const float *d = t.level(31, l, N);
        std::vector<double> x(d, d + N);
        if (l == 0)
        {
            double peak = 0;
            for (double v : x) peak = std::max(peak, std::fabs(v));
            CHECK(std::fabs(peak - 1.0) < 1e-6);
        }
        const auto p = power(x);
        const double a1 = std::sqrt(p[1]);
        static double first = 0;
        if (l == 0) first = a1 * 2.0 / N;
        CHECK(std::fabs(a1 * 2.0 / N - first) < 1e-6 * first); // one gain on every level: a crossfade keeps the level
        const int H = std::min(32, Wavetable::kMaxHarmonics >> l);
        for (int h = 2; h <= H; ++h) worstRatio = std::max(worstRatio, std::fabs(std::sqrt(p[(size_t)h]) / a1 * h - 1.0));
        for (int h = H + 1; h < N / 2; ++h) worstAbove = std::max(worstAbove, std::sqrt(p[(size_t)h]) / a1);
    }
    std::printf("    levels: harmonics at 1/h to %.2g, above each level's top %.2g of the fundamental\n", worstRatio, worstAbove);
    CHECK(worstRatio < 1e-5);
    CHECK(worstAbove < 1e-6);
}

TEST(Wavetable_reads_with_nothing_above_nyquist)
{
    // (W2)–(W3): the richest frames — a square, a saw grown from 32 harmonics, a hard-synced saw — from 55 Hz to 14 kHz:
    // what lies off the tone's harmonics (an alias, an image of the reading) is under −70 dB at every pitch
    struct Case { int table; double position; const char *name; };
    double worst = -200, fundLo = 1e9, fundHi = 0;
    for (const Case c : {Case{0, 1.0, "square"}, Case{2, 1.0, "32 harmonics"}, Case{4, 30.0 / 31.0, "sync x7.8"}})
        for (int m : {75, 600, 2400, 9600, 19114})
        {
            const auto y = tone(Wavetable::builtIn(c.table), c.position, m);
            const double off = offHarmonics(y, m);
            if (c.table == 0) // the square's fundamental: the same at every pitch, whatever levels it is read from
            {
                const double a = harmonic(power(y), m, 1);
                fundLo = std::min(fundLo, a);
                fundHi = std::max(fundHi, a);
            }
            worst = std::max(worst, off);
            std::printf("    %s at %.1f Hz: %.1f dB off its harmonics\n", c.name, 48000.0 * m / kN, off);
        }
    std::printf("    the worst: %.1f dB; the square's fundamental within %.4f dB from 55 Hz to 14 kHz\n", worst, 20 * std::log10(fundHi / fundLo));
    CHECK(worst < -70.0);
    CHECK(20 * std::log10(fundHi / fundLo) < 0.01);
}

TEST(Wavetable_position_mixes_two_frames)
{
    // (W4): a position between two frames reads their straight mix — halfway, half of each (to 1e-12)
    const Wavetable &t = Wavetable::builtIn(0);
    const double step = 1.0 / (t.frames() - 1), dt = 110.0 / 48000.0;
    double worst = 0, apart = 0;
    for (int i = 0; i < 64; ++i)
    {
        const double ph = i / 64.0, a = t.read(ph, 10 * step, dt), b = t.read(ph, 11 * step, dt);
        worst = std::max(worst, std::fabs(t.read(ph, 10.5 * step, dt) - 0.5 * (a + b)));
        apart = std::max(apart, std::fabs(a - b));
    }
    CHECK(apart > 0.01 && worst < 1e-12);
}

TEST(Wavetable_position_ramps_a_straight_line)
{
    // (W7): a position moved over 4800 samples steps the same amount every sample — the oscillator reads what the
    // table reads at each of those positions (to 1e-12); nothing jumps
    configure48();
    const Wavetable &t = Wavetable::builtIn(0);
    Oscillator o;
    o.setAttackMs(0.0); o.setDecayMs(0.0); o.setSustain(1.0); o.setReleaseMs(0.0);
    o.setWaveform(Oscillator::Table);
    o.setTable(&t);
    o.setFrequency(220.0);
    o.setPosition(0.0, 0);
    o.noteOn(1.0);
    o.setPosition(1.0, 4800);
    std::vector<Sample> y(6000, 0.0);
    o.addBlock(y.data(), 6000, 0, 1.0);
    double worst = 0, phase = 0;
    const double dt = 220.0 / 48000.0;
    for (int n = 0; n < 6000; ++n)
    {
        const double pos = std::min(1.0, (n + 1) / 4800.0);
        worst = std::max(worst, std::fabs(y[(size_t)n] - t.read(phase, pos, dt)));
        phase += dt;
        if (phase >= 1.0) phase -= 1.0;
    }
    std::printf("    the ramp against the table at each position: %.2g\n", worst);
    CHECK(worst < 1e-9);
    CHECK(std::fabs(o.position(0) - 1.0) < 1e-12);
}

TEST(Wavetable_from_a_sound)
{
    // (W6): three cycles of 2048 points — a sine, a saw, a square — are three frames, each its own shape; a sound
    // shorter than a cycle is one frame, stretched
    std::vector<float> s(3 * 2048);
    for (int n = 0; n < 2048; ++n)
    {
        const double x = n / 2048.0;
        s[(size_t)n] = (float)std::sin(kTau * x);
        s[(size_t)(2048 + n)] = (float)(2 * x - 1);
        s[(size_t)(4096 + n)] = x < 0.5 ? 1.0f : -1.0f;
    }
    auto t = Wavetable::fromSample(s.data(), (long long)s.size());
    CHECK(t && t->frames() == 3);
    auto spectrum = [&](const Wavetable &w, int f) {
        int N = 0;
        const float *d = w.level(f, 0, N);
        return power(std::vector<double>(d, d + N));
    };
    const auto p0 = spectrum(*t, 0), p1 = spectrum(*t, 1), p2 = spectrum(*t, 2);
    CHECK(std::sqrt(p0[2] / p0[1]) < 1e-6);                     // a sine
    CHECK(std::fabs(std::sqrt(p1[2] / p1[1]) - 0.5) < 1e-3);    // a saw: 1/h
    CHECK(std::sqrt(p2[2] / p2[1]) < 1e-6 && std::fabs(std::sqrt(p2[3] / p2[1]) - 1.0 / 3) < 1e-3); // a square: odd, 1/h
    std::vector<float> short1(1000);
    for (int n = 0; n < 1000; ++n) short1[(size_t)n] = (float)std::sin(kTau * n / 999.0);
    auto u = Wavetable::fromSample(short1.data(), 1000);
    CHECK(u && u->frames() == 1);
    const auto pu = spectrum(*u, 0);
    CHECK(std::sqrt(pu[2] / pu[1]) < 1e-2);
    CHECK(!Wavetable::fromSample(nullptr, 0));
}

TEST(BasicSynth_plays_a_table_moved_by_its_envelope)
{
    // REQ-wavetable-3: an oscillator on Basic, its position 0 (a sine) moved to 1 (a square) by the filter envelope
    // over a second — the note at its pitch; its third harmonic all but absent at the start and a third of the
    // fundamental at the end (the filter wide open, unmoved)
    configure48();
    BasicSynth s;
    BasicSynth::Params p;
    p.osc1.table = 0;
    p.osc1.position = 0.0;
    p.osc1.posEnv = 1.0;
    p.osc2.level = 0.0;
    p.cutoff = 20000.0;
    p.resonance = 0.0;
    p.envAmount = 0.0;
    p.keytrack = 0.0;
    p.filterEnv = {1000.0, 0.0, 1.0, 100.0};
    p.ampEnv = {1.0, 0.0, 1.0, 50.0};
    s.setParams(p);
    s.noteOn(57, 127); // 220 Hz
    std::vector<Sample> L(60000, 0.0), R(60000, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    s.render(o, 2, 60000);
    auto goertzel = [&](int from, int n, double hz) {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double a = kTau * hz * (from + i) / 48000.0;
            re += L[(size_t)(from + i)] * std::cos(a);
            im += L[(size_t)(from + i)] * std::sin(a);
        }
        return 2.0 * std::hypot(re, im) / n;
    };
    const int n = 2400; // 50 ms: 11 cycles of 220 Hz exactly
    const double early3 = goertzel(240, n, 660.0) / goertzel(240, n, 220.0); // 5 … 55 ms (whole cycles): position ≲ 0.055
    const double late1 = goertzel(55200, n, 220.0), late3 = goertzel(55200, n, 660.0) / late1, late2 = goertzel(55200, n, 440.0) / late1;
    std::printf("    the third harmonic: %.3f of the fundamental at the start, %.3f at the end (a square's: 0.333); the second %.4f\n",
                early3, late3, late2);
    CHECK(early3 < 0.03);
    CHECK(std::fabs(late3 - 1.0 / 3) < 0.01 && late2 < 0.01);
    CHECK(late1 > 0.1);
    // released and struck again: the new note's table starts at 0 again, not where the last one was left
    s.noteOff(57);
    std::fill(L.begin(), L.end(), 0.0);
    s.render(o, 2, 12000); // its 50 ms release, and silence
    std::fill(L.begin(), L.end(), 0.0);
    s.noteOn(57, 127);
    CHECK(s.voicePosition(0, 0) == 0.0); // where its envelope starts, at once — not ramped from the last note's 1
    s.render(o, 2, 3000);
    const double again3 = goertzel(240, n, 660.0) / goertzel(240, n, 220.0);
    std::printf("    struck again: %.3f\n", again3);
    CHECK(again3 < 0.03);
}

TEST(BasicSynth_plays_the_table_of_a_sound)
{
    // REQ-wavetable-3: a sound given (one square cycle) is the table `kSampleTable` plays — a third harmonic a third
    // of the fundamental, no second
    configure48();
    std::vector<float> sq(2048);
    for (int i = 0; i < 2048; ++i) sq[(size_t)i] = i < 1024 ? 1.0f : -1.0f;
    BasicSynth s;
    s.setWavetable(sq.data(), 2048);
    BasicSynth::Params p;
    p.osc1.table = BasicSynth::kSampleTable;
    p.osc2.level = 0.0;
    p.cutoff = 20000.0;
    p.resonance = 0.0;
    p.envAmount = 0.0;
    p.keytrack = 0.0;
    p.ampEnv = {1.0, 0.0, 1.0, 50.0};
    s.setParams(p);
    s.noteOn(57, 127);
    std::vector<Sample> L(7200, 0.0), R(7200, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    s.render(o, 2, 7200);
    auto at = [&](double hz) {
        double re = 0, im = 0;
        for (int i = 0; i < 2400; ++i)
        {
            const double a = kTau * hz * (4800 + i) / 48000.0;
            re += L[(size_t)(4800 + i)] * std::cos(a);
            im += L[(size_t)(4800 + i)] * std::sin(a);
        }
        return std::hypot(re, im);
    };
    const double h3 = at(660.0) / at(220.0), h2 = at(440.0) / at(220.0);
    std::printf("    a square's table: the 3rd %.3f, the 2nd %.4f of the fundamental\n", h3, h2);
    CHECK(std::fabs(h3 - 1.0 / 3) < 0.01 && h2 < 0.01);
}
