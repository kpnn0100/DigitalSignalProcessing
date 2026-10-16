// FMSynth: four sine operators by phase modulation (REQ-fm-1…3) — src/instrument/README.md ## FMSynth (M1)–(M6).
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
    /** J_n(x) by its series. */
    double besselJ(int n, double x)
    {
        n = std::abs(n);
        double sum = 0, term = std::pow(x / 2, n) / std::tgamma(n + 1.0);
        for (int k = 0; k < 40; ++k)
        {
            sum += term;
            term *= -(x * x / 4) / ((k + 1.0) * (k + 1.0 + n));
        }
        return sum;
    }
    /** An operator held at its level: no attack, no decay, full sustain, no velocity. */
    FMSynth::Op steady(double ratio, double level) { return FMSynth::Op{ratio, 0.0, level, 0.0, 0.0, 1.0, 50.0, 0.0}; }
    std::vector<Sample> play(FMSynth &s, int note, int frames, int block = 128)
    {
        std::vector<Sample> L((size_t)frames, 0.0), R((size_t)frames, 0.0);
        s.noteOn(note, 127);
        for (int at = 0; at < frames; at += block)
        {
            const int n = std::min(block, frames - at);
            Sample *o[2] = {L.data() + at, R.data() + at};
            s.render(o, 2, n);
        }
        return L;
    }
    /** The amplitude of `hz` over [from, from + n) — whole cycles of every frequency asked. */
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
}

TEST(FM_sidebands_are_bessel)
{
    // (M1), (M2), (M5): a carrier at 4·110 Hz under a modulator at 110 Hz of index 1 rad (level 1/8 of I = 8) — its
    // sidebands at 440 ± n·110 are J_n(1) of the carrier's J_0(1), to 0.3 %; nothing else above −70 dB
    configure48();
    FMSynth s;
    FMSynth::Params p;
    p.algorithm = 5; // 4→3, 2→1: op 2 on op 1 alone (3 and 4 silent)
    p.op = {steady(4.0, 1.0), steady(1.0, 1.0 / 8.0), steady(1.0, 0.0), steady(1.0, 0.0)};
    p.volumeDb = 0.0;
    s.setParams(p);
    const auto y = play(s, 45, 48000); // A2: 110 Hz exactly
    const int from = 4800, n = 43200;  // 0.9 s: whole cycles of every multiple of 10 Hz
    const double c = amp(y, from, n, 440.0);
    double worst = 0, sidebands = 0;
    for (int k = -4; k <= 8; ++k) // −4 and below fold onto 0 … : the sidebands' own place (J_8(1) ≈ 10⁻⁸)
    {
        const double a = amp(y, from, n, 440.0 + 110.0 * k);
        sidebands += a * a;
        if (k == 0 || std::abs(k) > 3) continue;
        worst = std::max(worst, std::fabs(a / c - std::fabs(besselJ(k, 1.0) / besselJ(0, 1.0))));
    }
    double total = 0;
    for (int i = 0; i < n; ++i) total += y[(size_t)(from + i)] * y[(size_t)(from + i)];
    total = 2.0 * total / n; // the power as amplitudes²
    const double rest = 10 * std::log10(std::max(1e-30, total - sidebands) / total);
    std::printf("    sidebands against J_n(1)/J_0(1): %.4f off at worst; what lies elsewhere %.1f dB\n", worst, rest);
    CHECK(worst < 0.003);
    CHECK(rest < -70.0);
}

TEST(FM_algorithms_route_as_drawn)
{
    // (M3): the table — each algorithm's modulators and carriers as the README draws them
    CHECK(FMSynth::modulates(1, 3, 2) && FMSynth::modulates(1, 2, 1) && FMSynth::modulates(1, 1, 0) && FMSynth::carrier(1, 0) && !FMSynth::carrier(1, 1));
    CHECK(FMSynth::modulates(2, 2, 1) && FMSynth::modulates(2, 3, 1) && FMSynth::modulates(2, 1, 0) && !FMSynth::modulates(2, 3, 0));
    CHECK(FMSynth::modulates(3, 2, 1) && FMSynth::modulates(3, 1, 0) && FMSynth::modulates(3, 3, 0) && !FMSynth::modulates(3, 3, 2));
    CHECK(FMSynth::modulates(4, 3, 2) && FMSynth::modulates(4, 2, 0) && FMSynth::modulates(4, 1, 0) && !FMSynth::modulates(4, 2, 1));
    CHECK(FMSynth::modulates(5, 3, 2) && FMSynth::modulates(5, 1, 0) && FMSynth::carrier(5, 0) && FMSynth::carrier(5, 2) && !FMSynth::carrier(5, 1));
    CHECK(FMSynth::modulates(6, 3, 0) && FMSynth::modulates(6, 3, 1) && FMSynth::modulates(6, 3, 2) && FMSynth::carrier(6, 1) && !FMSynth::carrier(6, 3));
    CHECK(FMSynth::modulates(7, 3, 2) && !FMSynth::modulates(7, 3, 0) && FMSynth::carrier(7, 0) && FMSynth::carrier(7, 1) && FMSynth::carrier(7, 2));
    for (int k = 0; k < 4; ++k) CHECK(FMSynth::carrier(8, k));
    for (int a = 1; a <= 8; ++a)
        for (int j = 0; j < 4; ++j)
            for (int k = j; k < 4; ++k) CHECK(!FMSynth::modulates(a, j, k)); // only from above: no loop but 4's own
    // and the sound: 8 is four sines, each a quarter (M4); 6 with operator 4 silent is three, each a third
    configure48();
    FMSynth s;
    FMSynth::Params p;
    p.algorithm = 8;
    p.op = {steady(1, 1), steady(2, 1), steady(3, 1), steady(4, 1)};
    p.volumeDb = 0.0;
    s.setParams(p);
    const auto y = play(s, 45, 24000);
    for (int h = 1; h <= 4; ++h) CHECK(std::fabs(amp(y, 4800, 19200, 110.0 * h) - 0.25) < 1e-3);
    CHECK(amp(y, 4800, 19200, 550.0) < 1e-4);
    p.algorithm = 6;
    p.op[3].level = 0.0;
    FMSynth s6;
    s6.setParams(p);
    const auto z = play(s6, 45, 24000);
    for (int h = 1; h <= 3; ++h) CHECK(std::fabs(amp(z, 4800, 19200, 110.0 * h) - 1.0 / 3) < 1e-3);
}

TEST(FM_feedback_brightens_operator_four)
{
    // (M2): operator 4 alone (algorithm 8, the others silent) — without feedback a sine; fed back by half, harmonics
    // falling with their number (a saw's way)
    configure48();
    auto run = [](double fb) {
        FMSynth s;
        FMSynth::Params p;
        p.algorithm = 8;
        p.feedback = fb;
        p.op = {steady(1, 0), steady(1, 0), steady(1, 0), steady(1, 1)};
        s.setParams(p);
        return play(s, 45, 24000);
    };
    const auto pure = run(0.0), fed = run(0.5);
    const double p2 = amp(pure, 4800, 19200, 220.0) / amp(pure, 4800, 19200, 110.0);
    const double h1 = amp(fed, 4800, 19200, 110.0), h2 = amp(fed, 4800, 19200, 220.0), h3 = amp(fed, 4800, 19200, 330.0);
    std::printf("    feedback 0: the 2nd %.2g of the 1st; feedback 0.5: %.3f, the 3rd %.3f\n", p2, h2 / h1, h3 / h1);
    CHECK(p2 < 1e-4);
    CHECK(h2 / h1 > 0.05 && h3 < h2);
    // fed back fully, still a tone: the mean of two (M2) keeps it periodic — what lies between its harmonics is far
    // under them (a single sample's feedback chatters at Nyquist and smears)
    const auto full = run(1.0);
    double on = 0, between = 0;
    for (int h = 1; h <= 40; ++h)
    {
        on += std::pow(amp(full, 4800, 19200, 110.0 * h), 2);
        between += std::pow(amp(full, 4800, 19200, 110.0 * h + 55.0), 2);
    }
    std::printf("    feedback 1: between its harmonics %.1f dB under them\n", 10 * std::log10(on / std::max(1e-30, between)));
    CHECK(10 * std::log10(on / std::max(1e-30, between)) > 60.0);
}

TEST(FM_envelopes_and_velocity_shape_each_operator)
{
    // (M1): a carrier's level follows its envelope (halfway up its 100 ms attack at 50 ms) and its velocity sense
    // (fully sensitive: velocity 64 is 64/127 of 127); the voice ends when its carriers' releases have
    configure48();
    FMSynth s;
    FMSynth::Params p;
    p.algorithm = 8;
    p.op = {FMSynth::Op{1, 0, 1, 100.0, 0.0, 1.0, 20.0, 1.0}, steady(1, 0), steady(1, 0), steady(1, 0)};
    p.volumeDb = 0.0;
    s.setParams(p);
    const auto y = play(s, 45, 9600);
    double atHalf = 0, full = 0; // one carrier of four: heard at a quarter (M4) — against its own full level
    for (int i = 2400 - 218; i < 2400 + 218; ++i) atHalf = std::max(atHalf, std::fabs(y[(size_t)i]));
    for (int i = 7200 - 218; i < 7200 + 218; ++i) full = std::max(full, std::fabs(y[(size_t)i]));
    std::printf("    50 ms into a 100 ms attack: %.3f of full (full %.3f: a quarter, one carrier of four)\n", atHalf / full, full);
    CHECK(std::fabs(atHalf / full - 0.5) < 0.03 && std::fabs(full - 0.25) < 0.002);
    FMSynth soft;
    soft.setParams(p);
    std::vector<Sample> L(9600, 0.0), R(9600, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    soft.noteOn(45, 64);
    soft.render(o, 2, 9600);
    CHECK(std::fabs(amp(L, 7200, 2400, 110.0) / amp(y, 7200, 2400, 110.0) - 64.0 / 127.0) < 1e-3);
    soft.noteOff(45);
    std::fill(L.begin(), L.end(), 0.0);
    soft.render(o, 2, 4800);
    CHECK(soft.activeVoices() == 0);
    // struck again: every operator from phase 0 (key sync) — its start the same as the first strike's, sample for sample
    std::fill(L.begin(), L.end(), 0.0);
    std::vector<Sample> first(L.begin(), L.end());
    {
        FMSynth fresh;
        FMSynth::Params q = p;
        q.algorithm = 1;
        q.op = {steady(1, 1), steady(2, 0.5), steady(3, 0.4), steady(1.5, 0.3)};
        fresh.setParams(q);
        soft.setParams(q);
        std::vector<Sample> A(2000, 0.0), B(2000, 0.0), C(2000, 0.0), D(2000, 0.0);
        Sample *oa[2] = {A.data(), B.data()}, *ob[2] = {C.data(), D.data()};
        fresh.noteOn(45, 127);
        fresh.render(oa, 2, 2000);
        soft.render(ob, 2, 700); // the phases somewhere else first
        std::fill(C.begin(), C.end(), 0.0);
        soft.noteOn(45, 127);
        soft.render(ob, 2, 2000);
        double worst = 0;
        for (int i = 0; i < 2000; ++i) worst = std::max(worst, std::fabs(A[(size_t)i] - C[(size_t)i]));
        CHECK(worst < 1e-12);
    }
}

TEST(FM_renders_the_same_however_chopped)
{
    // the pitch on the absolute 32-sample grid: bend and vibrato moving, blocks of 1, 128 and 777 give the same samples
    configure48();
    auto run = [](int block) {
        FMSynth s;
        FMSynth::Params p;
        p.feedback = 0.3;
        s.setParams(p);
        s.modWheel(1.0);
        s.pitchBend(0.4);
        return play(s, 60, 9000, block);
    };
    const auto a = run(1), b = run(128), c = run(777);
    double worst = 0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max({worst, std::fabs(a[i] - b[i]), std::fabs(a[i] - c[i])});
    CHECK(worst == 0.0);
}

TEST(FM_does_not_allocate)
{
    configure48();
    FMSynth s;
    for (int n = 0; n < 20; ++n) s.noteOn(40 + n, 100); // more than its voices: one stolen
    std::vector<Sample> L(4096, 0.0), R(4096, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    s.render(o, 2, 4096);
    s.render(o, 2, 64);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
    CHECK(s.activeVoices() == FMSynth::kVoices);
}
