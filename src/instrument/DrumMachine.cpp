#include "DrumMachine.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr Sample kTwoPi = 6.283185307179586;
        const int kNotes[DrumMachine::PadCount] = {36, 37, 38, 39, 41, 42, 45, 46, 48, 56};
        const char *kNames[DrumMachine::PadCount] = {"kick", "rim", "snare", "clap", "ltom",
                                                      "chat", "mtom", "ohat", "htom", "cowbell"};
        // (K0) default decay (ms to −60 dB) per pad.
        const Sample kDecay[DrumMachine::PadCount] = {450, 45, 220, 300, 500, 60, 420, 450, 360, 350};
        // (K9) voicing constants: each pad's output trim so the defaults sit at comparable loudness
        // (measured peaks at velocity 127, level 0 dB, volume 0 dB — README ## DrumMachine).
        const Sample kTrim[DrumMachine::PadCount] = {0.8, 0.9, 0.9, 3.4, 0.85, 2.0, 0.85, 1.75, 0.85, 1.3};
        // (K5) the TR-808's six hat/cymbal square-wave frequencies, Hz.
        const Sample kMetal[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
        const Sample kTomEnd[3] = {80.0, 120.0, 170.0}; // low, mid, high tom fundamentals, Hz
    }

    struct DrumMachine::Voice
    {
        Phasor ph[6];
        Noise noise;
        DecayEnvelope env, env2, env3;
        StateVariableFilter f1, f2;
        long t = 0;            // samples since the hit
        long burst[3] = {0, 0, 0};
        int nextBurst = 0;
        Sample vel = 0.0;
        bool active = false;
        Voice()
        {
            // A drum's filters are set at the hit and must take effect on its first sample, not
            // ramp in over a block.
            f1.setSmoothEnable(false);
            f2.setSmoothEnable(false);
        }
    };

    DrumMachine::DrumMachine()
    {
        for (int p = 0; p < PadCount; ++p)
        {
            mVoices[p] = std::make_unique<Voice>();
            mPads[p] = defaults((Pad)p);
        }
        reset();
    }

    DrumMachine::~DrumMachine() = default;

    int DrumMachine::noteFor(Pad p) { return (p >= 0 && p < PadCount) ? kNotes[p] : -1; }

    int DrumMachine::padFor(int note)
    {
        for (int p = 0; p < PadCount; ++p)
            if (kNotes[p] == note) return p;
        return -1;
    }

    const char *DrumMachine::padName(Pad p) { return (p >= 0 && p < PadCount) ? kNames[p] : ""; }

    DrumMachine::PadParams DrumMachine::defaults(Pad p)
    {
        PadParams d;
        d.decay = (p >= 0 && p < PadCount) ? kDecay[p] : 300.0;
        return d;
    }

    void DrumMachine::setPad(Pad p, const PadParams &params)
    {
        if (p < 0 || p >= PadCount) return;
        mPads[p] = params;
        mPads[p].tune = std::clamp(params.tune, (Sample)-24.0, (Sample)24.0);
        mPads[p].tone = std::clamp(params.tone, (Sample)0.0, (Sample)1.0);
        mPads[p].pan = std::clamp(params.pan, (Sample)-1.0, (Sample)1.0);
    }

    const DrumMachine::PadParams &DrumMachine::pad(Pad p) const { return mPads[std::clamp((int)p, 0, PadCount - 1)]; }

    void DrumMachine::reset()
    {
        for (int p = 0; p < PadCount; ++p)
        {
            Voice &v = *mVoices[p];
            v.active = false;
            v.noise.setSeed(0xD7500000u + (uint32_t)p); // (K11) reseeded: a reset replays the same noise
            for (auto &ph : v.ph) ph.reset();
            v.f1.reset();
            v.f2.reset();
            v.env = DecayEnvelope();
            v.env2 = DecayEnvelope();
            v.env3 = DecayEnvelope();
        }
    }

    int DrumMachine::activeVoices() const
    {
        int n = 0;
        for (const auto &v : mVoices) n += v->active ? 1 : 0;
        return n;
    }

    void DrumMachine::noteOff(int) {}

    void DrumMachine::noteOn(int note, int velocity)
    {
        const int pi = padFor(note);
        if (pi < 0 || velocity <= 0) return;
        const Pad p = (Pad)pi;
        Voice &v = *mVoices[pi];
        const PadParams &pp = mPads[pi];
        const Sample tr = std::pow(2.0, pp.tune / 12.0);
        const Sample vel = std::clamp(velocity, 1, 127) / 127.0;
        const Sample fs = AudioConfig::instance().sampleRate();
        v.vel = vel;
        v.t = 0;
        v.active = true;
        v.env.setAttackMs(0.0);
        v.env2.setAttackMs(0.0);
        v.env3.setAttackMs(0.0);
        for (auto &ph : v.ph) ph.reset();
        switch (p)
        {
        case Kick:
            v.env.setDecayMs(pp.decay);
            v.env.trigger(1.0);
            v.env2.setDecayMs(6.0);
            v.env2.trigger(1.0);
            v.f1.setMode(StateVariableFilter::HighPass);
            v.f1.setCutoff(3000.0);
            v.f1.setResonance(0.0);
            break;
        case LowTom:
        case MidTom:
        case HighTom:
            v.env.setDecayMs(pp.decay);
            v.env.trigger(1.0);
            v.env2.setDecayMs(25.0);
            v.env2.trigger(1.0);
            v.f1.setMode(StateVariableFilter::HighPass);
            v.f1.setCutoff(600.0);
            v.f1.setResonance(0.0);
            break;
        case Snare:
            v.env.setDecayMs(0.6 * pp.decay);
            v.env.trigger(1.0);
            v.env2.setDecayMs(pp.decay);
            v.env2.trigger(1.0);
            v.f1.setMode(StateVariableFilter::HighPass);
            v.f1.setCutoff(1200.0 * tr);
            v.f1.setResonance(0.0);
            v.f2.setMode(StateVariableFilter::LowPass);
            v.f2.setCutoff(9000.0);
            v.f2.setResonance(0.0);
            break;
        case Clap:
            // (K4) three 7 ms bursts 10 ms apart, then the tail at 30 ms.
            v.env3.setDecayMs(7.0);
            v.env3.trigger(1.0);
            v.burst[0] = (long)std::lround(0.010 * fs);
            v.burst[1] = (long)std::lround(0.020 * fs);
            v.burst[2] = (long)std::lround(0.030 * fs);
            v.nextBurst = 0;
            v.env = DecayEnvelope(); // the tail is silent until its hit
            v.env.setDecayMs(pp.decay);
            v.f1.setMode(StateVariableFilter::BandPass);
            v.f1.setCutoff(1100.0 * tr * std::pow(2.0, pp.tone - 0.5));
            v.f1.setResonance(0.35);
            break;
        case ClosedHat:
        case OpenHat:
            v.env.setDecayMs(pp.decay);
            v.env.trigger(1.0);
            v.f1.setMode(StateVariableFilter::BandPass);
            v.f1.setCutoff(10000.0);
            v.f1.setResonance(0.3);
            v.f2.setMode(StateVariableFilter::HighPass);
            v.f2.setCutoff(7000.0);
            v.f2.setResonance(0.0);
            if (p == ClosedHat) mVoices[OpenHat]->env.choke(8.0); // (K6) the choke
            break;
        case Rim:
            v.env.setDecayMs(pp.decay);
            v.env.trigger(1.0);
            v.env2.setDecayMs(0.5 * pp.decay);
            v.env2.trigger(1.0);
            v.f1.setMode(StateVariableFilter::BandPass);
            v.f1.setCutoff(2200.0 * tr);
            v.f1.setResonance(0.6);
            break;
        case Cowbell:
            v.env.setDecayMs(pp.decay / 6.0);
            v.env.trigger(0.6);
            v.env2.setDecayMs(pp.decay);
            v.env2.trigger(0.4);
            v.f1.setMode(StateVariableFilter::BandPass);
            v.f1.setCutoff(2640.0 * tr * std::pow(2.0, pp.tone - 0.5));
            v.f1.setResonance(0.3);
            break;
        default:
            break;
        }
    }

    Sample DrumMachine::tick(Voice &v, Pad p, Sample invFs, Sample tr)
    {
        const PadParams &pp = mPads[p];
        const Sample ts = v.t * invFs; // seconds since the hit
        Sample y = 0.0;
        switch (p)
        {
        case Kick:
        {
            // (K1) f(t) = f_end · 2^(S·e^(−t/τ)), S = 1.5 + 3·tone octaves, τ = 35 ms, f_end = 48 Hz·2^(tune/12)
            const Sample S = 1.5 + 3.0 * pp.tone;
            const Sample f = 48.0 * tr * std::pow(2.0, S * std::exp(-ts / 0.035));
            const Sample body = std::sin(kTwoPi * v.ph[0].advance(f * invFs));
            const Sample click = v.f1.out(v.noise.next(), 0) * v.env2.next() * (0.15 + 0.35 * pp.tone);
            y = body * v.env.next() + click;
            v.active = !v.env.isFinished();
            break;
        }
        case LowTom:
        case MidTom:
        case HighTom:
        {
            // (K2) the kick's model, gentler: S = 0.6 + 0.8·tone octaves, τ = 80 ms
            const int k = p == LowTom ? 0 : p == MidTom ? 1 : 2;
            const Sample S = 0.6 + 0.8 * pp.tone;
            const Sample f = kTomEnd[k] * tr * std::pow(2.0, S * std::exp(-ts / 0.080));
            const Sample body = std::sin(kTwoPi * v.ph[0].advance(f * invFs));
            const Sample stick = 0.08 * v.f1.out(v.noise.next(), 0) * v.env2.next();
            y = body * v.env.next() + stick;
            v.active = !v.env.isFinished();
            break;
        }
        case Snare:
        {
            // (K3) two tuned sines that drop 40% → 0 over 12 ms, plus HP/LP-shaped noise
            const Sample drop = 1.0 + 0.4 * std::exp(-ts / 0.012);
            const Sample body = 0.6 * std::sin(kTwoPi * v.ph[0].advance(185.0 * tr * drop * invFs)) +
                                0.4 * std::sin(kTwoPi * v.ph[1].advance(330.0 * tr * drop * invFs));
            const Sample n = v.f2.out(v.f1.out(v.noise.next(), 0), 0);
            y = (1.0 - pp.tone) * body * v.env.next() + (0.3 + 0.7 * pp.tone) * n * v.env2.next();
            v.active = !(v.env.isFinished() && v.env2.isFinished());
            break;
        }
        case Clap:
        {
            if (v.nextBurst < 3 && v.t == v.burst[v.nextBurst])
            {
                if (v.nextBurst < 2) v.env3.trigger(1.0);
                else v.env.trigger(0.9);
                ++v.nextBurst;
            }
            // (K4) band-passed noise; × 1/Q makes the band-pass 0 dB at its centre
            const Sample k = 1.0 / StateVariableFilter::qFor(0.35);
            y = k * v.f1.out(v.noise.next(), 0) * (v.env3.next() + v.env.next());
            v.active = v.nextBurst < 3 || !v.env.isFinished() || !v.env3.isFinished();
            break;
        }
        case ClosedHat:
        case OpenHat:
        {
            // (K5) six squares at the 808's ratios, mixed with noise by `tone`, band-pass 10 kHz, high-pass 7 kHz
            Sample metal = 0.0;
            for (int i = 0; i < 6; ++i) metal += v.ph[i].square(kMetal[i] * tr * invFs);
            metal /= 6.0;
            const Sample ns = 0.2 + 0.6 * pp.tone;
            const Sample src = (1.0 - ns) * metal + ns * v.noise.next();
            const Sample k = 1.0 / StateVariableFilter::qFor(0.3);
            y = v.f2.out(k * v.f1.out(src, 0), 0) * v.env.next();
            v.active = !v.env.isFinished();
            break;
        }
        case Rim:
        {
            // (K7) a ringing band-passed click plus a short 480 Hz knock
            const Sample k = 1.0 / StateVariableFilter::qFor(0.6);
            const Sample click = k * v.f1.out(v.noise.next(), 0) * v.env.next();
            const Sample knock = std::sin(kTwoPi * v.ph[0].advance(480.0 * tr * invFs)) * v.env2.next();
            y = (0.4 + 0.6 * pp.tone) * click + (1.0 - 0.5 * pp.tone) * knock;
            v.active = !(v.env.isFinished() && v.env2.isFinished());
            break;
        }
        case Cowbell:
        {
            // (K8) two squares at 540/800 Hz through a 2.64 kHz band-pass; a fast and a slow decay
            const Sample src = 0.5 * (v.ph[0].square(540.0 * tr * invFs) + v.ph[1].square(800.0 * tr * invFs));
            const Sample k = 1.0 / StateVariableFilter::qFor(0.3);
            y = k * v.f1.out(src, 0) * (v.env.next() + v.env2.next());
            v.active = !(v.env.isFinished() && v.env2.isFinished());
            break;
        }
        default:
            v.active = false;
            break;
        }
        ++v.t;
        return y;
    }

    void DrumMachine::render(Sample *const *out, int channels, int frames)
    {
        if (channels <= 0) return;
        const Sample invFs = 1.0 / AudioConfig::instance().sampleRate();
        const Sample master = std::pow(10.0, mVolumeDb / 20.0);
        for (int pi = 0; pi < PadCount; ++pi)
        {
            Voice &v = *mVoices[pi];
            if (!v.active) continue;
            const PadParams &pp = mPads[pi];
            const Sample g = master * kTrim[pi] * v.vel * std::pow(10.0, pp.levelDb / 20.0);
            // (K10) balance pan, unity at centre — the suite's mix law
            const Sample gl = g * (pp.pan > 0 ? std::cos(pp.pan * kTwoPi / 4.0) : 1.0);
            const Sample gr = g * (pp.pan < 0 ? std::cos(-pp.pan * kTwoPi / 4.0) : 1.0);
            const Sample tr = std::pow(2.0, pp.tune / 12.0); // (K0) the tune ratio
            for (int i = 0; i < frames && v.active; ++i)
            {
                const Sample s = tick(v, (Pad)pi, invFs, tr);
                if (channels == 1) out[0][i] += s * g;
                else { out[0][i] += s * gl; out[1][i] += s * gr; }
            }
        }
    }
}
