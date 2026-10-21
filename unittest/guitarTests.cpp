// The guitar: a plucked string (REQ-guitar-1, -2) — src/instrument/README.md ## Guitar (GT1)–(GT8) and
// src/physical/README.md ## 14 (the pluck, (P1)–(P3)).
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
    constexpr double kPi = 3.141592653589793;
    void configure48()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }
    Guitar::Params clean()
    {
        Guitar::Params p;
        p.body = 0.0;
        p.stiffness = 0.0;
        p.volumeDb = 0.0;
        p.velocity = 0.0;
        return p;
    }
    std::vector<Sample> run(Guitar &g, int frames, int block = 128)
    {
        std::vector<Sample> L((size_t)frames, 0.0), R((size_t)frames, 0.0);
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
        // Hann-windowed: a neighbour two bins away leaks under −30 dB, not a rectangle's −13
        double re = 0, im = 0, sw = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(kTau * (i + 0.5) / n);
            const double a = kTau * hz * (from + i) / 48000.0;
            re += w * y[(size_t)(from + i)] * std::cos(a);
            im += w * y[(size_t)(from + i)] * std::sin(a);
            sw += w;
        }
        return 2.0 * std::hypot(re, im) / sw;
    }
    double rms(const std::vector<Sample> &y, int from, int n)
    {
        double e = 0;
        for (int i = from; i < from + n; ++i) e += y[(size_t)i] * y[(size_t)i];
        return std::sqrt(e / n);
    }
    double db(double x) { return 20.0 * std::log10(std::max(x, 1e-15)); }
}

// (P1) a released string's partials go as |sin(nπβ)|/n — the comb's null where nβ is whole
TEST(Guitar_pluck_sets_each_partial_as_a_released_string)
{
    configure48();
    StringPartialBank s;
    s.setFundamentalHz(200.0);
    s.setInharmonicity(0.0);
    s.setBaseDecaySeconds(30.0);
    s.setBrightnessDecaySeconds(30.0); // no decay to speak of over the window
    s.setStrikePosition(0.2);
    s.setPolarizationSplit(0.0);
    s.pluck(1.0, 1e9);                 // no pick: the bare initial condition
    std::vector<Sample> y(9600);
    for (Sample &v : y) v = s.out(0.0, 0);
    const double a1 = amp(y, 0, 9600, 200), a2 = amp(y, 0, 9600, 400), a3 = amp(y, 0, 9600, 600), a5 = amp(y, 0, 9600, 1000);
    const double w1 = std::sin(kPi * 0.2), w2 = std::sin(2 * kPi * 0.2) / 2, w3 = std::sin(3 * kPi * 0.2) / 3;
    std::printf("    a2/a1 %.4f (|sin 2πβ|/2|sin πβ| %.4f), a3/a1 %.4f (%.4f), a5/a1 %.2e\n", a2 / a1, w2 / w1, a3 / a1, w3 / w1, a5 / a1);
    CHECK(std::fabs(a2 / a1 - w2 / w1) < 0.01 * w2 / w1 + 0.003);
    CHECK(std::fabs(a3 / a1 - w3 / w1) < 0.01 * w3 / w1 + 0.003);
    CHECK(a5 / a1 < 1e-3); // β = 1/5: the fifth partial is never set moving
}

// (P2) the pick's release: a one-pole roll-off at its corner — 1/√(1 + (f/f_p)²)
TEST(Guitar_pick_rounds_the_highs_as_a_one_pole)
{
    configure48();
    auto ratio = [](double fp) {
        StringPartialBank s;
        s.setFundamentalHz(250.0);
        s.setInharmonicity(0.0);
        s.setBaseDecaySeconds(30.0);
        s.setBrightnessDecaySeconds(30.0);
        s.setStrikePosition(0.13);
        s.setPolarizationSplit(0.0);
        s.pluck(1.0, fp);
        std::vector<Sample> y(9600);
        for (Sample &v : y) v = s.out(0.0, 0);
        return amp(y, 0, 9600, 2000.0) / amp(y, 0, 9600, 250.0);
    };
    const double open = ratio(1e9), picked = ratio(1000.0);
    const double want = (1.0 / std::sqrt(1.0 + 4.0)) / (1.0 / std::sqrt(1.0 + 0.0625)); // L(2000)/L(250) at 1 kHz
    std::printf("    2 kHz over 250 Hz: %.2f dB with a 1 kHz pick against the bare pluck (want %.2f)\n", db(picked / open), db(want));
    CHECK(std::fabs(db(picked / open) - db(want)) < 0.1);
}

// (P3) from rest, at its peak: the first sample is the largest of the first period, and the motion has no DC
TEST(Guitar_pluck_starts_at_rest_from_its_peak)
{
    configure48();
    StringPartialBank s;
    s.setFundamentalHz(110.0);
    s.setInharmonicity(0.0);
    s.setBaseDecaySeconds(4.0);
    s.setBrightnessDecaySeconds(0.5);
    s.setStrikePosition(0.18);
    s.pluck(0.5, 4000.0);
    std::vector<Sample> y(48000);
    for (Sample &v : y) v = s.out(0.0, 0);
    double peak = 0, mean = 0;
    for (int i = 0; i < 436; ++i) peak = std::max(peak, (double)std::fabs(y[(size_t)i])); // one period of 110 Hz
    for (int i = 0; i < 48000; ++i) mean += y[(size_t)i] / 48000.0;
    std::printf("    first sample %.4f, the first period's peak %.4f, the mean over a second %.2e\n", y[0], peak, mean);
    CHECK(std::fabs(y[0]) >= peak - 1e-12);
    CHECK(std::fabs(mean) < 1e-3 * peak);
}

// (GT1)(GT3) the guitar sounds its note, plucked; a harder pluck is louder and brighter
TEST(Guitar_is_tuned_and_plucked)
{
    configure48();
    Guitar g;
    g.setParams(clean());
    g.noteOn(45, 100); // A2, 110 Hz
    const auto y = run(g, 48000);
    // half a second: a Hann window's main lobe ±4 Hz, so 104 and 116 Hz lie outside it
    const double on = amp(y, 4800, 24000, 110.0), lo = amp(y, 4800, 24000, 104.0), hi = amp(y, 4800, 24000, 116.0);
    std::printf("    110 Hz %.4f, 104 / 116 Hz %.5f / %.5f\n", on, lo, hi);
    CHECK(on > 20 * std::max(lo, hi));
    Guitar::Params p = clean();
    p.velocity = 1.0;
    auto bright = [&](int vel) {
        Guitar h;
        h.setParams(p);
        h.noteOn(45, vel);
        const auto z = run(h, 9600);
        return std::make_pair(amp(z, 0, 4800, 110.0), amp(z, 0, 4800, 1980.0) / amp(z, 0, 4800, 110.0));
    };
    const auto soft = bright(30), hard = bright(127);
    // (GT3) f_p = pick·(0.5 + 0.5 v): the 18th partial over the 1st moves by L_hard(1980)/L_soft(1980) (the 1st barely)
    auto L = [](double f, double fp) { return 1.0 / std::sqrt(1.0 + (f / fp) * (f / fp)); };
    const double fs = 4000.0 * (0.5 + 0.5 * 30 / 127.0), fh = 4000.0;
    const double want = (L(1980, fh) / L(110, fh)) / (L(1980, fs) / L(110, fs));
    std::printf("    soft %.4f (18th/1st %.4f), hard %.4f (%.4f): brighter by %.3f (the formula %.3f); louder by %.2f (%.2f)\n",
                soft.first, soft.second, hard.first, hard.second, hard.second / soft.second, want, hard.first / soft.first,
                1.0 / (30 / 127.0));
    CHECK(std::fabs(hard.second / soft.second / want - 1.0) < 0.03);
    CHECK(hard.first > 3.5 * soft.first);
}

// (GT2) the sustain sets the fundamental's ring, a higher string rings shorter, and the highs die first
TEST(Guitar_sustains_as_set_and_the_highs_die_first)
{
    configure48();
    auto level = [](double sustain, int note, double at, double hz) {
        Guitar g;
        Guitar::Params p = clean();
        p.sustain = sustain;
        g.setParams(p);
        g.noteOn(note, 100);
        const auto y = run(g, (int)(48000 * (at + 0.2)));
        return amp(y, (int)(48000 * at), 4800, hz);
    };
    const double shortRing = level(1.0, 40, 1.5, 82.40689), longRing = level(8.0, 40, 1.5, 82.40689);
    std::printf("    E2 1.5 s after the pluck: sustain 1 s %.2f dB, 8 s %.2f dB\n", db(shortRing), db(longRing));
    CHECK(db(longRing) - db(shortRing) > 25.0);
    Guitar probe;
    CHECK(std::fabs(probe.sustainAt(82.40689) - probe.params().sustain) < 1e-6);
    CHECK(std::fabs(probe.sustainAt(4 * 82.40689) - probe.params().sustain / 2) < 1e-6);
    Guitar g;
    g.setParams(clean());
    g.noteOn(45, 100);
    const auto y = run(g, 48000);
    // (GT2) α(f) = c1 + c3·(2πf)²: at 2970 Hz (the 27th) T60 ≈ 1.2 s against the 1st's 4.3 s — over 0.65 s it falls
    // some 24 dB further than the fundamental
    const double early = amp(y, 2400, 2400, 2970.0) / amp(y, 2400, 2400, 110.0);
    const double late = amp(y, 33600, 2400, 2970.0) / amp(y, 33600, 2400, 110.0);
    std::printf("    the 27th over the 1st: %.2f dB at 50 ms, %.2f dB at 700 ms\n", db(early), db(late));
    CHECK(db(early) - db(late) > 15.0);
}

// (GT4) the palm mute shortens the ring while held; a release lets the string go quiet
TEST(Guitar_mutes_and_releases)
{
    configure48();
    auto after = [](double mute, bool release) {
        Guitar g;
        Guitar::Params p = clean();
        p.mute = mute;
        p.release = 40.0;
        g.setParams(p);
        g.noteOn(45, 100);
        auto y = run(g, 9600);
        if (release) g.noteOff(45);
        auto z = run(g, 24000);
        return rms(z, 14400, 4800);
    };
    const double open = after(0.0, false), muted = after(0.6, false), let = after(0.0, true);
    std::printf("    0.5 s on: open %.2f dB, palm-muted %.2f dB, released at 0.2 s %.2f dB\n", db(open), db(muted), db(let));
    CHECK(db(open) - db(muted) > 20.0);
    CHECK(db(open) - db(let) > 40.0);
}

// (GT1)(GT7) a bend lands where it is sent, and every render is the same however the host cuts time
TEST(Guitar_bends_and_renders_the_same_however_chopped)
{
    configure48();
    auto render = [](int block) {
        Guitar g;
        Guitar::Params p;
        g.setParams(p);
        std::vector<Sample> L(96000, 0.0), R(96000, 0.0);
        int at = 0;
        auto to = [&](int end) {
            while (at < end)
            {
                const int n = std::min(block, end - at);
                Sample *o[2] = {L.data() + at, R.data() + at};
                g.render(o, 2, n);
                at += n;
            }
        };
        g.noteOn(45, 100);
        g.noteOn(52, 90);
        to(10001);
        g.pitchBend(1.0); // +2 st
        to(30000);
        g.noteOff(45);
        to(96000);
        return L;
    };
    const auto a = render(1), b = render(128), c = render(777);
    bool same = true;
    for (size_t i = 0; i < a.size(); ++i) same = same && a[i] == b[i] && a[i] == c[i];
    CHECK(same);
    const double bent = amp(a, 14400, 9600, 123.4708), plain = amp(a, 14400, 9600, 110.0);
    std::printf("    after the bend: 123.47 Hz %.4f, 110 Hz %.5f\n", bent, plain);
    CHECK(bent > 10 * plain);
}

// (GT7)(GT8) a voice ends when it has gone quiet, the body after it; a ninth note steals the oldest string
TEST(Guitar_ends_its_voices_and_steals_the_oldest)
{
    configure48();
    Guitar g;
    Guitar::Params p;
    p.sustain = 0.5;
    p.release = 20.0;
    g.setParams(p);
    for (int k = 0; k < 9; ++k) g.noteOn(40 + k, 100);
    CHECK(g.activeVoices() == Guitar::kVoices);
    for (int k = 0; k < 9; ++k) g.noteOff(40 + k);
    run(g, 48000 * 6);
    CHECK(g.activeVoices() == 0);
}

// (GT5) the body: energy where its modes are
TEST(Guitar_body_rings_its_modes)
{
    configure48();
    auto fund = [](double body, int note, double hz) {
        Guitar g;
        Guitar::Params p = clean();
        p.body = body;
        g.setParams(p);
        g.noteOn(note, 100);
        const auto y = run(g, 19200);
        return amp(y, 4800, 9600, hz);
    };
    // a G3 (196 Hz) sits on the top plate's 200 Hz mode; an A4 (440 Hz) is past them all
    const double on = fund(1.0, 55, 196.0) / fund(0.0, 55, 196.0), off = fund(1.0, 69, 440.0) / fund(0.0, 69, 440.0);
    std::printf("    the body lifts a G3's fundamental %.2f×, an A4's %.2f×\n", on, off);
    CHECK(on > 1.8);
    CHECK(on > 1.3 * off);
}

TEST(Guitar_does_not_allocate)
{
    configure48();
    Guitar g;
    g.noteOn(45, 100);
    run(g, 4800);
    std::vector<Sample> L(9600), R(9600); // the buffers are the host's, made before
    Sample *o[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    for (int k = 0; k < 12; ++k) g.noteOn(40 + k, 90);
    g.pitchBend(0.5);
    g.render(o, 2, 9600);
    g.allNotesOff();
    g.render(o, 2, 4800);
    gCounting = false;
    CHECK(gAllocs == 0);
}
