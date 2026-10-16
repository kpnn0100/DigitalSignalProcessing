/*
 *  Arstro DSP — the spectral module (REQ-fft-1, REQ-stretch-1…3, REQ-resample-1): the FFT against the DFT it
 *  computes; a sound stretched, its pitch kept (a sine's frequency to 0.2 %, its level to 0.5 dB); its pitch moved,
 *  its length kept; attacks kept where they begin and not smeared ahead of themselves; the same samples twice; a
 *  resampler that passes what it should and nothing it should not. No main(): registration is shared through MiniTest.
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace arstro;

namespace
{
    constexpr double kFs = 48000.0;
    std::vector<float> sine(double hz, double amp, long long n)
    {
        std::vector<float> x((size_t)n);
        for (long long i = 0; i < n; ++i) x[(size_t)i] = (float)(amp * std::sin(2 * M_PI * hz * i / kFs));
        return x;
    }
    /** A frequency from upward zero crossings, each placed between its two samples. */
    double freqOf(const std::vector<float> &x, long long from, long long to)
    {
        double first = -1, last = -1;
        int count = 0;
        for (long long i = std::max(1LL, from); i < to && i < (long long)x.size(); ++i)
            if (x[(size_t)i - 1] < 0.0f && x[(size_t)i] >= 0.0f)
            {
                const double t = (i - 1) + x[(size_t)i - 1] / (x[(size_t)i - 1] - x[(size_t)i]);
                if (first < 0) first = t;
                last = t;
                ++count;
            }
        return count > 1 ? (count - 1) * kFs / (last - first) : 0.0;
    }
    double rmsOf(const std::vector<float> &x, long long from, long long to)
    {
        double e = 0;
        for (long long i = from; i < to; ++i) e += (double)x[(size_t)i] * x[(size_t)i];
        return std::sqrt(e / (double)(to - from));
    }
    /** Decaying bursts, sudden at each of `at`. */
    std::vector<float> bursts(const std::vector<long long> &at, long long n)
    {
        std::vector<float> x((size_t)n, 0.0f);
        for (long long a : at)
            for (long long i = 0; i < 7200 && a + i < n; ++i)
                x[(size_t)(a + i)] += (float)(0.6 * std::exp(-i / 1200.0) * std::sin(2 * M_PI * 520.0 * i / kFs));
        return x;
    }
}

TEST(FFT_is_the_DFT)
{
    // REQ-fft-1, (F1)–(F3): 64 points against the sum it is, and back
    const int N = 64;
    std::vector<std::complex<double>> x((size_t)N), X;
    for (int n = 0; n < N; ++n) x[(size_t)n] = {std::sin(0.37 * n) + 0.25 * std::cos(1.9 * n * n), 0.5 * std::cos(0.11 * n)};
    X = x;
    FFT fft(N);
    fft.forward(X.data());
    double worst = 0;
    for (int k = 0; k < N; ++k)
    {
        std::complex<double> s = 0;
        for (int n = 0; n < N; ++n) s += x[(size_t)n] * std::polar(1.0, -2 * M_PI * k * n / N);
        worst = std::max(worst, std::abs(s - X[(size_t)k]));
    }
    CHECK(worst < 1e-9);
    fft.inverse(X.data());
    double back = 0;
    for (int n = 0; n < N; ++n) back = std::max(back, std::abs(X[(size_t)n] - x[(size_t)n]));
    CHECK(back < 1e-12);
    bool refused = false;
    try { FFT bad(48); } catch (const std::invalid_argument &) { refused = true; }
    CHECK(refused);
}

TEST(Stretch_at_one_gives_the_sound_back)
{
    // REQ-stretch-1, (P7): β = 1 — the phases travel exactly as they were, so the sound comes back to rounding
    std::vector<std::vector<float>> in(2, std::vector<float>(48000));
    for (long long i = 0; i < 48000; ++i)
    {
        in[0][(size_t)i] = (float)(0.3 * std::sin(2 * M_PI * 233.0 * i / kFs) + 0.2 * std::sin(2 * M_PI * 3100.0 * i / kFs));
        in[1][(size_t)i] = (float)(0.4 * std::sin(2 * M_PI * 97.0 * i / kFs + 0.3));
    }
    const auto out = timeStretch(in, kFs);
    CHECK(out.size() == 2 && out[0].size() == 48000 && out[1].size() == 48000);
    double worst = 0;
    for (int c = 0; c < 2; ++c)
        for (size_t i = 0; i < 48000; ++i) worst = std::max(worst, (double)std::fabs(out[(size_t)c][i] - in[(size_t)c][i]));
    std::printf("    at 1: worst difference %.2e\n", worst);
    CHECK(worst < 1e-5);
}

TEST(Stretch_keeps_the_pitch)
{
    // REQ-stretch-1, (P1)–(P4): a 440 Hz sine half again as long, and three fifths as long — 440 Hz still, its level kept
    const auto x = sine(440.0, 0.5, 48000);
    for (double ratio : {1.5, 0.6})
    {
        StretchParams p;
        p.ratio = ratio;
        const auto y = timeStretch({x}, kFs, p)[0];
        const long long n = (long long)y.size();
        const double f = freqOf(y, n / 5, 4 * n / 5), level = 20 * std::log10(rmsOf(y, n / 5, 4 * n / 5) / (0.5 / std::sqrt(2.0)));
        std::printf("    ×%.2f: %lld frames, %.3f Hz, level %+.3f dB\n", ratio, n, f, level);
        CHECK(n == std::llround(48000 * ratio));
        CHECK(std::fabs(f / 440.0 - 1.0) < 0.002);
        CHECK(std::fabs(level) < 0.5);
    }
}

TEST(Pitch_moves_and_the_length_stays)
{
    // REQ-stretch-2, (P8), (R1)–(R3): a fifth up and an octave down, the same second long
    const auto x = sine(440.0, 0.5, 48000);
    for (double s : {7.0, -12.0})
    {
        StretchParams p;
        p.semitones = s;
        const auto y = timeStretch({x}, kFs, p)[0];
        const double want = 440.0 * std::pow(2.0, s / 12.0);
        const double f = freqOf(y, 9600, 38400), level = 20 * std::log10(rmsOf(y, 9600, 38400) / (0.5 / std::sqrt(2.0)));
        std::printf("    %+.0f st: %zu frames, %.3f Hz (want %.3f), level %+.3f dB\n", s, y.size(), f, want, level);
        CHECK(y.size() == 48000);
        CHECK(std::fabs(f / want - 1.0) < 0.002);
        CHECK(std::fabs(level) < 0.5);
    }
}

TEST(Transients_stay_where_they_begin)
{
    // REQ-stretch-3, (P6): bursts stretched twice as long begin at twice their time (found within 1 ms), and nothing of
    // an attack is heard ahead of it — the 10 ms before each silent (below −60 dB of the burst), where a vocoder with
    // no time map spreads it there (above −30 dB)
    const std::vector<long long> at = {4800, 19200, 33600, 43200};
    const auto x = bursts(at, 48000);
    StretchParams p, flat;
    p.ratio = flat.ratio = 2.0;
    flat.transients = -1.0;
    const auto y = timeStretch({x}, kFs, p)[0], z = timeStretch({x}, kFs, flat)[0];
    const auto found = detectOnsets(y.data(), (long long)y.size(), kFs);
    CHECK(found.size() == at.size());
    for (size_t k = 0; k < found.size() && k < at.size(); ++k)
    {
        std::printf("    burst %zu: at %lld, want %lld\n", k, found[k], 2 * at[k]);
        CHECK(std::llabs(found[k] - 2 * at[k]) <= 48);
    }
    auto ahead = [&](const std::vector<float> &v) {
        double pre = 0, body = 0;
        for (long long a : at)
        {
            const long long o = 2 * a;
            for (long long i = o - 480; i < o; ++i) pre += (double)v[(size_t)i] * v[(size_t)i];
            for (long long i = o; i < o + 4800; ++i) body += (double)v[(size_t)i] * v[(size_t)i];
        }
        return 10 * std::log10(pre / body + 1e-30);
    };
    const double withReset = ahead(y), without = ahead(z);
    std::printf("    ahead of the attacks: %.1f dB with the time map, %.1f dB without\n", withReset, without);
    CHECK(withReset < -60.0 && without > -30.0);
}

TEST(Stretch_is_the_same_twice)
{
    // R-RENDER-1's purity: no clock, no threads, no randomness
    const auto x = bursts({2400, 20000}, 24000);
    StretchParams p;
    p.ratio = 1.37;
    p.semitones = -3.0;
    CHECK(timeStretch({x}, kFs, p) == timeStretch({x}, kFs, p));
}

TEST(Resample_is_band_limited)
{
    // REQ-resample-1, (R1)–(R3): read every half sample, a 1 kHz sine is 500 Hz at its level; read every two, 5 kHz
    // passes (10 kHz) to 0.1 dB and 15 kHz — which would fold to 18 kHz — is gone below −60 dB
    const auto a = resample(sine(1000.0, 0.5, 24000), 0.5, 40000);
    CHECK(std::fabs(freqOf(a, 4000, 36000) / 500.0 - 1.0) < 1e-4);
    CHECK(std::fabs(20 * std::log10(rmsOf(a, 4000, 36000) / (0.5 / std::sqrt(2.0)))) < 0.05);
    const auto b = resample(sine(5000.0, 0.5, 48000), 2.0, 24000);
    CHECK(std::fabs(20 * std::log10(rmsOf(b, 2000, 22000) / (0.5 / std::sqrt(2.0)))) < 0.1);
    const auto c = resample(sine(15000.0, 0.5, 48000), 2.0, 24000);
    const double gone = 20 * std::log10(rmsOf(c, 2000, 22000) / (0.5 / std::sqrt(2.0)));
    std::printf("    15 kHz read every two: %.1f dB\n", gone);
    CHECK(gone < -60.0);
}

TEST(Stretch_follows_a_time_map)
{
    // REQ-stretch-4, (P9): a map in place of a ratio — the first half second as it was, the second twice as long (a
    // tempo halving there): 1.5 s out; a 440 Hz sine stays 440 Hz in both halves; bursts at 0.25 s and 0.75 s land at
    // 0.25 s and 1.0 s (within 1 ms), each attack whole
    StretchParams p;
    p.map = {{0.0, 0.0}, {24000.0, 24000.0}, {72000.0, 48000.0}};
    const auto y = timeStretch({sine(440.0, 0.5, 48000)}, kFs, p)[0];
    CHECK(y.size() == 72000);
    const double f1 = freqOf(y, 4800, 19200), f2 = freqOf(y, 31200, 64800);
    std::printf("    mapped: %.3f Hz then %.3f Hz\n", f1, f2);
    CHECK(std::fabs(f1 / 440.0 - 1.0) < 0.002 && std::fabs(f2 / 440.0 - 1.0) < 0.002);
    // between attacks the map's own shape: a sine swelling evenly from nothing (no attack — one long gap) is heard at
    // 0.25 of its level at 0.25 s and at 0.875 at 1.25 s (input 0.875 s) — a straight line would give 0.17 and 0.83
    std::vector<float> swell(48000);
    for (size_t i = 0; i < swell.size(); ++i) swell[i] = (float)((double)i / 48000.0 * std::sin(2 * M_PI * 440.0 * (double)i / kFs));
    const auto sw = timeStretch({swell}, kFs, p)[0];
    const double at1 = rmsOf(sw, 11520, 12480) * std::sqrt(2.0), at2 = rmsOf(sw, 59520, 60480) * std::sqrt(2.0);
    std::printf("    the swell heard at %.3f then %.3f (want 0.250, 0.875)\n", at1, at2);
    CHECK(std::fabs(at1 - 0.25) < 0.01 && std::fabs(at2 - 0.875) < 0.01);
    const auto z = timeStretch({bursts({12000, 36000}, 48000)}, kFs, p)[0];
    const auto found = detectOnsets(z.data(), (long long)z.size(), kFs);
    CHECK(found.size() == 2);
    if (found.size() == 2)
    {
        std::printf("    bursts at %lld and %lld (want 12000, 48000)\n", found[0], found[1]);
        CHECK(std::llabs(found[0] - 12000) <= 48 && std::llabs(found[1] - 48000) <= 48);
    }
}
