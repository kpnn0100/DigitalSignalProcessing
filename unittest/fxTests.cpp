// N34's effects (Solaris R-FX-6…17) — src/effects/README.md, each its Math section.
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <atomic>
#include <cmath>
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
