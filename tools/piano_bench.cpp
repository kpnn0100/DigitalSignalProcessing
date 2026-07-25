/*
 *  Arstro DSP — piano render benchmark (REQ-piano-17, plan §M0).
 *
 *  Measures how much faster than real time the physical-modeling piano renders,
 *  so the physics upgrades in docs/piano-physics-plan.md have a gate to be
 *  measured against (M2 raises the partial count ~5x; M4 doubles resonators on
 *  the low partials). Every milestone re-runs this and records the figure in
 *  docs/piano-physics-progress.md.
 *
 *  Deterministic by construction: fixed notes, fixed velocities, no RNG seeding
 *  beyond PianoVoice's own fixed-seed noise, no wall-clock in the DSP path. The
 *  only variance is machine scheduling noise, which is why it reports the BEST
 *  of several passes (least-contended = closest to the machine's true capacity)
 *  alongside the median.
 *
 *  Usage:
 *      piano_bench                 # full run: 8-voice + 1-voice, 10 s each, 5 passes
 *      piano_bench --smoke         # tiny run for CI (just proves it works)
 *      piano_bench --seconds 20 --passes 3
 *
 *  Output is machine-parseable ("KEY=VALUE" lines) so the ledger figure can be
 *  lifted without hand-transcription.
 */
#include "../apps/piano_demo/PianoEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace arstro;

namespace
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockFrames = 128; // matches the interactive app's frame size

    // Rendered output escapes here so the optimiser cannot delete the render loop
    // as dead code and report an absurd speed.
    volatile unsigned long long gSink = 0;

    struct Result
    {
        double bestRt = 0.0;   // x real-time, best pass
        double medianRt = 0.0; // x real-time, median pass
        double bestWallMs = 0.0;
    };

    // Renders `audioSeconds` of `voiceCount` sustained voices and returns how many
    // times faster than real time that was. Keys are held down (never released), so
    // the dampers stay lifted and every voice keeps ringing for the whole run —
    // otherwise the benchmark would mostly be measuring silence.
    double renderOnePass(int voiceCount, double audioSeconds)
    {
        AudioConfig::instance().setSampleRate(kSampleRate);
        AudioConfig::instance().setChannelCount(2);
        AudioConfig::instance().setOutputBitDepth(16);

        PianoEngine engine;
        // A spread voicing across the register so per-note costs (which vary with
        // pitch once M2 lands) are represented rather than one octave's worth.
        static const int kNotes[8] = {28, 40, 47, 52, 59, 64, 71, 76};
        for (int i = 0; i < voiceCount && i < 8; ++i)
            engine.noteOnMidi(kNotes[i], 0.85);

        const long totalFrames = (long)(audioSeconds * kSampleRate);
        std::vector<uint8_t> block;
        unsigned long long sink = 0;

        const auto t0 = std::chrono::steady_clock::now();
        for (long done = 0; done < totalFrames; done += kBlockFrames)
        {
            engine.renderBlockBytes(block, kBlockFrames);
            sink += block.empty() ? 0u : block[0];
        }
        const auto t1 = std::chrono::steady_clock::now();

        gSink = sink; // escape to a volatile so the render loop can't be elided
        const double wallSeconds = std::chrono::duration<double>(t1 - t0).count();
        return wallSeconds > 0.0 ? audioSeconds / wallSeconds : 0.0;
    }

    Result benchmark(int voiceCount, double audioSeconds, int passes)
    {
        std::vector<double> rts;
        rts.reserve((size_t)passes);
        for (int p = 0; p < passes; ++p)
            rts.push_back(renderOnePass(voiceCount, audioSeconds));

        std::vector<double> sorted = rts;
        std::sort(sorted.begin(), sorted.end());

        Result r;
        r.bestRt = sorted.back();
        r.medianRt = sorted[sorted.size() / 2];
        r.bestWallMs = r.bestRt > 0.0 ? (audioSeconds / r.bestRt) * 1000.0 : 0.0;
        return r;
    }
}

int main(int argc, char **argv)
{
    double seconds = 10.0;
    int passes = 5;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--smoke") == 0) { seconds = 0.5; passes = 1; }
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--passes") == 0 && i + 1 < argc) passes = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::printf("usage: piano_bench [--smoke] [--seconds N] [--passes N]\n");
            return 0;
        }
    }
    if (passes < 1) passes = 1;

    const Result eight = benchmark(8, seconds, passes);
    const Result one = benchmark(1, seconds, passes);

    // Scaling context for the upcoming milestones: how many resonators a voice
    // currently runs, so M2/M4's cost multiplier can be predicted from this run
    // rather than guessed. (Read from the live types, so it can't go stale.)
    // Partial count is pitch-dependent since M2, so report the cap AND the actual
    // active count for the benchmark's own lowest note (the worst case: the bass
    // fills the cap, the treble uses a handful).
    const int partialCap = StringPartialBank::kMaxPartials;
    StringPartialBank probe;
    probe.setFundamentalHz(41.2); // the lowest note the 8-voice chord uses
    probe.setInharmonicity(0.0019);
    const int activeAtLowNote = probe.partialCount();
    const int maxUnison = PianoVoice::kMaxUnison;

    std::printf("PIANO_BENCH_SECONDS=%.2f\n", seconds);
    std::printf("PIANO_BENCH_PASSES=%d\n", passes);
    std::printf("PIANO_BENCH_SAMPLE_RATE=%d\n", kSampleRate);
    std::printf("VOICES8_REALTIME_BEST=%.2f\n", eight.bestRt);
    std::printf("VOICES8_REALTIME_MEDIAN=%.2f\n", eight.medianRt);
    std::printf("VOICES8_WALL_MS=%.1f\n", eight.bestWallMs);
    std::printf("VOICES1_REALTIME_BEST=%.2f\n", one.bestRt);
    std::printf("VOICES1_REALTIME_MEDIAN=%.2f\n", one.medianRt);
    std::printf("PARTIAL_CAP=%d\n", partialCap);
    std::printf("PARTIALS_ACTIVE_LOW_NOTE=%d\n", activeAtLowNote);
    std::printf("MAX_UNISON_STRINGS=%d\n", maxUnison);
    std::printf("BUDGET_REALTIME_MIN=4.00\n");
    std::printf("BUDGET_MET=%s\n", eight.bestRt >= 4.0 ? "yes" : "NO");

    std::fprintf(stderr,
                 "\npiano_bench: 8 voices render %.2fx real-time (median %.2fx); "
                 "1 voice %.2fx. Budget >= 4x: %s\n",
                 eight.bestRt, eight.medianRt, one.bestRt,
                 eight.bestRt >= 4.0 ? "MET" : "BREACHED");

    // Exit code reflects only that the benchmark RAN. The >=4x budget is a
    // review gate recorded in docs/piano-physics-progress.md, deliberately not a
    // build failure: wall-clock thresholds are flaky on shared/CI machines, and a
    // benchmark that fails the build for being scheduled badly gets disabled,
    // which is worse than one that always reports an honest number.
    return (eight.bestRt > 0.0 && one.bestRt > 0.0) ? 0 : 1;
}
