#include "MiniTest.h"
#include "../synth_dsp.h"
#include <cmath>
#include <thread>

using namespace gyrus_space;

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

int main()
{
    return mini::runAll();
}
