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
