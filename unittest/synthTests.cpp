#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <cmath>
#include <thread>

using namespace arstro;

TEST(LockFreeQueue_basic)
{
    LockFreeQueue<int> q(8);
    CHECK(q.empty());
    CHECK(q.push(1));
    CHECK(q.push(2));
    int v = 0;
    CHECK(q.pop(v)); CHECK(v == 1);
    CHECK(q.pop(v)); CHECK(v == 2);
    CHECK(!q.pop(v));
}

TEST(ADSR_shape_and_finish)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    ADSREnvelope env;
    env.setAttackMs(1.0);
    env.setDecayMs(1.0);
    env.setSustain(0.5);
    env.setReleaseMs(1.0);
    env.noteOn(1.0);
    // Feed a constant 1.0; envelope should rise above 0 quickly.
    double first = env.out(1.0, 0);
    for (int i = 0; i < 200; ++i) env.out(1.0, 0);
    double sustain = env.out(1.0, 0);
    CHECK(first >= 0.0 && first < sustain + 1e-9);
    CHECK_NEAR(sustain, 0.5, 0.05); // near sustain level
    CHECK(!env.isFinished());
    env.noteOff();
    for (int i = 0; i < 200; ++i) env.out(1.0, 0);
    CHECK(env.isFinished());
}

TEST(Oscillator_produces_bounded_signal)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    Oscillator osc;
    osc.setVoiceCount(5);
    osc.setDetuneCents(20);
    osc.setFrequency(220.0);
    osc.noteOn(1.0);
    bool sawPositive = false, sawNegative = false;
    double peak = 0.0;
    for (int i = 0; i < 4800; ++i)
    {
        double l = osc.out(0.0, 0);
        double r = osc.out(0.0, 1);
        if (l > 0.01) sawPositive = true;
        if (l < -0.01) sawNegative = true;
        peak = std::fmax(peak, std::fabs(l));
        CHECK(std::isfinite(l) && std::isfinite(r));
    }
    CHECK(sawPositive && sawNegative);  // it actually oscillates
    // 5 detuned voices normalized by sqrt(5) can momentarily peak near 2.24.
    CHECK(peak > 0.05 && peak < 2.4);   // non-trivial, not exploding
}

TEST(Block_different_chain_per_channel)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    Gain gL(2.0), gR(0.5);
    Block b;
    b.add(&gL, 0); // channel 0 gets x2
    b.add(&gR, 1); // channel 1 gets x0.5
    CHECK_NEAR(b.out(1.0, 0), 2.0, 1e-9);
    CHECK_NEAR(b.out(1.0, 1), 0.5, 1e-9);
}

TEST(SynthEngine_eight_note_scenario_nonsilent)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    SynthEngine eng;
    // a simple chord-ish set of 8 notes
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i)
        eng.noteOn(notes[i], 0.8);

    std::vector<double> buf;
    double peak = 0.0;
    bool finite = true;
    // ~0.5 s of audio in blocks of 128 frames
    for (int blk = 0; blk < 200; ++blk)
    {
        eng.renderBlockDouble(buf, 128);
        for (double s : buf)
        {
            if (!std::isfinite(s)) finite = false;
            peak = std::fmax(peak, std::fabs(s));
        }
    }
    CHECK(finite);
    CHECK(peak > 0.001);  // produced sound
    CHECK(eng.activeVoices() == 8);
    for (int i = 0; i < 8; ++i)
        eng.noteOff(notes[i]);
    // let releases finish
    for (int blk = 0; blk < 400; ++blk)
        eng.renderBlockDouble(buf, 128);
    CHECK(eng.activeVoices() == 0);
}

TEST(OutputBlock_quantization_range)
{
    AudioConfig::instance().setOutputBitDepth(16);
    OutputBlock out;
    CHECK(out.quantize(1.0) == 32767);
    CHECK(out.quantize(-1.0) == -32767);
    CHECK(out.quantize(2.0) == 32767); // clamped
    CHECK(out.quantize(0.0) == 0);
}

// Renders the 8-note scenario (sequential voice-split path = the shipped path)
// and returns the packed bytes.
static std::vector<uint8_t> renderScenario()
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    AudioConfig::instance().setOutputBitDepth(16);
    SynthEngine eng;
    eng.applyParam(GROUP_OSC1 + OSC_VOICE_COUNT, 5);
    eng.applyParam(GROUP_OSC2 + OSC_VOICE_COUNT, 5);
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i) eng.noteOn(notes[i], 0.8);

    std::vector<uint8_t> all, blk;
    const int frames = 128;
    for (int b = 0; b < 100; ++b)
    {
        eng.renderBlockBytes(blk, frames);
        all.insert(all.end(), blk.begin(), blk.end());
    }
    return all;
}

// The render must be deterministic across instances (no uninitialized state).
TEST(SynthEngine_render_is_deterministic)
{
    std::vector<uint8_t> a = renderScenario();
    std::vector<uint8_t> b = renderScenario();
    CHECK(!a.empty());
    CHECK(a == b);
}

// Voice-split parallel render (two halves on separate threads) must equal the
// sequential render byte-for-byte => the multi-core path is race-free.
static std::vector<uint8_t> renderParallel()
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);
    AudioConfig::instance().setOutputBitDepth(16);
    SynthEngine eng;
    eng.applyParam(GROUP_OSC1 + OSC_VOICE_COUNT, 5);
    eng.applyParam(GROUP_OSC2 + OSC_VOICE_COUNT, 5);
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i) eng.noteOn(notes[i], 0.8);
    std::vector<uint8_t> all, blk;
    const int frames = 128;
    for (int b = 0; b < 100; ++b)
    {
        eng.beginBlock(frames);
        std::thread t1([&] { eng.renderVoiceHalf(1, frames); });
        eng.renderVoiceHalf(0, frames);
        t1.join();
        eng.finishBlock(frames);
        eng.packBytes(blk, frames);
        all.insert(all.end(), blk.begin(), blk.end());
    }
    return all;
}

TEST(MultiCore_voice_split_parallel_is_byte_identical)
{
    std::vector<uint8_t> seq = renderScenario(); // sequential voice halves
    std::vector<uint8_t> par = renderParallel(); // halves on two threads
    CHECK(seq.size() == par.size());
    CHECK(!seq.empty());
    CHECK(seq == par);
}

// ─────────── Refactor (v1.0.0) coverage: SmoothedParameter / ParameterSet ───────────

TEST(SmoothedParameter_ramp_and_snap)
{
    SmoothedParameter p;
    p.init(1.0);
    CHECK_NEAR(p.current, 1.0, 1e-12);
    CHECK_NEAR(p.last, 1.0, 1e-12);
    CHECK_NEAR(p.target, 1.0, 1e-12);
    CHECK(!p.setTarget(1.0));            // unchanged target => false, no ramp needed
    CHECK(p.setTarget(3.0));             // moved => true
    p.beginRamp();                       // anchor last at current (1.0)
    p.interpolate(0.0); CHECK_NEAR(p.current, 1.0, 1e-12);
    p.interpolate(0.5); CHECK_NEAR(p.current, 2.0, 1e-12);
    p.interpolate(1.0); CHECK_NEAR(p.current, 3.0, 1e-12);
    p.current = 0.0;
    p.snap();                            // jump to target
    CHECK_NEAR(p.current, 3.0, 1e-12);
    CHECK_NEAR(p.last, 3.0, 1e-12);
}

TEST(ParameterSet_bulk_ops)
{
    ParameterSet ps;
    ps.resize(2);
    CHECK(ps.size() == 2);
    ps.init(0, 1.0);
    ps.init(1, 10.0);
    CHECK(ps.setTarget(0, 2.0));
    CHECK(ps.setTarget(1, 20.0));
    ps.beginRamp();
    ps.interpolate(0.5);
    CHECK_NEAR(ps.current(0), 1.5, 1e-12);
    CHECK_NEAR(ps.current(1), 15.0, 1e-12);
    CHECK_NEAR(ps.target(0), 2.0, 1e-12);
    ps.snapAll();
    CHECK_NEAR(ps.current(0), 2.0, 1e-12);
    CHECK_NEAR(ps.current(1), 20.0, 1e-12);
}

// Drives SignalProcessor's smoothing scheduler through a concrete subclass (Gain):
// a parameter change must ramp across the buffer, not jump.
TEST(SignalProcessor_property_smoothing_ramps)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(1);
    AudioConfig::instance().setBufferSize(8);
    Gain g(1.0);
    g.setSmoothEnable(true);
    g.setGain(3.0);                                          // target 1 -> 3 over 8 samples
    CHECK_NEAR(g.getPropertyTargetValue(Gain::gainID), 3.0, 1e-12);
    double y1 = g.out(1.0, 0);
    CHECK(y1 > 1.0 && y1 < 3.0);                             // mid-ramp, not snapped
    for (int i = 0; i < 8; ++i) g.out(1.0, 0);              // finish the ramp
    CHECK_NEAR(g.out(1.0, 0), 3.0, 1e-9);                    // arrived at target
    AudioConfig::instance().setBufferSize(128);             // restore for later tests
}

// Smoothing disabled => parameter applies immediately (snap), no ramp.
TEST(SignalProcessor_smoothing_disabled_snaps)
{
    AudioConfig::instance().setChannelCount(1);
    AudioConfig::instance().setBufferSize(64);
    Gain g(1.0);
    g.setSmoothEnable(false);
    g.setGain(5.0);
    CHECK_NEAR(g.out(1.0, 0), 5.0, 1e-9);
    AudioConfig::instance().setBufferSize(128);
}

// Bypass returns the input untouched; otherwise gain is applied.
TEST(SignalProcessor_bypass_passthrough)
{
    AudioConfig::instance().setChannelCount(1);
    Gain g(4.0);
    CHECK(!g.isBypassed());
    CHECK_NEAR(g.out(2.0, 0), 8.0, 1e-9);
    g.setBypass(true);
    CHECK(g.isBypassed());
    CHECK_NEAR(g.out(2.0, 0), 2.0, 1e-9);                    // passthrough
}

// Natural stereo: a mono input fed to both channels must yield IDENTICAL tails at
// width 0 (mono) and DECORRELATED tails at width 1 (wide). See reverb/README.md.
TEST(Reverb_stereo_decorrelation)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);

    auto runDiff = [](Sample width) {
        Reverb rv;
        rv.setWidth(width);
        rv.setDelayInMs(20.0);
        rv.setDecayInMs(300.0);
        rv.setMix(1.0);                       // fully wet so we compare the tail
        double diff = 0.0, energy = 0.0;
        for (int i = 0; i < 8000; ++i)
        {
            Sample in = std::sin(i * 0.05);   // same mono excitation on both channels
            double l = rv.out(in, 0);
            double r = rv.out(in, 1);
            diff += (l - r) * (l - r);
            energy += l * l + r * r;
        }
        return std::make_pair(diff, energy);
    };

    auto mono = runDiff(0.0);
    CHECK(mono.first < 1e-12);                // width 0 => L and R identical (mono)

    auto wide = runDiff(1.0);
    CHECK(wide.second > 1e-6);                // produced a tail
    CHECK(wide.first > 1e-4 * wide.second);   // width 1 => L and R meaningfully decorrelated
}

int main()
{
    return mini::runAll();
}
