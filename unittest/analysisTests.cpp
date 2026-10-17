/*
 *  Arstro DSP — the analysis of a whole sound (REQ-analysis-1…4): an exact STFT, harmonic / percussive separation,
 *  monophonic transcription, chords.
 *
 *  Judged on sounds whose answers are known because they were built from them: noise round-tripped, a tone with hits
 *  laid over it, a melody of known notes (one pitch struck twice), a progression of known triads. No main():
 *  registration is shared with synthTests.cpp through MiniTest.
 */
#include "MiniTest.h"
#include "../src/analysis/Chords.h"
#include "../src/analysis/Separate.h"
#include "../src/analysis/Stft.h"
#include "../src/analysis/Transcribe.h"
#include "../src/meter/Yin.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace arstro;

namespace
{
    constexpr double kFs = 48000.0;
    double hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
    /** A tone of `midi` from `t0` to `t1` seconds, shaped by an attack of 5 ms and a decay of `decay` seconds. */
    void tone(std::vector<float> &x, int midi, double t0, double t1, double amp, double decay = 1e9)
    {
        for (long long i = (long long)(t0 * kFs); i < (long long)(t1 * kFs) && i < (long long)x.size(); ++i)
        {
            const double t = (i - t0 * kFs) / kFs;
            x[(size_t)i] += (float)(amp * std::min(1.0, t / 0.005) * std::exp(-t / decay) * std::sin(2.0 * M_PI * hz(midi) * t));
        }
    }
    double energy(const std::vector<float> &x, long long a, long long b)
    {
        double e = 0.0;
        for (long long i = std::max(0LL, a); i < std::min((long long)x.size(), b); ++i) e += (double)x[(size_t)i] * x[(size_t)i];
        return e;
    }
}

TEST(Stft_gives_the_sound_back)
{
    // REQ-analysis-1, (S1)–(S4): noise, its spectrogram, back — to float rounding everywhere, the ends too; any length
    for (int n : {256, 2048, 8192})
    {
        unsigned seed = 11;
        std::vector<float> x(48001 + n / 3);
        for (auto &v : x)
        {
            seed = seed * 1664525u + 1013904223u;
            v = (float)(seed >> 8) / 16777216.0f - 0.5f;
        }
        const Spectrogram s = stft(x.data(), (long long)x.size(), n);
        CHECK(s.hop == n / 4 && s.bins.size() == x.size() / (size_t)(n / 4) + 1 && s.bins[0].size() == (size_t)(n / 2 + 1));
        const std::vector<float> y = istft(s);
        double worst = 0.0;
        for (size_t i = 0; i < x.size(); ++i) worst = std::max(worst, (double)std::fabs(y[i] - x[i]));
        CHECK(y.size() == x.size() && worst < 1e-5);
    }
    CHECK(istft(stft(nullptr, 0, 512)).empty());
    CHECK(fftSizeFor(48000.0, 0.0427) == 2048 && fftSizeFor(44100.0, 0.0427) == 2048 && fftSizeFor(96000.0, 0.0427) == 4096 && fftSizeFor(48000.0, 0.17) == 8192);
}

TEST(Separation_splits_a_tone_from_its_hits)
{
    // REQ-analysis-2, (H1)–(H4): a held 440 Hz tone with a click every quarter second — the harmonic half is the tone, the
    // percussive the clicks, each to within a few percent of its energy; the halves sum back to the sound; a channel at
    // half the other splits at half (one mask for both)
    const long long n = 2 * 48000;
    std::vector<float> held((size_t)n, 0.0f), hits((size_t)n, 0.0f);
    for (long long i = 0; i < n; ++i) held[(size_t)i] = (float)(0.3 * std::sin(2.0 * M_PI * 440.0 * i / kFs));
    for (long long at = 6000; at < n; at += 12000)
        for (int j = 0; j < 96; ++j) hits[(size_t)(at + j)] += (float)(0.8 * std::exp(-j / 12.0) * (j % 2 ? -1.0 : 1.0));
    std::vector<float> l((size_t)n), r((size_t)n);
    for (long long i = 0; i < n; ++i)
    {
        l[(size_t)i] = held[(size_t)i] + hits[(size_t)i];
        r[(size_t)i] = 0.5f * l[(size_t)i];
    }
    const Separation s = separateHarmonicPercussive({l, r}, kFs);
    CHECK(s.harmonic.size() == 2 && s.percussive.size() == 2 && s.harmonic[0].size() == (size_t)n);
    double sumErr = 0.0, imageErr = 0.0, hErr = 0.0, pErr = 0.0;
    for (long long i = 0; i < n; ++i)
    {
        sumErr = std::max(sumErr, (double)std::fabs(s.harmonic[0][(size_t)i] + s.percussive[0][(size_t)i] - l[(size_t)i]));
        imageErr = std::max(imageErr, (double)std::fabs(s.harmonic[1][(size_t)i] - 0.5f * s.harmonic[0][(size_t)i]));
        hErr += std::pow((double)s.harmonic[0][(size_t)i] - held[(size_t)i], 2.0);
        pErr += std::pow((double)s.percussive[0][(size_t)i] - hits[(size_t)i], 2.0);
    }
    const double hRel = hErr / energy(held, 0, n), pRel = pErr / energy(hits, 0, n);
    std::printf("    separation: harmonic off by %.1f dB, percussive off by %.1f dB\n", 10.0 * std::log10(hRel), 10.0 * std::log10(pRel));
    CHECK(sumErr < 1e-4 && imageErr < 1e-5);
    CHECK(hRel < 0.05 && pRel < 0.25); // the tone within −13 dB, the clicks within −6 dB
    CHECK(separateHarmonicPercussive({}, kFs).harmonic.empty());
}

TEST(Yin_finds_a_period)
{
    // REQ-tuner-1 / REQ-analysis-3: the tuner's estimator as a function — a 220 Hz sine's period to a hundredth of a
    // sample; noise has none under the threshold
    Yin yin(2048);
    std::vector<double> x(2048);
    for (int j = 0; j < 2048; ++j) x[(size_t)j] = std::sin(2.0 * M_PI * 220.0 * j / kFs);
    double dip = 1.0;
    CHECK_NEAR(yin.period(x.data(), kFs, 50.0, 2000.0, 0.15, &dip), kFs / 220.0, 0.01);
    CHECK(dip < 0.05);
    unsigned seed = 3;
    for (auto &v : x)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (double)(seed >> 8) / 16777216.0 - 0.5;
    }
    CHECK(yin.period(x.data(), kFs, 50.0, 2000.0, 0.15) == 0.0);
}

TEST(Transcription_finds_a_melody)
{
    // REQ-analysis-3, (T1)–(T6): A4, C5 struck twice without a gap, E5 softer — four notes, their pitches, each start
    // within 5 ms (its attack's, T5), the soft one the quietest; silence, a tone under the gate and a short sound have none
    std::vector<float> x((size_t)(2.4 * kFs), 0.0f);
    tone(x, 69, 0.10, 0.55, 0.5, 0.4);
    tone(x, 72, 0.60, 1.10, 0.5, 0.15);
    tone(x, 72, 1.10, 1.55, 0.5, 0.15);
    tone(x, 76, 1.60, 2.10, 0.15, 0.4);
    const auto notes = transcribe(x.data(), (long long)x.size(), kFs);
    for (const auto &n : notes) std::printf("    note %d %.3f–%.3f s velocity %d\n", n.pitch, n.start, n.end, n.velocity);
    CHECK(notes.size() == 4);
    if (notes.size() == 4)
    {
        const int want[4] = {69, 72, 72, 76};
        const double at[4] = {0.10, 0.60, 1.10, 1.60};
        for (int i = 0; i < 4; ++i)
        {
            CHECK(notes[(size_t)i].pitch == want[i]);
            CHECK_NEAR(notes[(size_t)i].start, at[i], 0.005);
            CHECK(notes[(size_t)i].end > notes[(size_t)i].start && (i == 3 || notes[(size_t)i].end <= notes[(size_t)i + 1].start + 1e-9));
        }
        CHECK(notes[3].velocity < notes[0].velocity && notes[0].velocity > 100);
    }
    std::vector<float> quiet((size_t)kFs, 0.0f);
    CHECK(transcribe(quiet.data(), (long long)quiet.size(), kFs).empty());
    tone(quiet, 69, 0.1, 0.9, 0.002); // −57 dBFS: under the −50 dB gate
    CHECK(transcribe(quiet.data(), (long long)quiet.size(), kFs).empty());
    CHECK(transcribe(x.data(), 100, kFs).empty());
}

TEST(Chords_of_a_progression)
{
    // REQ-analysis-4, (C1)–(C6): C, Am, F, G a second each over half-second beats — four spans, at their seconds; a
    // passing B over the C's second beat — as loud as the triad, so that beat alone scores E minor (0.907 against C's
    // 0.841, measured) — does not flip it (C4: the detour costs one change more, 0.1, than the beat gains); a silent second is N
    std::vector<float> x((size_t)(5.0 * kFs), 0.0f);
    auto triad = [&](int a, int b, int c, double t0) {
        for (int m : {a, b, c}) tone(x, m, t0, t0 + 1.0, 0.2);
    };
    triad(60, 64, 67, 0.0); // C
    tone(x, 71, 0.5, 1.0, 0.2); // a passing B: alone, that beat is E minor's
    triad(57, 60, 64, 1.0); // Am
    triad(53, 57, 60, 2.0); // F
    triad(55, 59, 62, 3.0); // G
    std::vector<double> beats;
    for (int b = 0; b <= 10; ++b) beats.push_back(b * 0.5);
    const auto spans = detectChords(x.data(), (long long)x.size(), kFs, beats);
    for (const auto &s : spans) std::printf("    chord %s %.2f–%.2f s\n", s.name.c_str(), s.start, s.end);
    CHECK(spans.size() == 5);
    if (spans.size() == 5)
    {
        const char *want[5] = {"C", "Am", "F", "G", "N"};
        for (int i = 0; i < 5; ++i)
        {
            CHECK(spans[(size_t)i].name == want[i]);
            CHECK_NEAR(spans[(size_t)i].start, (double)i, 1e-9);
        }
    }
    CHECK(chordName(1, false) == "C#" && chordName(10, true) == "Bbm" && chordName(3, false) == "Eb" && chordName(-1, true) == "Bm");
    std::vector<float> quiet((size_t)kFs, 0.0f);
    const auto none = detectChords(quiet.data(), (long long)quiet.size(), kFs, {});
    CHECK(none.size() == 1 && none[0].name == "N" && none[0].end == 1.0);
}
