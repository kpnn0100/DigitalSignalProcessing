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
    else if (scenario == "piano") samples = renderPiano(arg);
    else if (scenario == "pianodamper") samples = renderPianoDamper(arg);
    else if (scenario == "pianoreuse") samples = renderPianoReuse();
    else if (scenario == "pianospectral") samples = renderPianoSpectral();
    else if (scenario == "pianodoubledecay") samples = renderPianoDoubleDecay();
    else if (scenario == "bridgeimpulse") samples = renderBridgeImpulse();
    else if (scenario == "pianosympathetic") samples = renderPianoSympathetic(arg);
    else if (scenario == "pianoregister") samples = renderPianoRegister(arg);
    else { std::fprintf(stderr, "unknown scenario: %s\n", scenario.c_str()); return 1; }

    writeWavMono16(outfile, samples, kSampleRate);
    return 0;
}
