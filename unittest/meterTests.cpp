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
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
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

TEST(Onsets_are_found_where_sounds_begin)
{
    // REQ-onset-1, (O1)–(O5): four decaying bursts over a quiet noise floor — each found within 64 samples; nothing in
    // the floor alone; two bursts closer than the gap counted once; a soft burst found at high sensitivity, not at low
    const double fs = 48000.0;
    const long long n = 4 * 48000;
    std::vector<float> x((size_t)n);
    unsigned seed = 7;
    auto noise = [&] { seed = seed * 1664525u + 1013904223u; return ((float)(seed >> 8) / 16777216.0f - 0.5f) * 0.002f; };
    for (auto &v : x) v = noise();
    auto burst = [&](long long at, double amp) {
        for (long long i = 0; i < 9600 && at + i < n; ++i)
            x[(size_t)(at + i)] += (float)(amp * std::exp(-i / 1500.0) * std::sin(2 * M_PI * 330.0 * i / fs));
    };
    const long long at[4] = {10000, 60000, 110000, 160000};
    for (long long a : at) burst(a, 0.6);
    const auto found = detectOnsets(x.data(), n, fs);
    CHECK(found.size() == 4);
    for (size_t k = 0; k < found.size() && k < 4; ++k) CHECK(std::llabs(found[k] - at[k]) <= 64);
    std::vector<float> quiet((size_t)n);
    seed = 7;
    for (auto &v : quiet) v = noise();
    CHECK(detectOnsets(quiet.data(), n, fs).empty());
    // two hits 20 ms apart: one onset (the gap is 50 ms)
    std::vector<float> two((size_t)n, 0.0f);
    for (long long i = 0; i < 4800; ++i)
    {
        two[(size_t)(20000 + i)] += (float)(0.6 * std::exp(-i / 800.0) * std::sin(2 * M_PI * 330.0 * i / fs));
        two[(size_t)(20960 + i)] += (float)(0.6 * std::exp(-i / 800.0) * std::sin(2 * M_PI * 330.0 * i / fs));
    }
    CHECK(detectOnsets(two.data(), n, fs).size() == 1);
    // a soft burst over a steady tone — its flux ~4 dB at most (the window spreads a rise over four hops): found when
    // sensitive (δ 3 dB), not when not (δ 15 dB)
    std::vector<float> soft((size_t)n, 0.0f);
    for (long long i = 0; i < n; ++i) soft[(size_t)i] = (float)(0.02 * std::sin(2 * M_PI * 110.0 * i / fs));
    for (long long i = 0; i < 4800; ++i) soft[(size_t)(96000 + i)] += (float)(0.05 * std::exp(-i / 1500.0) * std::sin(2 * M_PI * 2000.0 * i / fs));
    OnsetParams hi, lo;
    hi.sensitivity = 1.0;
    lo.sensitivity = 0.0;
    const auto withHi = detectOnsets(soft.data(), n, fs, hi), withLo = detectOnsets(soft.data(), n, fs, lo);
    bool hiFound = false, loFound = false;
    for (long long v : withHi) hiFound |= std::llabs(v - 96000) < 512;
    for (long long v : withLo) loFound |= std::llabs(v - 96000) < 512;
    CHECK(hiFound && !loFound);
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

namespace
{
    /** A groove: a kick on `kicks`, a snare on `snares`, a closed hat every eighth — `beats` long at `bpm`, cut to `seconds`. */
    std::vector<float> groove(double bpm, double seconds, const std::vector<int> &kicks, const std::vector<int> &snares, int barBeats = 4)
    {
        const double fs = 48000.0, spb = 60.0 / bpm * fs;
        const long long n = (long long)std::llround(seconds * fs);
        std::vector<float> x((size_t)n, 0.0f);
        unsigned seed = 11;
        auto noise = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f - 0.5f; };
        auto hit = [&](double at, int kind) {
            const long long a = (long long)std::llround(at);
            for (long long i = 0; i < 12000 && a + i < n; ++i)
            {
                double v = 0;
                if (kind == 0) v = 0.8 * std::exp(-i / 3000.0) * std::sin(2 * M_PI * (60.0 + 80.0 * std::exp(-i / 400.0)) * i / fs);
                else if (kind == 1) v = 0.5 * std::exp(-i / 1500.0) * noise();
                else if (i < 2400) v = 0.15 * std::exp(-i / 300.0) * noise();
                x[(size_t)(a + i)] += (float)v;
            }
        };
        for (int beat = 0; beat * spb < n; ++beat)
        {
            const int inBar = beat % barBeats;
            if (std::find(kicks.begin(), kicks.end(), inBar) != kicks.end()) hit(beat * spb, 0);
            if (std::find(snares.begin(), snares.end(), inBar) != snares.end()) hit(beat * spb, 1);
            hit(beat * spb, 2);
            hit((beat + 0.5) * spb, 2);
        }
        return x;
    }
}

TEST(Tempo_is_found)
{
    // REQ-tempo-1, (B1)–(B5): a four-on-the-floor groove at 133 bpm (between two lags) cut to no whole beat — within
    // 0.3 bpm; a loop of
    // four bars at 97 bpm (cut to the sample) — 97 to its length's sample (snapped to its whole beats); a backbeat at
    // 90 — 90, not 45 or 180; a steady tone — none
    const double fs = 48000.0;
    const auto a = groove(133.0, 7.3, {0, 1, 2, 3}, {1, 3});
    const double ta = estimateTempo(a.data(), (long long)a.size(), fs);
    const auto b = groove(97.0, 16 * 60.0 / 97.0, {0, 2}, {1, 3});
    const double tb = estimateTempo(b.data(), (long long)b.size(), fs);
    const auto c = groove(90.0, 8.0, {0, 2}, {1, 3});
    TempoParams noSnap;
    noSnap.snap = 0.0;
    const double tc = estimateTempo(c.data(), (long long)c.size(), fs, noSnap);
    std::vector<float> tone(4 * 48000);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = (float)(0.3 * std::sin(2 * M_PI * 220.0 * (double)i / fs));
    const double td = estimateTempo(tone.data(), (long long)tone.size(), fs);
    const double tbFree = estimateTempo(b.data(), (long long)b.size(), fs, noSnap);
    std::printf("    the 97 loop unsnapped: %.3f\n", tbFree);
    std::printf("    133 → %.3f, 97 (4 bars) → %.6f, 90 → %.3f, a tone → %.1f\n", ta, tb, tc, td);
    CHECK(std::fabs(ta - 133.0) < 0.3);
    CHECK(std::fabs(tb - 97.0) < 1e-4);
    CHECK(std::fabs(tc - 90.0) < 1.0);
    CHECK(td == 0.0);
}
