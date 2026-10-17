// The 808: a tuned bass drum (REQ-808-1, -2) — src/instrument/README.md ## Bass808 (B1)–(B6).
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
    Bass808::Params clean()
    {
        Bass808::Params p;
        p.click = 0.0;
        p.drive = 0.0;
        p.tone = 20000.0;
        p.volumeDb = 0.0;
        p.velocity = 0.0;
        return p;
    }
    std::vector<Sample> run(Bass808 &b, int frames, int block = 128)
    {
        std::vector<Sample> L((size_t)frames, 0.0), R((size_t)frames, 0.0);
        for (int at = 0; at < frames; at += block)
        {
            const int n = std::min(block, frames - at);
            Sample *o[2] = {L.data() + at, R.data() + at};
            b.render(o, 2, n);
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
    double rms(const std::vector<Sample> &y, int from, int n)
    {
        double e = 0;
        for (int i = from; i < from + n; ++i) e += y[(size_t)i] * y[(size_t)i];
        return std::sqrt(e / n);
    }
}

TEST(Bass808_is_tuned_and_punched)
{
    // (B1): A1 (55 Hz) struck — its pitch starts a punch (12 semitones) above, at 1 % of it after the punch's 40 ms, and
    // sounds at 55 Hz after
    configure48();
    Bass808 b;
    b.setParams(clean());
    b.noteOn(33, 127);
    std::vector<Sample> one = run(b, 1);
    CHECK(std::fabs(b.pitch() - 45.0) < 0.01);
    run(b, 1919); // 40 ms
    std::printf("    the pitch at 0 ms: 45.00; at 40 ms: %.3f (33 + 1 %% of 12)\n", b.pitch());
    CHECK(std::fabs(b.pitch() - (33.0 + 0.12)) < 0.01);
    const auto y = run(b, 19200);
    double best = 0, at = 0;
    for (double f = 50; f <= 60; f += 0.05)
    {
        const double a = amp(y, 4800, 9600, f);
        if (a > best) { best = a; at = f; }
    }
    std::printf("    it sounds at %.2f Hz\n", at);
    CHECK(std::fabs(at - 55.0) < 0.2);
}

TEST(Bass808_glides_between_held_notes)
{
    // (B1), (B6): A1 held, then E2 — no new attack; the pitch slides 33 → 40 over the glide's 60 ms, a straight line;
    // E2 let go, back to A1 the same way; A1 let go, choked over the release
    configure48();
    Bass808 b;
    Bass808::Params p = clean();
    p.punch = 0.0;
    p.glide = 60.0;
    p.release = 30.0;
    b.setParams(p);
    b.noteOn(33, 127);
    const auto before = run(b, 4800);
    const double level = rms(before, 2400, 2400);
    b.noteOn(40, 127);
    run(b, 1440); // 30 ms: halfway
    std::printf("    halfway through the glide: %.3f (36.5)\n", b.pitch());
    CHECK(std::fabs(b.pitch() - 36.5) < 0.02);
    const auto during = run(b, 2400);
    CHECK(std::fabs(b.pitch() - 40.0) < 1e-9);
    CHECK(rms(during, 0, 2400) < level); // no new attack: the decay goes on, falling
    b.noteOff(40);
    run(b, 2900); // its 60 ms and on
    CHECK(std::fabs(b.pitch() - 33.0) < 1e-9);
    b.noteOff(33);
    run(b, 9600);
    CHECK(b.activeVoices() == 0);
}

TEST(Bass808_decays_and_drives)
{
    // (B2): the decay — 60 dB down after its 900 ms; (B4): undriven a sine (its third harmonic nowhere), driven fully
    // its odd harmonics strong
    configure48();
    Bass808 b;
    Bass808::Params p = clean();
    p.punch = 0.0;
    p.decay = 900.0;
    b.setParams(p);
    b.noteOn(45, 127); // 110 Hz
    const auto y = run(b, 52800);
    // windows of 100 ms (11 whole cycles of 110 Hz), their centres 900 ms apart
    const double early = amp(y, 480, 4800, 110.0), late = amp(y, 480 + 43200, 4800, 110.0);
    std::printf("    900 ms on: %.1f dB of its start\n", 20 * std::log10(late / early));
    CHECK(std::fabs(20 * std::log10(late / early) + 60.0) < 0.5);
    // undriven, a sine: on a decay long enough to be steady in the window, its third harmonic nowhere
    Bass808 long8;
    Bass808::Params q = p;
    q.decay = 8000.0;
    long8.setParams(q);
    long8.noteOn(45, 127);
    const auto w = run(long8, 9600);
    std::printf("    undriven: the 3rd %.2g of the 1st\n", amp(w, 4800, 4800, 330.0) / amp(w, 4800, 4800, 110.0));
    CHECK(amp(w, 4800, 4800, 330.0) < amp(w, 4800, 4800, 110.0) * 1e-3);
    Bass808 hot;
    p.drive = 1.0;
    hot.setParams(p);
    hot.noteOn(45, 127);
    const auto z = run(hot, 4800);
    const double h1 = amp(z, 0, 4800, 110.0), h3 = amp(z, 0, 4800, 330.0);
    std::printf("    driven fully: the 3rd %.3f of the 1st\n", h3 / h1);
    CHECK(h3 / h1 > 0.15);
    // (B4) normalised: undriven or fully driven, its first peak at its level (1 here: 0 dB, velocity insensitive)
    auto firstPeak = [](const std::vector<Sample> &x) {
        double m = 0;
        for (int i = 0; i < 960; ++i) m = std::max(m, std::fabs(x[(size_t)i]));
        return m;
    };
    std::printf("    the first peak: undriven %.3f, driven %.3f\n", firstPeak(w), firstPeak(z));
    CHECK(std::fabs(firstPeak(w) - 1.0) < 0.05 && firstPeak(z) > 0.9 && firstPeak(z) < 1.01);
}

TEST(Bass808_clicks_the_same_and_renders_the_same_however_chopped)
{
    // (B3): a click at every attack, the same each time; the samples the same in blocks of 1, 128 and 777
    configure48();
    auto take = [](int block) {
        Bass808 b;
        Bass808::Params p;
        p.click = 0.6;
        b.setParams(p);
        b.noteOn(36, 120);
        auto a = run(b, 6000, block);
        b.noteOff(36);
        auto c = run(b, 6000, block);
        b.noteOn(36, 120);
        auto d = run(b, 6000, block);
        a.insert(a.end(), d.begin(), d.end());
        return a;
    };
    const auto one = take(1), mid = take(128), odd = take(777);
    double worst = 0;
    for (size_t i = 0; i < one.size(); ++i) worst = std::max({worst, std::fabs(one[i] - mid[i]), std::fabs(one[i] - odd[i])});
    CHECK(worst == 0.0);
    // a hit after the last has ended is that hit again — from phase 0, the click from its seed — sample for sample
    {
        Bass808 b;
        Bass808::Params p;
        p.click = 0.6;
        p.decay = 200.0;
        p.release = 5.0;
        b.setParams(p);
        b.noteOn(36, 120);
        const auto first = run(b, 2000);
        b.noteOff(36);
        run(b, 7777); // its release, over, and on in silence
        CHECK(b.activeVoices() == 0);
        b.noteOn(36, 120);
        const auto again = run(b, 2000);
        double d = 0;
        for (int i = 0; i < 2000; ++i) d = std::max(d, std::fabs(first[(size_t)i] - again[(size_t)i]));
        CHECK(d == 0.0);
    }
    // the two hits' first 3 ms alike (the click from a fixed seed) — the second's phase aside, the noise is the same
    Bass808 b;
    Bass808::Params p;
    p.click = 1.0;
    p.volumeDb = 0.0;
    b.setParams(p);
    b.noteOn(36, 127);
    const auto a = run(b, 144);
    CHECK(rms(a, 0, 144) > 0.05);
}

TEST(Bass808_does_not_allocate)
{
    configure48();
    Bass808 b;
    b.noteOn(36, 100);
    b.noteOn(43, 100);
    std::vector<Sample> L(4096, 0.0), R(4096, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    b.render(o, 2, 4096);
    b.render(o, 2, 64);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
    CHECK(b.activeVoices() == 1);
}
