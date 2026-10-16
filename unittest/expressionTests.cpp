/*
 *  Arstro DSP — expression in the instruments (REQ-inst-expr-1, REQ-synth2-7, REQ-synth2-8): the pitch wheel, the mod
 *  wheel (the Basic Synth's vibrato), a mono / legato mode with constant-time glide; the Sampler's bend.
 *
 *  Judged by the pitch they make, measured from zero crossings — a bend's interval, a vibrato's swing, a glide's
 *  midpoint — by what legato keeps (no new attack), and by the render being the same however time is chopped.
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <atomic>
#include <cmath>
#include <vector>

using namespace arstro;

extern std::atomic<long> gAllocs; // instrumentTests.cpp's counting allocator
extern std::atomic<bool> gCounting;

namespace
{
    constexpr double kFs = 48000.0;
    void configure()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }
    /** A pure sine voice: one oscillator, the filter open, no envelope to speak of. */
    BasicSynth::Params sineParams()
    {
        BasicSynth::Params p;
        p.osc1 = BasicSynth::Osc{BasicSynth::Sine, 0, 0.0, 0.0, 0.8, 1, 0.0, 0.0};
        p.osc2.level = 0.0;
        p.noise = 0.0;
        p.cutoff = 20000.0;
        p.keytrack = 0.0;
        p.envAmount = 0.0;
        p.ampEnv = {0.0, 0.0, 1.0, 5.0};
        p.volumeDb = 0.0;
        p.velocity = 0.0;
        return p;
    }
    /** Render `frames` of instrument `s` in blocks of `block`, left channel kept. */
    template <class I> std::vector<Sample> render(I &s, int frames, int block)
    {
        std::vector<Sample> out((size_t)frames, 0.0), r((size_t)frames, 0.0);
        for (int pos = 0; pos < frames; pos += block)
        {
            const int n = std::min(block, frames - pos);
            Sample *io[2] = {out.data() + pos, r.data() + pos};
            s.render(io, 2, n);
        }
        return out;
    }
    /** The frequency over [a, b): positive-going zero crossings, interpolated, first to last. */
    double hz(const std::vector<Sample> &x, size_t a, size_t b)
    {
        double first = -1, last = -1;
        int n = 0;
        for (size_t i = a + 1; i < b && i < x.size(); ++i)
            if (x[i - 1] < 0.0 && x[i] >= 0.0)
            {
                const double t = (double)(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0) first = t;
                else ++n;
                last = t;
            }
        return n > 0 ? n * kFs / (last - first) : 0.0;
    }
    double rms(const std::vector<Sample> &x, size_t a, size_t b)
    {
        double s = 0;
        for (size_t i = a; i < b; ++i) s += x[i] * x[i];
        return std::sqrt(s / (double)(b - a));
    }
}

TEST(Synth_pitch_bend_moves_every_note_by_its_range)
{
    configure();
    BasicSynth s;
    s.setParams(sineParams());
    s.noteOn(69, 100);
    auto a = render(s, 9600, 128);
    CHECK_NEAR(hz(a, 2400, 9600), 440.0, 0.5);
    s.pitchBend(1.0);                                   // the default range: 2 semitones
    auto b = render(s, 9600, 128);
    CHECK_NEAR(hz(b, 480, 9600), 440.0 * std::pow(2.0, 2.0 / 12.0), 0.6);
    s.pitchBend(-0.5);
    auto c = render(s, 9600, 128);
    CHECK_NEAR(hz(c, 480, 9600), 440.0 * std::pow(2.0, -1.0 / 12.0), 0.5);
    BasicSynth::Params wide = sineParams();
    wide.bendRange = 12.0;
    s.setParams(wide);
    s.pitchBend(1.0);
    auto d = render(s, 9600, 128);
    CHECK_NEAR(hz(d, 480, 9600), 880.0, 1.0);           // an octave at range 12
}

TEST(Synth_mod_wheel_is_vibrato)
{
    configure();
    BasicSynth::Params p = sineParams();
    p.vibrato = 0.5;
    p.vibratoRate = 5.0;
    BasicSynth s;
    s.setParams(p);
    s.noteOn(69, 100);
    s.modWheel(1.0);
    auto x = render(s, 48000, 128);
    // the swing of the instantaneous pitch over a second: ± 0.5 semitone
    double lo = 1e9, hi = 0;
    for (size_t a = 2400; a + 960 < x.size(); a += 240)
    {
        const double f = hz(x, a, a + 960);
        lo = std::min(lo, f);
        hi = std::max(hi, f);
    }
    CHECK_NEAR(hi, 440.0 * std::pow(2.0, 0.5 / 12.0), 2.5);
    CHECK_NEAR(lo, 440.0 * std::pow(2.0, -0.5 / 12.0), 2.5);
    s.modWheel(0.0);                                    // the wheel down: no vibrato
    auto still = render(s, 9600, 128);
    CHECK_NEAR(hz(still, 2400, 9600), 440.0, 0.5);
}

TEST(Synth_legato_glides_without_a_new_attack)
{
    configure();
    BasicSynth::Params p = sineParams();
    p.mode = BasicSynth::Legato;
    p.glide = 100.0;
    p.ampEnv = {80.0, 0.0, 1.0, 5.0};                    // a slow attack: a new one would show
    BasicSynth s;
    s.setParams(p);
    s.noteOn(57, 100);                                   // A3
    auto a = render(s, 14400, 128);                      // 0.3 s: the attack done
    s.noteOn(69, 100);                                   // A4, A3 still held: slide, no attack
    auto b = render(s, 2400, 128);                       // 50 ms: half way
    CHECK_NEAR(s.voicePitch(0), 63.0, 0.1);              // linear in semitones: the midpoint (to a control step, 0.08 st)
    auto c = render(s, 4800, 128);                       // past the glide
    CHECK_NEAR(s.voicePitch(0), 69.0, 1e-9);
    auto d = render(s, 9600, 128);
    CHECK_NEAR(hz(d, 480, 9600), 440.0, 0.5);
    CHECK(rms(b, 0, 480) > 0.9 * rms(a, 9600, 14400));   // legato: no new attack — the level carries on
    CHECK(s.activeVoices() == 1);
    s.noteOff(69);                                       // back to the key still held, gliding, no attack
    auto e = render(s, 9600, 128);
    CHECK_NEAR(s.voicePitch(0), 57.0, 1e-9);
    CHECK_NEAR(hz(e, 6000, 9600), 220.0, 0.4);
    s.noteOff(57);
    render(s, 4800, 128);
    CHECK(s.activeVoices() == 0);
    // mono: the same notes strike again — the attack starts over
    p.mode = BasicSynth::Mono;
    BasicSynth m;
    m.setParams(p);
    m.noteOn(57, 100);
    auto ma = render(m, 14400, 128);
    m.noteOn(69, 100);
    auto mb = render(m, 2400, 128);
    CHECK(rms(mb, 0, 480) < 0.5 * rms(ma, 9600, 14400));
    CHECK_NEAR(m.voicePitch(0), 63.0, 0.1);              // and it glides too
}

TEST(Synth_expression_is_the_same_however_time_is_chopped)
{
    configure();
    // the same events at the same samples — bend, wheel, a legato glide — rendered in blocks of 1, 37 and 128
    auto run = [](int block) {
        BasicSynth::Params p = sineParams();
        p.mode = BasicSynth::Legato;
        p.glide = 60.0;
        BasicSynth s;
        s.setParams(p);
        std::vector<Sample> out;
        struct Ev { int at; int kind; double v; };
        const Ev evs[] = {{0, 0, 57}, {3000, 1, 0.6}, {5000, 2, 0.8}, {7001, 0, 64}, {9333, 1, -0.3}, {12000, 3, 0}};
        int pos = 0;
        for (const auto &e : evs)
        {
            while (pos < e.at)
            {
                const int n = std::min(block, e.at - pos);
                std::vector<Sample> l((size_t)n, 0.0), r((size_t)n, 0.0);
                Sample *io[2] = {l.data(), r.data()};
                s.render(io, 2, n);
                out.insert(out.end(), l.begin(), l.end());
                pos += n;
            }
            if (e.kind == 0) s.noteOn((int)e.v, 100);
            if (e.kind == 1) s.pitchBend(e.v);
            if (e.kind == 2) s.modWheel(e.v);
            if (e.kind == 3) s.noteOff(64);
        }
        return out;
    };
    const auto a = run(1), b = run(37), c = run(128);
    CHECK(a == b && b == c);
}

TEST(Sampler_bends_its_rate)
{
    configure();
    // a 1 kHz sine recorded at 48 kHz, root 60: bent a whole step up, 1.122 kHz
    std::vector<float> wav(48000);
    for (size_t i = 0; i < wav.size(); ++i) wav[i] = (float)(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * (double)i / kFs));
    Sampler s;
    s.setSample(wav.data(), (long long)wav.size(), 1, kFs);
    s.noteOn(60, 127);
    s.pitchBend(1.0);
    auto x = render(s, 9600, 128);
    CHECK_NEAR(hz(x, 480, 9600), 1000.0 * std::pow(2.0, 2.0 / 12.0), 1.5);
}

TEST(Expression_reaches_the_instruments_through_the_registry)
{
    configure();
    // the Device seam forwards to the synth and the sampler; the new parameters were APPENDED (ids are kept)
    auto synth = DeviceRegistry::create("synth");
    const DeviceType &t = synth->type();
    CHECK(t.paramIndex("voice.mode") == (int)t.params.size() - 5 && t.paramIndex("vibrato.rate") == (int)t.params.size() - 1);
    CHECK(t.params[(size_t)t.paramIndex("voice.bend")].def == 2.0 && t.params[(size_t)t.paramIndex("voice.glide")].def == 0.0);
    synth->setParam(t.paramIndex("osc1.wave"), 0.0);    // sine
    synth->setParam(t.paramIndex("osc2.level"), 0.0);
    synth->setParam(t.paramIndex("filter.cutoff"), 20000.0);
    synth->setParam(t.paramIndex("filter.env"), 0.0);
    synth->setParam(t.paramIndex("filter.keytrack"), 0.0);
    synth->noteOn(69, 100);
    synth->pitchBend(1.0);
    std::vector<Sample> l(9600, 0.0), r(9600, 0.0);
    Sample *io[2] = {l.data(), r.data()};
    synth->process(io, 2, 9600);
    CHECK_NEAR(hz(l, 960, 9600), 440.0 * std::pow(2.0, 2.0 / 12.0), 1.0);
    auto drums = DeviceRegistry::create("drums");
    drums->pitchBend(1.0);                               // a kit has no pitch to bend: ignored, no harm
    drums->modWheel(1.0);
    CHECK(DeviceRegistry::find("sampler")->paramIndex("bend") >= 0);
}

TEST(Expression_does_not_allocate)
{
    configure();
    BasicSynth::Params p = sineParams();
    p.mode = BasicSynth::Legato;
    p.glide = 40.0;
    BasicSynth s;
    s.setParams(p);
    std::vector<Sample> l(128, 0.0), r(128, 0.0);
    Sample *io[2] = {l.data(), r.data()};
    s.noteOn(60, 100);
    s.render(io, 2, 128);
    gAllocs = 0;
    gCounting = true;
    for (int k = 0; k < 400; ++k)
    {
        if (k % 50 == 0) s.noteOn(60 + (k / 50) % 12, 100);
        s.pitchBend(std::sin(k * 0.1));
        s.modWheel(0.5);
        s.render(io, 2, 128);
    }
    gCounting = false;
    CHECK(gAllocs.load() == 0);
}
