/*
 *  Arstro DSP — the D2 instruments: BasicSynth and DrumMachine (REQ-synth2-*, REQ-drum-*), and
 *  the Phasor under the drums.
 *
 *  Judged on rendered samples: a pitch is counted from zero crossings, a filter from the energy a
 *  high-pass Biquad lets through, an envelope from when the output goes silent.
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

using namespace arstro;

namespace
{
    void configure()
    {
        AudioConfig::instance().setSampleRate(48000);
        AudioConfig::instance().setBufferSize(128);
        AudioConfig::instance().setChannelCount(2);
    }

    // Render `frames` stereo samples of `inst` in 128-sample blocks; `at` is called before each block.
    struct Render
    {
        std::vector<Sample> L, R;
    };
    template <class F> Render render(Instrument &inst, int frames, F at)
    {
        Render r;
        r.L.assign(frames, 0.0);
        r.R.assign(frames, 0.0);
        for (int pos = 0; pos < frames; pos += 128)
        {
            at(pos);
            const int n = std::min(128, frames - pos);
            Sample *o[2] = {r.L.data() + pos, r.R.data() + pos};
            inst.render(o, 2, n);
        }
        return r;
    }
    Render render(Instrument &inst, int frames) { return render(inst, frames, [](int) {}); }

    double rmsOf(const std::vector<Sample> &x, int from, int to)
    {
        double s = 0;
        for (int i = from; i < to; ++i) s += x[i] * x[i];
        return std::sqrt(s / std::max(1, to - from));
    }
    double peakOf(const std::vector<Sample> &x, int from, int to)
    {
        double p = 0;
        for (int i = from; i < to; ++i) p = std::max(p, (double)std::fabs(x[i]));
        return p;
    }
    // Frequency from rising zero crossings over [from, to).
    double pitchOf(const std::vector<Sample> &x, int from, int to)
    {
        int first = -1, last = -1, count = 0;
        for (int i = from + 1; i < to; ++i)
            if (x[i - 1] < 0 && x[i] >= 0)
            {
                if (first < 0) first = i;
                last = i;
                ++count;
            }
        return count > 1 ? (count - 1) * 48000.0 / (last - first) : 0.0;
    }
    // Share of energy above `hz` (through an RBJ high-pass), in dB.
    double highBandDb(const std::vector<Sample> &x, int from, int to, double hz)
    {
        Biquad hp(Biquad::HighPass, hz, 0.70710678);
        double all = 0, hi = 0;
        for (int i = 0; i < to; ++i)
        {
            const double y = hp.out(x[i], 0);
            if (i >= from) { all += x[i] * x[i]; hi += y * y; }
        }
        return 10.0 * std::log10(hi / all);
    }

    BasicSynth::Params sineOnly()
    {
        BasicSynth::Params p;
        p.osc1.wave = BasicSynth::Sine;
        p.osc2.level = 0.0;
        p.cutoff = 20000.0;
        p.envAmount = 0.0;
        p.keytrack = 0.0;
        p.resonance = 0.0;
        p.volumeDb = 0.0;
        return p;
    }
}

TEST(Phasor_wraps_and_counts_cycles)
{
    Phasor p;
    int wraps = 0;
    Sample prev = 0;
    for (int i = 0; i < 48000; ++i)
    {
        const Sample ph = p.advance(440.0 / 48000.0);
        CHECK(ph >= 0.0 && ph < 1.0);
        if (ph < prev) ++wraps;
        prev = ph;
    }
    CHECK(wraps == 439 || wraps == 440);
    p.reset(0.75);
    CHECK(p.phase() == 0.75);
    CHECK(p.square(0.0) == -1.0);
    p.reset(0.25);
    CHECK_NEAR(p.sine(0.0), 1.0, 1e-12);
    p.advance(-0.5);                  // a negative increment still wraps into [0,1)
    CHECK(p.phase() >= 0.0 && p.phase() < 1.0);
}

TEST(BasicSynth_plays_the_note_and_each_oscillator_moves_its_pitch)
{
    configure();
    BasicSynth s;
    s.setParams(sineOnly());
    s.noteOn(69, 127);
    auto r = render(s, 48000);
    CHECK_NEAR(pitchOf(r.L, 4800, 48000), 440.0, 0.5);
    CHECK_NEAR(s.oscFrequency(0, 69), 440.0, 1e-9);
    // octave, semitones and cents add: +1 oct, +7 st, +0 ct → E6
    auto p = sineOnly();
    p.osc1.octave = 1;
    p.osc1.semi = 7.0;
    s.setParams(p);
    CHECK_NEAR(s.oscFrequency(0, 69), 440.0 * 2.0 * std::pow(2.0, 7.0 / 12.0), 1e-9);
    BasicSynth s2;
    s2.setParams(p);
    s2.noteOn(69, 127);
    auto r2 = render(s2, 24000);
    CHECK_NEAR(pitchOf(r2.L, 4800, 24000), 440.0 * 2.0 * std::pow(2.0, 7.0 / 12.0), 2.0);
    p.osc2.fine = 50.0;
    s.setParams(p);
    CHECK_NEAR(s.oscFrequency(1, 60), 440.0 * std::pow(2.0, (60 - 12 - 69 + 0.5) / 12.0), 1e-9); // osc2 default: −1 octave
}

TEST(BasicSynth_filter_cutoff_and_envelope_shape_the_brightness)
{
    configure();
    auto bright = [](double cutoff, double envAmt) {
        BasicSynth s;
        BasicSynth::Params p;
        p.cutoff = cutoff;
        p.envAmount = envAmt;
        p.keytrack = 0.0;
        p.resonance = 0.0;
        s.setParams(p);
        s.noteOn(48, 127);
        auto r = render(s, 24000);
        return highBandDb(r.L, 2400, 24000, 3000.0);
    };
    const double open = bright(12000.0, 0.0), shut = bright(300.0, 0.0);
    CHECK(open - shut > 25.0);        // a 300 Hz low-pass removes the saw's upper harmonics
    // the filter envelope opens a shut filter: brighter in the attack than after the decay
    BasicSynth s;
    BasicSynth::Params p;
    p.cutoff = 300.0;
    p.envAmount = 5.0;
    p.keytrack = 0.0;
    p.filterEnv = {1.0, 150.0, 0.0, 100.0};
    s.setParams(p);
    s.noteOn(48, 127);
    auto r = render(s, 48000);
    CHECK(highBandDb(r.L, 240, 2400, 3000.0) - highBandDb(r.L, 28800, 48000, 3000.0) > 15.0);
    // the formula: env adds octaves, key tracking follows the note, clamped to [20, 20000]
    CHECK_NEAR(s.cutoffFor(48, 1.0), 300.0 * 32.0, 1e-6);
    p.keytrack = 1.0;
    p.envAmount = 0.0;
    s.setParams(p);
    CHECK_NEAR(s.cutoffFor(72, 0.0), 600.0, 1e-6);
    p.cutoff = 15000.0;
    p.envAmount = 8.0;
    s.setParams(p);
    CHECK(s.cutoffFor(60, 1.0) == 20000.0);
}

TEST(BasicSynth_release_ends_the_voice_and_the_tail_is_not_cut)
{
    configure();
    BasicSynth s;
    auto p = sineOnly();
    p.ampEnv = {1.0, 10.0, 1.0, 200.0}; // 200 ms release
    s.setParams(p);
    s.noteOn(60, 127);
    auto r = render(s, 48000, [&](int pos) { if (pos == 9600) s.noteOff(60); });
    // 100 ms into the 200 ms linear release the level is about half the sustain
    const double sustain = peakOf(r.L, 4800, 9600), mid = peakOf(r.L, 14000, 14600);
    CHECK(mid > 0.35 * sustain && mid < 0.65 * sustain);
    CHECK(peakOf(r.L, 20400, 48000) < 1e-9);  // silent once the release is over
    CHECK(s.activeVoices() == 0);
}

TEST(BasicSynth_polyphony_retrigger_steal_velocity)
{
    configure();
    BasicSynth s;
    s.setParams(sineOnly());
    for (int n = 0; n < BasicSynth::kVoices; ++n) s.noteOn(40 + n, 100);
    CHECK(s.activeVoices() == BasicSynth::kVoices);
    s.noteOn(40, 100);                 // the same note retriggers its own voice
    CHECK(s.activeVoices() == BasicSynth::kVoices);
    s.noteOn(100, 100);                // a 17th note steals the oldest — still 16
    CHECK(s.activeVoices() == BasicSynth::kVoices);
    s.allNotesOff();
    s.reset();
    CHECK(s.activeVoices() == 0);
    s.noteOn(200, 100);                // out of range: ignored
    s.noteOn(60, 0);                   // velocity 0 is a note-off
    CHECK(s.activeVoices() == 0);
    // velocity scales level by the sensitivity (0.8): vel 32 ≈ (0.2 + 0.8·32/127) of vel 127
    auto loud = [](int vel) {
        BasicSynth x;
        x.setParams(sineOnly());
        x.noteOn(69, vel);
        auto r = render(x, 9600);
        return rmsOf(r.L, 4800, 9600);
    };
    CHECK_NEAR(loud(32) / loud(127), 0.2 + 0.8 * 32.0 / 127.0, 0.01);
}

TEST(BasicSynth_is_deterministic_and_stereo_identical_without_spread)
{
    configure();
    auto take = []() {
        BasicSynth s;
        BasicSynth::Params p;
        p.noise = 0.3;
        p.osc1.voices = 3;
        s.setParams(p);
        s.noteOn(57, 90);
        s.noteOn(64, 70);
        return render(s, 12000);
    };
    auto a = take(), b = take();
    CHECK(a.L == b.L && a.R == b.R);
    CHECK(peakOf(a.L, 0, 12000) > 0.05);
    // with no noise and no stereo spread the two channels are the same signal
    BasicSynth s;
    s.setParams(sineOnly());
    s.noteOn(60, 100);
    auto r = render(s, 4800);
    CHECK(r.L == r.R);
    // a mono host still gets the left channel, and the voice still ends
    std::vector<Sample> mono(4800, 0.0);
    Sample *o[1] = {mono.data()};
    BasicSynth m;
    m.setParams(sineOnly());
    m.noteOn(60, 100);
    m.render(o, 1, 4800);
    CHECK(peakOf(mono, 0, 4800) > 0.1);
}

TEST(DrumMachine_every_pad_sounds_on_its_GM_note_and_ends)
{
    configure();
    const int notes[] = {36, 37, 38, 39, 41, 42, 45, 46, 48, 56};
    for (int p = 0; p < DrumMachine::PadCount; ++p)
    {
        CHECK(DrumMachine::noteFor((DrumMachine::Pad)p) == notes[p]);
        CHECK(DrumMachine::padFor(notes[p]) == p);
        DrumMachine d;
        d.noteOn(notes[p], 127);
        CHECK(d.activeVoices() == 1);
        auto r = render(d, 48000 * 3);
        CHECK(peakOf(r.L, 0, 4800) > 0.05);
        CHECK(d.activeVoices() == 0);                 // every pad decays and stops
        CHECK(peakOf(r.L, 48000 * 3 - 4800, 48000 * 3) == 0.0);
    }
    CHECK(DrumMachine::padFor(60) == -1);
    CHECK(DrumMachine::noteFor((DrumMachine::Pad)99) == -1);
    CHECK(std::string(DrumMachine::padName(DrumMachine::OpenHat)) == "ohat");
    CHECK(std::string(DrumMachine::padName((DrumMachine::Pad)-1)).empty());
    DrumMachine d;
    d.noteOn(60, 127);                 // not a pad
    d.noteOn(36, 0);                   // velocity 0
    d.noteOff(36);                     // drums ignore note-off
    d.allNotesOff();
    CHECK(d.activeVoices() == 0);
}

TEST(DrumMachine_kick_falls_to_its_tuned_fundamental)
{
    configure();
    DrumMachine d;
    d.noteOn(36, 127);
    auto r = render(d, 24000);
    const double early = pitchOf(r.L, 0, 1440), late = pitchOf(r.L, 9600, 19200);
    CHECK(early > 2.0 * late);         // the sweep: well over an octave higher in the first 30 ms
    CHECK_NEAR(late, 48.0, 2.0);       // settles on f_end = 48 Hz
    auto p = DrumMachine::defaults(DrumMachine::Kick);
    p.tune = 12.0;
    DrumMachine up;
    up.setPad(DrumMachine::Kick, p);
    up.noteOn(36, 127);
    auto r2 = render(up, 24000);
    CHECK_NEAR(pitchOf(r2.L, 9600, 19200), 96.0, 3.0); // +12 semitones = one octave up
}

TEST(DrumMachine_closed_hat_chokes_the_open_hat)
{
    configure();
    auto openTail = [](bool choke) {
        DrumMachine d;
        d.noteOn(46, 127);
        // the closed hat lands at 4864 (a block boundary, ~101 ms), velocity 1 so its own sound is tiny
        auto r = render(d, 24000, [&](int pos) { if (choke && pos == 4864) d.noteOn(42, 1); });
        return rmsOf(r.L, 7680, 14400);  // 160..300 ms: past the closed hat's own 60 ms

    };
    const double free = openTail(false), choked = openTail(true);
    CHECK(free > 1e-3);
    CHECK(choked < free * 0.001);      // more than 60 dB quieter once choked
}

TEST(DrumMachine_decay_level_pan_and_determinism)
{
    configure();
    auto length = [](double decayMs) {
        DrumMachine d;
        auto p = DrumMachine::defaults(DrumMachine::Snare);
        p.decay = decayMs;
        d.setPad(DrumMachine::Snare, p);
        d.noteOn(38, 127);
        int n = 0;
        while (d.activeVoices() && n < 48000 * 4) { render(d, 128); n += 128; }
        return n;
    };
    CHECK(length(400.0) > 1.6 * length(200.0));   // decay scales the sound's length
    // hard-left pan leaves the right channel silent; level −6 dB halves the amplitude
    DrumMachine d;
    auto p = DrumMachine::defaults(DrumMachine::Kick);
    p.pan = -1.0;
    d.setPad(DrumMachine::Kick, p);
    CHECK(d.pad(DrumMachine::Kick).pan == -1.0);
    d.noteOn(36, 127);
    auto r = render(d, 4800);
    CHECK(peakOf(r.R, 0, 4800) < 1e-12);
    CHECK(peakOf(r.L, 0, 4800) > 0.1);
    auto peakAt = [](double levelDb) {
        DrumMachine x;
        auto q = DrumMachine::defaults(DrumMachine::LowTom);
        q.levelDb = levelDb;
        x.setPad(DrumMachine::LowTom, q);
        x.noteOn(41, 127);
        return peakOf(render(x, 9600).L, 0, 9600);
    };
    CHECK_NEAR(peakAt(-6.0206) / peakAt(0.0), 0.5, 1e-6);
    // a reset replays the same noise: two renders of one pattern are byte-identical
    DrumMachine a;
    auto pattern = [&](DrumMachine &m) {
        m.reset();
        return render(m, 24000, [&](int pos) {
            if (pos == 0) { m.noteOn(36, 120); m.noteOn(42, 90); }
            if (pos == 6016) { m.noteOn(38, 110); m.noteOn(39, 100); }
            if (pos == 12032) { m.noteOn(46, 80); m.noteOn(37, 70); m.noteOn(56, 60); }
            if (pos == 18048) { m.noteOn(45, 100); m.noteOn(48, 100); }
        });
    };
    auto first = pattern(a), second = pattern(a);
    CHECK(first.L == second.L && first.R == second.R);
    a.setVolumeDb(-3.0);
    CHECK(a.volumeDb() == -3.0);
    a.setPad((DrumMachine::Pad)42, p); // out of range: ignored
    // a mono host gets the sum, unpanned
    DrumMachine m;
    std::vector<Sample> mono(4800, 0.0);
    Sample *o[1] = {mono.data()};
    m.noteOn(38, 127);
    m.render(o, 1, 4800);
    m.render(o, 0, 4800); // no channels: nothing
    CHECK(peakOf(mono, 0, 4800) > 0.05);
}

TEST(BasicSynth_noise_does_not_depend_on_block_size)
{
    configure();
    // the same note with noise, rendered in blocks of 128 and in blocks of 77: the same samples
    auto take = [](int block) {
        BasicSynth s;
        BasicSynth::Params p;
        p.noise = 0.5;
        s.setParams(p);
        s.noteOn(57, 100);
        std::vector<Sample> L(9600, 0.0), R(9600, 0.0);
        for (int pos = 0; pos < 9600; pos += block)
        {
            Sample *o[2] = {L.data() + pos, R.data() + pos};
            s.render(o, 2, std::min(block, 9600 - pos));
        }
        return std::make_pair(L, R);
    };
    const auto a = take(128), b = take(77);
    CHECK(a.first == b.first && a.second == b.second);
    CHECK(a.first != a.second); // and the noise is stereo: each channel has its own
}

// A counting allocator for the whole test binary: off except inside the window a test opens.
std::atomic<long> gAllocs{0};      // shared with deviceTests.cpp
std::atomic<bool> gCounting{false};
void *operator new(size_t n)
{
    if (gCounting.load(std::memory_order_relaxed)) gAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }

TEST(BasicSynth_render_does_not_allocate_after_construction)
{
    configure();
    // A host's audio thread must not allocate (Solaris R-PLAY-2): every buffer render() touches —
    // the voices' noise generators, the scratch block — exists once the constructor has run.
    BasicSynth s;
    BasicSynth::Params p;
    p.noise = 0.5;
    s.setParams(p);
    for (int n = 0; n < 16; ++n) s.noteOn(40 + n, 100);
    std::vector<Sample> L(4096, 0.0), R(4096, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    gAllocs = 0;
    gCounting = true;
    s.render(o, 2, 4096);           // the largest block the constructor sized for
    s.render(o, 2, 128);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
    CHECK(s.activeVoices() == 16);
}
