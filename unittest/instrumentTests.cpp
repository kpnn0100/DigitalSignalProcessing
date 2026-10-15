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
#include <functional>
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

// ── the piano (REQ-inst-piano-1) ───────────────────────────────────────────────────────────

namespace
{
    // a tone's energy, dB, by Goertzel over [from, to) of `x`
    double toneDb(const std::vector<Sample> &x, double hz, int from, int to)
    {
        const double w = 2.0 * M_PI * hz / 48000.0, c = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < to; ++i)
        {
            const double s0 = x[(size_t)i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return 10.0 * std::log10(s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-30);
    }
}

TEST(Piano_plays_in_tune)
{
    // (P1) and the engine's tuning: A4 sounds at 440 Hz — its fundamental found within ±30 cents in 1-cent steps
    configure();
    Piano p;
    p.noteOn(69, 100);
    const auto r = render(p, 48000 * 3);
    double best = -1e9;
    int at = 0;
    for (int cents = -30; cents <= 30; ++cents)
    {
        const double db = toneDb(r.L, 440.0 * std::pow(2.0, cents / 1200.0), 48000, 48000 * 3);
        if (db > best) { best = db; at = cents; }
    }
    CHECK(std::abs(at) <= 5);
    CHECK(peakOf(r.L, 0, 48000) > 0.05);
}

TEST(Piano_keeps_a_held_bass_under_an_arpeggio)
{
    // a held key is never stolen while a released one rings: forty sixteenths (more than the 16 voices) over a
    // held C2 — the bass sounds after them as it does alone
    configure();
    auto bass = [](bool arpeggio) {
        Piano p;
        p.noteOn(36, 100);
        const int step = 47 * 128; // a sixteenth at ~120 bpm, on a block edge
        int last = -1;
        const auto r = render(p, 48000 * 6, [&](int pos) {
            if (!arpeggio) return;
            if (pos % step == 0 && pos / step < 40)
            {
                last = 60 + (pos / step * 7) % 24;
                p.noteOn(last, 90);
            }
            if (pos % step == 39 * 128 && last >= 0) p.noteOff(last); // let go before the next
        });
        return toneDb(r.L, 65.406, 48000 * 5, 48000 * 6); // C2's fundamental, in the last second
    };
    const double alone = bass(false), under = bass(true);
    CHECK(alone > -40.0);
    CHECK(std::fabs(under - alone) < 6.0);
}

TEST(Piano_reset_is_silent_at_once_and_starts_again_the_same)
{
    // (P3): a seek is silence now, every voice and the bridge cleared — and the same notes after it are the
    // same samples (determinism); a stop lets the keys go and the pedal keeps them ringing
    configure();
    Piano p;
    auto play = [&] {
        p.noteOn(60, 100);
        p.noteOn(64, 90);
        return render(p, 9600);
    };
    const auto first = play();
    p.reset();
    CHECK(p.activeVoices() == 0);
    const auto quiet = render(p, 1280);
    CHECK(peakOf(quiet.L, 0, 1280) == 0.0 && peakOf(quiet.R, 0, 1280) == 0.0);
    const auto again = play();
    bool same = true;
    for (int i = 0; i < 9600; ++i) same &= first.L[(size_t)i] == again.L[(size_t)i] && first.R[(size_t)i] == again.R[(size_t)i];
    CHECK(same);
    auto after = [](bool pedal) {
        Piano q;
        Piano::Params prm = q.params();
        prm.sustain = pedal;
        q.setParams(prm);
        q.noteOn(60, 100);
        render(q, 4800);
        q.allNotesOff();
        const auto r = render(q, 48000);
        return rmsOf(r.L, 36000, 48000);
    };
    CHECK(after(true) > 4.0 * after(false)); // the pedal holds what the stop let go
}

TEST(BasicSynth_unison_is_wide_and_does_not_flam)
{
    // REQ-synth2-6: a width detunes the channels apart (a supersaw is wide); no width is mono-identical; unison
    // voices start at spread phases, so a detuned unison's attack is not a spike of five aligned saws
    configure();
    auto play = [](double width, std::vector<Sample> &L, std::vector<Sample> &R) {
        BasicSynth s;
        BasicSynth::Params p;
        p.osc1.voices = 5;
        p.osc1.detune = 30.0;
        p.osc1.width = width;
        p.osc2.level = 0.0;
        p.cutoff = 20000.0;
        p.envAmount = 0.0;
        p.ampEnv = {0.0, 10.0, 1.0, 50.0};
        s.setParams(p);
        s.noteOn(57, 127);
        const int n = 48000;
        L.assign(n, 0.0);
        R.assign(n, 0.0);
        for (int i = 0; i < n; i += 128)
        {
            Sample *o[2] = {L.data() + i, R.data() + i};
            s.render(o, 2, 128);
        }
    };
    std::vector<Sample> L, R;
    play(0.0, L, R);
    double diff = 0.0;
    for (size_t i = 0; i < L.size(); ++i) diff = std::max(diff, (double)std::fabs(L[i] - R[i]));
    CHECK(diff == 0.0);                                          // no width: mono-identical
    double attack = 0.0;
    for (int i = 0; i < 144; ++i) attack = std::max(attack, (double)std::fabs(L[(size_t)i]));
    CHECK(attack / rmsOf(L, 9600, 19200) < 1.5);                 // no aligned spike (aligned saws: well above)
    play(20.0, L, R);
    double sl = 0, sr = 0, slr = 0;
    for (int i = 12000; i < 36000; ++i) { sl += L[(size_t)i] * L[(size_t)i]; sr += R[(size_t)i] * R[(size_t)i]; slr += L[(size_t)i] * R[(size_t)i]; }
    CHECK(slr / std::sqrt(sl * sr) < 0.7);                       // wide: the channels drift apart
    CHECK(rmsOf(L, 12000, 36000) > 0.05 && rmsOf(R, 12000, 36000) > 0.05);
}

TEST(DrumMachine_crash_and_ride_ring_as_cymbals)
{
    // REQ-drum-6: crash 49 and ride 51 — at the kit's level, ringing for their decay, the crash bright and long,
    // the ride's bell standing out around 3 kHz and shorter
    configure();
    auto hit = [](int note, std::vector<Sample> &L) {
        DrumMachine d;
        d.setVolumeDb(0.0);
        d.noteOn(note, 127);
        const int n = 48000 * 3;
        L.assign(n, 0.0);
        std::vector<Sample> R(n, 0.0);
        for (int i = 0; i < n; i += 128)
        {
            Sample *o[2] = {L.data() + i, R.data() + i};
            d.render(o, 2, 128);
        }
    };
    auto t60 = [](const std::vector<Sample> &x) { // ms until the 50 ms RMS is 60 dB under its loudest
        std::vector<double> r;
        double best = 0;
        for (size_t i = 0; i + 2400 <= x.size(); i += 2400) { r.push_back(rmsOf(x, (int)i, (int)i + 2400)); best = std::max(best, r.back()); }
        for (size_t i = 0; i < r.size(); ++i)
            if (r[i] < best * 1e-3) return (double)i * 50.0;
        return 1e9;
    };
    auto band = [](const std::vector<Sample> &x, double lo, double hi) { // energy share between lo and hi Hz, 50–250 ms
        double in = 0, all = 0;
        for (double f = 250.0; f < 16000.0; f *= 1.05)
        {
            const double w = 2.0 * M_PI * f / 48000.0, c = 2.0 * std::cos(w);
            double s1 = 0, s2 = 0;
            for (int i = 2400; i < 12000; ++i) { const double s0 = x[(size_t)i] + c * s1 - s2; s2 = s1; s1 = s0; }
            const double e = s1 * s1 + s2 * s2 - c * s1 * s2;
            all += e;
            if (f >= lo && f < hi) in += e;
        }
        return in / all;
    };
    std::vector<Sample> crash, ride;
    hit(49, crash);
    hit(51, ride);
    CHECK(peakOf(crash, 0, 48000) > 0.45 && peakOf(crash, 0, 48000) < 0.7);
    CHECK(peakOf(ride, 0, 48000) > 0.45 && peakOf(ride, 0, 48000) < 0.7);
    CHECK(std::fabs(t60(crash) - 1800.0) <= 270.0);              // its decay, within 15 %
    CHECK(std::fabs(t60(ride) - 1200.0) <= 180.0);
    CHECK(t60(crash) > t60(ride));
    CHECK(band(crash, 3000.0, 16000.0) > 0.6 && band(crash, 250.0, 1000.0) < 0.08); // bright, no low mud
    CHECK(band(ride, 2500.0, 3600.0) > 0.2 && band(ride, 8000.0, 16000.0) > 0.1);   // the bell stands out over its wash
}

TEST(Volume_and_level_move_a_sounding_note)
{
    // D-8 (REQ-synth2-4, REQ-inst-sampler-1 amended): the synth's volume and the sampler's level, set while a note
    // sounds, move THAT note — by exactly their dB — and without a step (a ramp across the block)
    configure();
    auto drop = [](Instrument &inst, const std::function<void()> &lower, double &db, double &step, double &own) {
        const int n = 24000;
        std::vector<Sample> L(2 * n, 0.0), R(2 * n, 0.0);
        for (int i = 0; i < 2 * n; i += 128)
        {
            if (i <= n && i + 128 > n) lower(); // the block holding the change (blocks start at multiples of 128)
            Sample *o[2] = {L.data() + i, R.data() + i};
            inst.render(o, 2, 128);
        }
        db = 20.0 * std::log10(rmsOf(L, n + 4800, 2 * n) / rmsOf(L, 4800, n));
        step = 0.0;
        own = 0.0;
        for (int i = n - 256; i < n + 256; ++i) step = std::max(step, (double)std::fabs(L[(size_t)i + 1] - L[(size_t)i]));
        for (int i = 4800; i < n - 512; ++i) own = std::max(own, (double)std::fabs(L[(size_t)i + 1] - L[(size_t)i])); // before the change
    };
    double db = 0, step = 0, own = 0;
    BasicSynth s;
    BasicSynth::Params p;
    p.osc1.wave = BasicSynth::Sine;
    p.osc2.level = 0.0;
    p.cutoff = 20000.0;
    p.envAmount = 0.0;
    p.ampEnv = {0.0, 10.0, 1.0, 50.0};
    p.volumeDb = 0.0;
    s.setParams(p);
    s.noteOn(69, 127);
    drop(s, [&] { p.volumeDb = -20.0; s.setParams(p); }, db, step, own);
    CHECK(std::fabs(db + 20.0) < 0.3);   // the sounding note, 20 dB down
    CHECK(step <= own * 1.05);           // no step where it changed: no faster than the sine itself moves
    // the sampler: a recorded 440 Hz sine, its level lowered mid-note
    std::vector<float> rec(48000 * 2);
    for (int i = 0; i < 48000; ++i) rec[(size_t)i * 2] = rec[(size_t)i * 2 + 1] = (float)(0.5 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
    Sampler sm;
    sm.setSample(rec.data(), 48000, 2, 48000.0);
    Sampler::Params sp = sm.params();
    sp.levelDb = 0.0;
    sm.setParams(sp);
    sm.noteOn(sp.root, 127);
    drop(sm, [&] { sp.levelDb = -20.0; sm.setParams(sp); }, db, step, own);
    CHECK(std::fabs(db + 20.0) < 0.3);
    CHECK(step <= own * 1.05);
}

TEST(BasicSynth_resonance_is_heard)
{
    // REQ-inst-synth (Solaris D-4): the filter's resonance rings at the cutoff — a saw at 110 Hz through a low-pass at
    // its 8th harmonic, 880 Hz: at resonance 0.9 that harmonic stands far above its neighbours, at 0 it does not
    configure();
    auto ring = [](double res) {
        BasicSynth s;
        BasicSynth::Params p;
        p.cutoff = 880.0;
        p.envAmount = 0.0;
        p.keytrack = 0.0;
        p.resonance = res;
        p.osc2.level = 0.0;
        s.setParams(p);
        s.noteOn(45, 127);
        auto r = render(s, 48000);
        // the 8th harmonic against the 4th, by Goertzel over the sustained half second
        auto tone = [&](double hz) {
            const double w = 2.0 * M_PI * hz / 48000.0, c = 2.0 * std::cos(w);
            double s1 = 0.0, s2 = 0.0;
            for (int i = 24000; i < 48000; ++i)
            {
                const double s0 = r.L[(size_t)i] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            return 10.0 * std::log10(s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-30);
        };
        return tone(880.0) - tone(440.0);
    };
    const double flat = ring(0.0), resonant = ring(0.9);
    CHECK(resonant - flat > 15.0); // Q(0.9) ≈ 21: ~26 dB more at the cutoff than a Butterworth's −3 dB
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
    const int notes[] = {36, 37, 38, 39, 41, 42, 45, 46, 48, 56, 49, 51}; // crash and ride last (REQ-drum-6)
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
