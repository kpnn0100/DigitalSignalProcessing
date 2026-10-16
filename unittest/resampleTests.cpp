// Holding one stream to another device's clock (REQ-resample-2, REQ-resample-3): the streaming resampler and the
// fill controller — src/resample/README.md (A1)–(A4), (F1)–(F5). Two crystals running apart are SIMULATED here, minutes
// of them in a fraction of a second, with no device and no clock.
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

extern std::atomic<long> gAllocs; // the counting allocator in instrumentTests.cpp
extern std::atomic<bool> gCounting;

using namespace arstro;

namespace
{
    constexpr double kTau = 2.0 * M_PI;

    /** A deterministic generator (an LCG): a test's "randomness" the same every run. */
    struct Lcg
    {
        uint64_t s;
        explicit Lcg(uint64_t seed) : s(seed) {}
        double next() // 0 … 1
        {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            return (double)(s >> 11) / 9007199254740992.0;
        }
    };

    /** Through `r` at steps `stepOf(block)`, blocks sized `sizeOf(block)`: the output, and where each output frame
     *  read (its position P_j, input frames). The input is `x(m)` for frame m ≥ 0. */
    template <class X, class Step, class Size>
    void run(AdaptiveResampler &r, X x, Step stepOf, Size sizeOf, int blocks, std::vector<float> &out, std::vector<double> &pos)
    {
        const int C = r.channels();
        long long fed = 0;
        double P = 0.0;
        std::vector<float> in, o;
        for (int b = 0; b < blocks; ++b)
        {
            const int n = sizeOf(b);
            const double s = stepOf(b);
            const int need = r.inputNeeded(n, s);
            in.assign((size_t)need * C, 0.0f);
            for (int i = 0; i < need; ++i)
                for (int c = 0; c < C; ++c) in[(size_t)i * C + c] = (float)x(fed + i, c);
            fed += need;
            o.assign((size_t)n * C, 0.0f);
            r.process(in.data(), need, o.data(), n, s);
            for (int j = 0; j < n; ++j)
            {
                pos.push_back(P + j * s);
                for (int c = 0; c < C; ++c) out.push_back(o[(size_t)j * C + c]);
            }
            P += n * s;
        }
    }
}

TEST(AdaptiveResampler_at_step_one_is_the_input)
{
    // (A2) at s = 1 every output lands on a frame: h(0) = 1 and h(±k) = 0 — the input itself, whatever the blocks;
    // (A3)/(A4) the first block asks for R frames more than it plays, every later one for exactly what it plays
    AdaptiveResampler r(1, 512);
    CHECK(r.lookahead() == 16);
    CHECK(r.inputNeeded(100, 1.0) == 116);
    std::vector<float> out;
    std::vector<double> pos;
    auto x = [](long long m, int) { return 0.5 * std::sin(kTau * 997.0 * m / 48000.0) + 0.1 * std::sin(kTau * 13001.0 * m / 48000.0); };
    run(r, x, [](int) { return 1.0; }, [](int b) { return 1 + (b * 37) % 300; }, 400, out, pos);
    double worst = 0.0;
    for (size_t j = 0; j < out.size(); ++j) worst = std::max(worst, std::fabs(out[j] - (double)(float)x((long long)j, 0)));
    std::printf("    at step 1: %.2g from the input over %zu frames\n", worst, out.size());
    CHECK(worst < 1e-6);
    CHECK(r.inputNeeded(64, 1.0) == 64);
}

TEST(AdaptiveResampler_reads_a_sine_at_its_step)
{
    // (A1)–(A2): read every 1.0005 frames, 1 kHz is 1000.5 Hz in the output's frames; against the ideal sine at the
    // positions read, what is left is noise far below the sound (the kernel's passband, float storage)
    AdaptiveResampler r(1, 256);
    std::vector<float> out;
    std::vector<double> pos;
    auto x = [](long long m, int) { return 0.5 * std::sin(kTau * 1000.0 * m / 48000.0); };
    run(r, x, [](int) { return 1.0005; }, [](int) { return 256; }, 400, out, pos);
    double e = 0.0, s = 0.0;
    for (size_t j = 0; j < out.size(); ++j)
        if (pos[j] > 32.0)
        {
            const double want = 0.5 * std::sin(kTau * 1000.0 * pos[j] / 48000.0);
            e += (out[j] - want) * (out[j] - want);
            s += want * want;
        }
    const double sinad = 10.0 * std::log10(s / e);
    std::printf("    1 kHz read at 1.0005: SINAD %.1f dB\n", sinad);
    CHECK(sinad > 90.0); // the table's linear reading (R3) bounds it near 93 dB — README, Limits
}

TEST(AdaptiveResampler_bends_with_no_click)
{
    // (A1): the step changes EVERY block (a random walk within ±2000 ppm), blocks of 1 … 512 frames — the read
    // position is continuous, so the output follows the ideal sine at each position read with no step in it; two
    // channels read alike (one the other's negative)
    AdaptiveResampler r(2, 512, 1.01);
    std::vector<float> out;
    std::vector<double> pos;
    Lcg g(7);
    std::vector<double> steps(3000), sizes(3000);
    double d = 0.0;
    for (size_t b = 0; b < steps.size(); ++b)
    {
        d = std::min(2e-3, std::max(-2e-3, d + (g.next() - 0.5) * 4e-4));
        steps[b] = 1.0 + d;
        sizes[b] = 1 + (int)(g.next() * 511.0);
    }
    auto x = [](long long m, int c) { const double v = 0.4 * std::sin(kTau * 3000.0 * m / 48000.0) + 0.3 * std::sin(kTau * 440.0 * m / 48000.0); return c == 0 ? v : -v; };
    run(r, x, [&](int b) { return steps[(size_t)b]; }, [&](int b) { return (int)sizes[(size_t)b]; }, (int)steps.size(), out, pos);
    double worst = 0.0, mirror = 0.0, jump = 0.0;
    for (size_t j = 0; j < pos.size(); ++j)
    {
        mirror = std::max(mirror, (double)std::fabs(out[2 * j] + out[2 * j + 1]));
        if (pos[j] <= 32.0) continue;
        const double want = 0.4 * std::sin(kTau * 3000.0 * pos[j] / 48000.0) + 0.3 * std::sin(kTau * 440.0 * pos[j] / 48000.0);
        worst = std::max(worst, std::fabs(out[2 * j] - want));
        if (j > 0) jump = std::max(jump, std::fabs((double)out[2 * j] - out[2 * j - 2]));
    }
    std::printf("    a step changed every block: %.2g from the ideal at worst (%zu frames), the largest sample-to-sample move %.3f\n",
                worst, pos.size(), jump);
    CHECK(worst < 1e-5);
    CHECK(mirror == 0.0);
    CHECK(jump < 0.4 * kTau * 3000.0 / 48000.0 + 0.3 * kTau * 440.0 / 48000.0 + 1e-3); // never past the sound's own slope
}

TEST(AdaptiveResampler_does_not_allocate)
{
    // an audio thread may run it (Solaris R-PLAY-2): every buffer exists once it is built
    AdaptiveResampler r(2, 512);
    std::vector<float> in(2 * 600, 0.25f), out(2 * 512);
    gAllocs = 0;
    gCounting = true;
    for (int b = 0; b < 50; ++b)
    {
        const double s = 1.0 + 0.001 * ((b % 5) - 2);
        const int need = r.inputNeeded(512, s);
        r.process(in.data(), need, out.data(), 512, s);
    }
    gCounting = false;
    CHECK(gAllocs.load() == 0);
    CHECK(std::fabs(out[100] - 0.25f) < 1e-6); // a constant passes exactly — (A2)'s normalised weights
}

namespace
{
    struct Sim
    {
        double sinad = 0, worstFill = 0, worstDrift = 0, maxStepMove = 0, wobble = 0, maxDelta = 0;
        long long underruns = 0;
        double drift = 0, fill = 0;
    };
    struct SimSpec
    {
        double fc = 48000.0, ff = 48000.0; // the producer's and the consumer's TRUE rates
        int bc = 256, bf = 256;            // their blocks
        double seconds = 300.0, tail = 60.0;
        double jitter = 0.0;               // ± seconds, in when the consumer believes it is
        double retargetAt = -1.0, retargetBy = 0.0;
        double startPpm = 0.0;             // the drift the controller starts from
    };

    /** A producer pushing `bc`-frame blocks and a consumer pulling `bf`-frame blocks through an AdaptiveResampler
     *  steered by a FillController. The consumer's fill: what was pushed, plus the frames made since the last push
     *  (at the NOMINAL rate, its time off by the jitter), less a block (so it is continuous across a push), less what
     *  it popped, plus what the resampler holds (A5). The sound: 0.5·sin 1 kHz in the producer's frames. Measured
     *  over the last `tail` seconds. */
    Sim simulate(const SimSpec &k)
    {
        FillParams fp;
        fp.target = 1024.0;
        fp.rate = 48000.0;
        FillController ctl(fp);
        AdaptiveResampler rs(1, k.bf, 1.01);
        std::vector<float> ring(1 << 16);
        long long pushed = 0, popped = 0;
        double lastPush = 0.0;
        Lcg g(11);
        auto src = [&](long long m) { return 0.5 * std::sin(kTau * 1000.0 * m / k.fc); };
        for (; pushed < (long long)fp.target; ++pushed) ring[(size_t)(pushed & 0xFFFF)] = (float)src(pushed); // primed to the target
        ctl.reset(fp.target + rs.buffered(), k.startPpm);
        const double tPushStep = k.bc / k.fc, tPullStep = k.bf / k.ff;
        double tPush = 0.0, tPull = 0.0, P = 0.0, prevDelta = ctl.deviation();
        Sim sim;
        std::vector<float> out((size_t)k.bf), in;
        std::vector<double> deltas;
        double e2 = 0.0, s2 = 0.0;
        bool moved = false;
        while (tPull < k.seconds)
        {
            if (tPush <= tPull)
            {
                for (int i = 0; i < k.bc; ++i, ++pushed) ring[(size_t)(pushed & 0xFFFF)] = (float)src(pushed);
                lastPush = tPush;
                tPush += tPushStep;
                continue;
            }
            if (k.retargetAt >= 0.0 && !moved && tPull >= k.retargetAt) { ctl.setTarget(fp.target + k.retargetBy); moved = true; }
            const double noise = k.jitter > 0.0 ? (g.next() * 2.0 - 1.0) * k.jitter : 0.0;
            const double since = std::min((double)k.bc, std::max(0.0, (tPull - lastPush + noise) * 48000.0));
            const double fill = (double)(pushed - popped) + since - (double)k.bc + rs.buffered();
            const double delta = ctl.update(fill, k.bf);
            sim.maxDelta = std::max(sim.maxDelta, std::fabs(delta));
            sim.maxStepMove = std::max(sim.maxStepMove, std::fabs(delta - prevDelta));
            prevDelta = delta;
            const double step = 1.0 + delta;
            const int need = rs.inputNeeded(k.bf, step);
            if (pushed - popped < need) ++sim.underruns;
            in.assign((size_t)need, 0.0f);
            for (int i = 0; i < need && popped < pushed; ++i, ++popped) in[(size_t)i] = ring[(size_t)(popped & 0xFFFF)];
            rs.process(in.data(), need, out.data(), k.bf, step);
            if (tPull >= k.seconds - k.tail)
            {
                sim.worstFill = std::max(sim.worstFill, std::fabs(ctl.smoothedFill() - (fp.target + (moved ? k.retargetBy : 0.0))));
                const double truth = -(k.fc / k.ff - 1.0) * 1e6; // (F5): + when the consumer runs fast
                sim.worstDrift = std::max(sim.worstDrift, std::fabs(ctl.driftPpm() - truth));
                deltas.push_back(delta);
                // the interpolation, against the sound at the very positions read
                for (int j = 0; j < k.bf; ++j)
                {
                    const double want = 0.5 * std::sin(kTau * 1000.0 * (P + j * step) / k.fc);
                    e2 += (out[(size_t)j] - want) * (out[(size_t)j] - want);
                    s2 += want * want;
                }
            }
            P += k.bf * step;
            tPull += tPullStep;
        }
        // the wobble: the step's movement faster than a second (what would be heard as a waver), RMS
        const size_t w = (size_t)std::max(1.0, k.ff / k.bf);
        double wob = 0.0;
        size_t n = 0;
        for (size_t i = w; i + w < deltas.size(); ++i)
        {
            double m = 0.0;
            for (size_t j = i - w; j <= i + w; ++j) m += deltas[j];
            m /= (double)(2 * w + 1);
            wob += (deltas[i] - m) * (deltas[i] - m);
            ++n;
        }
        sim.wobble = n ? std::sqrt(wob / (double)n) : 0.0;
        sim.sinad = 10.0 * std::log10(s2 / std::max(1e-30, e2));
        sim.drift = ctl.driftPpm();
        sim.fill = ctl.smoothedFill();
        return sim;
    }
    void say(const char *what, const SimSpec &k, const Sim &s)
    {
        std::printf("    %s %.1f vs %.1f Hz: drift %+.3f ppm (truth %+.3f), fill %.2f; over the last %.0f s: worst %.2f frames / %.3f ppm, "
                    "wobble %.3f ppm, the step's largest move %.3f ppm (at most %.1f ppm); %lld underruns; SINAD %.1f dB\n",
                    what, k.fc, k.ff, s.drift, -(k.fc / k.ff - 1.0) * 1e6, s.fill, k.tail, s.worstFill, s.worstDrift, s.wobble * 1e6,
                    s.maxStepMove * 1e6, s.maxDelta * 1e6, s.underruns, s.sinad);
    }
}

TEST(FillController_holds_two_crystals_together)
{
    // REQ-resample-3, (F1)–(F5): 48 000 against 48 003 Hz (the consumer 62.5 ppm fast), and two 1000 ppm apart either
    // way, unlike blocks, for five simulated minutes — never an underrun; over the last minute the fill held within
    // 2 frames of its target, the drift read to 0.5 ppm, the step steady (no waver); the sound read at 90 dB clean
    SimSpec a, b, c;
    a.ff = 48003.0;
    b.ff = 48000.0 * (1 - 1e-3), b.bf = 128;
    c.ff = 48000.0 * (1 + 1e-3), c.bc = 128;
    for (const SimSpec &k : {a, b, c})
    {
        const Sim s = simulate(k);
        say("", k, s);
        CHECK(s.underruns == 0);
        CHECK(s.worstFill < 2.0);
        CHECK(s.worstDrift < 0.5);
        CHECK(s.wobble < 0.01e-6);
        CHECK(s.sinad > 90.0);
    }
}

TEST(FillController_rides_out_a_late_scheduler)
{
    // the consumer's idea of WHEN is off by up to ±1 ms each pull (48 frames — a scheduler's jitter): no underrun,
    // the drift read to 3 ppm, the step moving under 1 ppm a block and wavering under 2 ppm RMS — a pitch change no one
    // hears (2 ppm is 0.003 cents; a trained ear needs some 3 cents)
    SimSpec k;
    k.ff = 48000.0 * (1 + 3e-4);
    k.jitter = 1e-3;
    const Sim s = simulate(k);
    say("±1 ms of jitter,", k, s);
    CHECK(s.underruns == 0);
    CHECK(s.worstDrift < 3.0);
    CHECK(s.maxStepMove < 1e-6);
    CHECK(s.wobble < 2e-6);
}

TEST(FillController_travels_to_a_new_target)
{
    // (F6): a target moved 480 frames while running (a path re-aligned) — the step never kicks (under 0.5 ppm a
    // block); it follows the 200 ppm ramp, overshooting it by at most half (a loop following a ramp), so under 400 ppm;
    // no underrun; the fill settled on the new target by the end
    SimSpec k;
    k.seconds = 600.0;
    k.retargetAt = 120.0;
    k.retargetBy = 480.0;
    const Sim s = simulate(k);
    say("a target 480 frames on,", k, s);
    CHECK(s.underruns == 0);
    CHECK(s.worstFill < 2.0);
    CHECK(s.maxStepMove < 0.5e-6);
    CHECK(s.maxDelta < 400e-6);
}

TEST(FillController_starts_locked_with_a_known_drift)
{
    // (F5): started from a device's drift as last measured, a run is locked from its first block — over its first
    // half minute the fill already within 2 frames
    SimSpec k;
    k.ff = 48000.0 * (1 - 5e-4);
    k.startPpm = -(k.fc / k.ff - 1.0) * 1e6;
    k.seconds = 30.0;
    k.tail = 30.0;
    const Sim s = simulate(k);
    say("started at its drift,", k, s);
    CHECK(s.underruns == 0);
    CHECK(s.worstFill < 2.0);
}

TEST(FillController_clamps_and_recovers)
{
    // (F3): started with the drift pinned wrong at the clamp (+2000 ppm, the truth 0), the step never passes ±2000 ppm,
    // there is no underrun, and by the end the drift reads true and the fill holds
    SimSpec k;
    k.startPpm = 2000.0;
    k.seconds = 600.0;
    const Sim s = simulate(k);
    say("pinned wrong at the start,", k, s);
    CHECK(s.maxDelta <= 2e-3 + 1e-12);
    CHECK(s.underruns == 0);
    CHECK(s.worstDrift < 0.5 && s.worstFill < 2.0);
}

TEST(FillController_never_winds_up)
{
    // (F3): a producer that stops for minutes (the fill held at nothing) pins the step AND the integral at the clamp —
    // no further: a stall's length is not a debt the loop must later pay back
    FillParams fp;
    FillController c(fp);
    for (int i = 0; i < 40000; ++i) c.update(0.0, 256); // 213 s
    CHECK(c.deviation() == -2e-3);
    CHECK(c.driftPpm() == 2000.0);
}
