// A multi-sample instrument read from SFZ (REQ-multisample-1…3) — src/instrument/README.md ## MultiSampler and SFZ.
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
    std::vector<float> sine(double hz, double seconds, double amp = 0.5)
    {
        std::vector<float> x((size_t)(seconds * 48000));
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(amp * std::sin(kTau * hz * (double)i / 48000.0));
        return x;
    }
    void render(MultiSampler &m, std::vector<Sample> &L, std::vector<Sample> &R, int frames)
    {
        L.assign((size_t)frames, 0.0);
        R.assign((size_t)frames, 0.0);
        for (int at = 0; at < frames; at += 128)
        {
            Sample *o[2] = {L.data() + at, R.data() + at};
            m.render(o, 2, std::min(128, frames - at));
        }
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
    double peak(const std::vector<Sample> &y, int from, int n)
    {
        double p = 0;
        for (int i = from; i < from + n && i < (int)y.size(); ++i) p = std::max(p, std::fabs(y[(size_t)i]));
        return p;
    }
}

TEST(Sfz_reads_regions_and_inherits)
{
    // (Z1): headers, inheritance (region ← group ← global), default_path, names of keys, comments, a path with spaces,
    // an opcode not read said as a problem — not an error
    const std::string text = R"(// a piano, two layers
<control> default_path=Samples/
<global> ampeg_release=0.5 /* every region's */
<group> lovel=1 hivel=63
<region> sample=soft C4.wav key=c4
<region> sample=soft E4.wav lokey=d#4 hikey=f4 pitch_keycenter=e4 tune=-12
<group> lovel=64 hivel=127 volume=-3 amp_velcurve_1=0.2
<region> sample=loud C4.wav key=60 loop_mode=one_shot
<region> sample=loud Bb3.wav key=bb3
)";
    const SfzInstrument s = parseSfz(text);
    CHECK(s.regions.size() == 4);
    CHECK(s.regions[0].sample == "Samples/soft C4.wav" && s.regions[0].lokey == 60 && s.regions[0].hikey == 60 && s.regions[0].keycenter == 60);
    CHECK(s.regions[0].lovel == 1 && s.regions[0].hivel == 63 && s.regions[0].release == 0.5);
    CHECK(s.regions[1].lokey == 63 && s.regions[1].hikey == 65 && s.regions[1].keycenter == 64 && s.regions[1].tune == -12);
    CHECK(s.regions[2].lovel == 64 && s.regions[2].volume == -3 && s.regions[2].loop == SfzRegion::OneShot && s.regions[2].release == 0.5);
    CHECK(s.regions[3].keycenter == 58);
    CHECK(sfzSounds(s).size() == 4 && sfzSounds(s)[3] == "Samples/loud Bb3.wav");
    bool said = false;
    for (const auto &p : s.problems) said = said || p.find("amp_velcurve_1") != std::string::npos;
    CHECK(said);
    CHECK(sfzKey("c-1") == 0 && sfzKey("g9") == 127 && sfzKey("a4") == 69 && sfzKey("h3") == -1 && sfzKey("200") == -1);
}

TEST(MultiSampler_velocity_layers_choose_their_sound)
{
    // (Z2), (Z5): soft (a 220 Hz sound) under velocity 64, loud (330 Hz) from 64 — each note the layer its velocity is in,
    // at the pitch recorded (the note is its root); level by velocity squared
    configure48();
    MultiSampler m;
    m.setDefinition(parseSfz("<region> sample=soft lovel=1 hivel=63 key=57 <region> sample=loud lovel=64 hivel=127 key=57"));
    const auto soft = sine(220, 1), loud = sine(330, 1);
    m.setSound("soft", soft.data(), (long long)soft.size(), 1, 48000);
    m.setSound("loud", loud.data(), (long long)loud.size(), 1, 48000);
    std::vector<Sample> L, R;
    m.noteOn(57, 40);
    CHECK(m.voiceRegion(0) == 0);
    render(m, L, R, 9600);
    CHECK(amp(L, 4800, 4800, 220.0) > 0.01 && amp(L, 4800, 4800, 330.0) < 1e-4);
    const double a40 = amp(L, 4800, 4800, 220.0);
    m.noteOff(57);
    render(m, L, R, 4800);
    m.noteOn(57, 120);
    render(m, L, R, 9600);
    CHECK(amp(L, 4800, 4800, 330.0) > 0.01 && amp(L, 4800, 4800, 220.0) < 1e-4);
    std::printf("    velocity 40: %.4f (0.5·(40/127)² = %.4f)\n", a40, 0.5 * std::pow(40 / 127.0, 2));
    CHECK(std::fabs(a40 - 0.5 * std::pow(40 / 127.0, 2)) < 1e-3);
}

TEST(MultiSampler_pitch_follows_the_key)
{
    // (Z3): a 440 Hz sound rooted at 69, played at 76 with tune +50 cents: 440·2^((7 + 0.5)/12) Hz; recorded at 24 kHz
    // and played at 48: the same pitch (its rate counted)
    configure48();
    MultiSampler m;
    m.setDefinition(parseSfz("<region> sample=a lokey=50 hikey=90 pitch_keycenter=69 tune=50"));
    std::vector<float> a24((size_t)24000);
    for (size_t i = 0; i < a24.size(); ++i) a24[i] = (float)(0.5 * std::sin(kTau * 440.0 * (double)i / 24000.0));
    m.setSound("a", a24.data(), (long long)a24.size(), 1, 24000);
    std::vector<Sample> L, R;
    m.noteOn(76, 127);
    render(m, L, R, 24000);
    const double want = 440.0 * std::pow(2.0, 7.5 / 12.0);
    double best = 0, at = 0;
    for (double f = want - 5; f <= want + 5; f += 0.05)
    {
        const double a = amp(L, 2400, 19200, f);
        if (a > best) { best = a; at = f; }
    }
    std::printf("    played at %.2f Hz (want %.2f)\n", at, want);
    CHECK(std::fabs(at - want) < 0.15);
}

TEST(MultiSampler_round_robin_and_chance)
{
    // (Z2): two regions on one key in turn (seq_length 2) — they alternate; two more split by chance (lorand) — about
    // half each over 1000 notes, the same split after a reset (a counter, not a clock)
    configure48();
    const auto snd = sine(220, 0.2);
    MultiSampler alt;
    alt.setDefinition(parseSfz("<group> key=60 seq_length=2 <region> sample=a seq_position=1 <region> sample=b seq_position=2"));
    alt.setSound("a", snd.data(), (long long)snd.size(), 1, 48000);
    alt.setSound("b", snd.data(), (long long)snd.size(), 1, 48000);
    std::vector<Sample> L, R;
    std::vector<int> turns;
    for (int i = 0; i < 4; ++i)
    {
        alt.noteOn(60, 100);
        int newest = -1;
        long long best = -1;
        for (int v = 0; v < MultiSampler::kVoices; ++v)
            if (alt.voiceRegion(v) >= 0 && v > best) { best = v; newest = alt.voiceRegion(v); }
        turns.push_back(newest);
        alt.noteOff(60);
        render(alt, L, R, 2400);
    }
    CHECK(turns.size() == 4 && turns[0] == 0 && turns[1] == 1 && turns[2] == 0 && turns[3] == 1);
    MultiSampler seq;
    seq.setDefinition(parseSfz("<group> key=60 <region> sample=a hirand=0.5 <region> sample=b lorand=0.5"));
    seq.setSound("a", snd.data(), (long long)snd.size(), 1, 48000);
    seq.setSound("b", snd.data(), (long long)snd.size(), 1, 48000);
    auto run = [&](MultiSampler &m) {
        std::vector<int> picks;
        for (int i = 0; i < 1000; ++i)
        {
            m.noteOn(60, 100);
            int r = -1;
            long long newest = -1;
            for (int v = 0; v < MultiSampler::kVoices; ++v)
                if (m.voiceRegion(v) >= 0 && v > newest) { newest = v; r = m.voiceRegion(v); }
            picks.push_back(r);
            m.allNotesOff();
            std::vector<Sample> l, rr2;
            render(m, l, rr2, 3000); // its release done: the voices free
        }
        return picks;
    };
    const auto first = run(seq);
    seq.reset();
    const auto again = run(seq);
    int a = 0;
    for (int r : first) a += r == 0 ? 1 : 0;
    std::printf("    chance: %d of 1000 to the first region; the same picks after a reset: %s\n", a, first == again ? "yes" : "no");
    CHECK(a > 450 && a < 550 && first == again);
}

TEST(MultiSampler_loops_one_shots_and_chokes)
{
    // (Z4): a 0.1 s sound looped while held (loop_sustain) still sounds after a second, and ends once released; one
    // without a loop ends with its sound; a one-shot plays on past its note-off. (Z6): a closed hat (group 1) chokes
    // the open one (off_by 1) within 10 ms. (Z5): pan −100 is the left alone
    configure48();
    const auto shortSound = sine(480, 0.1); // 48 whole cycles: a seamless loop
    MultiSampler m;
    m.setDefinition(parseSfz("<region> sample=s key=60 loop_mode=loop_sustain loop_start=0 loop_end=4799 ampeg_release=0.02 "
                             "<region> sample=s key=62 "
                             "<region> sample=s key=64 loop_mode=one_shot "
                             "<region> sample=s key=66 pan=-100"));
    m.setSound("s", shortSound.data(), (long long)shortSound.size(), 1, 48000);
    std::vector<Sample> L, R;
    m.noteOn(60, 127);
    render(m, L, R, 48000);
    CHECK(peak(L, 46000, 2000) > 0.4); // still looping after a second
    m.noteOff(60);
    render(m, L, R, 4800);
    CHECK(m.activeVoices() == 0);
    m.noteOn(62, 127);
    render(m, L, R, 9600);
    CHECK(peak(L, 0, 4000) > 0.4 && peak(L, 5000, 4000) == 0.0 && m.activeVoices() == 0); // over with its sound
    m.noteOn(64, 127);
    m.noteOff(64);
    render(m, L, R, 4000);
    CHECK(peak(L, 3000, 1000) > 0.4); // a one-shot plays on
    render(m, L, R, 4800);
    m.noteOn(66, 127);
    render(m, L, R, 4800);
    CHECK(peak(L, 0, 4000) > 0.4 && peak(R, 0, 4000) == 0.0);
    // the choke
    MultiSampler hats;
    const auto hiss = sine(5000, 1.0);
    hats.setDefinition(parseSfz("<region> sample=h key=46 group=1 off_by=1 loop_mode=one_shot <region> sample=h key=42 group=1"));
    hats.setSound("h", hiss.data(), (long long)hiss.size(), 1, 48000);
    hats.noteOn(46, 127);
    render(hats, L, R, 2400);
    hats.noteOn(42, 127); // the closed one: the open one is choked
    int open = -1;
    for (int v = 0; v < MultiSampler::kVoices; ++v)
        if (hats.voiceRegion(v) == 0) open = v;
    CHECK(open >= 0);
    render(hats, L, R, 480); // 10 ms
    CHECK(hats.voiceRegion(open) == -1);
}

TEST(MultiSampler_through_the_registry)
{
    // REQ-multisample-3: the registry's "multisample" reads its definition, says the sounds it names (and what it could
    // not read), takes each by name, plays; once given, a render allocates nothing
    configure48();
    auto d = DeviceRegistry::create("multisample");
    CHECK(d && d->type().takesDefinition && d->type().takesSample);
    std::vector<std::string> problems;
    const auto names = d->setDefinition("<region> sample=one.wav key=60 <region> sample=two.wav key=62 bogus=1", problems);
    CHECK(names.size() == 2 && names[0] == "one.wav" && names[1] == "two.wav" && problems.size() == 1);
    const auto snd = sine(261.63, 0.5);
    for (const auto &n : names) d->setNamedSample(n, snd.data(), (long long)snd.size(), 1, 48000);
    std::vector<Sample> L(4096, 0.0), R(4096, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    d->noteOn(60, 127);
    d->noteOn(62, 127);
    gAllocs = 0;
    gCounting = true;
    d->process(o, 2, 4096);
    gCounting = false;
    CHECK(gAllocs.load() == 0);
    CHECK(d->activeVoices() == 2 && peak(L, 0, 4096) > 0.3);
}
