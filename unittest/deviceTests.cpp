/*
 *  Arstro DSP — D3: Device and DeviceRegistry (REQ-device-1…4).
 *
 *  Two kinds of test. The registry's own invariants (names unique, defaults in range, every
 *  write clamped) and a ROBUSTNESS SWEEP: every parameter of every device driven to its minimum
 *  and its maximum while it renders, asserting every sample stays finite — the test that catches
 *  the corner of a range nobody listened to. Then the adapters, each judged on rendered audio: a
 *  parameter written by NAME must do what the module's own setter does.
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <atomic>
#include <cmath>
#include <set>
#include <vector>

using namespace arstro;

extern std::atomic<long> gAllocs;     // the counting allocator in instrumentTests.cpp
extern std::atomic<bool> gCounting;

namespace
{
    void configure()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }

    struct Stereo
    {
        std::vector<Sample> L, R;
        Stereo(int n) : L(n, 0.0), R(n, 0.0) {}
    };

    // Render `frames` through a device: an instrument from silence (a note held), an effect over
    // a 220 Hz sine at amplitude `amp`. Returns whether every sample was finite.
    bool renderFinite(Device &d, int frames, double amp = 0.5)
    {
        Stereo s(128);
        bool finite = true;
        for (int pos = 0; pos < frames; pos += 128)
        {
            for (int i = 0; i < 128; ++i)
            {
                const double x = d.isInstrument() ? 0.0 : amp * std::sin(2 * M_PI * 220.0 * (pos + i) / 48000.0);
                s.L[i] = s.R[i] = x;
            }
            Sample *io[2] = {s.L.data(), s.R.data()};
            d.process(io, 2, 128);
            for (int i = 0; i < 128; ++i) finite = finite && std::isfinite(s.L[i]) && std::isfinite(s.R[i]);
        }
        return finite;
    }

    // The steady RMS gain, in dB, a device applies to a unit-ish sine at `hz` (channel 0).
    double gainDb(Device &d, double hz, double amp = 0.5)
    {
        double sIn = 0, sOut = 0;
        Stereo s(128);
        for (int pos = 0; pos < 96000; pos += 128)
        {
            for (int i = 0; i < 128; ++i) s.L[i] = s.R[i] = amp * std::sin(2 * M_PI * hz * (pos + i) / 48000.0);
            std::vector<Sample> in = s.L;
            Sample *io[2] = {s.L.data(), s.R.data()};
            d.process(io, 2, 128);
            if (pos >= 48000)
                for (int i = 0; i < 128; ++i) { sIn += in[i] * in[i]; sOut += s.L[i] * s.L[i]; }
        }
        return 10.0 * std::log10(sOut / sIn);
    }
}

TEST(DeviceRegistry_has_every_instrument_and_effect_with_unique_names)
{
    configure();
    const std::vector<std::string> want = {"synth", "drums", "compressor", "eq", "reverb", "delay", "chorus", "drive", "filter"};
    std::set<std::string> names;
    for (const auto &t : DeviceRegistry::types()) names.insert(t.name);
    CHECK(names.size() == DeviceRegistry::types().size());
    for (const auto &w : want) CHECK(DeviceRegistry::find(w) != nullptr);
    CHECK(DeviceRegistry::find("synth")->kind == DeviceKind::Instrument);
    CHECK(DeviceRegistry::find("drums")->kind == DeviceKind::Instrument);
    CHECK(DeviceRegistry::find("eq")->kind == DeviceKind::Effect);
    CHECK(DeviceRegistry::find("nope") == nullptr);
    CHECK(DeviceRegistry::create("nope") == nullptr);
    for (const auto &t : DeviceRegistry::types())
    {
        CHECK(!t.label.empty() && !t.summary.empty() && !t.params.empty());
        std::set<std::string> pn;
        for (const auto &p : t.params)
        {
            pn.insert(p.name);
            CHECK(p.min <= p.def && p.def <= p.max);
            CHECK(!p.label.empty());
            if (p.isChoice()) CHECK(p.max == (double)p.choices.size() - 1);
        }
        CHECK(pn.size() == t.params.size()); // no two parameters share a name
        CHECK(t.paramIndex("no.such") == -1);
    }
}

TEST(Device_writes_are_clamped_rounded_and_named)
{
    configure();
    auto d = DeviceRegistry::create("synth");
    const int cutoff = d->type().paramIndex("filter.cutoff");
    const int wave = d->type().paramIndex("osc1.wave");
    const int oct = d->type().paramIndex("osc1.octave");
    CHECK(d->param(cutoff) == 2400.0);                  // the instrument's own default
    CHECK(d->setParam(cutoff, 1e9));
    CHECK(d->param(cutoff) == 20000.0);                 // clamped to the spec
    CHECK(d->setParam("filter.cutoff", std::nan("")));
    CHECK(d->param(cutoff) == 2400.0);                  // non-finite → the default
    CHECK(d->setParam(wave, 2.6));
    CHECK(d->param(wave) == 3.0);                       // a choice rounds to an index
    CHECK(d->setParam(wave, -4));
    CHECK(d->param(wave) == 0.0);
    CHECK(d->setParam(oct, 1.4));
    CHECK(d->param(oct) == 1.0);                        // an integer rounds
    CHECK(!d->setParam("osc1.wav", 1.0));               // an unknown name changes nothing
    CHECK(!d->setParam(999, 1.0));
    CHECK(d->param(999) == 0.0);
    CHECK(d->paramCount() == (int)d->type().params.size());
    CHECK(d->isInstrument());
}

TEST(Device_every_parameter_at_both_ends_renders_finite)
{
    configure();
    for (const auto &t : DeviceRegistry::types())
    {
        for (int i = 0; i < (int)t.params.size(); ++i)
            for (double v : {t.params[i].min, t.params[i].max})
            {
                auto d = DeviceRegistry::create(t.name);
                d->setParam(i, v);
                d->noteOn(36, 127);
                d->noteOn(60, 127);
                const bool ok = renderFinite(*d, 4800);
                if (!ok) printf("    non-finite: %s.%s = %g\n", t.name.c_str(), t.params[i].name.c_str(), v);
                CHECK(ok);
            }
    }
}

TEST(Device_instruments_sound_and_stop)
{
    configure();
    for (const char *name : {"synth", "drums"})
    {
        auto d = DeviceRegistry::create(name);
        d->noteOn(36, 120);
        CHECK(d->activeVoices() > 0);
        Stereo s(48000);
        Sample *io[2] = {s.L.data(), s.R.data()};
        d->process(io, 2, 24000);
        double peak = 0;
        for (int i = 0; i < 24000; ++i) peak = std::max(peak, (double)std::fabs(s.L[i]));
        CHECK(peak > 0.05);
        d->noteOff(36);
        d->allNotesOff();
        Sample *rest[2] = {s.L.data() + 24000, s.R.data() + 24000};
        for (int k = 0; k < 40 && d->activeVoices(); ++k) d->process(rest, 2, 24000);
        CHECK(d->activeVoices() == 0);
        d->noteOn(38, 100);
        d->reset();
        CHECK(d->activeVoices() == 0);
    }
}

TEST(Device_adapters_do_what_the_module_setters_do)
{
    configure();
    // eq: peak2.gain = +6 dB at its 1 kHz centre
    auto eq = DeviceRegistry::create("eq");
    CHECK_NEAR(gainDb(*eq, 1000.0), 0.0, 0.01);                 // fresh: transparent
    eq->setParam("peak2.gain", 6.0);
    eq->setParam("peak1.gain", -6.0);
    eq->reset();
    // each name reaches ITS band: the two peaks' closed-form responses add
    auto both = [](double hz) {
        return Biquad::magnitudeDb(Biquad::design(Biquad::Peak, 250.0, 1.0, -6.0, 48000.0), hz, 48000.0) +
               Biquad::magnitudeDb(Biquad::design(Biquad::Peak, 1000.0, 1.0, 6.0, 48000.0), hz, 48000.0);
    };
    CHECK_NEAR(gainDb(*eq, 1000.0), both(1000.0), 0.05);
    eq->reset();
    CHECK_NEAR(gainDb(*eq, 250.0), both(250.0), 0.05);
    eq->setParam("highcut.on", 1.0);
    eq->setParam("highcut.freq", 2000.0);
    eq->reset();
    CHECK(gainDb(*eq, 12000.0) < -25.0);
    // filter: a 500 Hz low-pass takes 8 kHz down; mix 0 is dry
    auto f = DeviceRegistry::create("filter");
    f->setParam("cutoff", 500.0);
    f->setParam("res", 0.0);
    CHECK(gainDb(*f, 8000.0) < -30.0);
    f->setParam("mix", 0.0);
    CHECK_NEAR(gainDb(*f, 8000.0), 0.0, 1e-9);
    f->setParam("mode", 2.0);                                  // high-pass
    f->setParam("mix", 1.0);
    f->reset();
    CHECK(gainDb(*f, 100.0) < -20.0);
    // compressor: a −6 dBFS-peak sine against a −20 dB threshold at 4:1 → ~10.5 dB of reduction,
    // and the makeup puts dB back
    auto c = DeviceRegistry::create("compressor");
    c->setParam("threshold", -20.0);
    c->setParam("ratio", 4.0);
    const double reduced = gainDb(*c, 220.0, 0.5);
    CHECK(reduced < -9.0 && reduced > -12.0);
    c->setParam("makeup", 6.0);
    CHECK_NEAR(gainDb(*c, 220.0, 0.5) - reduced, 6.0, 0.05);
    // drive: level is dB
    auto dr = DeviceRegistry::create("drive");
    const double base = gainDb(*dr, 220.0, 0.1);
    dr->setParam("level", -6.0);
    CHECK_NEAR(gainDb(*dr, 220.0, 0.1) - base, -6.0, 0.05);
    // reverb mix 0 = dry; delay mix 0 = dry
    auto rv = DeviceRegistry::create("reverb");
    rv->setParam("mix", 0.0);
    CHECK_NEAR(gainDb(*rv, 440.0), 0.0, 0.01);
    auto dl = DeviceRegistry::create("delay");
    dl->setParam("mix", 0.0);
    CHECK_NEAR(gainDb(*dl, 440.0), 0.0, 0.01);
    auto ch = DeviceRegistry::create("chorus");
    ch->setParam("mix", 0.0);
    CHECK_NEAR(gainDb(*ch, 440.0), 0.0, 0.05);
    for (auto *d : {rv.get(), dl.get(), ch.get(), dr.get(), c.get()}) d->reset(); // a reset is always safe
}

TEST(Device_synth_and_drum_params_reach_the_instrument)
{
    configure();
    // synth: osc1 a sine, osc2 silent, filter open → A4 at 440 Hz; osc1.octave = 1 → 880
    auto pitch = [](Device &d) {
        Stereo s(24000);
        Sample *io[2] = {s.L.data(), s.R.data()};
        d.process(io, 2, 24000);
        int first = -1, last = -1, n = 0;
        for (int i = 4801; i < 24000; ++i)
            if (s.L[i - 1] < 0 && s.L[i] >= 0) { if (first < 0) first = i; last = i; ++n; }
        return n > 1 ? (n - 1) * 48000.0 / (last - first) : 0.0;
    };
    auto sy = DeviceRegistry::create("synth");
    sy->setParam("osc1.wave", 0.0);
    sy->setParam("osc2.level", 0.0);
    sy->setParam("filter.cutoff", 20000.0);
    sy->setParam("filter.env", 0.0);
    sy->setParam("filter.keytrack", 0.0);
    sy->setParam("osc1.octave", 1.0);
    sy->noteOn(69, 127);
    CHECK_NEAR(pitch(*sy), 880.0, 1.0);
    // drums: kick.tune +12 → the kick settles an octave up; kick.pan −1 → right silent
    auto dm = DeviceRegistry::create("drums");
    dm->setParam("kick.pan", -1.0);
    dm->noteOn(36, 127);
    Stereo s(9600);
    Sample *io[2] = {s.L.data(), s.R.data()};
    dm->process(io, 2, 9600);
    double r = 0, l = 0;
    for (int i = 0; i < 9600; ++i) { r = std::max(r, (double)std::fabs(s.R[i])); l = std::max(l, (double)std::fabs(s.L[i])); }
    CHECK(r < 1e-12 && l > 0.05);
    CHECK(dm->type().paramIndex("cowbell.decay") >= 0);
    CHECK(dm->param(dm->type().paramIndex("cowbell.decay")) == 350.0);
}

TEST(Device_process_does_not_allocate_once_warm)
{
    configure();
    // Solaris runs every device on its audio thread, which must not allocate (R-PLAY-2): after one
    // warm block (the engine warms every device at build), process() and notes allocate nothing.
    for (const auto &t : DeviceRegistry::types())
    {
        auto d = DeviceRegistry::create(t.name);
        std::vector<Sample> L(128, 0.0), R(128, 0.0);
        Sample *io[2] = {L.data(), R.data()};
        d->process(io, 2, 128);
        gAllocs = 0;
        gCounting = true;
        d->noteOn(36, 120);
        d->noteOn(60, 100);
        for (int k = 0; k < 8; ++k)
        {
            for (int i = 0; i < 128; ++i) L[i] = R[i] = 0.3 * std::sin(0.05 * (k * 128 + i));
            d->process(io, 2, 128);
        }
        d->noteOff(60);
        gCounting = false;
        if (gAllocs.load() != 0) printf("    %s allocated %ld times on the audio path\n", t.name.c_str(), gAllocs.load());
        CHECK(gAllocs.load() == 0);
    }
}
