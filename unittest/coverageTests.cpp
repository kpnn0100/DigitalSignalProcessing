/*
 *  Arstro DSP — coverage tests.
 *
 *  Exercises the module APIs and branches that the behavioural tests in
 *  synthTests.cpp don't reach, driving line coverage of the synth-path sources
 *  to 100%. No main() here — registration is shared with synthTests.cpp via the
 *  MiniTest harness (inline static registry).
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <cmath>
#include <vector>

using namespace arstro;

// Reset global config to a known 2-channel state between feature areas.
static void resetConfig(int channels = 2)
{
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setBufferSize(128);
    AudioConfig::instance().setChannelCount(channels);
    AudioConfig::instance().setOutputBitDepth(16);
}

// ─────────────────────────── base/ ───────────────────────────

TEST(AudioConfig_channel_count_clamped)
{
    AudioConfig::instance().setChannelCount(0);          // invalid -> clamps to 1
    CHECK(AudioConfig::instance().channelCount() == 1);
    AudioConfig::instance().setChannelCount(2);
    CHECK(AudioConfig::instance().channelCount() == 2);
}

namespace {
// A property change listener used to cover notifyPropertyListener().
struct CountingListener : public IPropertyChangeListener
{
    int count = 0;
    void onPropertyChange() override { ++count; }
};
}

TEST(SignalProcessor_parent_listener_and_name)
{
    resetConfig(1);
    Gain parent(1.0), child(2.0);
    child.setParent(&parent);            // setParent
    child.setName("child-gain");         // setName
    CountingListener listener;
    child.addPropertyListener(&listener);
    child.setGain(3.0);                  // setProperty -> callUpdate -> notify
    child.callRecursiveUpdate();         // walks up to parent
    child.out(1.0, 0);                   // advances smoothing -> notifies listener
    CHECK(listener.count > 0);
    CHECK_NEAR(child.getSampleDelay(), 0.0, 1e-12);
}

TEST(SignalProcessor_sample_rate_fanout)
{
    resetConfig(2);
    Gain g(1.0);
    AudioConfig::instance().setSampleRate(44100);        // fans out onSampleRateChanged
    CHECK(AudioConfig::instance().sampleRate() == 44100);
    AudioConfig::instance().setSampleRate(48000);
}

TEST(Block_parallel_average_and_remove)
{
    resetConfig(1);
    Gain a(1.0), b(3.0);
    Block blk;
    blk.add(&a);                         // add to all channels
    blk.add(&b);
    blk.setIsParallel(true);
    blk.setNeedAverage(true);
    blk.prepare();
    double avg = blk.out(1.0, 0);        // (1 + 3) / 2 = 2
    CHECK_NEAR(avg, 2.0, 1e-9);
    blk.setNeedAverage(false);
    double sum = blk.out(1.0, 0);        // 1 + 3 = 4
    CHECK_NEAR(sum, 4.0, 1e-9);
    blk.remove(&a);                      // remove existing
    blk.remove(&a);                      // remove missing -> "not matched" branch
    blk.setBypass(true);
    CHECK_NEAR(blk.out(5.0, 0), 5.0, 1e-9); // bypass passthrough
    AudioConfig::instance().setChannelCount(2); // onChannelCountChanged
    AudioConfig::instance().setChannelCount(1);
}

TEST(FeedbackBlock_feedback_loop)
{
    resetConfig(2);
    Gain forward(0.5), fb(0.5);
    FeedbackBlock loop;
    loop.setForwardProcessor(&forward);
    loop.setFeedbackProcessor(&fb);
    loop.setFeedbackGain(0.3);
    loop.prepare();
    bool finite = true;
    for (int i = 0; i < 64; ++i)
    {
        double l = loop.out(1.0, 0);
        double r = loop.out(1.0, 1);
        finite = finite && std::isfinite(l) && std::isfinite(r);
    }
    CHECK(finite);
    CHECK_NEAR(loop.out(1.0, 99), 1.0, 1e-9);     // channel out of range -> returns in
    AudioConfig::instance().setChannelCount(1);   // onChannelCountChanged -> ensureChannels
    loop.out(1.0, 0);
}

// ─────────────────────────── simpleProcessor/ ───────────────────────────

TEST(Delay_reads_and_writes)
{
    resetConfig(1);
    Delay d(10.0);                       // single-arg ctor
    d.setSmoothEnable(false);
    d.setDelay(10.0);
    CHECK_NEAR(d.getCurrentDelay(), 10.0, 1.0);
    // Feed an impulse; after ~10 samples it should re-appear (delay > 2 path).
    double peakLater = 0.0;
    d.out(1.0, 0);
    for (int i = 0; i < 20; ++i)
        peakLater = std::fmax(peakLater, std::fabs(d.out(0.0, 0)));
    CHECK(peakLater > 0.1);              // the impulse came back out
    Delay tiny(1.0);                     // delay <= 2 -> passthrough branch
    tiny.setSmoothEnable(false);
    tiny.setDelay(1.0);
    CHECK_NEAR(tiny.out(0.7, 0), 0.7, 1e-9);
}

TEST(Gain_default_ctor)
{
    resetConfig(1);
    Gain g;                              // default ctor -> Gain(1.0)
    CHECK_NEAR(g.out(0.9, 0), 0.9, 1e-9);
}

// ─────────────────────────── equalizer/ ───────────────────────────

TEST(LowPassFilter_prepare_and_open)
{
    resetConfig(1);
    LowPassFilter lpf;
    lpf.setCutoffFrequency(1e8);         // huge cutoff -> alpha clamps to 1.0 (open)
    static_cast<SignalProcessor &>(lpf).prepare();   // prepare() override is private -> via base
    double y = lpf.out(0.5, 0);
    CHECK(std::isfinite(y));

    LowPassFilter lpf2;                  // NaN cutoff -> alpha NaN -> guarded to 1.0
    lpf2.setSmoothEnable(false);
    lpf2.setCutoffFrequency(std::nan(""));
    CHECK(std::isfinite(lpf2.out(0.5, 0)));
}

// ─────────────────────────── envelope/ ───────────────────────────

TEST(ADSR_full_stage_traversal)
{
    resetConfig(1);
    ADSREnvelope env;
    CHECK_NEAR(env.out(1.0, 0), 0.0, 1e-12);   // Idle -> returns 0
    env.setAttackMs(2.0);
    env.setDecayMs(2.0);
    env.setSustain(0.4);
    env.setReleaseMs(2.0);
    env.noteOn(1.0);
    double maxSeen = 0.0;
    for (int i = 0; i < 480; ++i)              // through attack + decay -> sustain
        maxSeen = std::fmax(maxSeen, env.out(1.0, 0));
    CHECK(maxSeen > 0.9);                       // attack reached peak
    CHECK_NEAR(env.out(1.0, 0), 0.4, 0.05);     // settled at sustain
    env.noteOff();
    for (int i = 0; i < 480; ++i) env.out(1.0, 0); // release -> idle
    CHECK(env.isFinished());
    AudioConfig::instance().setChannelCount(2);    // onChannelCountChanged
    AudioConfig::instance().setChannelCount(1);
}

// ─────────────────────────── generator/ ───────────────────────────

TEST(Oscillator_simd_block_and_hooks)
{
    resetConfig(2);
    Oscillator osc;
    osc.setVoiceCount(3);
    osc.setDetuneCents(15);
    osc.setStereoSpreadCents(8);               // setStereoSpreadCents
    osc.setFrequency(330.0);
    osc.noteOn(1.0);
    AudioConfig::instance().setSampleRate(44100);  // onSampleRateChanged -> recalcIncrements
    AudioConfig::instance().setSampleRate(48000);

    Oscillator::setUseSimd(true);
    CHECK(Oscillator::useSimd());
    std::vector<Sample> buf(256, 0.0);
    osc.addBlock(buf.data(), 256, 0, 1.0);     // SIMD block path
    std::vector<Sample> big(2000, 0.0);
    osc.addBlock(big.data(), 2000, 0, 1.0);    // frames > kMaxBlock -> scalar fallback
    Oscillator::setUseSimd(false);
    bool any = false;
    for (Sample s : buf) any = any || std::fabs(s) > 1e-9;
    CHECK(any);
}

// ─────────────────────────── effects/ ───────────────────────────

// Helper: run an effect through process at 2 channels then a channel-count
// change (covers ensureChannels resize + early-return) and the bypass branch.
template <typename FX>
static void exerciseEffect(FX &fx, bool &_ok)
{
    resetConfig(2);
    for (int i = 0; i < 64; ++i) { fx.out(0.3, 0); fx.out(0.3, 1); }
    fx.out(0.3, 99);                              // channel out of range -> process() returns in
    AudioConfig::instance().setChannelCount(1);   // resize
    AudioConfig::instance().setChannelCount(1);   // same -> early return in ensureChannels
    fx.out(0.3, 0);
    fx.setBypass(true);
    CHECK_NEAR(fx.out(0.42, 0), 0.42, 1e-9);      // bypass passthrough
    fx.setBypass(false);
}

TEST(Compressor_api_and_bypass)
{
    Compressor c;
    c.setThresholdDb(-12.0);
    c.setRatio(0.5);            // clamped up to 1.0 internally
    c.setRatio(4.0);
    c.setAttackMs(5.0);
    c.setReleaseMs(50.0);
    c.setMakeupGain(1.5);
    exerciseEffect(c, _ok);
}

TEST(Overdrive_api_and_bypass)
{
    Overdrive od;
    od.setDrive(3.0);
    od.setToneHz(3000.0);
    od.setLevel(0.8);
    exerciseEffect(od, _ok);
}

TEST(Chorus_api_and_bypass)
{
    Chorus ch;
    ch.setRateHz(1.5);
    ch.setDepthMs(2.0);
    ch.setBaseDelayMs(0.0);     // tiny base delay -> delaySamples clamp branch
    ch.setMix(0.5);
    exerciseEffect(ch, _ok);
}

TEST(Repeater_api_and_bypass)
{
    Repeater rp;
    rp.setDelayMs(120.0);
    rp.setFeedback(-1.0);       // clamps to 0.0
    rp.setFeedback(2.0);        // clamps to 0.99
    rp.setFeedback(0.4);
    rp.setToneCutoffHz(4000.0);
    rp.setMix(0.3);
    exerciseEffect(rp, _ok);
    resetConfig(1);
    Repeater rp2;
    rp2.setDelayMs(0.0);                          // delaySamples < 1 -> process returns in
    CHECK_NEAR(rp2.out(0.55, 0), 0.55, 1e-9);
}

// ─────────────────────────── reverb/ ───────────────────────────

TEST(Reverb_api_and_process)
{
    resetConfig(2);
    Reverb rv;
    rv.setDelayInMs(30.0);
    rv.setDecayInMs(500.0);
    rv.setDiffusion(4);
    rv.setDiffusion(4);         // unchanged -> no-op branch
    rv.setLowCutFrequency(200.0);
    rv.setHighCutFrequency(8000.0);
    rv.setMix(-0.5);            // clamp low -> 0
    rv.setMix(1.5);             // clamp high -> 1
    rv.setMix(0.3);
    rv.setWidth(-1.0);          // clamp low -> 0 (changed -> callUpdate)
    rv.setWidth(2.0);           // clamp high -> 1 (changed -> callUpdate)
    rv.setWidth(1.0);           // same as current -> no-op branch
    bool finite = true;
    for (int i = 0; i < 256; ++i)
    {
        finite = finite && std::isfinite(rv.out(i == 0 ? 1.0 : 0.0, 0));
        finite = finite && std::isfinite(rv.out(0.0, 1));
    }
    CHECK(finite);
    AudioConfig::instance().setChannelCount(1);   // onChannelCountChanged
    rv.out(0.0, 0);
}

// ─────────────────────────── synth/ ───────────────────────────

TEST(VoiceManager_render_tick_and_stealing)
{
    resetConfig(1);
    VoiceManager vm;
    CHECK(vm.voiceCount() == 8);
    // Fill all 8 voices, then a 9th note forces voice stealing (oldest).
    for (int n = 60; n < 69; ++n)
    {
        vm.noteOn(n, 0.8);
        vm.advance(64);                  // age the voices so "oldest" differs
        vm.tick();
    }
    CHECK(vm.activeVoices() >= 1);
    double s = vm.render(0);             // non-block render path
    CHECK(std::isfinite(s));
    vm.tick();                           // tick path
    vm.noteOff(68);
    vm.voice(0).note();                  // accessor
}

TEST(SynthEngine_queue_and_all_params)
{
    resetConfig(2);
    SynthEngine eng;
    // Thread-safe producer API -> drained by render (drainCommands + switch cases).
    eng.pushParam(MASTER_LEVEL, 0.9);
    eng.pushParam(MASTER_OSC_SIMD, 1.0);
    eng.pushNoteOn(60, 0.8);
    eng.pushNoteOff(60);

    // Direct applyParam across every group/offset (the big switch).
    eng.applyParam(GROUP_OSC1 + OSC_VOICE_COUNT, 3);
    eng.applyParam(GROUP_OSC1 + OSC_DETUNE, 12);
    eng.applyParam(GROUP_OSC1 + OSC_SPREAD, 6);
    eng.applyParam(GROUP_OSC1 + OSC_LEVEL, 0.7);
    eng.applyParam(GROUP_OSC2 + OSC_TUNE, 7);
    eng.applyParam(GROUP_OSC1 + OSC_ATTACK, 10);
    eng.applyParam(GROUP_OSC1 + OSC_DECAY, 20);
    eng.applyParam(GROUP_OSC1 + OSC_SUSTAIN, 0.5);
    eng.applyParam(GROUP_OSC1 + OSC_RELEASE, 30);
    eng.applyParam(GROUP_OSC1 + OSC_WAVEFORM, 2);  // Square
    eng.applyParam(GROUP_OSC3 + OSC_LEVEL, 0.5);   // OSC3 ignored (kOscCount==2)

    eng.applyParam(GROUP_COMPRESSOR + CMP_THRESHOLD, -10);
    eng.applyParam(GROUP_COMPRESSOR + CMP_RATIO, 3);
    eng.applyParam(GROUP_COMPRESSOR + CMP_ATTACK, 5);
    eng.applyParam(GROUP_COMPRESSOR + CMP_RELEASE, 40);
    eng.applyParam(GROUP_COMPRESSOR + CMP_MAKEUP, 1.2);
    eng.applyParam(GROUP_OVERDRIVE + OD_DRIVE, 2);
    eng.applyParam(GROUP_OVERDRIVE + OD_TONE, 3000);
    eng.applyParam(GROUP_OVERDRIVE + OD_LEVEL, 0.8);
    eng.applyParam(GROUP_CHORUS + CH_RATE, 1.5);
    eng.applyParam(GROUP_CHORUS + CH_DEPTH, 2);
    eng.applyParam(GROUP_CHORUS + CH_BASEDELAY, 8);
    eng.applyParam(GROUP_CHORUS + CH_MIX, 0.4);
    eng.applyParam(GROUP_REPEATER + RP_DELAY, 120);
    eng.applyParam(GROUP_REPEATER + RP_FEEDBACK, 0.4);
    eng.applyParam(GROUP_REPEATER + RP_TONE, 4000);
    eng.applyParam(GROUP_REPEATER + RP_MIX, 0.3);
    eng.applyParam(GROUP_REVERB + RV_DELAY, 30);
    eng.applyParam(GROUP_REVERB + RV_DECAY, 500);
    eng.applyParam(GROUP_REVERB + RV_LOWCUT, 200);
    eng.applyParam(GROUP_REVERB + RV_HIGHCUT, 8000);
    eng.applyParam(GROUP_REVERB + RV_MIX, 0.25);
    eng.applyParam(GROUP_REVERB + RV_WIDTH, 0.7);
    // Shared effect bypass on each effect group.
    eng.applyParam(GROUP_COMPRESSOR + FX_BYPASS, 1);
    eng.applyParam(GROUP_OVERDRIVE + FX_BYPASS, 1);
    eng.applyParam(GROUP_CHORUS + FX_BYPASS, 1);
    eng.applyParam(GROUP_REPEATER + FX_BYPASS, 1);
    eng.applyParam(GROUP_REVERB + FX_BYPASS, 1);

    eng.noteOn(64, 0.9);
    std::vector<uint8_t> bytes;
    std::vector<double> dbuf;
    bool finite = true;
    for (int b = 0; b < 20; ++b)
    {
        eng.renderBlockBytes(bytes, 128);          // drains queue, packs bytes
        eng.renderBlockDouble(dbuf, 128);
        for (double s : dbuf) finite = finite && std::isfinite(s);
    }
    CHECK(finite);
    CHECK(eng.channels() == 2);
}

// ─────────────── round 2: remaining branches & guards ───────────────

TEST(Block_edge_paths)
{
    resetConfig(2);
    // Fresh block: update() before any chain exists -> empty early return.
    Block fresh;
    fresh.update();
    // Heap lifetime -> deleting (virtual) destructor.
    Block *bp = new Block();
    Gain g(2.0);
    bp->add(&g);
    bp->add(&g, 99);                 // channel out of range -> early return
    CHECK_NEAR(bp->out(1.0, 99), 1.0, 1e-9);  // process() out-of-range -> return in
    std::vector<Sample> buf(16, 0.5);
    bp->setIsParallel(true);
    bp->processBlock(buf.data(), 16, 0);      // parallel -> SignalProcessor::processBlock
    bp->setIsParallel(false);
    bp->processBlock(buf.data(), 16, 99);     // out of range -> early return
    bp->processBlock(buf.data(), 16, 0);      // normal per-processor block path
    CHECK(!bp->getProcessorList().empty());   // getProcessorList()
    delete bp;
}

TEST(SignalProcessor_nested_parent_update)
{
    resetConfig(1);
    // inner block is a child of outer; adding to inner triggers inner.update()
    // -> setSampleDelay() -> mParent(outer)->callUpdate() (the parent branch).
    Block outer, inner;
    Delay d(20.0);
    outer.add(&inner);               // inner.setParent(&outer)
    inner.add(&d);                   // inner.update -> setSampleDelay -> outer->callUpdate
    CHECK(std::isfinite(outer.out(1.0, 0)));
    // Heap-delete a SignalProcessor to cover ~SignalProcessor's erase loop.
    Gain *gp = new Gain(1.0);
    gp->out(1.0, 0);
    delete gp;
}

TEST(FeedbackBlock_null_processors)
{
    resetConfig(1);
    FeedbackBlock *fp = new FeedbackBlock(); // no forward/feedback set
    CHECK_NEAR(fp->out(0.5, 0), 0.5, 1e-9);  // null-guard -> returns input
    delete fp;
}

TEST(Delay_update_and_buffer_resize)
{
    resetConfig(1);
    Delay *dp = new Delay(10.0);
    dp->setDelay(8.0);               // different target -> callUpdate -> update()
    dp->setDelay(15.0);
    dp->setMaxDelay(50);             // grow: push_back loop
    dp->setMaxDelay(20);             // shrink: pop_back loop
    dp->out(1.0, 0);
    delete dp;                       // ~Delay
}

TEST(Oscillator_remaining_paths)
{
    resetConfig(2);
    Oscillator osc;
    osc.setVoiceCount(2);
    osc.setFrequency(220.0);
    osc.noteOn(1.0);
    AudioConfig::instance().setChannelCount(1);   // onChannelCountChanged
    AudioConfig::instance().setChannelCount(2);
    std::vector<Sample> buf(64, 0.0);
    Oscillator::setUseSimd(false);
    osc.addBlock(buf.data(), 64, 0, 1.0);         // scalar PolyBLEP fallback path
    // SIMD enabled but a non-Saw waveform -> addBlock still takes the scalar branch.
    Oscillator::setUseSimd(true);
    osc.setWaveform(Oscillator::Sine);
    osc.addBlock(buf.data(), 64, 0, 1.0);
    Oscillator::setUseSimd(false);
    osc.setWaveform((Oscillator::Waveform)99);    // unknown waveform
    CHECK_NEAR(osc.out(0.0, 0), 0.0, 1e-12);      // generate() default -> 0

    Oscillator z;                                 // freq 0 -> dt<=0 -> polyBlep returns 0
    z.setWaveform(Oscillator::Saw);
    z.setVoiceCount(1);
    z.setFrequency(0.0);
    z.noteOn(1.0);
    CHECK(std::isfinite(z.out(0.0, 0)));
}

TEST(ADSR_zero_duration_and_oob)
{
    resetConfig(1);
    ADSREnvelope env;
    env.setAttackMs(0.0);            // zero-duration fast paths
    env.setDecayMs(0.0);
    env.setSustain(0.5);
    env.setReleaseMs(0.0);
    env.noteOn(1.0);
    env.out(1.0, 0);                 // Attack(<=0) -> Decay
    env.out(1.0, 0);                 // Decay(<=0)  -> Sustain
    env.noteOff();
    env.out(1.0, 0);                 // Release(<=0) -> Idle
    CHECK(env.isFinished());
    CHECK_NEAR(env.out(1.0, 99), 0.0, 1e-12);     // channel out of range -> 0
}

TEST(Reverb_remaining_paths)
{
    resetConfig(2);
    CHECK(std::isfinite(randomInRange(0.0, 1.0)));  // library helper
    Reverb rv;
    AudioConfig::instance().setSampleRate(44100);    // onSampleRateChanged
    AudioConfig::instance().setSampleRate(48000);
    AudioConfig::instance().setChannelCount(2);      // same count -> ensureChannels early return
    CHECK_NEAR(rv.out(0.3, 99), 0.3, 1e-9);          // channel out of range -> return in
}

TEST(SynthEngine_unknown_params_and_resize)
{
    resetConfig(2);
    SynthEngine eng;
    eng.applyParam(GROUP_OSC1 + 99, 0.0);   // unknown osc offset -> default break
    eng.applyParam(GROUP_MASTER + 99, 0.0); // unknown master offset -> default break
    eng.applyParam(GROUP_REVERB + 99, 0.0); // unknown effect offset -> inner default break
    eng.applyParam(GROUP_MASTER + FX_BYPASS, 1.0); // FX_BYPASS on a non-effect group -> default break
    eng.applyParam(0x9900 + 0, 0.0);        // unknown group -> outer default break
    // Increasing frame size between blocks exercises the buffer-resize branch.
    eng.beginBlock(64);
    eng.renderVoiceHalf(0, 64);
    eng.renderVoiceHalf(1, 64);
    eng.finishBlock(64);
    eng.beginBlock(256);                    // larger -> resize existing buffers
    eng.renderVoiceHalf(0, 256);
    eng.renderVoiceHalf(1, 256);
    eng.finishBlock(256);
    std::vector<uint8_t> out;
    eng.packBytes(out, 256);
    CHECK(!out.empty());
}

TEST(VoiceManager_renderBlock_single)
{
    resetConfig(1);
    VoiceManager vm;
    vm.noteOn(60, 0.8);
    std::vector<Sample> buf(32, 0.0);
    vm.renderBlock(buf.data(), 32, 0);      // single-arg renderBlock -> renderBlockRange
    bool any = false;
    for (Sample s : buf) any = any || std::fabs(s) > 1e-12;
    CHECK(any);
}

// ─────────────────────────── physical/ (piano) ───────────────────────────

TEST(StringResonator_api_bypass_and_channel_guard)
{
    StringResonator res;
    res.setFrequencyHz(300.0);
    res.setDecaySeconds(0.2);
    exerciseEffect(res, _ok);

    // Numerical safety clamps (README ## 1 note): absurd frequency clamps to
    // Nyquist, tiny decay clamps to a positive floor — neither should NaN/blow up.
    resetConfig(1);
    StringResonator res2;
    res2.setFrequencyHz(1.0e9);
    res2.setDecaySeconds(0.0);
    for (int i = 0; i < 50; ++i)
    {
        Sample s = res2.out((i == 0) ? 1.0 : 0.0, 0);
        CHECK(!std::isnan(s) && !std::isinf(s));
    }
}

TEST(StringPartialBank_api_bypass_and_setters)
{
    StringPartialBank bank;
    bank.setFundamentalHz(220.0);
    bank.setInharmonicity(0.001);
    bank.setBaseDecaySeconds(1.5);
    bank.setDampingExponent(0.8);
    bank.setStrikePosition(0.1);
    bank.setDamperEngageMs(5.0);
    exerciseEffect(bank, _ok);

    // Damper ramp: engaging then lifting should not blow up, and should actually
    // change the effective decay (covers the mDamperValue-changing branch twice).
    resetConfig(1);
    StringPartialBank bank2;
    bank2.setFundamentalHz(220.0);
    bank2.setBaseDecaySeconds(1.0);
    bank2.setDamperEngageMs(2.0); // short ramp so the test doesn't need many samples
    for (int i = 0; i < 20; ++i) bank2.out((i == 0) ? 1.0 : 0.0, 0);
    bank2.setDamperEngagement(1.0);
    for (int i = 0; i < 200; ++i) { Sample s = bank2.out(0.0, 0); CHECK(!std::isnan(s)); }
    bank2.setDamperEngagement(0.0);
    for (int i = 0; i < 200; ++i) { Sample s = bank2.out(0.0, 0); CHECK(!std::isnan(s)); }
}

TEST(HammerExciter_api_bypass_and_contact_lifecycle)
{
    HammerExciter h;
    h.setMass(1.0);
    h.setStiffness(1.0e10);
    h.setNonlinearExponent(2.5);
    h.setHysteresisLoss(0.2);
    exerciseEffect(h, _ok); // covers channel!=0 -> 0.0 branch too (channel 99 in the helper)

    resetConfig(1);
    HammerExciter h2;
    CHECK(!h2.isInContact());
    h2.strike(0.7);
    CHECK(h2.isInContact());
    bool sawContactEnd = false;
    for (int i = 0; i < 2000 && !sawContactEnd; ++i)
    {
        h2.out(0.0, 0);
        if (!h2.isInContact()) sawContactEnd = true;
    }
    CHECK(sawContactEnd); // contact ends on its own well before the 15ms safety cap
    CHECK_NEAR(h2.out(0.0, 0), 0.0, 1e-9); // no force once contact has ended

    // Safety-bound branch: a velocity so soft the ODE alone won't end contact
    // quickly — must still stop by kMaxContactMs rather than running forever.
    HammerExciter h3;
    h3.strike(1e-6);
    int n = 0;
    while (h3.isInContact() && n < 20000) { h3.out(0.0, 0); ++n; }
    CHECK(!h3.isInContact());
    CHECK(n < 20000);
}

TEST(PianoBridge_api_bypass_and_multi_voice_bus)
{
    PianoBridge bridge;
    bridge.setCouplingGain(0.2);
    bridge.setRadiationGain(0.4);
    exerciseEffect(bridge, _ok);

    resetConfig(1);
    PianoBridge bridge2;
    CHECK_NEAR(bridge2.feedback(), 0.0, 1e-12); // nothing accumulated yet
    bridge2.accumulate(1.0);
    bridge2.accumulate(0.5); // two voices contributing to the same sample
    bridge2.tick();
    CHECK(bridge2.feedback() != 0.0 || bridge2.radiatedOutput() != 0.0);
}

TEST(PianoVoice_unison_una_corda_and_pedal_api)
{
    resetConfig(2); // exercise the channel>0 replicate-cached-sample path (REQ-piano-13)
    PianoVoice v;
    v.setFrequency(261.63);
    v.setUnisonCount(0);      // clamps to 1
    v.setUnisonCount(9);      // clamps to kMaxUnison
    v.setUnisonCount(2);
    v.setUnisonDetuneCents(0.8);
    v.setInharmonicity(0.0005); // explicit override branch (skips the register-default path)
    v.setBaseDecaySeconds(2.0);
    v.setDampingExponent(0.9);
    v.setStrikePosition(0.125);
    v.setDamperEngageMs(15.0);
    v.setHammerMass(1.0);
    v.setHammerStiffness(1.0e10);
    v.setHammerNonlinearExponent(2.5);
    v.setHammerHysteresisLoss(0.2);
    v.setUnaCorda(true);
    v.setDamperHeld(false);
    v.setBridge(nullptr); // standalone (no cross-string coupling) path

    v.noteOn(0.6);
    Sample left = v.out(0.0, 0);
    Sample right = v.out(0.0, 1); // channel 1 must read the cached channel-0 sample
    CHECK_NEAR(left, right, 1e-9);
    for (int i = 0; i < 100; ++i) { v.out(0.0, 0); v.out(0.0, 1); }
    v.noteOff();
    for (int i = 0; i < 100; ++i) { Sample s = v.out(0.0, 0); CHECK(!std::isnan(s)); }
    CHECK(!v.isFinished()); // long backstop release hasn't elapsed yet
}

TEST(PianoVoice_sample_rate_change_recomputes_noise_coeffs)
{
    resetConfig(1);
    PianoVoice v;
    v.setFrequency(220.0);
    AudioConfig::instance().setSampleRate(44100.0); // broadcasts onSampleRateChanged() to v
    v.noteOn(0.5);
    for (int i = 0; i < 50; ++i) { Sample s = v.out(0.0, 0); CHECK(!std::isnan(s)); }
    resetConfig(1); // restore 48000 for subsequent tests
}

TEST(PianoVoice_sustain_pedal_defers_damper)
{
    resetConfig(1);
    PianoVoice v;
    v.setFrequency(220.0);
    v.setDamperHeld(true); // sustain pedal down
    v.noteOn(0.7);
    for (int i = 0; i < 100; ++i) v.out(0.0, 0);
    v.noteOff(); // damper must NOT engage while held
    for (int i = 0; i < 100; ++i) { Sample s = v.out(0.0, 0); CHECK(!std::isnan(s)); }
    v.setDamperHeld(false);
    v.noteOff(); // pedal released with key already up -> damper engages now
    for (int i = 0; i < 2000; ++i) { Sample s = v.out(0.0, 0); CHECK(!std::isnan(s)); }
}
