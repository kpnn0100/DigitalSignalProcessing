// N34's effects (Solaris R-FX-6…17) — src/effects/README.md, each its Math section.
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

extern std::atomic<long> gAllocs; // the counting allocator in instrumentTests.cpp
extern std::atomic<bool> gCounting;

using namespace arstro;

namespace
{
    constexpr double kTau = 6.283185307179586;
    void configure48()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }
    /** A sum of sines (hz, amplitude) on both channels, `frames` long. */
    void tones(std::vector<Sample> &L, std::vector<Sample> &R, int frames, const std::vector<std::pair<double, double>> &t)
    {
        L.assign((size_t)frames, 0.0);
        for (int i = 0; i < frames; ++i)
            for (const auto &x : t) L[(size_t)i] += x.second * std::sin(kTau * x.first * i / 48000.0);
        R = L;
    }
    template <class P> void blocks(P &p, std::vector<Sample> &L, std::vector<Sample> &R, int block = 128)
    {
        for (size_t at = 0; at < L.size(); at += (size_t)block)
        {
            Sample *io[2] = {L.data() + at, R.data() + at};
            p.process(io, 2, (int)std::min((size_t)block, L.size() - at));
        }
    }
    double amp(const std::vector<Sample> &y, int from, int n, double hz)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double a = kTau * hz * (from + i) / 48000.0;
            re += y[(size_t)(from + i)] * std::cos(a);
            im += y[(size_t)(from + i)] * std::sin(a);
        }
        return 2.0 * std::hypot(re, im) / n;
    }
    double db(double x) { return 20.0 * std::log10(std::max(1e-30, x)); }
}

TEST(Multiband_at_ratio_one_is_flat)
{
    // (MB1)–(MB2): nothing compressed — the three bands sum to an all-pass: every frequency at its level, to 0.01 dB
    configure48();
    double worst = 0;
    for (double hz : {40.0, 150.0, 200.0, 600.0, 2500.0, 5000.0, 15000.0})
    {
        MultibandCompressor m;
        for (int k = 0; k < 3; ++k) m.setRatio(k, 1.0);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{hz, 0.5}});
        blocks(m, L, R);
        worst = std::max(worst, std::fabs(db(amp(L, 24000, 24000, hz) / 0.5)));
    }
    std::printf("    ratio 1 everywhere: within %.4f dB from 40 Hz to 15 kHz\n", worst);
    CHECK(worst < 0.01);
    // (MB4) its output gain: −6 dB
    MultibandCompressor m;
    for (int k = 0; k < 3; ++k) m.setRatio(k, 1.0);
    m.setOutputDb(-6.0);
    std::vector<Sample> L, R;
    tones(L, R, 48000, {{1000.0, 0.5}});
    blocks(m, L, R);
    CHECK(std::fabs(db(amp(L, 24000, 24000, 1000.0) / 0.5) + 6.0) < 0.01);
}

TEST(Multiband_compresses_only_its_band)
{
    // (MB3): the low band at −30 dB, 4:1 — a 60 Hz tone at −6 dBFS cut as the library's own Compressor cuts it alone
    // (its peak follower's, near the curve's 18 dB); a 5 kHz tone at −20 dBFS in the mid band (ratio 1) untouched.
    // (With the split at 200 Hz the mid band's uncompressed leak of 60 Hz — −42 dB — lifts the cut tone 0.5 dB: so a
    // multiband behaves; the test keeps its tone clear of the split to compare the bands' compression alone.)
    configure48();
    MultibandCompressor m;
    m.setSplits(1000.0, 8000.0); // 60 Hz far from the split: the mid band's leak of it (LR4: (60/1000)⁴) is −98 dB
    m.setThresholdDb(0, -30.0);
    m.setRatio(0, 4.0);
    m.setRatio(1, 1.0);
    m.setRatio(2, 1.0);
    std::vector<Sample> L, R;
    tones(L, R, 96000, {{60.0, 0.5}, {5000.0, 0.1}});
    blocks(m, L, R);
    const double cut = db(amp(L, 48000, 48000, 60.0) / 0.5), high = db(amp(L, 48000, 48000, 5000.0) / 0.1);
    Compressor alone; // the same, on the 60 Hz tone alone
    alone.setThresholdDb(-30.0);
    alone.setRatio(4.0);
    alone.setAttackMs(10.0);
    alone.setReleaseMs(150.0);
    std::vector<Sample> A, B;
    tones(A, B, 96000, {{60.0, 0.5}});
    alone.processBlock(A.data(), 96000, 0);
    const double ref = db(amp(A, 48000, 48000, 60.0) / 0.5);
    std::printf("    60 Hz cut by %.2f dB (the Compressor alone: %.2f; the curve 18); 5 kHz %+.3f dB\n", -cut, -ref, high);
    CHECK(std::fabs(cut - ref) < 0.02);
    CHECK(std::fabs(high) < 0.05);
}

TEST(Multiband_does_not_allocate)
{
    configure48();
    auto d = DeviceRegistry::create("multiband");
    std::vector<Sample> L(4096, 0.1), R(4096, 0.1);
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    d->process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

TEST(Gate_passes_the_loud_and_shuts_the_quiet)
{
    // (GT2): a tone at −20 dBFS (over −40) unchanged; at −50 dBFS shut by its range (80 dB); 2:1 below the threshold,
    // a tone 10 dB under it turned down 10 dB more
    configure48();
    auto run = [](double amplitude, double ratio) {
        Gate g;
        g.setRatio(ratio);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{1000.0, amplitude}});
        blocks(g, L, R);
        return db(amp(L, 24000, 24000, 1000.0) / amplitude);
    };
    const double loud = run(0.1, 100.0), shut = run(std::pow(10.0, -50.0 / 20.0), 100.0);
    // the 2:1 slope on a steady level (a peak follower reads a sine a little under its peak between peaks; a steady
    // level it reads exactly): 10 dB under the threshold, turned down 10 dB more
    Gate g;
    g.setRatio(2.0);
    std::vector<Sample> L(48000, (Sample)std::pow(10.0, -50.0 / 20.0)), R = L;
    blocks(g, L, R);
    const double expanded = db(L.back() / std::pow(10.0, -50.0 / 20.0));
    std::printf("    −20 dBFS: %+.3f dB; −50 dBFS gated: %.1f dB; 2:1, 10 dB under: %.3f dB\n", loud, shut, expanded);
    CHECK(std::fabs(loud) < 0.01);
    CHECK(shut < -79.0 && shut > -81.0); // its range: 80 dB, no more
    CHECK(std::fabs(expanded + 10.0) < 0.01);
    // (GT4) it opens at its attack (1 ms): shut on silence, a loud tone open within 0.6 dB 5 ms on
    Gate o;
    std::vector<Sample> S(48000, 0.0), S2(48000, 0.0);
    blocks(o, S, S2);
    std::vector<Sample> T, T2;
    tones(T, T2, 240, {{1000.0, 0.5}});
    blocks(o, T, T2);
    std::printf("    5 ms after a loud tone starts on a shut gate: %.2f dB\n", o.gainDb());
    CHECK(o.gainDb() > -0.6);
    // (GT1) linked: a tone in the right channel alone opens both — the left (silent) takes the same gain
    Gate k;
    std::vector<Sample> Lq, Rq;
    tones(Lq, Rq, 48000, {{1000.0, 0.1}});
    std::fill(Lq.begin(), Lq.end(), 0.0);
    blocks(k, Lq, Rq);
    CHECK(std::fabs(db(amp(Rq, 24000, 24000, 1000.0) / 0.1)) < 0.01);
}

TEST(Gate_holds_open_before_it_closes)
{
    // (GT3): a −6 dBFS tone, then −50: its level falls past −40 about 196 ms on (the detector's 50 ms fall); open still
    // 150 ms on; with a 300 ms hold still open 450 ms on, shut by 900 ms
    configure48();
    Gate g;
    g.setHoldMs(300.0);
    std::vector<Sample> L, R;
    tones(L, R, 24000, {{1000.0, 0.5}});
    blocks(g, L, R);
    CHECK(std::fabs(g.gainDb()) < 1e-9); // open (the smoothed gain at 0 to as near as a float says)
    std::vector<Sample> Q, Q2;
    tones(Q, Q2, 7200, {{1000.0, 0.00316}});
    blocks(g, Q, Q2); // 150 ms after the fall
    const double at150 = g.gainDb();
    tones(Q, Q2, 14400, {{1000.0, 0.00316}});
    blocks(g, Q, Q2); // 450 ms
    const double at450 = g.gainDb();
    tones(Q, Q2, 21600, {{1000.0, 0.00316}});
    blocks(g, Q, Q2); // 900 ms
    std::printf("    after the fall: %.2f dB at 150 ms, %.2f at 450, %.1f at 900 (four release constants: 80·(1 − e⁻⁴))\n", at150, at450, g.gainDb());
    CHECK(std::fabs(at150) < 1e-9 && std::fabs(at450) < 1e-9 && g.gainDb() < -75.0);
}

TEST(Gate_keyed_opens_on_another_track)
{
    // (GT1) keyed: a steady tone passes while the key is loud, and is shut while it is silent
    configure48();
    Gate g;
    g.setSidechain(true);
    g.setReleaseMs(5.0);
    g.setHoldMs(0.0);
    std::vector<Sample> L, R, kl(4800, 0.5), kr(4800, 0.5);
    const Sample *key[2] = {kl.data(), kr.data()};
    tones(L, R, 4800, {{1000.0, 0.1}});
    g.setKey(key, 2);
    Sample *io[2] = {L.data(), R.data()};
    g.process(io, 2, 4800);
    const double open = amp(L, 2400, 2400, 1000.0);
    std::fill(kl.begin(), kl.end(), 0.0f);
    std::fill(kr.begin(), kr.end(), 0.0f);
    tones(L, R, 4800, {{1000.0, 0.1}});
    g.process(io, 2, 4800);
    std::vector<Sample> M, N;
    tones(M, N, 24000, {{1000.0, 0.1}});
    for (size_t at = 0; at < M.size(); at += 4800)
    {
        Sample *jo[2] = {M.data() + at, N.data() + at};
        g.process(jo, 2, 4800);
    }
    std::printf("    keyed: open %.4f, shut %.2g (the tone 0.1)\n", open, amp(M, 19200, 4800, 1000.0));
    CHECK(std::fabs(open - 0.1) < 1e-3 && amp(M, 19200, 4800, 1000.0) < 1e-4);
}

namespace
{
    /** A 1 kHz square (|x| steady: its level exact) — `a` before `at`, `b` from it. */
    void squares(std::vector<Sample> &L, std::vector<Sample> &R, int frames, double a, int at, double b)
    {
        L.assign((size_t)frames, 0.0);
        for (int i = 0; i < frames; ++i) L[(size_t)i] = (i % 48 < 24 ? 1.0 : -1.0) * (i < at ? a : b);
        R = L;
    }
    /** Its gain, dB, after every frame (processed one at a time). */
    std::vector<double> trace(TransientShaper &t, std::vector<Sample> &L, std::vector<Sample> &R)
    {
        std::vector<double> g(L.size());
        for (size_t i = 0; i < L.size(); ++i)
        {
            Sample *io[2] = {L.data() + i, R.data() + i};
            t.process(io, 2, 1);
            g[i] = t.gainDb();
        }
        return g;
    }
    double pole(double ms) { return std::exp(-1.0 / (ms * 48.0)); }
}

TEST(Transient_at_nothing_is_the_input_to_the_bit)
{
    // (TS4)–(TS5): attack and sustain at 0 — the input untouched, to the bit; the output gain alone, exactly
    configure48();
    std::vector<Sample> L, R, L0, R0;
    tones(L, R, 48000, {{55.0, 0.4}, {1234.5, 0.2}});
    for (int i = 0; i < 48000; i += 6000) L[(size_t)i] = R[(size_t)i] = 0.9; // clicks: transients it must not touch
    L0 = L;
    R0 = R;
    TransientShaper t;
    blocks(t, L, R);
    CHECK(L == L0 && R == R0);
    TransientShaper o;
    o.setOutputDb(-6.0);
    blocks(o, L, R);
    double worst = 0;
    for (size_t i = 0; i < L.size(); ++i) worst = std::max(worst, std::fabs(L[i] - L0[i] * std::pow(10.0, -6.0 / 20.0)));
    CHECK(worst < 1e-15);
}

TEST(Transient_attack_shapes_the_rise)
{
    // (TS6): a square from silence — the gain is A·20·log10(e_f / e_r) from the closed forms, to 1e-9 dB, until the
    // limit; −100 % its mirror; the sustain does nothing on a rise; steady, nothing at all
    configure48();
    const double l = 0.02, af = pole(TransientShaper::kFastAttackMs), as = pole(TransientShaper::kSlowAttackMs), eps = 1e-5;
    auto want = [&](int n) {
        const double ef = l * (1.0 - std::pow(af, n + 1));
        const double er = l * ((1.0 - std::pow(as, n + 1)) - (1.0 - as) * af * (std::pow(as, n + 1) - std::pow(af, n + 1)) / (as - af));
        return 20.0 * std::log10((ef + eps) / (er + eps));
    };
    std::vector<double> g[3];
    const double amounts[3][2] = {{100, 0}, {-100, 0}, {0, 100}};
    for (int k = 0; k < 3; ++k)
    {
        TransientShaper t;
        t.setAttack(amounts[k][0]);
        t.setSustain(amounts[k][1]);
        std::vector<Sample> L, R;
        squares(L, R, 48000, 0.0, 0, l);
        g[k] = trace(t, L, R);
    }
    double worst = 0, sus = 0;
    int limited = 0;
    for (int n = 0; n < 48000; ++n)
    {
        const double w = want(n);
        if (w >= TransientShaper::kLimitDb) { limited += g[0][(size_t)n] == TransientShaper::kLimitDb && g[1][(size_t)n] == -TransientShaper::kLimitDb; continue; }
        worst = std::max({worst, std::fabs(g[0][(size_t)n] - w), std::fabs(g[1][(size_t)n] + w)});
        sus = std::max(sus, std::fabs(g[2][(size_t)n]));
    }
    std::printf("    +100 %%: %.2f dB at 2 ms, %.2f at 10, %.2f at 50, %.4f at 500 (limited at 24 for %d samples); off the closed form by %.2g\n",
                g[0][96], g[0][480], g[0][2400], g[0][23999], limited, worst);
    CHECK(worst < 1e-9);
    CHECK(limited > 0 && g[0][0] == TransientShaper::kLimitDb);
    CHECK(g[0][480] > 3.0 && g[0][23999] < 1e-3);
    CHECK(sus == 0.0);
}

TEST(Transient_sustain_shapes_the_fall)
{
    // (TS6): a square falls 20 dB — the gain is S·20·log10(e_s / e_f) from the closed forms, to 1e-9 dB; −100 % its
    // mirror; the attack does nothing on a fall
    configure48();
    const double hi = 0.25, lo = 0.025, d = hi - lo, eps = 1e-5;
    const double rf = pole(TransientShaper::kFastReleaseMs), rs = pole(TransientShaper::kSlowReleaseMs);
    const int at = 24000;
    auto want = [&](int k) {
        const double u = d * std::pow(rf, k + 1);
        const double v = d * (std::pow(rs, k + 1) + rf * (1.0 - rs) * (std::pow(rs, k + 1) - std::pow(rf, k + 1)) / (rs - rf));
        return 20.0 * std::log10((lo + v + eps) / (lo + u + eps));
    };
    std::vector<double> g[3];
    const double amounts[3][2] = {{0, 100}, {0, -100}, {100, 0}};
    for (int k = 0; k < 3; ++k)
    {
        TransientShaper t;
        t.setAttack(amounts[k][0]);
        t.setSustain(amounts[k][1]);
        std::vector<Sample> L, R;
        squares(L, R, 72000, hi, at, lo);
        g[k] = trace(t, L, R);
    }
    double worst = 0, att = 0;
    for (int k = 0; k < 48000; ++k)
    {
        const double w = std::min(want(k), TransientShaper::kLimitDb);
        worst = std::max({worst, std::fabs(g[0][(size_t)(at + k)] - w), std::fabs(g[1][(size_t)(at + k)] + w)});
        att = std::max(att, std::fabs(g[2][(size_t)(at + k)]));
    }
    std::printf("    +100 %%: %.2f dB 10 ms after the fall, %.2f at 100, %.2f at 500; off the closed form by %.2g\n",
                g[0][(size_t)(at + 480)], g[0][(size_t)(at + 4800)], g[0][(size_t)(at + 23999)], worst);
    CHECK(worst < 1e-9);
    CHECK(g[0][(size_t)(at + 4800)] > 6.0);
    CHECK(att == 0.0);
}

TEST(Transient_steady_is_untouched)
{
    // (TS3): a steady sound — its envelopes meet: a square (|x| steady) at ±100 % exactly 0 dB; a 100 Hz sine, its |x|
    // rippling, within 0.2 dB at its frequency (each envelope reads the ripple: 0.07 dB the attack's, 0.07 the sustain's)
    configure48();
    for (double a : {100.0, -100.0})
    {
        TransientShaper t;
        t.setAttack(a);
        t.setSustain(a);
        std::vector<Sample> L, R;
        squares(L, R, 48000, 0.25, 0, 0.25);
        const auto g = trace(t, L, R);
        CHECK(std::fabs(g[47999]) < 1e-9);
        TransientShaper s;
        s.setAttack(a);
        s.setSustain(a);
        tones(L, R, 96000, {{100.0, 0.25}});
        blocks(s, L, R);
        const double off = db(amp(L, 48000, 48000, 100.0) / 0.25);
        std::printf("    %+.0f %%: a steady 100 Hz sine %+.3f dB\n", a, off);
        CHECK(std::fabs(off) < 0.2);
    }
}

TEST(Transient_is_linked)
{
    // (TS1): the left loud and steady, the right rising under it — the loudest channel's level never moved, so the
    // right's rise passes untouched
    configure48();
    TransientShaper t;
    t.setAttack(100);
    std::vector<Sample> L, R, Rq, unused;
    squares(L, unused, 96000, 0.5, 0, 0.5);
    squares(Rq, unused, 96000, 0.0, 72000, 0.25); // 1.5 s on: the left's own rise long gone (a_s^72000 = e^−50)
    R = Rq;
    const auto g = trace(t, L, R);
    double moved = 0;
    for (int i = 72000; i < 72480; ++i) moved = std::max(moved, std::fabs(g[(size_t)i]));
    CHECK(moved < 1e-9);
}

TEST(Transient_does_not_allocate)
{
    configure48();
    TransientShaper t;
    t.setAttack(50);
    t.setSustain(-50);
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{110.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    t.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

namespace
{
    /** The de-esser's band filter at `hz` (DE1): H(e^{jω}) from Biquad::design's high-pass at `f`. */
    std::complex<double> bandAt(double f, double hz)
    {
        const auto c = Biquad::design(Biquad::HighPass, f, 0.7071067811865476, 0.0, 48000.0);
        const std::complex<double> z1 = std::polar(1.0, -kTau * hz / 48000.0), z2 = z1 * z1;
        return ((double)c.b0 + (double)c.b1 * z1 + (double)c.b2 * z2) / (1.0 + (double)c.a1 * z1 + (double)c.a2 * z2);
    }
    /** (DE4) split, a steady cut: a sine at `hz` comes out |H_shelf| of itself — the RBJ high shelf at f, −cut dB. */
    double splitAt(double f, double hz, double cutDb)
    {
        return std::pow(10.0, Biquad::magnitudeDb(Biquad::design(Biquad::HighShelf, f, 0.7071067811865476, -cutDb, 48000.0), hz, 48000.0) / 20.0);
    }
    DeEsser deesser(double f, double t, double range, bool split)
    {
        DeEsser d;
        d.setFrequency(f);
        d.setThresholdDb(t);
        d.setRangeDb(range);
        d.setSplit(split);
        return d;
    }
}

TEST(DeEsser_leaves_what_is_under_its_threshold)
{
    // (DE3): a voice's 1 kHz at −6 dBFS, the band at 6 kHz hearing it some 30 dB down — under −20 dB: nothing cut,
    // the input to the bit, either mode
    configure48();
    for (bool split : {true, false})
    {
        DeEsser d = deesser(6000, -20, 12, split);
        std::vector<Sample> L, R, L0;
        tones(L, R, 48000, {{1000.0, 0.5}});
        L0 = L;
        blocks(d, L, R);
        CHECK(L == L0 && R == L0 && d.cutDb() == 0.0);
    }
}

TEST(DeEsser_holds_the_band_at_its_threshold)
{
    // (DE2)–(DE4): a 10 kHz sibilance at −12 dBFS, the band at 6 kHz, the threshold −24 dB: cut by its level over the
    // threshold, 20·log10(0.25·|H|) + 24 — or a little less: the detector, a peak follower with a 1 ms attack, sits under
    // a sine's peak (0.43 dB at 10 kHz; never over it); a range of
    // 6 dB: cut 6 dB exactly, the sine out |H_shelf(−6 dB)| of itself to 0.01 dB
    configure48();
    const double want = 20.0 * std::log10(0.25 * std::abs(bandAt(6000, 10000))) + 24.0;
    DeEsser d = deesser(6000, -24, 24, true);
    std::vector<Sample> L, R;
    tones(L, R, 48000, {{10000.0, 0.25}});
    blocks(d, L, R);
    const double free = d.cutDb();
    DeEsser e = deesser(6000, -24, 6, true);
    tones(L, R, 48000, {{10000.0, 0.25}});
    blocks(e, L, R);
    const double out = db(amp(L, 24000, 24000, 10000.0) / 0.25), wantOut = db(splitAt(6000, 10000, 6.0));
    std::printf("    10 kHz at −12 dBFS over −24: cut %.2f dB (the formula %.2f); a 6 dB range: cut %.3f, out %.3f dB (want %.3f)\n", free, want, e.cutDb(), out, wantOut);
    CHECK(free <= want + 1e-9 && free > want - 0.5);
    CHECK(e.cutDb() == 6.0);
    CHECK(std::fabs(out - wantOut) < 0.01);
    // the corner moved while the cut holds at its range: the shelf moves with it
    e.setFrequency(4000);
    tones(L, R, 48000, {{10000.0, 0.25}});
    blocks(e, L, R);
    const double moved = db(amp(L, 24000, 24000, 10000.0) / 0.25), wantMoved = db(splitAt(4000, 10000, 6.0));
    std::printf("    the corner at 4 kHz: out %.3f dB (want %.3f)\n", moved, wantMoved);
    CHECK(std::fabs(moved - wantMoved) < 0.01 && std::fabs(moved - out) > 0.1);
}

TEST(DeEsser_split_spares_the_rest_wide_does_not)
{
    // (DE4)–(DE5): a voice (1 kHz) and its sibilance (10 kHz), cut 6 dB — split: each comes out |H_shelf| of itself (the
    // voice near untouched); wide: the voice 6 dB down with it
    configure48();
    std::vector<Sample> L, R;
    DeEsser s = deesser(6000, -24, 6, true);
    tones(L, R, 48000, {{1000.0, 0.25}, {10000.0, 0.25}});
    blocks(s, L, R);
    const double voice = db(amp(L, 24000, 24000, 1000.0) / 0.25), wantVoice = db(splitAt(6000, 1000, 6.0));
    DeEsser w = deesser(6000, -24, 6, false);
    tones(L, R, 48000, {{1000.0, 0.25}, {10000.0, 0.25}});
    blocks(w, L, R);
    const double wide = db(amp(L, 24000, 24000, 1000.0) / 0.25), wideS = db(amp(L, 24000, 24000, 10000.0) / 0.25);
    std::printf("    split: the voice %+.3f dB (the formula %+.3f); wide: the voice %+.3f dB, the sibilance %+.3f\n", voice, wantVoice, wide, wideS);
    CHECK(std::fabs(voice - wantVoice) < 0.01 && std::fabs(voice) < 0.5);
    CHECK(std::fabs(wide + 6.0) < 0.01 && std::fabs(wideS + 6.0) < 0.01);
}

TEST(DeEsser_listens_where_it_is_told)
{
    // (DE1): a 5 kHz tone at −12 dBFS, threshold −24 — heard by a band at 3 kHz (cut its range, 6 dB); not by one at
    // 12 kHz (some 15 dB further down: under the threshold, untouched)
    configure48();
    std::vector<Sample> L, R, L0;
    DeEsser lo = deesser(3000, -24, 6, true);
    tones(L, R, 24000, {{5000.0, 0.25}});
    blocks(lo, L, R);
    DeEsser hi = deesser(12000, -24, 6, true);
    tones(L, R, 24000, {{5000.0, 0.25}});
    L0 = L;
    blocks(hi, L, R);
    CHECK(lo.cutDb() == 6.0);
    CHECK(hi.cutDb() == 0.0 && L == L0);
}

TEST(DeEsser_is_linked)
{
    // (DE2): the sibilance in the right only — wide, the left (a voice alone) goes down with it, 6 dB
    configure48();
    DeEsser d = deesser(6000, -24, 6, false);
    std::vector<Sample> L, R, unused;
    tones(L, unused, 48000, {{1000.0, 0.25}});
    tones(R, unused, 48000, {{10000.0, 0.25}});
    blocks(d, L, R);
    CHECK(std::fabs(db(amp(L, 24000, 24000, 1000.0) / 0.25) + 6.0) < 0.01);
}

TEST(DeEsser_engages_without_a_click)
{
    // (DE4): a voice (1 kHz, −6 dBFS), then a quiet sibilance comes in (10 kHz, −26 dBFS, over −40) — the shelf has run
    // all along, so cutting starts from its own past: no step in the output larger than the input's own
    configure48();
    DeEsser d = deesser(6000, -40, 12, true);
    std::vector<Sample> L(24000), R;
    for (int i = 0; i < 24000; ++i)
        L[(size_t)i] = 0.5 * std::sin(kTau * 1000.0 * i / 48000.0) + (i >= 12007 ? 0.05 * std::sin(kTau * 10000.0 * (i - 12007) / 48000.0) : 0.0);
    R = L;
    const std::vector<Sample> X = L;
    blocks(d, L, R);
    double in = 0, out = 0;
    for (size_t i = 1; i < L.size(); ++i) { in = std::max(in, std::fabs(X[i] - X[i - 1])); out = std::max(out, std::fabs(L[i] - L[i - 1])); }
    std::printf("    the largest step: in %.4f, out %.4f (cut %.1f dB)\n", in, out, d.cutDb());
    CHECK(d.cutDb() > 0.0 && out <= in * 1.02);
}

TEST(DeEsser_does_not_allocate)
{
    configure48();
    DeEsser d = deesser(6000, -40, 12, true);
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{8000.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    d.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0 && d.cutDb() > 0.0);
}

namespace
{
    /** The largest in-band component that is NOT a harmonic of `hz` (an alias), in dB of the fundamental — read at
     *  each frequency a harmonic folds to at 48 kHz, every odd and even one up to the 60th. */
    double worstAlias(const std::vector<Sample> &y, int from, int n, double hz, double *where = nullptr)
    {
        const double f1 = amp(y, from, n, hz);
        double worst = -400;
        for (int h = 2; h <= 60; ++h)
        {
            double f = std::fmod(h * hz, 48000.0);
            if (f > 24000.0) f = 48000.0 - f;
            if (std::fabs(std::remainder(f, hz)) < 1.0 || f < 20.0 || f > 23900.0) continue; // on a harmonic: not told apart
            const double a = db(amp(y, from, n, f) / f1);
            if (a > worst) { worst = a; if (where) *where = f; }
        }
        return worst;
    }
}

TEST(Saturator_curves_are_their_formulas)
{
    // (SA2): soft is tanh; hard clips at ±1; tube rests at 0 with a slope of 1 there and is not odd (even harmonics);
    // fold is sin(πu/2) — 1 at u = 1, folded back to 0 at u = 2
    volatile double u = 0.7; // run-time tanh both sides (a folded constant may differ by an ulp)
    CHECK(std::fabs(Saturator::shape(Saturator::Soft, u) - std::tanh(u)) < 1e-15);
    CHECK(Saturator::shape(Saturator::Hard, 0.7) == 0.7 && Saturator::shape(Saturator::Hard, 3.0) == 1.0 && Saturator::shape(Saturator::Hard, -3.0) == -1.0);
    const double e = 1e-6, slope = (Saturator::shape(Saturator::Tube, e) - Saturator::shape(Saturator::Tube, -e)) / (2 * e);
    CHECK(Saturator::shape(Saturator::Tube, 0.0) == 0.0 && std::fabs(slope - 1.0) < 1e-9);
    CHECK(std::fabs(Saturator::shape(Saturator::Tube, 0.8) + Saturator::shape(Saturator::Tube, -0.8)) > 0.05);
    CHECK(std::fabs(Saturator::shape(Saturator::Fold, 1.0) - 1.0) < 1e-15 && std::fabs(Saturator::shape(Saturator::Fold, 2.0)) < 1e-15);
}

TEST(Saturator_harmonics_are_the_curves)
{
    // (SA1)–(SA4): a 1 kHz sine at 0.8, driven 6 dB — each curve's harmonics 1 … 5 are its Fourier series (summed here
    // over 4096 points of one cycle): the fundamental to 0.02 dB, each harmonic over −60 dB to 0.1 (both filters flat
    // to ±0.003 dB under 16 kHz)
    configure48();
    const double k = std::pow(10.0, 6.0 / 20.0);
    for (int curve = 0; curve < 4; ++curve)
    {
        Saturator s;
        s.setCurve(curve);
        s.setDriveDb(6.0);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{1000.0, 0.8}});
        blocks(s, L, R);
        double want[6] = {};
        for (int h = 1; h <= 5; ++h)
        {
            double re = 0, im = 0;
            for (int j = 0; j < 4096; ++j)
            {
                const double th = kTau * j / 4096.0, y = Saturator::shape(curve, k * 0.8 * std::sin(th));
                re += y * std::cos(h * th);
                im += y * std::sin(h * th);
            }
            want[h] = 2.0 * std::hypot(re, im) / 4096.0;
        }
        double worst = 0;
        for (int h = 1; h <= 5; ++h)
        {
            if (db(want[h] / want[1]) < -60.0) continue;
            const double got = amp(L, 24000, 24000, 1000.0 * h), off = std::fabs(db(got / want[h]));
            worst = std::max(worst, off);
            CHECK(off < (h == 1 ? 0.02 : 0.1));
        }
        std::printf("    curve %d: 2nd %.1f dB, 3rd %.1f dB of the fundamental; off its series by %.4f dB\n", curve, db(want[2] / want[1]), db(want[3] / want[1]), worst);
    }
}

TEST(Saturator_keeps_its_aliases_down)
{
    // (SA1)–(SA4): a 7 kHz sine at 0.8, driven 12 dB — every harmonic above 24 kHz folds back; oversampled four times,
    // what folds into the band is far under the same curve at the base rate's (soft −81 dB, hard −46, tube −56, fold
    // −62, against −21, −18, −22, −2)
    configure48();
    const double bound[4] = {-75.0, -42.0, -50.0, -55.0};
    for (int curve = 0; curve < 4; ++curve)
    {
        Saturator s;
        s.setCurve(curve);
        s.setDriveDb(12.0);
        std::vector<Sample> L, R, N;
        tones(L, R, 48000, {{7000.0, 0.8}});
        N = L;
        for (auto &x : N) x = Saturator::shape(curve, std::pow(10.0, 12.0 / 20.0) * x);
        blocks(s, L, R);
        double at = 0;
        const double o = worstAlias(L, 24000, 24000, 7000.0, &at), naive = worstAlias(N, 24000, 24000, 7000.0);
        std::printf("    curve %d: aliases %.1f dB (at %.0f Hz); the curve at the base rate %.1f dB\n", curve, o, at, naive);
        CHECK(o < bound[curve] && o < naive - 25.0);
    }
}

TEST(Saturator_is_exactly_its_latency_late)
{
    // (SA4), (SA6): 15 samples late, said by latency(); dry (mix 0) the input exactly that late; a quiet sine (tanh is
    // a line there) at mix 1 the input that late to 1e−6, at mix ½ too — the two paths aligned, no comb
    configure48();
    Saturator d;
    d.setMix(0.0);
    CHECK(d.latency() == 15);
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{1000.0, 0.5}, {7777.0, 0.2}});
    const std::vector<Sample> X = L;
    blocks(d, L, R);
    bool late = true;
    for (int n = 0; n < 4800; ++n) late &= L[(size_t)n] == (n < 15 ? 0.0 : X[(size_t)(n - 15)]);
    CHECK(late);
    Saturator o; // and the output gain on it, exactly
    o.setMix(0.0);
    o.setOutputDb(-6.0);
    tones(L, R, 4800, {{1000.0, 0.5}, {7777.0, 0.2}});
    blocks(o, L, R);
    double gainOff = 0;
    for (int n = 15; n < 4800; ++n) gainOff = std::max(gainOff, std::fabs(L[(size_t)n] - X[(size_t)(n - 15)] * std::pow(10.0, -6.0 / 20.0)));
    CHECK(gainOff < 1e-15);
    for (double mix : {1.0, 0.5})
    {
        Saturator w;
        w.setDriveDb(0.0);
        w.setMix(mix);
        tones(L, R, 4800, {{1000.0, 0.001}});
        const std::vector<Sample> Y = L;
        blocks(w, L, R);
        double off = 0;
        for (int n = 200; n < 4800; ++n) off = std::max(off, std::fabs(L[(size_t)n] - Y[(size_t)(n - 15)]));
        std::printf("    mix %.1f: a −60 dBFS sine 15 samples late to %.2g\n", mix, off);
        CHECK(off < 1e-6);
    }
}

TEST(Saturator_tube_is_dc_free)
{
    // (SA5): the tube's bias makes a DC the blocker takes away; its even harmonics stay
    configure48();
    Saturator s;
    s.setCurve(Saturator::Tube);
    s.setDriveDb(12.0);
    std::vector<Sample> L, R;
    tones(L, R, 48000, {{1000.0, 0.5}});
    blocks(s, L, R);
    double mean = 0;
    for (int n = 24000; n < 48000; ++n) mean += L[(size_t)n];
    mean /= 24000;
    const double second = db(amp(L, 24000, 24000, 2000.0) / amp(L, 24000, 24000, 1000.0));
    std::printf("    tube: mean %.2g, the 2nd harmonic %.1f dB\n", mean, second);
    CHECK(std::fabs(mean) < 1e-4 && second > -40.0);
}

TEST(Saturator_same_in_any_blocks)
{
    configure48();
    auto run = [](int block) {
        Saturator s;
        s.setCurve(Saturator::Tube);
        s.setDriveDb(18.0);
        s.setMix(0.7);
        std::vector<Sample> L, R;
        tones(L, R, 9600, {{440.0, 0.5}, {3100.0, 0.3}});
        blocks(s, L, R, block);
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
}

TEST(Saturator_does_not_allocate)
{
    configure48();
    Saturator s;
    s.setCurve(Saturator::Fold);
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    s.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

TEST(Bitcrusher_rounds_to_its_grid)
{
    // (BC1): 4 bits — every sample k/8, within 1/16 of its input; 8 and 12 bits — a sine's noise is its bits':
    // SNR = 6.02·b + 1.76 + 20·log10(a) dB, to 0.5
    configure48();
    Bitcrusher b;
    b.setBits(4);
    std::vector<Sample> L, R;
    tones(L, R, 48000, {{997.0, 0.9}});
    const std::vector<Sample> X = L;
    blocks(b, L, R);
    bool grid = true, near = true;
    for (size_t n = 0; n < L.size(); ++n)
    {
        grid &= L[n] * 8.0 == std::round(L[n] * 8.0);
        near &= std::fabs(L[n] - X[n]) <= 1.0 / 16.0 + 1e-15;
    }
    CHECK(grid && near);
    for (int bits : {8, 12})
    {
        Bitcrusher c;
        c.setBits(bits);
        tones(L, R, 48000, {{997.0, 0.9}});
        blocks(c, L, R);
        double ps = 0, pe = 0;
        for (size_t n = 0; n < L.size(); ++n) { ps += X[n] * X[n]; pe += (L[n] - X[n]) * (L[n] - X[n]); }
        const double snr = 10.0 * std::log10(ps / pe), want = 6.0206 * bits + 1.7609 + 20.0 * std::log10(0.9);
        std::printf("    %d bits: SNR %.2f dB (6.02·b + 1.76 + 20·log10 a: %.2f)\n", bits, snr, want);
        CHECK(std::fabs(snr - want) < 0.5);
    }
}

TEST(Bitcrusher_holds_at_its_rate)
{
    // (BC2): at 12 kHz every value held 4 samples, 12000 taken a second; at 7 kHz held 6 or 7, 7000 taken; at or above
    // the sample rate, every sample its own
    configure48();
    for (double rate : {12000.0, 7000.0, 96000.0})
    {
        Bitcrusher b;
        b.setBits(24);
        b.setRate(rate);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{311.0, 0.9}});
        blocks(b, L, R);
        int shortest = 1 << 30, longest = 0, run = 1;
        for (size_t n = 1; n < L.size(); ++n)
        {
            if (L[n] == L[n - 1]) { ++run; continue; }
            if (n > (size_t)run) { shortest = std::min(shortest, run); longest = std::max(longest, run); }
            run = 1;
        }
        std::printf("    %.0f Hz: %lld taken, held %d … %d samples\n", rate, b.taken(), shortest, longest);
        const double want = std::min(rate, 48000.0);
        CHECK(std::llabs(b.taken() - (long long)want) <= 1);
        if (rate == 12000.0) CHECK(shortest == 4 && longest == 4);
        if (rate == 7000.0) CHECK(shortest == 6 && longest == 7);
        if (rate == 96000.0) CHECK(longest == 1);
    }
}

TEST(Bitcrusher_dry_is_the_input)
{
    // (BC3): mix 0 — the input, to the bit; the output gain alone, exactly
    configure48();
    Bitcrusher b;
    b.setBits(3);
    b.setRate(1000);
    b.setMix(0.0);
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{1000.0, 0.5}});
    const std::vector<Sample> X = L;
    blocks(b, L, R);
    CHECK(L == X);
    b.setOutputDb(-6.0);
    L = X;
    R = X;
    blocks(b, L, R);
    double off = 0;
    for (size_t n = 0; n < L.size(); ++n) off = std::max(off, std::fabs(L[n] - X[n] * std::pow(10.0, -6.0 / 20.0)));
    CHECK(off < 1e-15);
}

TEST(Bitcrusher_does_not_allocate)
{
    configure48();
    Bitcrusher b;
    b.setRate(5000);
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    b.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

namespace
{
    /** (PH1)–(PH4) still (depth 0): the phaser's response at `hz` — (1 − m) + m·A^N / (1 − fb·A^N·e^(−jω)). */
    double phaserAt(double fb, double mix, int stages, double center, double hz)
    {
        const double a = Phaser::coefficient(center, 48000.0);
        const std::complex<double> z1 = std::polar(1.0, -kTau * hz / 48000.0);
        const std::complex<double> A = (a + z1) / (1.0 + a * z1), An = std::pow(A, stages);
        return std::abs((1.0 - mix) + mix * An / (1.0 - fb * An * z1));
    }
    Phaser still(int stages, double fb, double mix, double center = 1000.0)
    {
        Phaser p;
        p.setStages(stages);
        p.setFeedback(fb);
        p.setMix(mix);
        p.setCenterHz(center);
        p.setDepthOct(0.0);
        return p;
    }
}

TEST(Phaser_stage_is_an_allpass)
{
    // (PH1): a stage passes every frequency at its level and turns a quarter-turn at its break frequency
    const double a = Phaser::coefficient(1000.0, 48000.0);
    double worst = 0;
    for (double hz : {20.0, 300.0, 1000.0, 5000.0, 20000.0})
    {
        const std::complex<double> z1 = std::polar(1.0, -kTau * hz / 48000.0), A = (a + z1) / (1.0 + a * z1);
        worst = std::max(worst, std::fabs(std::abs(A) - 1.0));
    }
    const std::complex<double> zb = std::polar(1.0, -kTau * 1000.0 / 48000.0), Ab = (a + zb) / (1.0 + a * zb);
    CHECK(worst < 1e-12 && std::fabs(std::arg(Ab) + kTau / 4) < 1e-12);
}

TEST(Phaser_notches_where_the_phase_says)
{
    // (PH4)–(PH5) still: N stages, no feedback, mix ½ — a sine at each f_k = (fs/π)·atan(t·tan(π(2k+1)/(2N))) cancels
    // (under −80 dB); at the break frequency the chain has turned whole turns (N = 4: one): 0 dB; with feedback ½ every
    // frequency comes out as the closed form says, to 0.01 dB
    configure48();
    for (int stages : {4, 8})
    {
        const double t = std::tan(kTau / 2 * 1000.0 / 48000.0);
        for (int k = 0; k < stages / 2; ++k)
        {
            const double f = 48000.0 / (kTau / 2) * std::atan(t * std::tan(kTau / 2 * (2 * k + 1) / (2.0 * stages)));
            Phaser p = still(stages, 0.0, 0.5);
            std::vector<Sample> L, R;
            tones(L, R, 48000, {{f, 0.5}});
            blocks(p, L, R);
            const double left = db(amp(L, 24000, 24000, f) / 0.5);
            std::printf("    %d stages, notch %d at %.1f Hz: %.1f dB\n", stages, k, f, left);
            CHECK(left < -80.0);
        }
    }
    Phaser p = still(4, 0.0, 0.5);
    std::vector<Sample> L, R;
    tones(L, R, 48000, {{1000.0, 0.5}});
    blocks(p, L, R);
    CHECK(std::fabs(db(amp(L, 24000, 24000, 1000.0) / 0.5)) < 0.001);
    double worst = 0;
    for (double hz : {150.0, 700.0, 1900.0, 4400.0, 12000.0})
    {
        Phaser q = still(6, 0.5, 0.7);
        tones(L, R, 48000, {{hz, 0.25}});
        blocks(q, L, R);
        worst = std::max(worst, std::fabs(db(amp(L, 24000, 24000, hz) / 0.25) - db(phaserAt(0.5, 0.7, 6, 1000.0, hz))));
    }
    std::printf("    feedback ½, mix 0.7, 6 stages: off the closed form by %.4f dB\n", worst);
    CHECK(worst < 0.01);
}

TEST(Phaser_sweeps_its_break)
{
    // (PH2): the break frequency is f_c·2^(depth·sin(2π(rate·n/fs + offset))) — the right channel `stereo` ahead
    configure48();
    Phaser p;
    p.setCenterHz(700.0);
    p.setDepthOct(1.5);
    p.setRateHz(2.0);
    p.setStereoDeg(90.0);
    std::vector<Sample> L, R;
    tones(L, R, 48000, {{440.0, 0.3}});
    double worst = 0;
    for (size_t at = 0; at < L.size(); at += 1000)
    {
        Sample *io[2] = {L.data() + at, R.data() + at};
        p.process(io, 2, 1000);
        const double n = (double)(at + 999); // the last sample's phase
        for (int c : {0, 1})
        {
            const double want = 700.0 * std::exp2(1.5 * std::sin(kTau * (2.0 * n / 48000.0 + (c ? 0.25 : 0.0))));
            worst = std::max(worst, std::fabs(p.breakHz(c) / want - 1.0));
        }
    }
    CHECK(worst < 1e-9);
}

TEST(Phaser_dry_is_the_input)
{
    // (PH4): mix 0 — the input, to the bit
    configure48();
    Phaser p;
    p.setMix(0.0);
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{440.0, 0.5}, {3000.0, 0.2}});
    const std::vector<Sample> X = L;
    blocks(p, L, R);
    CHECK(L == X);
}

TEST(Phaser_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    auto run = [](int block) {
        Phaser p;
        p.setRateHz(3.0);
        p.setStages(10);
        std::vector<Sample> L, R;
        tones(L, R, 9600, {{220.0, 0.5}, {2500.0, 0.2}});
        blocks(p, L, R, block);
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
    Phaser p;
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    p.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

// REQ-fx-shaper-1 (SH2): each shape's open amount through its cycle — closed where the beat falls (a saw down open)
TEST(Shaper_shapes_open_and_close_on_the_beat)
{
    using S = Shaper::Shape;
    CHECK(std::fabs(Shaper::shapeAt(S::Sine, 0.0)) < 1e-12 && std::fabs(Shaper::shapeAt(S::Sine, 0.5) - 1.0) < 1e-12 &&
          std::fabs(Shaper::shapeAt(S::Sine, 0.25) - 0.5) < 1e-12);
    CHECK(std::fabs(Shaper::shapeAt(S::Triangle, 0.25) - 0.5) < 1e-12 && std::fabs(Shaper::shapeAt(S::Triangle, 0.5) - 1.0) < 1e-12);
    CHECK(std::fabs(Shaper::shapeAt(S::SawDown, 0.0) - 1.0) < 1e-12 && std::fabs(Shaper::shapeAt(S::SawUp, 0.75) - 0.75) < 1e-12);
    CHECK(Shaper::shapeAt(S::Square, 0.49) == 0.0 && Shaper::shapeAt(S::Square, 0.5) == 1.0);
    // the pump: (1 − e^(−φ/τ)) / (1 − e^(−1/τ)), τ 0.18 — closed on the beat, back to 63 % of open by τ
    const double tau = 0.18, want = (1 - std::exp(-1.0)) / (1 - std::exp(-1.0 / tau));
    CHECK(Shaper::shapeAt(S::Pump, 0.0) == 0.0 && std::fabs(Shaper::shapeAt(S::Pump, tau) - want) < 1e-12 &&
          std::fabs(Shaper::shapeAt(S::Pump, 1.0 - 1e-12) - 1.0) < 1e-9);
}

// (SH1) a note value in beats — straight, triplet, dotted
TEST(Shaper_rates_are_note_values)
{
    const double want[] = {4, 2, 1, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75};
    for (int r = 0; r < Shaper::kRates; ++r) CHECK(std::fabs(Shaper::beatsOfRate(r) - want[r]) < 1e-15);
}

// REQ-device-12, (SH1)/(SH3): the gain at each sample is the shape at the SONG's beat there — told by the transport
TEST(Shaper_follows_the_song_beat)
{
    configure48();
    Shaper sh;
    sh.setRate(3); // an eighth: half a beat
    sh.setShape(0);
    sh.setDepth(0.75);
    sh.setSmoothMs(0);
    const long long at = 1234567;
    const double spb = 22500.0, offset = 3.25; // 128 bpm, a song whose line starts at beat 3.25
    sh.setTransport(at, spb, offset);
    std::vector<Sample> L(4096, 1.0), R(4096, 1.0);
    Sample *io[2] = {L.data(), R.data()};
    sh.process(io, 2, 4096);
    double worst = 0;
    for (int i = 0; i < 4096; ++i)
    {
        double phi = (offset + (double)(at + i) / spb) / 0.5;
        phi -= std::floor(phi);
        const double want = 1.0 - 0.75 * (1.0 - (0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * phi))); // (SH3)
        worst = std::max(worst, std::fabs(L[(size_t)i] - want));
    }
    CHECK(worst < 1e-12);
}

// the odd channels a `stereo` of a cycle ahead
TEST(Shaper_stereo_offsets_the_odd_channels)
{
    configure48();
    Shaper sh;
    sh.setRate(2); // a quarter: one beat
    sh.setShape(1);
    sh.setDepth(1);
    sh.setSmoothMs(0);
    sh.setStereo(0.5);
    sh.setTransport(0, 24000.0, 0.0);
    std::vector<Sample> L(24000, 1.0), R(24000, 1.0);
    Sample *io[2] = {L.data(), R.data()};
    sh.process(io, 2, 24000);
    double worst = 0;
    for (int i = 0; i < 12000; ++i) worst = std::max(worst, std::fabs(R[(size_t)i] - L[(size_t)i + 12000]));
    CHECK(worst < 1e-12 && L[0] < 1e-12 && std::fabs(R[0] - 1.0) < 1e-12);
}

// (SH4) a square's edge glides: 1 − e^(−1) of the way after one time constant
TEST(Shaper_smooths_its_edges)
{
    configure48();
    Shaper sh;
    sh.setRate(2);
    sh.setShape(4);
    sh.setDepth(1);
    sh.setSmoothMs(5);
    sh.setTransport(0, 24000.0, 0.0);
    std::vector<Sample> L(24000, 1.0), R(24000, 1.0);
    Sample *io[2] = {L.data(), R.data()};
    sh.process(io, 2, 24000);
    // the edge at half a beat (12000): 240 samples (5 ms) later the gain has come 63 % of the way
    const double got = L[12000 + 240], want = 1.0 - std::exp(-241.0 / 240.0);
    CHECK(L[11999] < 1e-9 && std::fabs(got - want) < 0.01 && L[12000] < 0.01);
}

// the same however a render is cut, told the transport each block or counting its own; nothing allocated
TEST(Shaper_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    auto run = [](int block, bool told) {
        Shaper sh;
        sh.setRate(8); // a sixteenth triplet
        sh.setShape(5);
        sh.setStereo(0.25);
        std::vector<Sample> L, R;
        tones(L, R, 9600, {{220.0, 0.5}, {2500.0, 0.2}});
        for (size_t at = 0; at < L.size(); at += (size_t)block)
        {
            if (told) sh.setTransport(777000 + (long long)at, 22500.0, 1.5);
            Sample *io[2] = {L.data() + at, R.data() + at};
            sh.process(io, 2, (int)std::min((size_t)block, L.size() - at));
        }
        return L;
    };
    const auto a = run(1, true), b = run(128, true), c = run(777, true);
    CHECK(a == b && a == c);
    const auto d = run(1, false), e = run(500, false);
    CHECK(d == e);
    Shaper sh;
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    sh.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

// REQ-device-12: the registry's "shaper" is told where the song is through Device::setTransport
TEST(Shaper_device_follows_the_transport)
{
    configure48();
    auto d = DeviceRegistry::create("shaper");
    CHECK(d != nullptr);
    d->setParam("rate", 4); // a sixteenth
    d->setParam("shape", 4);
    d->setParam("depth", 1);
    d->setParam("smooth", 0);
    Device::Transport t;
    t.sample = 0;
    t.samplesPerBeat = 24000.0;
    t.beatOffset = 0.125; // half a sixteenth in: the square already open
    d->setTransport(t);
    std::vector<Sample> L(64, 1.0), R(64, 1.0);
    Sample *io[2] = {L.data(), R.data()};
    d->process(io, 2, 64);
    CHECK(std::fabs(L[0] - 1.0) < 1e-12);
    t.beatOffset = 0.0; // on the sixteenth: closed
    d->setTransport(t);
    std::fill(L.begin(), L.end(), 1.0);
    std::fill(R.begin(), R.end(), 1.0);
    d->process(io, 2, 64);
    CHECK(L[0] < 1e-12);
}

// REQ-fx-vowel-1 (VW1)/(VW2): the formants are the table's at each vowel, between two in log frequency, shifted in semitones
TEST(Vowel_formants_are_the_table)
{
    for (int v = 0; v < Vowel::kVowels; ++v)
        for (int k = 0; k < Vowel::kFormants; ++k) CHECK(std::fabs(Vowel::formantHz(v, k, 0) - Vowel::kTable[v][k]) < 1e-9);
    CHECK(std::fabs(Vowel::formantHz(0.5, 0, 0) - std::sqrt(730.0 * 530.0)) < 1e-9);  // A to E, half way: the geometric mean
    CHECK(std::fabs(Vowel::formantHz(2, 1, 12) - 2 * 2290.0) < 1e-9);                // an octave up
}

// (VW3)/(VW4): a sine at each of A's formants comes through at that band's weight; far from them it is held down
TEST(Vowel_peaks_at_its_formants)
{
    configure48();
    auto through = [](double hz, double vowel) {
        Vowel f;
        f.setVowel(vowel);
        f.setGlideMs(0);
        f.setOutputDb(0);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{hz, 0.5}});
        blocks(f, L, R);
        return amp(L, 24000, 24000, hz) / 0.5;
    };
    const double a1 = through(730, 0), a2 = through(1090, 0), a3 = through(2440, 0), far = through(6000, 0);
    CHECK(std::fabs(20 * std::log10(a1)) < 1.0);             // F1 about 0 dB
    CHECK(std::fabs(20 * std::log10(a2 / 0.63)) < 1.5);      // F2 its −4 dB
    CHECK(std::fabs(20 * std::log10(a3 / 0.32)) < 1.5);      // F3 its −10 dB
    CHECK(20 * std::log10(far) < -20.0);                     // between and beyond: down
    // (VW3) narrower with resonance: a tenth off F1 falls further at resonance 4 than at 1
    auto off = [](double res) {
        Vowel f;
        f.setVowel(0);
        f.setGlideMs(0);
        f.setOutputDb(0);
        f.setResonance(res);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{803.0, 0.5}});
        blocks(f, L, R);
        return 20 * std::log10(amp(L, 24000, 24000, 803.0) / 0.5);
    };
    CHECK(off(1.0) - off(4.0) > 6.0);
}

// (VW2): a vowel's own formants pass, another's are held down — A opens 730 Hz, I opens 2290 Hz
TEST(Vowel_morphs_between_vowels)
{
    configure48();
    auto through = [](double hz, double vowel) {
        Vowel f;
        f.setVowel(vowel);
        f.setGlideMs(0);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{hz, 0.5}});
        blocks(f, L, R);
        return 20 * std::log10(amp(L, 24000, 24000, hz) / 0.5);
    };
    CHECK(through(730, 0) - through(730, 2) > 10.0);   // A's first formant: open at A, shut at I
    CHECK(through(2290, 2) - through(2290, 0) > 10.0); // I's second: open at I, shut at A
}

// the vowel glides to a new setting: 1 − e^(−1) of the way after one time constant
TEST(Vowel_glides_between_vowels)
{
    configure48();
    Vowel f;
    f.setGlideMs(0);
    f.setVowel(0);
    std::vector<Sample> L(64, 0.0), R(64, 0.0);
    Sample *io[2] = {L.data(), R.data()};
    f.process(io, 2, 64);
    f.setGlideMs(10);
    f.setVowel(4);
    std::vector<Sample> L2(480, 0.0), R2(480, 0.0);
    Sample *io2[2] = {L2.data(), R2.data()};
    f.process(io2, 2, 480);
    CHECK(std::fabs(f.vowelNow() - 4.0 * (1 - std::exp(-1.0))) < 0.02);
    // the bands follow the glide: settled at U, U's first formant (300 Hz) passes and A's (730) is held down
    std::vector<Sample> L3, R3;
    tones(L3, R3, 48000, {{300.0, 0.25}, {730.0, 0.25}});
    blocks(f, L3, R3);
    CHECK(amp(L3, 24000, 24000, 300.0) > 4.0 * amp(L3, 24000, 24000, 730.0));
}

// (VW5) dry: the input times the output gain
TEST(Vowel_dry_is_the_input)
{
    configure48();
    Vowel f;
    f.setMix(0);
    f.setOutputDb(0);
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{440.0, 0.5}, {3000.0, 0.2}});
    const auto x = L;
    blocks(f, L, R);
    double worst = 0;
    for (size_t i = 0; i < L.size(); ++i) worst = std::max(worst, std::fabs(L[i] - x[i]));
    CHECK(worst < 1e-12);
}

TEST(Vowel_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    auto run = [](int block) {
        Vowel f;
        f.setVowel(1.3);
        f.setShift(3);
        std::vector<Sample> L, R;
        tones(L, R, 9600, {{110.0, 0.4}, {880.0, 0.2}, {2500.0, 0.1}});
        blocks(f, L, R, block);
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
    Vowel f;
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    f.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

TEST(Delay_reads_between_samples)
{
    // D-18 (Solaris DEFECTS): read(D + f) before a write lies between the samples D + 1 and D + 2 back —
    // (1 − f)·x[n−1−D] + f·x[n−2−D], interpolated, never past the newer one; at a whole D, x[n−1−D] as always.
    // Its gain at ω is |(1 − f) + f·e^(−jω)| ≤ 1: a 20 kHz sine at f = ½ comes out |cos(ω/2)| of itself
    configure48();
    for (double D : {10.0, 10.25, 10.5, 10.75, 37.9})
    {
        Delay d;
        d.setSmoothEnable(false);
        d.setMaxDelay(256);
        d.prepare();
        std::vector<double> x(2000), y(2000);
        for (int n = 0; n < 2000; ++n) x[(size_t)n] = std::sin(0.37 * n) + 0.5 * std::sin(2.9 * n + 1.0);
        for (int n = 0; n < 2000; ++n)
        {
            y[(size_t)n] = d.read(D);
            d.write(x[(size_t)n]);
        }
        const int i = (int)std::floor(D);
        const double f = D - i;
        double worst = 0;
        for (int n = 300; n < 2000; ++n) worst = std::max(worst, std::fabs(y[(size_t)n] - ((1 - f) * x[(size_t)(n - 1 - i)] + f * x[(size_t)(n - 2 - i)])));
        CHECK(worst < 1e-12);
    }
    Delay d;
    d.setSmoothEnable(false);
    d.setMaxDelay(256);
    d.prepare();
    std::vector<Sample> y(48000);
    for (int n = 0; n < 48000; ++n)
    {
        y[(size_t)n] = d.read(20.5);
        d.write(std::sin(kTau * 20000.0 * n / 48000.0));
    }
    const double g = amp(y, 24000, 24000, 20000.0), want = std::fabs(std::cos(kTau / 2 * 20000.0 / 48000.0));
    std::printf("    a 20 kHz sine through a delay of 20.5 samples: %.2f dB (interpolation's %.2f)\n", db(g), db(want));
    CHECK(std::fabs(db(g) - db(want)) < 0.01);
}

namespace
{
    /** (FL2)–(FL4) still at a whole D: (1 − m) + m·z^−D / (1 − fb·z^−D). */
    double flangerAt(double fb, double mix, int D, double hz)
    {
        const std::complex<double> zD = std::polar(1.0, -kTau * hz * D / 48000.0);
        return std::abs((1.0 - mix) + mix * zD / (1.0 - fb * zD));
    }
    Flanger stillFlanger(double ms, double fb, double mix)
    {
        Flanger f;
        f.setDelayMs(ms);
        f.setDepthMs(0.0);
        f.setFeedback(fb);
        f.setMix(mix);
        return f;
    }
}

TEST(Flanger_is_a_comb_where_it_stands)
{
    // (FL4) still, 1 ms (48 samples): no feedback, mix ½ — a sine at (2k + 1)·500 Hz cancels, one at 1 kHz passes at
    // 0 dB; feedback 0.7, mix 0.6 — every frequency the closed form's, to 0.01 dB
    configure48();
    for (double hz : {500.0, 1500.0, 2500.0})
    {
        Flanger f = stillFlanger(1.0, 0.0, 0.5);
        std::vector<Sample> L, R;
        tones(L, R, 24000, {{hz, 0.5}});
        blocks(f, L, R);
        const double left = db(amp(L, 4800, 19200, hz) / 0.5);
        std::printf("    1 ms still, %.0f Hz: %.1f dB\n", hz, left);
        CHECK(left < -100.0);
    }
    double worst = 0;
    for (double hz : {130.0, 1000.0, 1234.0, 3700.0, 9000.0})
    {
        Flanger f = stillFlanger(1.0, 0.7, 0.6);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{hz, 0.25}});
        blocks(f, L, R);
        worst = std::max(worst, std::fabs(db(amp(L, 24000, 24000, hz) / 0.25) - db(flangerAt(0.7, 0.6, 48, hz))));
    }
    std::printf("    feedback 0.7, mix 0.6: off the closed form by %.4f dB\n", worst);
    CHECK(worst < 0.01);
}

TEST(Flanger_sweeps_its_delay)
{
    // (FL1): D = (delay + depth·(1 − cos 2π(rate·n/fs + offset))/2)·fs/1000 — the right channel `stereo` ahead
    configure48();
    Flanger f;
    f.setDelayMs(0.5);
    f.setDepthMs(4.0);
    f.setRateHz(1.5);
    f.setStereoDeg(90.0);
    std::vector<Sample> L, R;
    tones(L, R, 48000, {{440.0, 0.3}});
    double worst = 0, lo = 1e9, hi = 0;
    for (size_t at = 0; at < L.size(); at += 1000)
    {
        Sample *io[2] = {L.data() + at, R.data() + at};
        f.process(io, 2, 1000);
        const double n = (double)(at + 999);
        for (int c : {0, 1})
        {
            const double want = (0.5 + 4.0 * 0.5 * (1.0 - std::cos(kTau * (1.5 * n / 48000.0 + (c ? 0.25 : 0.0))))) * 48.0;
            worst = std::max(worst, std::fabs(f.delaySamples(c) - want));
            lo = std::min(lo, f.delaySamples(c));
            hi = std::max(hi, f.delaySamples(c));
        }
    }
    std::printf("    swept %.1f … %.1f samples; off the formula by %.2g\n", lo, hi, worst);
    CHECK(worst < 1e-9);
}

TEST(Flanger_fed_back_hard_stays_bounded)
{
    // (FL2), REQ-delay-1: feedback ±0.95, swept fast through the shortest delays, a loud broadband input — the loop's
    // gain is under 1 at every frequency (the line never amplifies), so the output stays bounded: under 1/(1 − 0.95)
    // of the input's peak
    configure48();
    for (double fb : {0.95, -0.95})
    {
        Flanger f;
        f.setDelayMs(0.05);
        f.setDepthMs(10.0);
        f.setRateHz(7.0);
        f.setFeedback(fb);
        f.setMix(1.0);
        std::vector<Sample> L(96000), R;
        unsigned s = 12345;
        for (auto &x : L) { s = s * 1103515245u + 12345u; x = 0.5 * ((double)(s >> 8) / 16777216.0 - 0.5); }
        R = L;
        blocks(f, L, R);
        double most = 0;
        bool finite = true;
        for (Sample x : L) { finite = finite && std::isfinite(x); most = std::max(most, (double)std::fabs(x)); }
        std::printf("    feedback %+.2f: the loudest sample %.3f (the input's 0.25)\n", fb, most);
        CHECK(finite && most < 0.25 / (1.0 - 0.95));
    }
}

TEST(Flanger_dry_is_the_input)
{
    // (FL3): mix 0 — the input, to the bit
    configure48();
    Flanger f;
    f.setMix(0.0);
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{440.0, 0.5}, {3000.0, 0.2}});
    const std::vector<Sample> X = L;
    blocks(f, L, R);
    CHECK(L == X);
}

TEST(Flanger_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    auto run = [](int block) {
        Flanger f;
        f.setRateHz(2.0);
        std::vector<Sample> L, R;
        tones(L, R, 9600, {{220.0, 0.5}, {2500.0, 0.2}});
        blocks(f, L, R, block);
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
    Flanger f;
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    f.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

namespace
{
    /** A Butterworth section's response at `hz` (Biquad::design at 48 kHz). */
    std::complex<double> sectionAt(Biquad::Type t, double f, double hz)
    {
        const auto c = Biquad::design(t, f, 0.70710678118654752, 0.0, 48000.0);
        const std::complex<double> z1 = std::polar(1.0, -kTau * hz / 48000.0), z2 = z1 * z1;
        return ((double)c.b0 + (double)c.b1 * z1 + (double)c.b2 * z2) / (1.0 + (double)c.a1 * z1 + (double)c.a2 * z2);
    }
    /** Mid and side amplitudes of a stereo pair at `hz`. */
    std::pair<double, double> midSide(const std::vector<Sample> &L, const std::vector<Sample> &R, int from, int n, double hz)
    {
        std::vector<Sample> M(L.size()), S(L.size());
        for (size_t i = 0; i < L.size(); ++i) { M[i] = 0.5 * (L[i] + R[i]); S[i] = 0.5 * (L[i] - R[i]); }
        return {amp(M, from, n, hz), amp(S, from, n, hz)};
    }
}

TEST(Imager_at_its_defaults_is_the_input)
{
    // (IM4): width 100 %, mono off — the input, to the bit
    configure48();
    Imager m;
    std::vector<Sample> L, R, unused;
    tones(L, unused, 4800, {{300.0, 0.5}});
    tones(R, unused, 4800, {{700.0, 0.4}});
    const std::vector<Sample> L0 = L, R0 = R;
    blocks(m, L, R);
    CHECK(L == L0 && R == R0);
}

TEST(Imager_width_scales_the_side)
{
    // (IM1), (IM3)–(IM4): width 0 — both sides (L + R)/2 exactly; 200 % — the side doubled, the mid kept (to 1e−15)
    configure48();
    std::vector<Sample> L, R, unused;
    tones(L, unused, 4800, {{300.0, 0.5}});
    tones(R, unused, 4800, {{700.0, 0.4}});
    const std::vector<Sample> L0 = L, R0 = R;
    Imager narrow;
    narrow.setWidth(0.0);
    blocks(narrow, L, R);
    bool mono = true;
    for (size_t i = 0; i < L.size(); ++i) mono &= L[i] == R[i] && L[i] == (Sample)(0.5 * ((double)L0[i] + (double)R0[i]));
    CHECK(mono);
    L = L0;
    R = R0;
    Imager wide;
    wide.setWidth(200.0);
    blocks(wide, L, R);
    double off = 0;
    for (size_t i = 0; i < L.size(); ++i)
    {
        off = std::max(off, std::fabs((L[i] - R[i]) - 2.0 * (L0[i] - R0[i])));
        off = std::max(off, std::fabs((L[i] + R[i]) - (L0[i] + R0[i])));
    }
    CHECK(off < 1e-15);
}

TEST(Imager_mono_below_its_frequency)
{
    // (IM2): mono below 120 Hz — a 50 Hz tone in the left only: its side comes out |HP(50)|² of itself (two Butterworth
    // high-passes: −30 dB), its mid whole (the all-pass); a 3 kHz one keeps its side (|HP(3 k)|² ≈ 1); both to 0.01 dB
    configure48();
    for (double hz : {50.0, 3000.0})
    {
        Imager m;
        m.setMono(true);
        m.setMonoHz(120.0);
        std::vector<Sample> L, R, unused;
        tones(L, unused, 48000, {{hz, 0.5}});
        R.assign(L.size(), 0.0);
        blocks(m, L, R);
        const auto ms = midSide(L, R, 24000, 24000, hz);
        const double hp = std::norm(sectionAt(Biquad::HighPass, 120.0, hz)); // |H|² — two sections
        const double sideDb = db(ms.second / 0.25), wantSide = db(hp), midDb = db(ms.first / 0.25);
        std::printf("    %.0f Hz in the left: side %.2f dB (want %.2f), mid %.3f dB\n", hz, sideDb, wantSide, midDb);
        CHECK(std::fabs(sideDb - wantSide) < 0.01 && std::fabs(midDb) < 0.01);
    }
    // the mid's all-pass and the side's high-pass agree in phase above the frequency: a 3 kHz tone hard left stays left
    Imager m;
    m.setMono(true);
    std::vector<Sample> L, R, unused;
    tones(L, unused, 48000, {{3000.0, 0.5}});
    R.assign(L.size(), 0.0);
    blocks(m, L, R);
    const double leak = db(amp(R, 24000, 24000, 3000.0) / 0.5);
    std::printf("    3 kHz hard left, mono below 120 Hz: the right at %.1f dB\n", leak);
    CHECK(leak < -60.0);
}

TEST(Imager_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    auto run = [](int block) {
        Imager m;
        m.setMono(true);
        m.setWidth(150.0);
        std::vector<Sample> L, R, unused;
        tones(L, unused, 9600, {{60.0, 0.5}, {2500.0, 0.2}});
        tones(R, unused, 9600, {{90.0, 0.3}});
        blocks(m, L, R, block);
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
    Imager m;
    m.setMono(true);
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    m.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

namespace
{
    std::vector<float> noise(size_t n, unsigned seed, double a = 0.5)
    {
        std::vector<float> v(n);
        for (auto &x : v) { seed = seed * 1103515245u + 12345u; x = (float)(a * ((double)(seed >> 8) / 16777216.0 - 0.5) * 2.0); }
        return v;
    }
}

TEST(Convolver_is_the_convolution)
{
    // (CV1)–(CV6): a stereo response of 3000 taps (unit energy made), a noise through it — every sample is the direct
    // sum Σ h_n[k]·x[n − 256 − k], each channel its own response, to 1e−9; latency() 256
    configure48();
    const int taps = 3000, n = 12000;
    const auto hl = noise(taps, 7), hr = noise(taps, 11);
    std::vector<float> ir((size_t)taps * 2);
    for (int i = 0; i < taps; ++i) { ir[(size_t)(2 * i)] = hl[(size_t)i]; ir[(size_t)(2 * i + 1)] = hr[(size_t)i]; }
    Convolver cv;
    cv.setSample(ir.data(), taps, 2, 48000.0);
    cv.setMix(1.0);
    CHECK(cv.latency() == 256 && cv.responseFrames() == taps);
    const auto xl = noise(n, 3), xr = noise(n, 5);
    std::vector<Sample> L(xl.begin(), xl.end()), R(xr.begin(), xr.end());
    blocks(cv, L, R, 100);
    double el = 0, er = 0;
    for (float v : hl) el += (double)v * v;
    for (float v : hr) er += (double)v * v;
    const double g = 1.0 / std::sqrt(std::max(el, er)); // the louder channel's energy made 1
    double worst = 0;
    for (int i = 256; i < n; i += 7)
    {
        double wl = 0, wr = 0;
        for (int k = 0; k < taps && i - 256 - k >= 0; ++k)
        {
            wl += g * hl[(size_t)k] * xl[(size_t)(i - 256 - k)];
            wr += g * hr[(size_t)k] * xr[(size_t)(i - 256 - k)];
        }
        worst = std::max({worst, std::fabs(L[(size_t)i] - wl), std::fabs(R[(size_t)i] - wr)});
    }
    std::printf("    3000 taps, stereo: off the direct sum by %.2g\n", worst);
    CHECK(worst < 1e-9);
}

TEST(Convolver_dry_and_without_a_response)
{
    // (CV6): mix 0 — the input exactly 256 samples late; no response — the wet silent, the dry (1 − mix) of the input,
    // 256 late
    configure48();
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{440.0, 0.5}, {3000.0, 0.2}});
    const std::vector<Sample> X = L;
    const auto ir = noise(500, 9);
    Convolver d;
    d.setSample(ir.data(), 500, 1, 48000.0);
    d.setMix(0.0);
    blocks(d, L, R);
    bool late = true;
    for (int i = 0; i < 4800; ++i) late &= L[(size_t)i] == (i < 256 ? 0.0 : X[(size_t)(i - 256)]);
    CHECK(late);
    Convolver none;
    L = X;
    R = X;
    blocks(none, L, R);
    double off = 0;
    for (int i = 256; i < 4800; ++i) off = std::max(off, std::fabs(L[(size_t)i] - 0.7 * X[(size_t)(i - 256)]));
    CHECK(off < 1e-15 && none.responseFrames() == 0);
}

TEST(Convolver_reads_its_response_at_its_rate)
{
    // (CV1): a response recorded at 24 kHz — one impulse at its sample 100 — heard at 48 kHz at sample 200 (resampled,
    // band-limited); a mono response serves both channels; one longer than 10 s cut there
    configure48();
    std::vector<float> ir(400, 0.0f);
    ir[100] = 1.0f;
    Convolver cv;
    cv.setSample(ir.data(), 400, 1, 24000.0);
    cv.setMix(1.0);
    std::vector<Sample> L(2048, 0.0), R(2048, 0.0);
    L[0] = R[0] = 1.0;
    blocks(cv, L, R);
    size_t at = 0;
    for (size_t i = 0; i < L.size(); ++i) if (std::fabs(L[i]) > std::fabs(L[at])) at = i;
    std::printf("    a 24 kHz impulse at 100: heard at %zu (256 + 200), %zu frames at 48 kHz\n", at, (size_t)cv.responseFrames());
    CHECK(at == 456 && cv.responseFrames() == 800 && L == R);
    std::vector<float> longest((size_t)(11 * 48000), 0.0f);
    longest[0] = 1.0f;
    Convolver big;
    big.setSample(longest.data(), (long long)longest.size(), 1, 48000.0);
    CHECK(big.responseFrames() == 480000);
}

TEST(Convolver_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    const auto ir = noise(5000, 21);
    auto run = [&](int block) {
        Convolver cv;
        cv.setSample(ir.data(), 2500, 2, 48000.0);
        std::vector<Sample> L, R;
        tones(L, R, 9600, {{220.0, 0.5}, {2500.0, 0.2}});
        blocks(cv, L, R, block);
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
    Convolver cv;
    cv.setSample(ir.data(), 5000, 1, 48000.0);
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    cv.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

TEST(Convolution_device_takes_its_response)
{
    // REQ-fx-convolution-1 through the registry: "convolution" takes a sample — a response of one impulse (unit energy
    // already) at mix 1 is the input exactly 256 samples late, through the Device face
    configure48();
    const DeviceType *t = DeviceRegistry::find("convolution");
    CHECK(t && t->takesSample && t->kind == DeviceKind::Effect);
    auto d = DeviceRegistry::create("convolution");
    const float ir[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    d->setSample(ir, 4, 1, 48000.0);
    CHECK(d->setParam("mix", 1.0));
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{440.0, 0.5}});
    const std::vector<Sample> X = L;
    for (size_t at = 0; at < L.size(); at += 128)
    {
        Sample *io[2] = {L.data() + at, R.data() + at};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - at));
    }
    double off = 0;
    for (int i = 256; i < 4800; ++i) off = std::max(off, std::fabs(L[(size_t)i] - X[(size_t)(i - 256)]));
    CHECK(off < 1e-12 && d->latency() == 256);
}

namespace
{
    /** The share of a signal's power within ±band Hz of `hz`, by a Hann-windowed DFT over [from, from + n). */
    double powerNear(const std::vector<Sample> &y, int from, int n, double hz, double band)
    {
        double near = 0, all = 0;
        const double df = 48000.0 / n;
        for (int k = 1; k < n / 2; ++k)
        {
            const double f = k * df;
            if (f > 20000.0) break;
            double re = 0, im = 0;
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos(kTau * i / n), a = kTau * k * i / n;
                re += w * y[(size_t)(from + i)] * std::cos(a);
                im += w * y[(size_t)(from + i)] * std::sin(a);
            }
            const double p = re * re + im * im;
            all += p;
            if (std::fabs(f - hz) <= band) near += p;
        }
        return near / all;
    }
}

TEST(PitchShifter_moves_a_tone)
{
    // (PS1)–(PS4): a 440 Hz sine, 40 ms window — +12 st: its power gathers at 880 Hz (within the window's sidebands,
    // ±2/T, T = W/|1 − r|), −12 st at 220, +7 st at 659.3; next to none left at 440
    configure48();
    for (double st : {12.0, -12.0, 7.0})
    {
        PitchShifter p;
        p.setSemitones(st);
        std::vector<Sample> L, R;
        tones(L, R, 48000, {{440.0, 0.5}});
        blocks(p, L, R);
        const double r = std::exp2(st / 12.0), to = 440.0 * r, side = 2.0 * std::fabs(1.0 - r) / 0.040;
        const double there = powerNear(L, 24000, 8192, to, side + 12.0), left = powerNear(L, 24000, 8192, 440.0, 12.0);
        std::printf("    %+.0f st: %.1f %% of the power within %.0f Hz of %.1f Hz; %.2f %% left at 440\n", st, 100 * there, side + 12.0, to, 100 * left);
        CHECK(there > 0.9 && left < 0.01);
    }
}

TEST(PitchShifter_sweeps_its_taps)
{
    // (PS2): φ moves (1 − r)/W a sample — +5 st with a 40 ms window (W = 1920) after 1000 samples is frac(1000·(1 − r)/W)
    configure48();
    PitchShifter p;
    p.setSemitones(5.0);
    std::vector<Sample> L(1000, 0.0), R(1000, 0.0);
    blocks(p, L, R);
    const double r = std::exp2(5.0 / 12.0), want = 1000.0 * (1.0 - r) / 1920.0;
    CHECK(std::fabs(p.phase() - (want - std::floor(want))) < 1e-9 && p.latency() == 961);
}

TEST(PitchShifter_unshifted_is_the_input_late)
{
    // (PS4)–(PS5): at no shift the output is the input latency() samples late, to the bit — as the delay line keeps
    // it (arstroFlush: under 1e−15, 0) — at any mix; a window of 10 ms: latency 241
    configure48();
    for (double mix : {1.0, 0.4})
    {
        PitchShifter p;
        p.setMix(mix);
        std::vector<Sample> L, R;
        tones(L, R, 4800, {{440.0, 0.5}, {3000.0, 0.2}});
        const std::vector<Sample> X = L;
        blocks(p, L, R);
        bool late = true;
        for (int i = 0; i < 4800; ++i)
        {
            const double d = i < 961 ? 0.0 : arstroFlush(X[(size_t)(i - 961)]);
            late &= L[(size_t)i] == (Sample)(mix * d + (1.0 - mix) * d);
        }
        CHECK(late);
    }
    // shifted, then shifted no more: the window's phase is left mid-way, yet the output is the input late again,
    // exactly — not the two taps frozen where they stood (a comb)
    PitchShifter b;
    b.setSemitones(5.0);
    std::vector<Sample> L, R;
    tones(L, R, 3000, {{440.0, 0.5}});
    blocks(b, L, R);
    b.setSemitones(0.0);
    tones(L, R, 4800, {{440.0, 0.5}, {3000.0, 0.2}});
    const std::vector<Sample> X = L;
    blocks(b, L, R);
    bool back = b.phase() > 0.01;
    for (int i = 961; i < 4800; ++i) back &= L[(size_t)i] == arstroFlush(X[(size_t)(i - 961)]);
    CHECK(back);
    PitchShifter s;
    s.setWindowMs(10.0);
    CHECK(s.latency() == 241);
}

TEST(PitchShifter_crossfades_without_a_click)
{
    // (PS3): the two weights sum to 1 — a steady level comes out that level (DC 0.5, +7 st, to 1e−12); the taps half a
    // window apart, each jumping back only at its weight's zero — a sine shifted has no step larger than 3× its own slope
    configure48();
    PitchShifter p;
    p.setSemitones(7.0);
    std::vector<Sample> L(24000, 0.5), R(24000, 0.5);
    blocks(p, L, R);
    double off = 0;
    for (int i = 4000; i < 24000; ++i) off = std::max(off, std::fabs(L[(size_t)i] - 0.5));
    PitchShifter q;
    q.setSemitones(7.0);
    std::vector<Sample> M, N;
    tones(M, N, 48000, {{220.0, 0.5}});
    blocks(q, M, N);
    double step = 0;
    for (int i = 2001; i < 48000; ++i) step = std::max(step, std::fabs(M[(size_t)i] - M[(size_t)(i - 1)]));
    const double slope = 0.5 * kTau * 220.0 * std::exp2(7.0 / 12.0) / 48000.0;
    std::printf("    DC through +7 st: off by %.2g; a 220 Hz sine's largest step %.4f (its shifted slope %.4f)\n", off, step, slope);
    CHECK(off < 1e-12 && step < 3.0 * slope);
}

TEST(PitchShifter_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    auto run = [](int block) {
        PitchShifter p;
        p.setSemitones(-5);
        p.setCents(30);
        std::vector<Sample> L, R;
        tones(L, R, 9600, {{220.0, 0.5}, {2500.0, 0.2}});
        blocks(p, L, R, block);
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
    PitchShifter p;
    p.setSemitones(3);
    std::vector<Sample> L, R;
    tones(L, R, 4096, {{220.0, 0.5}});
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    p.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}

namespace
{
    /** Run a vocoder over a modulator (both channels) with a carrier as its key, in blocks of 128. */
    std::vector<Sample> vocode(Vocoder &v, std::vector<Sample> mod, const std::vector<Sample> &car)
    {
        std::vector<Sample> R = mod;
        for (size_t at = 0; at < mod.size(); at += 128)
        {
            const int n = (int)std::min<size_t>(128, mod.size() - at);
            const Sample *key[1] = {car.data() + at};
            v.setKey(key, 1);
            Sample *io[2] = {mod.data() + at, R.data() + at};
            v.process(io, 2, n);
        }
        return mod;
    }
    std::vector<Sample> sines(int frames, const std::vector<std::pair<double, double>> &t)
    {
        std::vector<Sample> L, R;
        tones(L, R, frames, t);
        return L;
    }
}

TEST(Vocoder_without_a_key_is_silent)
{
    // (VO4)–(VO5): no carrier, mix 1 — nothing, exactly
    configure48();
    Vocoder v;
    std::vector<Sample> L, R;
    tones(L, R, 4800, {{440.0, 0.5}});
    blocks(v, L, R);
    double most = 0;
    for (Sample x : L) most = std::max(most, (double)std::fabs(x));
    CHECK(most == 0.0);
    // (VO5): mix 0 — the input, to the bit; mix ½ — half the vocoded and half the input
    const auto mod = sines(4800, {{700.0, 0.4}}), car = sines(4800, {{690.0, 0.3}});
    Vocoder dry, full, half;
    dry.setMix(0.0);
    half.setMix(0.5);
    const auto y0 = vocode(dry, mod, car), y1 = vocode(full, mod, car), yh = vocode(half, mod, car);
    double off = 0;
    for (size_t i = 0; i < mod.size(); ++i) off = std::max(off, std::fabs(yh[i] - (0.5 * y1[i] + 0.5 * mod[i])));
    CHECK(y0 == mod && off < 1e-15);
}

TEST(Vocoder_bands_are_log_spaced)
{
    // (VO1): centre b = lo·(hi/lo)^((b + ½)/N)
    configure48();
    Vocoder v;
    v.setBands(12);
    v.setLowHz(200);
    v.setHighHz(6400);
    double worst = 0;
    for (int b = 0; b < 12; ++b) worst = std::max(worst, std::fabs(v.centerHz(b) / (200.0 * std::pow(32.0, (b + 0.5) / 12.0)) - 1.0));
    CHECK(worst < 1e-12 && v.bands() == 12);
}

TEST(Vocoder_multiplies_its_two_sounds)
{
    // (VO2)–(VO4): the filters are linear and the follower homogeneous (env(k·x) = k·env(x)), so the output is bilinear —
    // the modulator doubled doubles it, the carrier doubled doubles it, to 1e−12 of its peak
    configure48();
    const auto mod = sines(24000, {{700.0, 0.3}, {2100.0, 0.2}}), car = sines(24000, {{110.0, 0.2}, {690.0, 0.2}, {2050.0, 0.2}});
    auto twice = [](std::vector<Sample> x) { for (auto &v : x) v *= 2.0; return x; };
    Vocoder a, b, c;
    const auto y = vocode(a, mod, car), ym = vocode(b, twice(mod), car), yc = vocode(c, mod, twice(car));
    double peak = 0, offM = 0, offC = 0;
    for (size_t i = 0; i < y.size(); ++i)
    {
        peak = std::max(peak, (double)std::fabs(y[i]));
        offM = std::max(offM, std::fabs(ym[i] - 2.0 * y[i]));
        offC = std::max(offC, std::fabs(yc[i] - 2.0 * y[i]));
    }
    std::printf("    peak %.4f; doubled modulator off by %.2g, doubled carrier off by %.2g\n", peak, offM, offC);
    CHECK(peak > 0.01 && offM < 1e-12 * peak && offC < 1e-12 * peak);
}

TEST(Vocoder_speaks_in_the_carriers_pitch)
{
    // (VO2)–(VO4): a modulator at 1 kHz alone; a carrier of tones at every band's centre — out comes the carrier's tone
    // in the modulator's band; two bands off, under it by what two sections give there (each 1/√(1 + Q²(r² − r⁻²)²):
    // 25 dB at 16 bands from 100 Hz to 8 kHz) less 3 dB for the follower's ripple; none of the modulator's own 1 kHz
    configure48();
    Vocoder v;
    int k = 0;
    for (int b = 0; b < v.bands(); ++b) if (std::fabs(std::log(v.centerHz(b) / 1000.0)) < std::fabs(std::log(v.centerHz(k) / 1000.0))) k = b;
    std::vector<std::pair<double, double>> parts;
    for (int b = 0; b < v.bands(); ++b) parts.push_back({v.centerHz(b), 0.1});
    const auto y = vocode(v, sines(48000, {{1000.0, 0.5}}), sines(48000, parts));
    const double there = amp(y, 24000, 24000, v.centerHz(k));
    double others = 0;
    for (int b = 0; b < v.bands(); ++b) if (std::abs(b - k) >= 2) others = std::max(others, amp(y, 24000, 24000, v.centerHz(b)));
    const double own = amp(y, 24000, 24000, 1000.0);
    const double r = std::pow(80.0, 1.0 / 16.0), q = std::sqrt(r) / (r - 1.0), x = r * r - 1.0 / (r * r);
    const double skirt = -2.0 * db(1.0 / std::sqrt(1.0 + q * q * x * x)); // two sections, two bands off
    std::printf("    band %d (%.0f Hz): %.4f; two bands off at most %.1f dB under (the skirts: %.1f); the modulator's 1 kHz %.1f dB under\n", k,
                v.centerHz(k), there, db(there / others), skirt, db(there / own));
    CHECK(db(there / others) > skirt - 3.0 && db(there / own) > 30.0);
}

TEST(Vocoder_follows_the_modulators_envelope)
{
    // (VO3): the modulator stops — the output falls with the release (50 ms): 250 ms on (five release constants), under
    // −40 dB of where it was
    configure48();
    Vocoder v;
    auto mod = sines(48000, {{1000.0, 0.5}});
    for (size_t i = 24000; i < mod.size(); ++i) mod[i] = 0.0;
    const auto y = vocode(v, mod, sines(48000, {{990.0, 0.3}}));
    const double before = amp(y, 19200, 4800, 990.0), after = amp(y, 36000, 4800, 990.0);
    std::printf("    at 990 Hz: %.4f while it speaks, %.1f dB 250 ms after it stops\n", before, db(after / before));
    CHECK(before > 0.01 && db(after / before) < -40.0);
}

TEST(Vocoder_same_in_any_blocks_and_does_not_allocate)
{
    configure48();
    const auto mod = sines(9600, {{300.0, 0.4}}), car = sines(9600, {{220.0, 0.3}, {330.0, 0.3}});
    auto run = [&](int block) {
        Vocoder v;
        std::vector<Sample> L = mod, R = mod;
        for (size_t at = 0; at < L.size(); at += (size_t)block)
        {
            const int n = (int)std::min<size_t>((size_t)block, L.size() - at);
            const Sample *key[1] = {car.data() + at};
            v.setKey(key, 1);
            Sample *io[2] = {L.data() + at, R.data() + at};
            v.process(io, 2, n);
        }
        return L;
    };
    const auto a = run(1), b = run(128), c = run(777);
    CHECK(a == b && a == c);
    Vocoder v;
    std::vector<Sample> L = mod, R = mod;
    const Sample *key[1] = {car.data()};
    v.setKey(key, 1);
    Sample *io[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    v.process(io, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0 && DeviceRegistry::find("vocoder")->takesKey);
}

namespace
{
    Tuner &listen(Tuner &t, std::vector<Sample> L)
    {
        std::vector<Sample> R = L;
        blocks(t, L, R);
        return t;
    }
    double centsOff(double got, double want) { return 1200.0 * std::log2(got / want); }
}

TEST(Tuner_reads_a_sine_to_a_cent)
{
    // (Y1)–(Y6): a sine from 55 Hz to 1760 Hz read to ±0.2 cent (the parabola on d itself: 0.06 at 1760 Hz); its note
    // the nearest and its cents what is left
    configure48();
    double worst = 0;
    for (double hz : {55.0, 82.41, 110.0, 220.0, 440.0, 445.0, 880.0, 1318.5, 1760.0})
    {
        Tuner t;
        listen(t, sines(24000, {{hz, 0.5}}));
        const double got = t.reading(Tuner::Frequency), semis = 12.0 * std::log2(hz / 440.0);
        worst = std::max(worst, std::fabs(centsOff(got, hz)));
        CHECK(t.reading(Tuner::Note) == 69.0 + std::round(semis));
        CHECK(std::fabs(t.reading(Tuner::Cents) - 100.0 * (semis - std::round(semis))) < 1.0);
    }
    std::printf("    55 … 1760 Hz: the worst %.3f cents\n", worst);
    CHECK(worst < 0.2);
}

TEST(Tuner_hears_the_fundamental)
{
    // (Y3): a 110 Hz sawtooth (30 harmonics at 1/h) and a tone whose fundamental is weak (0.05, its 2nd … 6th at 0.3) —
    // both read 110 Hz to a cent, not an octave or a fifth off
    configure48();
    std::vector<std::pair<double, double>> saw, weak{{110.0, 0.05}};
    for (int h = 1; h <= 30; ++h) saw.push_back({110.0 * h, 0.4 / h});
    for (int h = 2; h <= 6; ++h) weak.push_back({110.0 * h, 0.3});
    Tuner a, b;
    listen(a, sines(24000, saw));
    listen(b, sines(24000, weak));
    std::printf("    saw %.3f Hz, weak fundamental %.3f Hz\n", a.reading(Tuner::Frequency), b.reading(Tuner::Frequency));
    CHECK(std::fabs(centsOff(a.reading(Tuner::Frequency), 110.0)) < 1.0 && std::fabs(centsOff(b.reading(Tuner::Frequency), 110.0)) < 1.0);
}

TEST(Tuner_says_no_pitch_and_its_level)
{
    // (Y3), (Y5): silence and noise — no pitch (0); a sine at 0.5 — its level 20·log10(0.5/√2) = −9.03 dBFS (0.05)
    // (its window's: 2048 samples are not whole cycles of 440 Hz — 0.04 dB of that)
    configure48();
    Tuner q, n, s;
    listen(q, std::vector<Sample>(24000, 0.0));
    std::vector<Sample> hiss(24000);
    unsigned seed = 99;
    for (auto &x : hiss) { seed = seed * 1103515245u + 12345u; x = 0.5 * ((double)(seed >> 8) / 16777216.0 - 0.5); }
    listen(n, hiss);
    listen(s, sines(24000, {{440.0, 0.5}}));
    std::printf("    silence %.1f Hz, noise %.1f Hz; a 0.5 sine at %.3f dB\n", q.reading(Tuner::Frequency), n.reading(Tuner::Frequency), s.reading(Tuner::Level));
    CHECK(q.reading(Tuner::Frequency) == 0.0 && n.reading(Tuner::Frequency) == 0.0);
    // under −60 dBFS (a whisper): no pitch, though it is one; a tone in the right channel alone is heard (the mean)
    Tuner w, r;
    listen(w, sines(24000, {{440.0, 0.0003}}));
    std::vector<Sample> Lq(24000, 0.0), Rq = sines(24000, {{440.0, 0.5}});
    blocks(r, Lq, Rq);
    CHECK(w.reading(Tuner::Frequency) == 0.0 && std::fabs(centsOff(r.reading(Tuner::Frequency), 440.0)) < 0.2);
    CHECK(std::fabs(s.reading(Tuner::Level) - 20.0 * std::log10(0.5 / std::sqrt(2.0))) < 0.05);
}

TEST(Tuner_against_its_reference)
{
    // (Y6): A4 = 432 Hz — a 432 Hz sine is note 69, 0 cents; a 440 Hz one note 69, +31.8 cents (1200·log2(440/432))
    configure48();
    Tuner a, b;
    a.setReferenceHz(432.0);
    b.setReferenceHz(432.0);
    listen(a, sines(24000, {{432.0, 0.5}}));
    listen(b, sines(24000, {{440.0, 0.5}}));
    CHECK(a.reading(Tuner::Note) == 69.0 && std::fabs(a.reading(Tuner::Cents)) < 1.0);
    CHECK(b.reading(Tuner::Note) == 69.0 && std::fabs(b.reading(Tuner::Cents) - 1200.0 * std::log2(440.0 / 432.0)) < 1.0);
}

TEST(Tuner_passes_the_sound_untouched)
{
    // a meter: the audio out is the audio in, to the bit; the same readings in any blocks
    configure48();
    std::vector<Sample> L, R;
    tones(L, R, 9600, {{330.0, 0.5}, {990.0, 0.1}});
    const std::vector<Sample> X = L;
    double got[3];
    int k = 0;
    for (int block : {1, 128, 777})
    {
        Tuner t;
        std::vector<Sample> A = X, B = X;
        blocks(t, A, B, block);
        CHECK(A == X);
        got[k++] = t.reading(Tuner::Frequency);
    }
    CHECK(got[0] == got[1] && got[0] == got[2] && got[0] > 0.0);
}

TEST(Tuner_device_reads_through_its_face)
{
    // REQ-device-11: the registry's "tuner" names its readings; through the Device face, a 440 Hz sine reads 440 —
    // and a device that measures nothing reads 0; no allocation as it listens
    configure48();
    const DeviceType *t = DeviceRegistry::find("tuner");
    CHECK(t && t->readings.size() == 4 && t->readingIndex("frequency") == 0 && t->readingIndex("cents") == 2 && t->readingIndex("pitch") == -1);
    auto d = DeviceRegistry::create("tuner");
    std::vector<Sample> L, R;
    tones(L, R, 24000, {{440.0, 0.5}});
    gAllocs = 0;
    gCounting = true;
    for (size_t at = 0; at < L.size(); at += 128)
    {
        Sample *io[2] = {L.data() + at, R.data() + at};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - at));
    }
    gCounting = false;
    CHECK(gAllocs.load() == 0);
    CHECK(std::fabs(centsOff(d->reading(0), 440.0)) < 1.0 && d->reading(1) == 69.0);
    CHECK(DeviceRegistry::create("reverb")->reading(0) == 0.0 && DeviceRegistry::find("reverb")->readings.empty());
}
