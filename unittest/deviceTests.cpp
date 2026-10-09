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

TEST(DeviceRegistry_names_the_drum_kits_keys)
{
    // REQ-device-6: a kit's keys mean something; a melodic instrument's do not
    const DeviceType *drums = DeviceRegistry::find("drums");
    CHECK(drums->noteNames.size() == (size_t)DrumMachine::PadCount);
    CHECK(drums->noteNames.front().first == 36 && drums->noteNames.front().second == "Kick");
    CHECK(drums->noteNames.back().first == 56 && drums->noteNames.back().second == "Cowbell");
    for (size_t i = 1; i < drums->noteNames.size(); ++i) CHECK(drums->noteNames[i - 1].first < drums->noteNames[i].first);
    for (const auto &n : drums->noteNames) CHECK(DrumMachine::padFor(n.first) >= 0); // every named key plays a pad
    CHECK(DeviceRegistry::find("synth")->noteNames.empty());
    CHECK(DeviceRegistry::find("eq")->noteNames.empty());
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

TEST(ParamSpec_normalised_and_text_faces_round_trip)
{
    // REQ-device-7: one mapping, so a VST3 plugin, a host and a panel agree to the last digit
    for (const auto &t : DeviceRegistry::types())
        for (const auto &p : t.params)
        {
            const int steps = paramSteps(p);
            if (p.isChoice() || p.integer)
            {
                // discrete: every step exact both ways, each owning an equal slice of 0…1
                CHECK(steps == (p.isChoice() ? (int)p.choices.size() - 1 : (int)std::lround(p.max - p.min)));
                for (int k = 0; k <= steps; ++k)
                {
                    const double v = (p.isChoice() ? 0.0 : p.min) + k;
                    const double n = normalizedFromValue(p, v);
                    CHECK(steps == 0 || n == (double)k / steps);
                    CHECK(valueFromNormalized(p, n) == v);
                    if (steps > 0) CHECK(valueFromNormalized(p, (k + 0.999) / (steps + 1)) == v); // its whole slice
                }
            }
            else
            {
                CHECK(steps == 0);
                CHECK(normalizedFromValue(p, p.min) == 0.0 && normalizedFromValue(p, p.max) == 1.0);
                CHECK(valueFromNormalized(p, 0.0) == p.min && std::fabs(valueFromNormalized(p, 1.0) - p.max) <= 1e-12 * std::fabs(p.max));
                for (double n : {0.1, 0.25, 0.5, 0.77, 0.9})
                {
                    const double v = valueFromNormalized(p, n);
                    CHECK(std::fabs(normalizedFromValue(p, v) - n) < 1e-12);
                }
                if (p.logScale && p.min > 0.0)
                {
                    // equal ratios, equal travel: the geometric mean sits at the middle
                    CHECK(std::fabs(valueFromNormalized(p, 0.5) - std::sqrt(p.min * p.max)) < 1e-9 * p.max);
                }
            }
            // the default round-trips through both faces exactly
            double back = -1.0;
            CHECK(paramFromText(p, paramToText(p, p.def), back) && back == p.def);
            // out of range and nonsense: clamped, or refused with nothing written
            CHECK(normalizedFromValue(p, 1e300) == 1.0 && normalizedFromValue(p, -1e300) == 0.0);
            double untouched = 42.0;
            CHECK(!paramFromText(p, "loud", untouched) && untouched == 42.0);
        }
    // the text is the suite's canonical number (Solaris's .slp): at least one decimal, shortest
    ParamSpec cut;
    cut.min = 20.0;
    cut.max = 20000.0;
    cut.def = 900.0;
    CHECK(paramToText(cut, 900.0) == "900.0" && paramToText(cut, 1234.5678) == "1234.5678");
    const double third = 20000.0 / 3.0;                     // no short text: the shortest that reads back
    double t3 = 0.0;
    CHECK(paramFromText(cut, paramToText(cut, third), t3) && t3 == third && paramToText(cut, third) == "6666.666666666667");
    ParamSpec wave;
    wave.choices = {"sine", "saw", "square"};
    double w = -1.0;
    CHECK(paramToText(wave, 1.0) == "saw" && paramFromText(wave, "square", w) && w == 2.0 && paramFromText(wave, "1", w) && w == 1.0);
}

TEST(Limiter_never_exceeds_its_ceiling_and_glides)
{
    configure();
    // REQ-fx-limiter-1: (L1)–(L5) keep every frame under the ceiling by construction — the final clamp
    // never does more than round — and the gain moves at most 1/(L+1) a frame (a glide, not a click)
    for (double look : {0.0, 0.5, 2.0, 10.0})
        for (int block : {1, 37, 128})
        {
            Limiter lim;
            lim.setGainDb(12.0);
            lim.setCeilingDb(-1.0);
            lim.setReleaseMs(40.0);
            lim.setLookaheadMs(look);
            const Sample ceiling = std::pow(10.0, -1.0 / 20.0);
            std::vector<Sample> L(24000), R(24000);
            for (size_t n = 0; n < L.size(); ++n)
            {
                const double t = n / 48000.0;
                L[n] = 0.6 * std::sin(2 * M_PI * 110 * t) + 0.3 * std::sin(2 * M_PI * 1730 * t) + (n % 4801 == 0 ? 2.5 : 0.0);
                R[n] = 0.5 * std::sin(2 * M_PI * 220 * t + 0.4) - (n % 3001 == 7 ? 3.0 : 0.0);
            }
            Sample worst = 0.0, step = 0.0, prev = 1.0;
            for (size_t pos = 0; pos < L.size(); pos += (size_t)block)
            {
                const int n = (int)std::min<size_t>((size_t)block, L.size() - pos);
                Sample *io[2] = {L.data() + pos, R.data() + pos};
                lim.process(io, 2, n);
                if (block == 1) { step = std::max(step, std::fabs(lim.lastGain() - prev)); prev = lim.lastGain(); }
                for (int i = 0; i < n; ++i) worst = std::max({worst, std::fabs(io[0][i]), std::fabs(io[1][i])});
            }
            CHECK(worst <= ceiling && lim.clamped() == 0);
            CHECK(worst > 0.9 * ceiling); // and it is limiting, not muting
            if (block == 1) CHECK(step <= 1.0 / (lim.latency() + 1) + 1e-12);
        }
    // under the ceiling it is transparent — the input, L samples late, exactly
    Limiter quiet;
    quiet.setLookaheadMs(1.0);
    std::vector<Sample> L(2000), R(2000), in(2000);
    for (size_t n = 0; n < L.size(); ++n) in[n] = L[n] = R[n] = 0.5 * std::sin(0.01 * n);
    Sample *io[2] = {L.data(), R.data()};
    quiet.process(io, 2, 2000);
    const int lat = quiet.latency();
    CHECK(lat == 48);
    bool exact = true;
    for (int n = lat; n < 2000; ++n) exact &= L[(size_t)n] == in[(size_t)(n - lat)];
    CHECK(exact);
    // after a burst the gain comes back: within 5 release times, above 0.99
    Limiter rel;
    rel.setLookaheadMs(0.0);
    rel.setReleaseMs(20.0);
    std::vector<Sample> b(48000, 0.0), b2(48000, 0.0);
    for (int n = 0; n < 480; ++n) b[(size_t)n] = b2[(size_t)n] = 4.0;
    for (int n = 480; n < 48000; ++n) b[(size_t)n] = b2[(size_t)n] = 0.1;
    Sample *bio[2] = {b.data(), b2.data()};
    rel.process(bio, 2, 480 + 4800);
    CHECK(rel.lastGain() > 0.99);
}

TEST(Compressor_sidechain_ducks_on_the_key_not_the_input)
{
    configure();
    // REQ-fx-sidechain-1: keyed, the detector follows the key; the gain is (C3) of the KEY's level
    auto run = [](bool sidechain, bool withKey) {
        Compressor c;
        c.setThresholdDb(-30.0);
        c.setRatio(4.0);
        c.setAttackMs(1.0);
        c.setReleaseMs(50.0);
        c.setSidechain(sidechain);
        std::vector<Sample> L(9600), R(9600), kL(9600, 1.0), kR(9600, 1.0); // a 0 dBFS key, steady
        for (size_t n = 0; n < L.size(); ++n) L[n] = R[n] = 0.01 * std::sin(2 * M_PI * 55 * n / 48000.0); // −40 dBFS: below threshold
        const Sample *key[2] = {kL.data(), kR.data()};
        for (size_t pos = 0; pos < L.size(); pos += 128)
        {
            const Sample *k[2] = {key[0] + pos, key[1] + pos};
            c.setKey(withKey ? k : nullptr, 2);
            c.processBlock(L.data() + pos, 128, 0);
            c.processBlock(R.data() + pos, 128, 1);
        }
        // the gain at the end: output / input on the last cycle's peak region
        double out = 0, in = 0;
        for (size_t n = 8640; n < 9600; ++n) { out += L[n] * L[n]; const double x = 0.01 * std::sin(2 * M_PI * 55 * n / 48000.0); in += x * x; }
        return 20.0 * std::log10(std::sqrt(out / in));
    };
    // keyed by a 0 dBFS key: (0 − (−30)) · (1 − 1/4) = 22.5 dB of reduction, though the input is quiet
    CHECK(std::fabs(run(true, true) - (-22.5)) < 0.05);
    // keyed with no key: a silent key, no reduction
    CHECK(std::fabs(run(true, false)) < 1e-9);
    // not keyed: the quiet input is under threshold — untouched, whatever key is handed in
    CHECK(std::fabs(run(false, true)) < 1e-9);
    // the registry says so, and the device passes the key through
    const DeviceType *t = DeviceRegistry::find("compressor");
    CHECK(t && t->takesKey && t->paramIndex("sidechain") == (int)t->params.size() - 1);
    CHECK(!DeviceRegistry::find("eq")->takesKey);
    auto d = DeviceRegistry::create("compressor");
    d->setParam("threshold", -30.0);
    d->setParam("sidechain", 1.0);
    std::vector<Sample> L(4800), R(4800), kL(4800, 1.0), kR(4800, 1.0);
    for (size_t n = 0; n < L.size(); ++n) L[n] = R[n] = 0.01;
    for (size_t pos = 0; pos < L.size(); pos += 96)
    {
        const Sample *k[2] = {kL.data() + pos, kR.data() + pos};
        d->setKey(k, 2);
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, 96);
    }
    CHECK(L.back() < 0.01 * std::pow(10.0, -20.0 / 20.0));
}

TEST(Sampler_plays_its_sound_pitched_spanned_and_reversed)
{
    configure();
    // REQ-inst-sampler-1, (S1)–(S5): a 440 Hz recording at 48 kHz, a second long
    const int N = 48000;
    std::vector<float> rec(N);
    for (int i = 0; i < N; ++i) rec[(size_t)i] = (float)(0.5 * std::sin(2 * M_PI * 440.0 * i / 48000.0));
    auto play = [&](Sampler::Params p, int note, int frames, double rate = 48000.0, bool release = false) {
        Sampler s;
        s.setParams(p);
        s.setSample(rec.data(), N, 1, rate);
        s.noteOn(note, 127);
        if (release) s.noteOff(note);
        std::vector<Sample> L(frames, 0.0), R(frames, 0.0);
        Sample *io[2] = {L.data(), R.data()};
        for (int pos = 0; pos < frames; pos += 128)
        {
            Sample *seg[2] = {L.data() + pos, R.data() + pos};
            s.render(seg, 2, std::min(128, frames - pos));
        }
        (void)io;
        return L;
    };
    auto crossingsHz = [](const std::vector<Sample> &y, int from, int to) {
        int n = 0;
        for (int i = from + 1; i < to; ++i) n += (y[(size_t)i - 1] < 0.0) != (y[(size_t)i] < 0.0);
        return n / 2.0 / ((to - from) / 48000.0);
    };
    Sampler::Params flat;
    flat.velocity = 0.0; // every note full: the level is the recording's
    // at the root: the recording itself, sample for sample (a mono sound on both sides)
    const auto root = play(flat, 60, N);
    bool exact = true;
    for (int i = 0; i < N - 1; ++i) exact &= root[(size_t)i] == (Sample)rec[(size_t)i];
    CHECK(exact);
    // an octave up: twice the frequency, and over in half the time
    const auto up = play(flat, 72, N);
    CHECK(std::fabs(crossingsHz(up, 0, 12000) - 880.0) < 4.0);
    double tail = 0;
    for (int i = 24100; i < N; ++i) tail = std::max(tail, std::fabs(up[(size_t)i]));
    CHECK(tail == 0.0);
    // a fifth down: 440 · 2^(−7/12) — and (S3)'s linear interpolation keeps it a clean tone: within
    // 2e-3 of the analytic sine (nearest-frame playback is off by ~1.4e-2)
    const auto fifth = play(flat, 53, N);
    const double f5 = 440.0 * std::pow(2.0, -7.0 / 12.0);
    CHECK(std::fabs(crossingsHz(fifth, 0, 24000) - f5) < 4.0);
    double err = 0;
    for (int i = 0; i < 24000; ++i) err = std::max(err, std::fabs(fifth[(size_t)i] - 0.5 * std::sin(2 * M_PI * f5 * i / 48000.0)));
    CHECK(err < 2e-3);
    // one-shot: as recorded whatever the key, and a note-off does not stop it
    Sampler::Params shot = flat;
    shot.mode = Sampler::OneShot;
    const auto hit = play(shot, 84, N, 48000.0, true);
    CHECK(std::fabs(crossingsHz(hit, 0, 24000) - 440.0) < 4.0 && std::fabs(hit[40000]) > 0.0);
    // reversed: the recording backwards; a span: from its middle
    Sampler::Params rev = flat;
    rev.reverse = true;
    const auto back = play(rev, 60, N);
    bool reversed = true;
    for (int i = 0; i < N - 1; ++i) reversed &= back[(size_t)i] == (Sample)rec[(size_t)(N - 1 - i)];
    CHECK(reversed);
    Sampler::Params half = flat;
    half.start = 0.5;
    const auto mid = play(half, 60, N / 2);
    bool spanned = true;
    for (int i = 0; i < N / 2 - 1; ++i) spanned &= mid[(size_t)i] == (Sample)rec[(size_t)(N / 2 + i)];
    CHECK(spanned);
    // recorded at 24 kHz, it keeps its pitch at 48 kHz (the file's rate against ours)
    {
        std::vector<float> r24(24000);
        for (int i = 0; i < 24000; ++i) r24[(size_t)i] = (float)(0.5 * std::sin(2 * M_PI * 440.0 * i / 24000.0));
        Sampler s;
        s.setParams(flat);
        s.setSample(r24.data(), 24000, 1, 24000.0);
        s.noteOn(60, 127);
        std::vector<Sample> L(24000, 0.0), R(24000, 0.0);
        Sample *io[2] = {L.data(), R.data()};
        s.render(io, 2, 24000);
        CHECK(std::fabs(crossingsHz(L, 0, 24000) - 440.0) < 4.0);
    }
    // chromatic: released, it rings out by its release and stops
    Sampler::Params rel = flat;
    rel.releaseMs = 10.0;
    const auto off = play(rel, 60, N, 48000.0, true);
    double late = 0;
    for (int i = 1000; i < N; ++i) late = std::max(late, std::fabs(off[(size_t)i]));
    CHECK(late == 0.0);
    // through the registry: it takes a sample, nothing else does, and with none it is silence
    const DeviceType *t = DeviceRegistry::find("sampler");
    CHECK(t && t->takesSample && t->kind == DeviceKind::Instrument && !DeviceRegistry::find("synth")->takesSample);
    auto d = DeviceRegistry::create("sampler");
    std::vector<Sample> L(256, 0.0), R(256, 0.0);
    Sample *io[2] = {L.data(), R.data()};
    d->noteOn(60, 100);
    d->process(io, 2, 256);
    CHECK(L[100] == 0.0);
    d->setSample(rec.data(), N, 1, 48000.0);
    d->noteOn(60, 127);
    d->process(io, 2, 256);
    CHECK(std::fabs(L[100]) > 0.0);
}
