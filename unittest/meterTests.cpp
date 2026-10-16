/*
 *  Arstro DSP — the meters (REQ-meter-1…4): loudness by BS.1770-4, true peak, correlation, a spectrum.
 *
 *  Judged by what they read off signals whose readings are known in closed form — a sine's loudness and its
 *  peak between samples, the gates of the integrated loudness, two channels in and out of phase — and by the
 *  standard's own filter table. No main(): registration is shared with synthTests.cpp through MiniTest.
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include "../src/meter/Meters.h"
#include <atomic>
#include <cmath>
#include <vector>

using namespace arstro;

extern std::atomic<long> gAllocs; // instrumentTests.cpp's counting allocator
extern std::atomic<bool> gCounting;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    void configure()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }
    /** A sine block by block, as a host would feed it. */
    template <class M> void feedSine(M &m, double hz, double ampL, double ampR, double seconds, double phase = 0.0, double phaseR = 0.0)
    {
        const int fs = 48000, block = 128;
        std::vector<float> l(block), r(block);
        const long long n = (long long)(seconds * fs);
        for (long long i = 0; i < n; i += block)
        {
            const int k = (int)std::min<long long>(block, n - i);
            for (int j = 0; j < k; ++j)
            {
                const double t = (double)(i + j) / fs;
                l[(size_t)j] = (float)(ampL * std::sin(2.0 * kPi * hz * t + phase));
                r[(size_t)j] = (float)(ampR * std::sin(2.0 * kPi * hz * t + phase + phaseR));
            }
            m.process(l.data(), r.data(), k);
        }
    }
}

TEST(KWeighting_at_48k_is_BS1770s_table)
{
    // ITU-R BS.1770-4 Table 1 and 2: the pre-filter and the RLB high-pass at 48 kHz
    KWeighting k;
    k.design(48000.0);
    CHECK_NEAR(k.shelf.b0, 1.53512485958697, 1e-9);
    CHECK_NEAR(k.shelf.b1, -2.69169618940638, 1e-9);
    CHECK_NEAR(k.shelf.b2, 1.19839281085285, 1e-9);
    CHECK_NEAR(k.shelf.a1, -1.69065929318241, 1e-9);
    CHECK_NEAR(k.shelf.a2, 0.73248077421585, 1e-9);
    CHECK_NEAR(k.highPass.a1, -1.99004745483398, 1e-9);
    CHECK_NEAR(k.highPass.a2, 0.99007225036621, 1e-9);
}

TEST(Loudness_of_a_sine_is_its_level)
{
    configure();
    // BS.1770-4: a 0 dBFS 1 kHz sine in one channel reads −3.01 LKFS; in both, 0. Half its amplitude, 6.02 dB less.
    LoudnessMeter both;
    feedSine(both, 1000.0, 0.5, 0.5, 5.0);
    CHECK_NEAR(both.integrated(), -6.02, 0.05);
    CHECK_NEAR(both.momentary(), -6.02, 0.05);
    CHECK_NEAR(both.shortTerm(), -6.02, 0.05);
    LoudnessMeter left;
    feedSine(left, 1000.0, 1.0, 0.0, 5.0);
    CHECK_NEAR(left.integrated(), -3.01, 0.05);
    LoudnessMeter quiet;
    CHECK(quiet.integrated() == LoudnessMeter::kSilence && quiet.momentary() == LoudnessMeter::kSilence);
}

TEST(Loudness_is_gated_absolutely_and_relatively)
{
    configure();
    // 10 s at −20 LUFS then 10 s of silence: the absolute gate (−70) leaves the silence out
    const double a20 = 0.5 * std::pow(10.0, (-20.0 + 6.02) / 20.0); // the amplitude that reads −20 LUFS in both channels
    LoudnessMeter m;
    feedSine(m, 1000.0, a20, a20, 10.0);
    feedSine(m, 1000.0, 0.0, 0.0, 10.0);
    CHECK_NEAR(m.integrated(), -20.0, 0.1);
    // then 10 s at −40: the relative gate (−10 LU under the ungated mean, ≈ −33) leaves it out too
    feedSine(m, 1000.0, a20 * 0.1, a20 * 0.1, 10.0);
    CHECK_NEAR(m.integrated(), -20.0, 0.1);
    CHECK(m.shortTerm() < -39.0); // the last 3 s are the quiet part
}

TEST(TruePeak_finds_the_peak_between_samples)
{
    configure();
    // a full-scale sine at a quarter of the rate, 45° out: every sample is ±0.707 (−3 dB) — the wave reaches 1.0
    TruePeakMeter t;
    feedSine(t, 12000.0, 1.0, 1.0, 0.5, kPi / 4.0);
    CHECK(t.peakDb() > -0.3 && t.peakDb() < 0.3);
    // a slow sine reads its own amplitude
    TruePeakMeter slow;
    feedSine(slow, 997.0, 0.5, 0.5, 0.5);
    CHECK_NEAR(slow.peakDb(), -6.02, 0.05);
}

TEST(Correlation_is_one_minus_one_and_zero)
{
    configure();
    CorrelationMeter same, flipped, quad;
    feedSine(same, 440.0, 0.5, 0.5, 2.0);
    feedSine(flipped, 440.0, 0.5, -0.5, 2.0);
    feedSine(quad, 440.0, 0.5, 0.5, 2.0, 0.0, kPi / 2.0);
    CHECK(same.correlation() > 0.99);
    CHECK(flipped.correlation() < -0.99);
    CHECK(std::fabs(quad.correlation()) < 0.05);
}

TEST(Spectrum_puts_a_sine_in_its_band)
{
    configure();
    SpectrumAnalyser s;
    feedSine(s, 1000.0, 0.5, 0.5, 3.0);
    CHECK_NEAR(SpectrumAnalyser::bandHz(34), 1000.0, 1e-9);
    CHECK_NEAR(s.bandDb(34), -6.02, 1.0);                       // its band reads its level
    CHECK(s.bandDb(22) < s.bandDb(34) - 20.0);                  // two octaves down
    CHECK(s.bandDb(46) < s.bandDb(34) - 20.0);                  // two octaves up
    CHECK(s.bandDb(SpectrumAnalyser::kBands - 1) <= -100.0 || SpectrumAnalyser::bandHz(SpectrumAnalyser::kBands - 1) < 21600.0);
}

TEST(Meters_do_not_allocate_after_construction)
{
    configure();
    LoudnessMeter lm;
    TruePeakMeter tp;
    CorrelationMeter cm;
    SpectrumAnalyser sa;
    std::vector<float> l(256, 0.25f), r(256, -0.25f);
    gAllocs = 0;
    gCounting = true;
    for (int k = 0; k < 400; ++k) // ~2 s: sub-blocks, gating blocks, the histogram
    {
        lm.process(l.data(), r.data(), 256);
        tp.process(l.data(), r.data(), 256);
        cm.process(l.data(), r.data(), 256);
        sa.process(l.data(), r.data(), 256);
    }
    (void)lm.integrated();
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}
