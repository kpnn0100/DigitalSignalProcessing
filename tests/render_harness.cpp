/*
 *  Arstro DSP — integration test render harness.
 *
 *  Builds a small graph from the channel-aware library (synth_dsp.h) and writes
 *  the result to a mono 16-bit PCM WAV that the Python stdlib `wave` module can
 *  read. NO libsndfile dependency.
 *
 *  Usage:  render_harness <scenario> <outfile.wav> [arg]
 *    gain <out>           0.4-amplitude 200 Hz sine through Gain(2.0)
 *    osc  <out> <freqHz>  1-voice saw oscillator at <freqHz>, settled tone
 *    lpf  <out> <freqHz>  0.5-amplitude sine at <freqHz> through LPF cutoff 500
 *    adsr <out>           constant 1.0 through an ADSR (note on, then off)
 *    synth <out>          8-note SynthEngine scenario (full path)
 *    synthparallel <out> <workers>  the same render driven through a worker pool
 *                         (0 = serial reference) — REQ-compute-3 bit-identity
 *    reverb <out>         mono burst -> STEREO reverb (width 1); writes a 2-ch WAV
 *    piano <out> <freqHz> PianoVoice struck note (large inharmonicity override so
 *                         partial-4's shift is easy to measure), 1 s render
 *    pianodamper <out> <0|1>  PianoVoice: strike, 100ms, noteOff. arg=0 damper
 *                         engages (not held); arg=1 sustain held (setDamperHeld)
 *    pianospectral <out>  PianoVoice C4 held 1.5 s, all defaults — for the M1
 *                         spectral-evolution measurement (high/low band collapse)
 *    pianodoubledecay <out>  PianoVoice C4 held 5 s, 1 unison string — for the M4
 *                         prompt-sound/aftersound (double decay) measurement
 *    bridgeimpulse <out>  PianoBridge impulse response (normalised) — for the M5
 *                         soundboard transfer-function measurement
 *    pianoregister <out> <hz>  One PianoVoice struck at <hz> with every default —
 *                         so README ## 11's register scaling is what differs
 *                         between renders. For the M6 timbre-vs-pitch measurement.
 *    pianoreuse <out>     PianoVoice: strike C4, release (damper engages+settles),
 *                         then setFrequency(A4)+noteOn() on the SAME voice —
 *                         the exact voice-steal sequence PianoEngine uses
 *    peq <out> <hz>       unit sine through a ParametricEQ (+6 dB @ 1 kHz, high cut 12 kHz), ×0.5
 *    svf <out> <hz>       unit sine through a 1 kHz resonance-0.5 low-pass SVF, ×0.1
 *    noisedecay <out>     seeded noise × DecayEnvelope(200 ms), peak 0.9
 *    synth2 <out> <note>  BasicSynth, osc 1 sine only, filter open, MIDI <note> for 1 s
 *    kick <out> <tune>    DrumMachine kick tuned <tune> semitones, 0.5 s
 *    deviceeq <out> <hz>  unit sine through DeviceRegistry "eq" with peak2.gain = 6 by name, ×0.5
 *    pianosympathetic <out> <0|1>  Struck A3 (220Hz) + a silently-depressed voice
 *                         sharing one PianoBridge; arg=0 renders the SAME-pitch
 *                         silent voice, arg=1 the OFF-pitch (233.08Hz) one
 */
#include "../src/synth_dsp.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace arstro;

static const int kSampleRate = 48000;

static int16_t toPcm(double s)
{
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    return static_cast<int16_t>(std::lround(s * 32767.0));
}

static void writeWavMono16(const std::string &path, const std::vector<double> &samples, int sr)
{
    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) pcm[i] = toPcm(samples[i]);

    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t byteRate = static_cast<uint32_t>(sr) * 1 * 2;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror("fopen"); std::exit(2); }

    auto u32 = [&](uint32_t v){ std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v){ std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);  u32(36 + dataBytes);  std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  u32(16);  u16(1);     // PCM
    u16(1);                                              // mono
    u32(static_cast<uint32_t>(sr));  u32(byteRate);  u16(2);  u16(16);
    std::fwrite("data", 1, 4, f);  u32(dataBytes);
    std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
    std::fclose(f);
}

static void writeWavStereo16(const std::string &path,
                             const std::vector<double> &L, const std::vector<double> &R, int sr)
{
    const size_t frames = L.size();
    std::vector<int16_t> pcm(frames * 2);
    for (size_t i = 0; i < frames; ++i) { pcm[2 * i] = toPcm(L[i]); pcm[2 * i + 1] = toPcm(R[i]); }

    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t byteRate = static_cast<uint32_t>(sr) * 2 * 2;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror("fopen"); std::exit(2); }
    auto u32 = [&](uint32_t v){ std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v){ std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);  u32(36 + dataBytes);  std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  u32(16);  u16(1);     // PCM
    u16(2);                                              // stereo
    u32(static_cast<uint32_t>(sr));  u32(byteRate);  u16(4);  u16(16);
    std::fwrite("data", 1, 4, f);  u32(dataBytes);
    std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
    std::fclose(f);
}

// Mono burst through the STEREO reverb; returns L and R (must decorrelate at width 1).
static void renderReverbStereo(std::vector<double> &L, std::vector<double> &R)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    Reverb rv;
    rv.setWidth(1.0);
    rv.setDelayInMs(20.0);
    rv.setDecayInMs(400.0);
    rv.setMix(1.0);                       // fully wet so the tail is what we measure
    const int n = kSampleRate;            // 1 s
    L.resize(n); R.resize(n);
    for (int i = 0; i < n; ++i)
    {
        Sample in = (i < kSampleRate / 10) ? std::sin(i * 0.05) : 0.0; // 0.1 s burst, then tail
        L[i] = rv.out(in, 0);
        R[i] = rv.out(in, 1);
    }
}

static std::vector<double> renderGain()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    Gain g(2.0);
    const int n = kSampleRate; // 1 s
    std::vector<double> out(n);
    const double w = 2.0 * M_PI * 200.0 / kSampleRate;
    for (int i = 0; i < n; ++i)
        out[i] = g.out(0.4 * std::sin(w * i), 0);
    return out;
}

static std::vector<double> renderOsc(double freq)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    Oscillator osc;
    osc.setVoiceCount(1);
    osc.setDetuneCents(0);
    osc.setStereoSpreadCents(0);
    osc.setAttackMs(1.0);
    osc.setDecayMs(1.0);
    osc.setSustain(1.0);
    osc.setReleaseMs(1.0);
    osc.setFrequency(freq);
    osc.noteOn(1.0);
    const int n = kSampleRate / 2; // 0.5 s
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = osc.out(0.0, 0);
    return out;
}

static std::vector<double> renderOscWave(double waveform)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    Oscillator osc;
    osc.setVoiceCount(1);
    osc.setDetuneCents(0);
    osc.setStereoSpreadCents(0);
    osc.setWaveform((Oscillator::Waveform)(int)(waveform + 0.5));
    osc.setAttackMs(1.0);
    osc.setDecayMs(1.0);
    osc.setSustain(1.0);
    osc.setReleaseMs(1.0);
    osc.setFrequency(220.0); // fixed fundamental so the harness/asserts agree
    osc.noteOn(1.0);
    const int n = kSampleRate / 2; // 0.5 s
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = osc.out(0.0, 0);
    return out;
}

static std::vector<double> renderLpf(double freq)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    LowPassFilter lpf;
    lpf.setCutoffFrequency(500.0);
    const int n = kSampleRate; // 1 s
    std::vector<double> out(n);
    const double w = 2.0 * M_PI * freq / kSampleRate;
    for (int i = 0; i < n; ++i)
        out[i] = lpf.out(0.5 * std::sin(w * i), 0);
    return out;
}

static std::vector<double> renderAdsr()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    ADSREnvelope env;
    env.setAttackMs(50.0);
    env.setDecayMs(20.0);
    env.setSustain(0.5);
    env.setReleaseMs(50.0);
    std::vector<double> out;
    env.noteOn(1.0);
    for (int i = 0; i < kSampleRate / 5; ++i) out.push_back(env.out(1.0, 0)); // 0.2 s on
    env.noteOff();
    for (int i = 0; i < kSampleRate / 5; ++i) out.push_back(env.out(1.0, 0)); // 0.2 s release
    return out;
}

static std::vector<double> renderSynth()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    AudioConfig::instance().setOutputBitDepth(16);
    SynthEngine eng;
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i) eng.noteOn(notes[i], 0.8);
    std::vector<double> out, blk;
    for (int b = 0; b < 200; ++b) // ~0.5 s
    {
        eng.renderBlockDouble(blk, 128);
        out.insert(out.end(), blk.begin(), blk.end());
    }
    return out;
}

// REQ-compute-3: the same SynthEngine render, driven through a worker pool
// instead of serially. arg = worker count (0 = serial reference). The two must
// come out BIT-IDENTICAL, which Python checks by comparing the WAV bytes.
static std::vector<double> renderSynthParallel(double workers)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    AudioConfig::instance().setOutputBitDepth(16);

    SerialExecutor serial;
    ThreadPoolExecutor pool((int)(workers + 0.5));
    ComputeConfig::instance().setExecutor(workers < 0.5 ? (ParallelExecutor *)&serial
                                                        : (ParallelExecutor *)&pool);
    SynthEngine eng;
    int notes[8] = {48, 52, 55, 60, 64, 67, 72, 76};
    for (int i = 0; i < 8; ++i) eng.noteOn(notes[i], 0.8);
    std::vector<double> out;
    std::vector<uint8_t> blk;
    for (int b = 0; b < 200; ++b) // ~0.5 s
    {
        eng.renderBlockBytesParallel(blk, 128);
        for (size_t i = 0; i + 1 < blk.size(); i += 2)
            out.push_back((double)(int16_t)(blk[i] | (blk[i + 1] << 8)) / 32767.0);
    }
    ComputeConfig::instance().setExecutor(nullptr);
    return out;
}

static std::vector<double> renderPiano(double freqHz)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    if (freqHz <= 0.0) freqHz = 220.0;
    PianoVoice v;
    v.setFrequency(freqHz);
    v.setInharmonicity(0.02); // large, fixed override -> partial shift easy to measure
    v.setStrikePosition(0.125);
    v.noteOn(0.85);
    const int n = kSampleRate; // 1 s
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

static std::vector<double> renderPianoDamper(double heldFlag)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(220.0);
    v.setDamperHeld(heldFlag > 0.5);
    v.noteOn(0.8);
    const int pre = kSampleRate / 10;  // 100 ms struck
    const int post = kSampleRate / 2;  // 500 ms tail
    std::vector<double> out(pre + post);
    for (int i = 0; i < pre; ++i) out[i] = v.out(0.0, 0);
    v.noteOff();
    for (int i = 0; i < post; ++i) out[pre + i] = v.out(0.0, 0);
    return out;
}

// M9.1 acceptance (README ## 7.1): the top octaves have no damper. C7 (2093 Hz, MIDI
// 96 — above the ~1357 Hz cutoff) rings on after noteOff exactly as if the key were
// held. heldFlag > 0.5 holds the (nonexistent) damper; either way the string keeps
// ringing, which is the whole point.
static std::vector<double> renderPianoNoDamper(double heldFlag)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(2093.0); // C7
    v.setDamperHeld(heldFlag > 0.5);
    v.noteOn(0.8);
    const int pre = kSampleRate / 20;  // 50 ms struck
    const int post = kSampleRate / 4;  // 250 ms tail
    std::vector<double> out(pre + post);
    for (int i = 0; i < pre; ++i) out[i] = v.out(0.0, 0);
    v.noteOff();
    for (int i = 0; i < post; ++i) out[pre + i] = v.out(0.0, 0);
    return out;
}

// M9.2 acceptance (README ## 8.1): the duplex/aliquot shimmer. C6 (1046 Hz, treble)
// held with the damper lifted; onFlag > 0.5 runs the duplex, else disables it. The
// checker differences the two to isolate the shimmer (## 12.2's lesson).
static std::vector<double> renderPianoDuplex(double onFlag)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(1046.5); // C6
    v.setDamperHeld(true);
    if (onFlag <= 0.5)
        v.setDuplexDriveGain(0.0); // the reference render
    v.noteOn(0.9);
    const int n = (int)(0.8 * kSampleRate);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M9.4 acceptance (README ## 9): una corda strikes a subset of the unison strings,
// so the note is measurably softer than a normal strike. C4 trichord (3->2 struck);
// onFlag > 0.5 engages the soft pedal. Held so the tails are comparable.
static std::vector<double> renderPianoUnaCorda(double onFlag)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(261.63); // C4, trichord
    v.setDamperHeld(true);
    v.setUnaCorda(onFlag > 0.5);
    v.noteOn(0.9);
    const int n = (int)(0.5 * kSampleRate);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M9.3 acceptance (README ## 6): the Stulov felt relaxation is a real, dissipative stage
// in the rendered path. C3 (130.8 Hz, below C4 so relaxation is at full depth); arg =
// |velocity*100|, negated ⇒ relaxation OFF (depth 0). The checker differences the two to
// isolate the felt term's contribution (## 12.2's lesson). Held so the tails are clean.
static std::vector<double> renderPianoFelt(double arg)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(130.8); // C3 — relaxation full here
    v.setDamperHeld(true);
    if (arg < 0.0)
        v.setHammerRelaxationDepth(0.0); // the reference render (branch model only)
    v.noteOn(std::fabs(arg) / 100.0);
    const int n = (int)(0.5 * kSampleRate);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

static std::vector<double> renderPianoSympathetic(double whichFlag)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoBridge bridge;
    PianoVoice struckVoice, samePitchSilent, offPitchSilent;
    struckVoice.setFrequency(220.0);
    samePitchSilent.setFrequency(220.0);
    offPitchSilent.setFrequency(233.08);
    struckVoice.setBridge(&bridge);
    samePitchSilent.setBridge(&bridge);
    offPitchSilent.setBridge(&bridge);

    samePitchSilent.noteOn(0.0); // silently depressed: damper lifted, no strike
    offPitchSilent.noteOn(0.0);
    struckVoice.noteOn(0.9);

    const int n = kSampleRate; // 1 s
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i)
    {
        struckVoice.out(0.0, 0);
        Sample same = samePitchSilent.out(0.0, 0);
        Sample off = offPitchSilent.out(0.0, 0);
        bridge.tick();
        out[i] = (whichFlag > 0.5) ? off : same;
    }
    return out;
}

// Regression: reuse (voice-steal) a PianoVoice whose damper is fully engaged,
// via the exact sequence PianoEngine::noteOnMidi uses on a stolen voice
// (setFrequency() THEN noteOn()) — see the comment on
// StringPartialBank::reset() / the synthTests.cpp regression test for the
// full mechanism this guards against (a stale short-decay/high-gain
// coefficient set producing a large amplitude spike on the reused note).
static std::vector<double> renderPianoReuse()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(261.63); // C4
    v.noteOn(0.8);
    const int pre = kSampleRate / 10; // 100 ms struck
    for (int i = 0; i < pre; ++i) v.out(0.0, 0);
    v.noteOff();                                    // engage damper (no sustain)
    const int settle = kSampleRate / 10;             // let it fully engage
    for (int i = 0; i < settle; ++i) v.out(0.0, 0);

    v.setFrequency(440.0); // A4 — voice-stealing order: setFrequency() then noteOn()
    v.noteOn(0.8);
    const int n = kSampleRate / 2; // 500 ms of the reused note
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M1 acceptance criterion 2 (plan §M1): spectral evolution. A struck C4 held with
// the damper lifted for 1.5 s, all defaults — so the pitch-derived T60 and the
// c1+c3*w^2 loss law are the ones under test. Python measures the high-band /
// low-band energy ratio at the attack vs at t=1 s and asserts it collapses.
static std::vector<double> renderPianoSpectral()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(261.63); // C4 — defaults supply B and T60 from pitch
    v.noteOn(0.9);          // never released: damper stays lifted
    const int n = (int)(1.5 * kSampleRate);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M6 acceptance (plan §M6): per-register voicing. One voice, one velocity, ALL
// defaults — the only thing that differs between two renders at two pitches is
// README ## 11's register scaling. Python compares the timbre of the renders.
static std::vector<double> renderPianoRegister(double hz)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(hz);
    v.noteOn(1.0);
    const int n = (int)(0.5 * kSampleRate);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M7 acceptance (plan §M7): phantom partials. A0 struck at <velocity*100>, with
// the tension coupling either at its default or forced to zero, so Python can
// subtract the two renders and isolate README ## 12's contribution exactly.
// Encoding: arg = velocity*100, negated to mean "coupling off".
static std::vector<double> renderPianoPhantom(double arg)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(27.5); // A0 — where longitudinal colour lives
    if (arg < 0.0)
        v.setTensionCoupling(0.0); // the reference render
    v.noteOn(std::fabs(arg) / 100.0);
    const int n = (int)(0.6 * kSampleRate);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M8 acceptance (plan §M8): tension modulation / attack pitch glide. A hard blow
// on a bass note (C2) must start sharp and settle down; a soft blow must not. Held
// with the damper lifted so the tail is undisturbed by damping. Everything here is
// deterministic (fixed noise seed), so a glide-off reference differs from glide-on
// ONLY in partial tuning — the checker differences the two to cancel the broadband
// onset transient exactly (README ## 12.2's "difference two renders" lesson) and
// isolate the glide. Encoding: arg = |velocity*100|, negated ⇒ glide off (κ_t = 0).
static std::vector<double> renderPianoGlide(double arg)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(65.41); // C2 — a solid bass note where the glide lives (README ## 12.5)
    if (arg < 0.0)
        v.setTensionModulation(0.0); // the reference render
    v.setDamperHeld(true);
    v.noteOn(std::fabs(arg) / 100.0);
    const int n = (int)(1.3 * kSampleRate);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M4 acceptance (plan §M4): double decay. C4 held with the damper lifted for 5 s,
// ONE unison string so unison beating (README ## 5) does not modulate the envelope
// on a timescale comparable to the measurement — this targets the polarisation
// mechanism specifically.
static std::vector<double> renderPianoDoubleDecay()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoVoice v;
    v.setFrequency(261.63);
    v.setUnisonCount(1);
    v.setDamperHeld(true);
    v.noteOn(0.9);
    const int n = 5 * kSampleRate;
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = v.out(0.0, 0);
    return out;
}

// M5 acceptance (plan §M5): the soundboard must colour the treble. Impulses the
// shared bridge bus and renders its radiated response, so the measurement runs
// end-to-end through the real audio path (including 16-bit quantisation).
static std::vector<double> renderBridgeImpulse()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    PianoBridge bridge;
    const int n = kSampleRate / 2;
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i)
    {
        bridge.accumulate(i == 0 ? 1.0 : 0.0);
        bridge.tick();
        out[i] = bridge.radiatedOutput();
    }
    // Normalise so the impulse response uses the WAV's full range rather than
    // disappearing into quantisation noise.
    double peak = 0.0;
    for (double v : out) peak = std::max(peak, std::fabs(v));
    if (peak > 0.0)
        for (double &v : out) v *= 0.9 / peak;
    return out;
}

// ── D1 primitives (REQ-eq-*, REQ-svf-1, REQ-noise-1, REQ-decay-1) ─────────────────────────────

// A unit sine at `hz` for 1.5 s through a ParametricEQ with Peak2 = +6 dB @ 1 kHz (Q 1) and the
// high cut on at 12 kHz (Q 0.7071). Python compares the settled RMS gain with the RBJ closed form
// it computes itself.
static std::vector<double> renderPeq(double hz)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    ParametricEQ eq;
    eq.band(ParametricEQ::Peak2).setGainDb(6.0);
    eq.setBandEnabled(ParametricEQ::HighCut, true);
    eq.band(ParametricEQ::HighCut).setFrequency(12000.0);
    std::vector<double> y(72000);
    for (size_t n = 0; n < y.size(); ++n)
        y[n] = 0.5 * eq.out(std::sin(2 * M_PI * hz * n / kSampleRate), 0);
    return y;
}

// A unit sine at `hz` through a low-pass StateVariableFilter at 1 kHz, resonance 0.5. Python
// compares with the bilinear-transformed analog 2-pole low-pass it computes itself.
static std::vector<double> renderSvf(double hz)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(1);
    StateVariableFilter f(StateVariableFilter::LowPass, 1000.0, 0.5);
    std::vector<double> y(72000);
    for (size_t n = 0; n < y.size(); ++n)
        y[n] = 0.1 * f.out(std::sin(2 * M_PI * hz * n / kSampleRate), 0); // ×0.1: the 4.76× resonant peak must not clip the 16-bit WAV
    return y;
}

// Seeded noise under a DecayEnvelope (decay 200 ms): Python fits the slope of the windowed RMS.
static std::vector<double> renderNoiseDecay()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    Noise noise(42);
    DecayEnvelope env;
    env.setDecayMs(200.0);
    env.trigger(0.9);
    std::vector<double> y(kSampleRate / 2);
    for (auto &v : y) v = noise.next() * env.next();
    return y;
}

// ── D2 instruments (REQ-synth2-*, REQ-drum-*) ────────────────────────────────────────────────

// BasicSynth, osc 1 a sine (osc 2 silent, filter wide open), playing MIDI `note` for 1 s.
static std::vector<double> renderSynth2(double note)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    BasicSynth s;
    BasicSynth::Params p;
    p.osc1.wave = BasicSynth::Sine;
    p.osc2.level = 0.0;
    p.cutoff = 20000.0;
    p.envAmount = 0.0;
    p.keytrack = 0.0;
    p.resonance = 0.0;
    p.volumeDb = -6.0;
    s.setParams(p);
    s.noteOn((int)note, 127);
    std::vector<Sample> L(kSampleRate, 0.0), R(kSampleRate, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    s.render(o, 2, kSampleRate);
    return std::vector<double>(L.begin(), L.end());
}

// DrumMachine kick tuned `tune` semitones, 0.5 s.
static std::vector<double> renderKick(double tune)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    DrumMachine d;
    d.setVolumeDb(0.0);
    auto p = DrumMachine::defaults(DrumMachine::Kick);
    p.tune = tune;
    d.setPad(DrumMachine::Kick, p);
    d.noteOn(36, 127);
    std::vector<Sample> L(kSampleRate / 2, 0.0), R(kSampleRate / 2, 0.0);
    Sample *o[2] = {L.data(), R.data()};
    d.render(o, 2, (int)L.size());
    return std::vector<double>(L.begin(), L.end());
}

// ── D3 the registry (REQ-device-*) ──────────────────────────────────────────────────────────

// A unit sine at `hz` through the registry's "eq" with ONLY `peak2.gain = 6` written by name, ×0.5.
static std::vector<double> renderDeviceEq(double hz)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("eq");
    d->setParam("peak2.gain", 6.0);
    std::vector<Sample> L(72000), R(72000);
    for (size_t n = 0; n < L.size(); ++n) L[n] = R[n] = std::sin(2 * M_PI * hz * n / kSampleRate);
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, 128);
    }
    std::vector<double> y(L.size());
    for (size_t n = 0; n < L.size(); ++n) y[n] = 0.5 * L[n];
    return y;
}

// D4: a loud mix through the registry's limiter, `drive` dB into a −1 dBFS ceiling (REQ-fx-limiter-1)
static std::vector<double> renderLimiter(double drive)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("limiter");
    d->setParam("gain", drive);
    d->setParam("ceiling", -1.0);
    std::vector<Sample> L(96000), R(96000);
    for (size_t n = 0; n < L.size(); ++n)
    {
        const double t = (double)n / kSampleRate;
        L[n] = 0.45 * std::sin(2 * M_PI * 82.4 * t) + 0.25 * std::sin(2 * M_PI * 659 * t) * (std::fmod(t, 0.25) < 0.05 ? 1.0 : 0.3);
        R[n] = L[n];
    }
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, 128);
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-fx-limiter-2: peaks BETWEEN samples through the registry's limiter, 6 dB into a −1 dB ceiling, True Peak
// off (arg 0) or on (arg 1) — a 12 kHz sine 45° out (every sample 3 dB under its peak), then 11 + 7 kHz; faded in
// over 5 ms and cross-faded over 10, so every change is band-limited too
static std::vector<double> renderLimiterTruePeak(double on)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("limiter");
    d->setParam("gain", 6.0);
    d->setParam("ceiling", -1.0);
    d->setParam("truePeak", on);
    std::vector<Sample> L(9600), R(9600);
    for (size_t n = 0; n < L.size(); ++n)
    {
        const double t = (double)n / kSampleRate;
        const double fade = t < 0.005 ? 0.5 - 0.5 * std::cos(M_PI * t / 0.005) : 1.0;
        // the two halves cross-faded over 10 ms: a switch in one sample is no band-limited signal, and its ringing
        // between samples is whatever a meter's kernel length makes of it
        const double k = std::clamp((t - 0.095) / 0.01, 0.0, 1.0), x = 0.5 - 0.5 * std::cos(M_PI * k);
        L[n] = R[n] = fade * ((1.0 - x) * std::sin(2 * M_PI * 12000 * t + M_PI / 4) + x * (0.55 * std::sin(2 * M_PI * 11000 * t) + 0.45 * std::sin(2 * M_PI * 7000 * t + 1.0)));
    }
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - pos));
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-out-dither-1: a constant 1000 + arg steps of 16 bits, TPDF-dithered onto the 16-bit grid, 96000 frames.
// The WAV writer's ×32767 lands every grid value k < 16384 on k itself, so the file holds the dither's integers.
static std::vector<double> renderDither(double frac)
{
    Dither d(16);
    std::vector<double> y(96000);
    const float x = (float)((1000.0 + frac) / 32768.0);
    for (auto &v : y) v = d.quantize(x);
    return y;
}

// REQ-synth2-7, -8: the registry's synth, a sine, legato with 200 ms of glide — A3, then A4 at 0.5 s with A3 held;
// at 0.9 s the wheel bent fully up (2 semitones); 1.2 s
static std::vector<double> renderGlide()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("synth");
    d->setParam("osc1.wave", 0);
    d->setParam("osc2.level", 0);
    d->setParam("filter.cutoff", 20000);
    d->setParam("filter.env", 0);
    d->setParam("filter.keytrack", 0);
    d->setParam("amp.attack", 0);
    d->setParam("amp.sustain", 1);
    d->setParam("voice.mode", 2); // legato
    d->setParam("voice.glide", 200);
    std::vector<Sample> L((size_t)(1.2 * kSampleRate), 0.0), R(L.size(), 0.0);
    auto run = [&](size_t from, size_t to) {
        for (size_t pos = from; pos < to; pos += 128)
        {
            Sample *io[2] = {L.data() + pos, R.data() + pos};
            d->process(io, 2, (int)std::min<size_t>(128, to - pos));
        }
    };
    const size_t half = (size_t)(0.5 * kSampleRate), bend = (size_t)(0.9 * kSampleRate);
    d->noteOn(57, 100);
    run(0, half);
    d->noteOn(69, 100);
    run(half, bend);
    d->pitchBend(1.0);
    run(bend, L.size());
    return std::vector<double>(L.begin(), L.end());
}

// D4: a 55 Hz bass through the registry's compressor with Sidechain on, keyed (arg 1) by a kick every
// half second, or with no key (arg 0) — the pump (REQ-fx-sidechain-1)
static std::vector<double> renderSidechain(double keyed)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("compressor");
    d->setParam("threshold", -30.0);
    d->setParam("ratio", 4.0);
    d->setParam("attack", 2.0);
    d->setParam("release", 120.0);
    d->setParam("sidechain", 1.0);
    const size_t N = 96000;
    std::vector<Sample> L(N), R(N), kL(N), kR(N);
    for (size_t n = 0; n < N; ++n)
    {
        L[n] = R[n] = 0.3 * std::sin(2 * M_PI * 55.0 * n / kSampleRate);
        const double tk = std::fmod((double)n / kSampleRate, 0.5); // a kick: a 60 Hz thump decaying over ~60 ms
        kL[n] = kR[n] = std::exp(-tk / 0.06) * std::sin(2 * M_PI * 60.0 * tk);
    }
    for (size_t pos = 0; pos < N; pos += 128)
    {
        const Sample *k[2] = {kL.data() + pos, kR.data() + pos};
        d->setKey(keyed > 0.5 ? k : nullptr, 2);
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, 128);
    }
    return std::vector<double>(L.begin(), L.end());
}

// D5: a 220 Hz recording (half a second, mono, at 44.1 kHz) through the registry's sampler, the note
// `semitones` from its root — pitch by the key and by the file's rate (REQ-inst-sampler-1)
static std::vector<double> renderSampler(double semitones)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    std::vector<float> rec(22050);
    for (size_t i = 0; i < rec.size(); ++i) rec[i] = (float)(0.6 * std::sin(2 * M_PI * 220.0 * (double)i / 44100.0));
    auto d = DeviceRegistry::create("sampler");
    d->setParam("velocity", 0.0);
    d->setSample(rec.data(), (long long)rec.size(), 1, 44100.0);
    d->noteOn(60 + (int)std::lround(semitones), 100);
    std::vector<Sample> L(24000, 0.0), R(24000, 0.0);
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - pos)); // the last block is short
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-inst-sampler-2: a recording of eight tones (200 + 100·k Hz, 6000 frames each) through the registry's sampler in
// slices mode — the key root + `k` plays tone k, as recorded, to its slice's end
static std::vector<double> renderSamplerSlices(double kk)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    std::vector<float> rec(48000);
    for (size_t i = 0; i < rec.size(); ++i) rec[i] = (float)(0.6 * std::sin(2 * M_PI * (200.0 + 100.0 * (double)(i / 6000)) * (double)i / 48000.0));
    auto d = DeviceRegistry::create("sampler");
    d->setParam("velocity", 0.0);
    d->setParam("mode", 2.0); // slices
    d->setParam("slices", 8.0);
    d->setSample(rec.data(), (long long)rec.size(), 1, 48000.0);
    d->noteOn(60 + (int)std::lround(kk), 100);
    std::vector<Sample> L(8000, 0.0), R(8000, 0.0);
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - pos));
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-stretch-1/2: two seconds of a 440 Hz sine, `ratio` times as long (stretch) or `semitones` higher, as long (pitch) —
// run_integration.py measures the length and the frequency
static std::vector<double> renderStretched(double ratio, double semitones)
{
    std::vector<float> x((size_t)(2 * kSampleRate));
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(0.5 * std::sin(2 * M_PI * 440.0 * (double)i / kSampleRate));
    StretchParams p;
    p.ratio = ratio;
    p.semitones = semitones;
    const auto y = timeStretch({x}, kSampleRate, p)[0];
    return std::vector<double>(y.begin(), y.end());
}

// REQ-onset-1: a Drum Machine's kick, snare, hat and kick at known samples through the registry, its onsets found by
// `detectOnsets` and written as clicks (0.5 where one was found, else silence) for run_integration.py to place
static std::vector<double> renderOnsets()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("drums");
    const long long hits[4] = {12000, 36000, 60000, 84000};
    const int notes[4] = {36, 38, 42, 36};
    std::vector<Sample> L(96000, 0.0), R(96000, 0.0);
    int next = 0;
    for (long long pos = 0; pos < 96000;)
    {
        const long long until = next < 4 ? std::min<long long>(96000, hits[next]) : 96000;
        while (pos < until)
        {
            const int n = (int)std::min<long long>(128, until - pos);
            Sample *io[2] = {L.data() + pos, R.data() + pos};
            d->process(io, 2, n);
            pos += n;
        }
        if (next < 4 && pos == hits[next]) d->noteOn(notes[next++], 120);
    }
    std::vector<float> mono(L.size());
    for (size_t i = 0; i < L.size(); ++i) mono[i] = (float)(0.5 * (L[i] + R[i]));
    std::vector<double> out(L.size(), 0.0);
    for (long long at : detectOnsets(mono.data(), (long long)mono.size(), kSampleRate)) out[(size_t)at] = 0.5;
    return out;
}

// REQ-tempo-1: the Drum Machine through the registry playing `bpm` — kicks on 1 and 3, snares on 2 and 4, closed hats
// on every eighth — for 6.1 s (no whole beat); its tempo printed (bpm=…) for run_integration.py, the groove written
static std::vector<double> renderTempo(double bpm)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("drums");
    const long long n = (long long)(6.1 * kSampleRate);
    const double half = 30.0 / bpm * kSampleRate; // an eighth, samples
    std::vector<Sample> L((size_t)n, 0.0), R((size_t)n, 0.0);
    long long pos = 0;
    for (int k = 0; pos < n; ++k)
    {
        const long long until = std::min<long long>(n, (long long)std::llround((k + 1) * half));
        if (k % 2 == 0) d->noteOn((k / 2) % 2 == 0 ? 36 : 38, 120); // a beat: kick on 1 and 3, snare on 2 and 4
        d->noteOn(42, 90);
        while (pos < until)
        {
            const int m = (int)std::min<long long>(128, until - pos);
            Sample *io[2] = {L.data() + pos, R.data() + pos};
            d->process(io, 2, m);
            pos += m;
        }
    }
    std::vector<float> mono(L.size());
    for (size_t i = 0; i < L.size(); ++i) mono[i] = (float)(0.5 * (L[i] + R[i]));
    std::printf("bpm=%.6f\n", estimateTempo(mono.data(), (long long)mono.size(), kSampleRate));
    return std::vector<double>(mono.begin(), mono.end());
}

// REQ-meter-1…3: a stereo two-tone, loud then 26 dB down, with a burst whose peak lies between samples — the
// meters' readings printed for run_integration.py, which computes BS.1770-4 and the true peak itself
static double meterSignal(int ch, double t)
{
    const double PI2 = 2.0 * 3.14159265358979323846;
    double x = ch == 0 ? 0.3 * std::sin(PI2 * 1000.0 * t) + 0.2 * std::sin(PI2 * 5000.0 * t + 0.3)
                       : 0.3 * std::sin(PI2 * 1000.0 * t) + 0.25 * std::sin(PI2 * 100.0 * t + 1.1);
    if (t >= 2.0 && t < 2.1) x += 0.4 * std::sin(PI2 * 12000.0 * t + 0.7853981633974483); // fs/4, 45° out
    return t < 4.0 ? x : 0.05 * x;
}

// REQ-resample-2/3: a follower — a producer at 48 000 Hz, a consumer `ppm` fast, for two simulated minutes: 1 kHz at
// −6 dBFS through a buffer held at 1024 frames by a FillController steering an AdaptiveResampler; the consumer's last
// two seconds, its own frames
static std::vector<double> renderFollower(double ppm)
{
    const double fc = 48000.0, ff = 48000.0 * (1.0 + ppm * 1e-6);
    const int bc = 256, bf = 256;
    arstro::FillParams fp;
    fp.target = 1024.0;
    fp.rate = 48000.0;
    arstro::FillController ctl(fp);
    arstro::AdaptiveResampler rs(1, bf, 1.01);
    std::vector<float> ring(1 << 16), in, out((size_t)bf);
    long long pushed = 0, popped = 0;
    auto src = [&](long long m) { return (float)(0.5 * std::sin(2.0 * M_PI * 1000.0 * (double)m / fc)); };
    for (; pushed < 1024; ++pushed) ring[(size_t)(pushed & 0xFFFF)] = src(pushed);
    ctl.reset(1024.0 + rs.buffered());
    double tPush = 0.0, tPull = 0.0, lastPush = 0.0;
    const double seconds = 120.0;
    std::vector<double> y;
    while (tPull < seconds)
    {
        if (tPush <= tPull)
        {
            for (int i = 0; i < bc; ++i, ++pushed) ring[(size_t)(pushed & 0xFFFF)] = src(pushed);
            lastPush = tPush;
            tPush += bc / fc;
            continue;
        }
        const double since = std::min((double)bc, (tPull - lastPush) * 48000.0);
        const double step = 1.0 + ctl.update((double)(pushed - popped) + since - bc + rs.buffered(), bf);
        const int need = rs.inputNeeded(bf, step);
        in.assign((size_t)need, 0.0f);
        for (int i = 0; i < need && popped < pushed; ++i, ++popped) in[(size_t)i] = ring[(size_t)(popped & 0xFFFF)];
        rs.process(in.data(), need, out.data(), bf, step);
        if (tPull >= seconds - 2.0)
            for (int j = 0; j < bf; ++j) y.push_back(out[(size_t)j]);
        tPull += bf / ff;
    }
    return y;
}

// REQ-wavetable-3: the registry's "wavetable" — A4 for a second; arg 0: its defaults (Basic toward a saw, three
// unison voices), arg 1: one voice on Basic at position 0 (a sine), the filter wide open
static std::vector<double> renderWavetable(double sine)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("wavetable");
    if (sine > 0.5)
    {
        d->setParam("osc1.position", 0.0);
        d->setParam("osc1.voices", 1);
        d->setParam("osc1.width", 0.0);
        d->setParam("filter.cutoff", 20000.0);
        d->setParam("filter.env", 0.0);
        d->setParam("filter.keytrack", 0.0);
    }
    d->noteOn(69, 100);
    std::vector<double> y;
    std::vector<Sample> L(128), R(128);
    while ((int)y.size() < kSampleRate)
    {
        std::fill(L.begin(), L.end(), 0.0);
        std::fill(R.begin(), R.end(), 0.0);
        Sample *io[2] = {L.data(), R.data()};
        d->process(io, 2, 128);
        y.insert(y.end(), L.begin(), L.end());
    }
    return y;
}

// REQ-fm-3: the registry's "fm" — A4 for a second; arg 0 its defaults (algorithm 1, a bell-ish stack), arg 1 a carrier
// at A4 under one modulator at A4 of index 1 rad (algorithm 5, operators 3 and 4 silent, every envelope held)
static std::vector<double> renderFm(double pair)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("fm");
    if (pair > 0.5)
    {
        d->setParam("algorithm", 4); // the 5th: 4→3, 2→1
        for (const char *o : {"op1", "op2", "op3", "op4"})
        {
            const std::string k = o;
            d->setParam(k + ".attack", 0.0);
            d->setParam(k + ".decay", 0.0);
            d->setParam(k + ".sustain", 1.0);
            d->setParam(k + ".velocity", 0.0);
            d->setParam(k + ".ratio", 1.0);
        }
        d->setParam("op1.level", 1.0);
        d->setParam("op2.level", 0.125);
        d->setParam("op3.level", 0.0);
        d->setParam("op4.level", 0.0);
    }
    d->noteOn(69, 100);
    std::vector<double> y;
    std::vector<Sample> L(128), R(128);
    while ((int)y.size() < kSampleRate)
    {
        std::fill(L.begin(), L.end(), 0.0);
        std::fill(R.begin(), R.end(), 0.0);
        Sample *io[2] = {L.data(), R.data()};
        d->process(io, 2, 128);
        y.insert(y.end(), L.begin(), L.end());
    }
    return y;
}

// REQ-multisample-3: the registry's "multisample" given an SFZ of two velocity layers — soft (220 Hz) and loud (330 Hz)
// — on A3: velocity 40 for the first half second, 110 for the second
static std::vector<double> renderMultisample(double)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("multisample");
    std::vector<std::string> problems;
    const auto names = d->setDefinition("<group> key=57 <region> sample=soft.wav hivel=63 <region> sample=loud.wav lovel=64", problems);
    for (const auto &n : names)
    {
        const double hz = n == "soft.wav" ? 220.0 : 330.0;
        std::vector<float> x((size_t)kSampleRate);
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(0.5 * std::sin(2 * M_PI * hz * (double)i / kSampleRate));
        d->setNamedSample(n, x.data(), (long long)x.size(), 1, kSampleRate);
    }
    std::vector<double> y;
    std::vector<Sample> L(128), R(128);
    for (int half = 0; half < 2; ++half)
    {
        d->allNotesOff();
        d->noteOn(57, half == 0 ? 40 : 110);
        for (int b = 0; b < kSampleRate / 2 / 128; ++b)
        {
            std::fill(L.begin(), L.end(), 0.0);
            std::fill(R.begin(), R.end(), 0.0);
            Sample *io[2] = {L.data(), R.data()};
            d->process(io, 2, 128);
            y.insert(y.end(), L.begin(), L.end());
        }
    }
    return y;
}

// REQ-granular-2: the registry's "granular" given a second of 440 Hz, rooted at A4, no spray — A4 for a second, then a
// fifth above it (E5) for a second
static std::vector<double> renderGranular(double)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("granular");
    std::vector<float> x((size_t)kSampleRate);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(0.5 * std::sin(2 * M_PI * 440.0 * (double)i / kSampleRate));
    d->setSample(x.data(), (long long)x.size(), 1, kSampleRate);
    d->setParam("root", 69);
    d->setParam("grain.spray", 0.0);
    d->setParam("grain.pspray", 0.0);
    d->setParam("grain.size", 200.0);
    d->setParam("grain.density", 10.0);
    std::vector<double> y;
    std::vector<Sample> L(128), R(128);
    for (int note : {69, 76})
    {
        d->allNotesOff();
        d->reset();
        d->noteOn(note, 127);
        for (int b = 0; b < kSampleRate / 128; ++b)
        {
            std::fill(L.begin(), L.end(), 0.0);
            std::fill(R.begin(), R.end(), 0.0);
            Sample *io[2] = {L.data(), R.data()};
            d->process(io, 2, 128);
            y.insert(y.end(), L.begin(), L.end());
        }
    }
    return y;
}

// REQ-fx-multiband-1: 60 Hz and 5 kHz, 0.25 each, 1.5 s through the registry's "multiband" — arg 0: every band at 1:1
// (the bands sum flat); arg 1: the low band at −30 dB, 4:1, the rest at 1:1
static std::vector<double> renderMultiband(double squeeze)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("multiband");
    for (const char *band : {"low", "mid", "high"}) d->setParam(std::string(band) + ".ratio", 1.0);
    if (squeeze > 0)
    {
        d->setParam("low.threshold", -30.0);
        d->setParam("low.ratio", 4.0);
    }
    std::vector<Sample> L(72000), R(72000);
    for (size_t n = 0; n < L.size(); ++n)
    {
        const double t = (double)n / kSampleRate;
        L[n] = R[n] = 0.25 * std::sin(2 * M_PI * 60 * t) + 0.25 * std::sin(2 * M_PI * 5000 * t);
    }
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - pos));
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-fx-gate-1: a 1 kHz square (its |x| steady, so its level is exact) through the registry's "gate" — ±0.1 (−20 dBFS)
// for half a second, then ±10^(−50/20) for two; arg: the ratio (100: the gate, 2: a 2:1 expander)
static std::vector<double> renderGate(double ratio)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("gate");
    d->setParam("ratio", ratio);
    std::vector<Sample> L(120000), R(120000);
    for (size_t n = 0; n < L.size(); ++n)
    {
        const double a = n < 24000 ? 0.1 : std::pow(10.0, -50.0 / 20.0);
        L[n] = R[n] = n % 48 < 24 ? a : -a;
    }
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - pos));
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-fx-transient-1: a 1 kHz square (|x| steady) through the registry's "transient" — arg 0: attack +100 %, the square
// ±0.02 from 0.1 s out of silence; arg 1: sustain +100 %, ±0.25 then ±0.025 from 0.5 s
static std::vector<double> renderTransient(double which)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("transient");
    d->setParam(which > 0 ? "sustain" : "attack", 100.0);
    std::vector<Sample> L(72000), R(72000);
    for (size_t n = 0; n < L.size(); ++n)
    {
        const double a = which > 0 ? (n < 24000 ? 0.25 : 0.025) : (n < 4800 ? 0.0 : 0.02);
        L[n] = R[n] = n % 48 < 24 ? a : -a;
    }
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - pos));
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-fx-deesser-1: a voice (1 kHz) and its sibilance (10 kHz), 0.25 each, 1 s through the registry's "deesser" at
// 6 kHz, −24 dB, a 6 dB range (the sibilance well over it: the cut held at the range) — arg 0: split; arg 1: wide
static std::vector<double> renderDeEsser(double wide)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("deesser");
    d->setParam("frequency", 6000.0);
    d->setParam("threshold", -24.0);
    d->setParam("range", 6.0);
    d->setParam("mode", wide);
    std::vector<Sample> L(48000), R(48000);
    for (size_t n = 0; n < L.size(); ++n)
    {
        const double t = (double)n / kSampleRate;
        L[n] = R[n] = 0.25 * std::sin(2 * M_PI * 1000 * t) + 0.25 * std::sin(2 * M_PI * 10000 * t);
    }
    for (size_t pos = 0; pos < L.size(); pos += 128)
    {
        Sample *io[2] = {L.data() + pos, R.data() + pos};
        d->process(io, 2, (int)std::min<size_t>(128, L.size() - pos));
    }
    return std::vector<double>(L.begin(), L.end());
}

// REQ-808-2: the registry's "808" on A1 (55 Hz) for a second, its defaults but no click, no drive and a long decay (8 s:
// steady enough in a window to read its pitch)
static std::vector<double> render808(double)
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    AudioConfig::instance().setChannelCount(2);
    auto d = DeviceRegistry::create("808");
    d->setParam("click", 0.0);
    d->setParam("drive", 0.0);
    d->setParam("amp.decay", 8000.0);
    d->noteOn(33, 127);
    std::vector<double> y;
    std::vector<Sample> L(128), R(128);
    while ((int)y.size() < kSampleRate)
    {
        std::fill(L.begin(), L.end(), 0.0);
        std::fill(R.begin(), R.end(), 0.0);
        Sample *io[2] = {L.data(), R.data()};
        d->process(io, 2, 128);
        y.insert(y.end(), L.begin(), L.end());
    }
    return y;
}

static std::vector<double> renderMeters()
{
    AudioConfig::instance().setSampleRate(kSampleRate);
    LoudnessMeter lm;
    TruePeakMeter tp;
    CorrelationMeter cm;
    const int n = 8 * kSampleRate, block = 128;
    std::vector<float> l(block), r(block);
    std::vector<double> out;
    out.reserve((size_t)n);
    for (int i = 0; i < n; i += block)
    {
        const int k = std::min(block, n - i);
        for (int j = 0; j < k; ++j)
        {
            const double t = (double)(i + j) / kSampleRate;
            l[(size_t)j] = (float)meterSignal(0, t);
            r[(size_t)j] = (float)meterSignal(1, t);
            out.push_back(l[(size_t)j]);
        }
        lm.process(l.data(), r.data(), k);
        tp.process(l.data(), r.data(), k);
        cm.process(l.data(), r.data(), k);
    }
    std::printf("integrated=%.6f shortterm=%.6f truepeak=%.6f correlation=%.6f\n", lm.integrated(), lm.shortTerm(), tp.peakDb(), cm.correlation());
    return out;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <scenario> <out.wav> [arg]\n", argv[0]);
        return 1;
    }
    std::string scenario = argv[1];
    std::string outfile = argv[2];
    double arg = (argc > 3) ? std::atof(argv[3]) : 0.0;

    if (scenario == "reverb")     // stereo output
    {
        std::vector<double> L, R;
        renderReverbStereo(L, R);
        writeWavStereo16(outfile, L, R, kSampleRate);
        return 0;
    }

    std::vector<double> samples;
    if (scenario == "gain")       samples = renderGain();
    else if (scenario == "osc")   samples = renderOsc(arg);
    else if (scenario == "oscwave") samples = renderOscWave(arg);
    else if (scenario == "lpf")   samples = renderLpf(arg);
    else if (scenario == "adsr")  samples = renderAdsr();
    else if (scenario == "synth") samples = renderSynth();
    else if (scenario == "synthparallel") samples = renderSynthParallel(arg);
    else if (scenario == "piano") samples = renderPiano(arg);
    else if (scenario == "pianodamper") samples = renderPianoDamper(arg);
    else if (scenario == "pianonodamper") samples = renderPianoNoDamper(arg);
    else if (scenario == "pianoduplex") samples = renderPianoDuplex(arg);
    else if (scenario == "pianounacorda") samples = renderPianoUnaCorda(arg);
    else if (scenario == "pianofelt") samples = renderPianoFelt(arg);
    else if (scenario == "pianoreuse") samples = renderPianoReuse();
    else if (scenario == "pianospectral") samples = renderPianoSpectral();
    else if (scenario == "pianodoubledecay") samples = renderPianoDoubleDecay();
    else if (scenario == "pianoglide") samples = renderPianoGlide(arg);
    else if (scenario == "bridgeimpulse") samples = renderBridgeImpulse();
    else if (scenario == "pianosympathetic") samples = renderPianoSympathetic(arg);
    else if (scenario == "pianoregister") samples = renderPianoRegister(arg);
    else if (scenario == "pianophantom") samples = renderPianoPhantom(arg);
    else if (scenario == "peq") samples = renderPeq(arg);
    else if (scenario == "svf") samples = renderSvf(arg);
    else if (scenario == "noisedecay") samples = renderNoiseDecay();
    else if (scenario == "synth2") samples = renderSynth2(arg);
    else if (scenario == "kick") samples = renderKick(arg);
    else if (scenario == "deviceeq") samples = renderDeviceEq(arg);
    else if (scenario == "limiter") samples = renderLimiter(arg);
    else if (scenario == "limiter_tp") samples = renderLimiterTruePeak(arg);
    else if (scenario == "dither") samples = renderDither(arg);
    else if (scenario == "glide") samples = renderGlide();
    else if (scenario == "sidechain") samples = renderSidechain(arg);
    else if (scenario == "sampler") samples = renderSampler(arg);
    else if (scenario == "sampler_slices") samples = renderSamplerSlices(arg);
    else if (scenario == "onsets") samples = renderOnsets();
    else if (scenario == "stretch") samples = renderStretched(arg, 0.0);
    else if (scenario == "tempo") samples = renderTempo(arg);
    else if (scenario == "pitch") samples = renderStretched(1.0, arg);
    else if (scenario == "meters") samples = renderMeters();
    else if (scenario == "follower") samples = renderFollower(arg);
    else if (scenario == "wavetable") samples = renderWavetable(arg);
    else if (scenario == "fm") samples = renderFm(arg);
    else if (scenario == "multisample") samples = renderMultisample(arg);
    else if (scenario == "granular") samples = renderGranular(arg);
    else if (scenario == "808") samples = render808(arg);
    else if (scenario == "multiband") samples = renderMultiband(arg);
    else if (scenario == "gate") samples = renderGate(arg);
    else if (scenario == "transient") samples = renderTransient(arg);
    else if (scenario == "deesser") samples = renderDeEsser(arg);
    else { std::fprintf(stderr, "unknown scenario: %s\n", scenario.c_str()); return 1; }

    writeWavMono16(outfile, samples, kSampleRate);
    return 0;
}
