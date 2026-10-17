// A recorded sound as a cloud of grains (REQ-granular-1, -2) — src/instrument/README.md ## GranularSampler (G1)–(G5).
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
    /** A clean cloud: no spray, no spread, held at full, full velocity, 0 dB. */
    GranularSampler::Params plain()
    {
        GranularSampler::Params p;
        p.spray = 0.0;
        p.pspray = 0.0;
        p.spread = 0.0;
        p.attack = 0.0;
        p.release = 20.0;
        p.volumeDb = 0.0;
        p.velocity = 0.0;
        return p;
    }
    std::vector<Sample> play(GranularSampler &g, int note, int frames, int block = 128)
    {
        std::vector<Sample> L((size_t)frames, 0.0), R((size_t)frames, 0.0);
        g.noteOn(note, 127);
        for (int at = 0; at < frames; at += block)
        {
            const int n = std::min(block, frames - at);
            Sample *o[2] = {L.data() + at, R.data() + at};
            g.render(o, 2, n);
        }
        return L;
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
    std::vector<float> tone(double hz, int frames, double a = 0.5)
    {
        std::vector<float> x((size_t)frames);
        for (int i = 0; i < frames; ++i) x[(size_t)i] = (float)(a * std::sin(kTau * hz * i / 48000.0));
        return x;
    }
}

TEST(Granular_starts_grains_on_its_schedule)
{
    // (G1): 25 grains a second — 25 in a second, counted from the note; the same samples in blocks of 1, 128 and 777
    configure48();
    const auto snd = tone(220, 48000);
    auto run = [&](int block, long long *started) {
        GranularSampler g;
        GranularSampler::Params p;
        p.density = 25;
        g.setParams(p);
        g.setSample(snd.data(), 48000, 1, 48000);
        const auto y = play(g, 60, 48000, block);
        if (started) *started = g.grainsStarted(0);
        return y;
    };
    long long started = 0;
    const auto a = run(1, &started), b = run(128, nullptr), c = run(777, nullptr);
    double worst = 0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max({worst, std::fabs(a[i] - b[i]), std::fabs(a[i] - c[i])});
    std::printf("    %lld grains in a second at 25 a second; blocks of 1, 128, 777 apart by %.2g\n", started, worst);
    CHECK(started == 25);
    CHECK(worst == 0.0);
}

TEST(Granular_pitch_and_place)
{
    // (G2): a sound of 220 Hz then 440 Hz — the position chooses which (0.25: the first, 0.75: the second); a note 7
    // above its root plays it 2^(7/12) higher
    configure48();
    std::vector<float> snd = tone(220, 48000);
    const auto hi = tone(440, 48000);
    snd.insert(snd.end(), hi.begin(), hi.end());
    auto run = [&](double position, int note) {
        GranularSampler g;
        GranularSampler::Params p = plain();
        p.position = position;
        p.size = 200;
        p.density = 10;
        p.root = 60;
        g.setParams(p);
        g.setSample(snd.data(), (long long)snd.size(), 1, 48000);
        return play(g, note, 48000);
    };
    const auto a = run(0.25, 60), b = run(0.75, 60), up = run(0.25, 67);
    const double f = 220.0 * std::pow(2.0, 7.0 / 12.0);
    std::printf("    at 0.25: 220 Hz %.3f, 440 Hz %.4f; at 0.75: 440 Hz %.3f, 220 Hz %.4f; 7 up: %.2f Hz %.3f\n", amp(a, 4800, 38400, 220), amp(a, 4800, 38400, 440),
                amp(b, 4800, 38400, 440), amp(b, 4800, 38400, 220), f, amp(up, 4800, 38400, f));
    CHECK(amp(a, 4800, 38400, 220) > 0.1 && amp(a, 4800, 38400, 440) < 0.01);
    CHECK(amp(b, 4800, 38400, 440) > 0.1 && amp(b, 4800, 38400, 220) < 0.01);
    CHECK(amp(up, 4800, 38400, f) > 0.1 && amp(up, 4800, 38400, 220) < 0.01);
}

TEST(Granular_cloud_is_near_its_level_and_never_clicks)
{
    // (G3)–(G4): a steady sound (0.5, constant) in a frozen cloud — its mean 0.5 at 2.4 grains overlapping and at 12
    // (the overlap's mean divided out); each grain Hann-windowed: the cloud starts from nothing, no sample-to-sample
    // step larger than the window's own slope
    configure48();
    const std::vector<float> dc(48000, 0.5f);
    for (double density : {30.0, 150.0})
    {
        GranularSampler g;
        GranularSampler::Params p = plain();
        p.density = density;
        p.size = 80;
        g.setParams(p);
        g.setSample(dc.data(), 48000, 1, 48000);
        const auto y = play(g, 60, 24000);
        double mean = 0, step = 0;
        for (int i = 9600; i < 19200; ++i) mean += y[(size_t)i];
        mean /= 9600;
        for (size_t i = 1; i < y.size(); ++i) step = std::max(step, std::fabs(y[i] - y[i - 1]));
        std::printf("    %.0f a second (%.1f overlapping): mean %.3f, the first sample %.2g, the largest step %.4f\n", density, density * 0.08, mean, y[0], step);
        CHECK(std::fabs(mean - 0.5) < 0.03);
        CHECK(std::fabs(y[0]) < 1e-9);
        CHECK(step < 0.5 * kTau / (0.08 * 48000) * density * 0.08 + 1e-3); // each grain's Hann slope, summed over the overlap
    }
}

TEST(Granular_spray_is_the_same_every_render)
{
    // (G2): spray and spread — a different cloud from a plain one, the same every render (a counter, never a clock)
    configure48();
    const auto snd = tone(330, 48000);
    auto run = [&](double spray) {
        GranularSampler g;
        GranularSampler::Params p;
        p.spray = spray;
        g.setParams(p);
        g.setSample(snd.data(), 48000, 1, 48000);
        return play(g, 60, 24000);
    };
    const auto a = run(0.3), b = run(0.3), c = run(0.0);
    double same = 0, apart = 0;
    for (size_t i = 0; i < a.size(); ++i) { same = std::max(same, std::fabs(a[i] - b[i])); apart = std::max(apart, std::fabs(a[i] - c[i])); }
    CHECK(same == 0.0 && apart > 0.01);
    // spread: grains panned apart (the left and the right differ); none: the two sides alike
    auto sides = [&](double spread) {
        GranularSampler g;
        GranularSampler::Params p = plain();
        p.spread = spread;
        g.setParams(p);
        g.setSample(snd.data(), 48000, 1, 48000);
        std::vector<Sample> L(24000, 0.0), R(24000, 0.0);
        Sample *o[2] = {L.data(), R.data()};
        g.noteOn(60, 127);
        g.render(o, 2, 24000);
        double d = 0;
        for (int i = 0; i < 24000; ++i) d = std::max(d, std::fabs(L[(size_t)i] - R[(size_t)i]));
        return d;
    };
    CHECK(sides(0.0) == 0.0 && sides(1.0) > 0.01);
}

TEST(Granular_counts_its_sounds_rate)
{
    // (G2): a 440 Hz sound recorded at 24 kHz, played at 48 on its root — still 440 Hz
    configure48();
    std::vector<float> s24(24000);
    for (int i = 0; i < 24000; ++i) s24[(size_t)i] = (float)(0.5 * std::sin(kTau * 440.0 * i / 24000.0));
    GranularSampler g;
    GranularSampler::Params p = plain();
    p.size = 200;
    p.density = 10;
    p.root = 69;
    g.setParams(p);
    g.setSample(s24.data(), 24000, 1, 24000);
    const auto y = play(g, 69, 48000);
    CHECK(amp(y, 4800, 38400, 440.0) > 0.1 && amp(y, 4800, 38400, 220.0) < 0.01);
}

TEST(Granular_does_not_allocate)
{
    configure48();
    const auto snd = tone(220, 48000);
    GranularSampler g;
    g.setSample(snd.data(), 48000, 1, 48000);
    for (int n = 0; n < 20; ++n) g.noteOn(40 + n, 100);
    std::vector<Sample> L(4096, 0.0), R(4096, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    g.render(o, 2, 4096);
    g.render(o, 2, 64);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
    CHECK(g.activeVoices() == GranularSampler::kVoices);
}
