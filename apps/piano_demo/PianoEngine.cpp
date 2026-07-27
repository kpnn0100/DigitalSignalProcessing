#include "PianoEngine.h"
#include <cmath>
#include <algorithm>

namespace arstro
{
    static int16_t toPcm16(Sample s)
    {
        if (s > 1.0) s = 1.0;
        if (s < -1.0) s = -1.0;
        return (int16_t)std::lround(s * 32767.0);
    }

    // Soft limiter for the summed output. tanh saturates smoothly instead of
    // hard-clipping into a harsh digital click — but it is a SAFETY CATCH for rare
    // coincident transients, not a gain stage, and kMasterGain is what keeps it in
    // that role.
    //
    // Recalibrated 2026-07-26, because it had drifted badly out of that role. M6
    // and M7 changed per-voice levels and this gain was never restaged against
    // them; measured squash (how much tanh compresses the loudest peak) at the old
    // 0.6 was 35 % at two voices, 58 % at three and 91 % at eight — chords were
    // being driven close to a square wave. Audible as crunch, and a direct
    // contradiction of what this comment used to claim.
    //
    // Measured peaks, 1 s from a SIMULTANEOUS strike (worst case: every voice hit
    // on the same sample, so attacks add coherently):
    //   1 voice 0.85 | 2 2.25 | 3 3.87 | 5 8.89 | 8 19.25
    // Eight voices peak ~22x one voice rather than 8x — simultaneous attacks are
    // phase aligned, and the shared bridge (README ## 8) adds to it.
    //
    // At 0.22 the limiter is a safety catch again. Share of a 1 s render in which
    // tanh compresses at all, same worst case:
    //   1-3 voices  0.000 %  — never engages; fully linear
    //   5 voices    0.133 %  — clean after 1.8 ms
    //   8 voices    0.210 %  — clean after 49 ms
    // i.e. only the coincident attack of a large chord, which is what it is for.
    static Sample softLimit(Sample s) { return std::tanh(s); }

    static constexpr Sample kMasterGain = 0.22;

    Sample PianoEngine::midiToHz(int note)
    {
        return 440.0 * std::pow(2.0, (note - 69) / 12.0);
    }

    PianoEngine::PianoEngine()
    {
        mNote.fill(-1);
        mKeyDown.fill(false);
        mSostenutoLatched.fill(false);
        mAge.fill(0);
        for (auto &v : mVoices)
            v.setBridge(&mBridge);
        for (int p = 0; p < TuneCount; ++p)
            mTune[p] = tuneSpec(p).def; // neutral: reproduces the shipped model
        mMasterGain = mTune[TuneMasterGain];
        // README ## 8.2 (M10): the soundboard defaults to a series body mix (0.5), so the
        // played instrument radiates through the board rather than as raw strings. The
        // per-voice PianoVoice default (mBodyMix = 0) is untouched, so every standalone
        // PianoVoice test / render harness still measures the raw string physics.
        mBridge.setRadiationGain(mTune[TuneBodyResonance]);
        mBridge.setBodyDirectGain(mTune[TuneBody] * PianoVoice::velocityToSignal());
        for (auto &v : mVoices)
            v.setBodyMix(mTune[TuneBody]);
    }

    // ─────────────────── live voicing/tuning controls ───────────────────

    PianoEngine::TuneSpec PianoEngine::tuneSpec(int param)
    {
        // One row per Tune enumerator, in the same order. Ranges are chosen to be
        // audible but to stay clear of contact-loop instability (README ## 6/## 11.3).
        static const TuneSpec kSpecs[TuneCount] = {
            {"Hammer hardness", 0.5, 1.6, 1.0, "x"},     // TuneHardness
            {"Felt curve (p)", 1.5, 3.5, 2.5, ""},        // TuneFeltCurve
            {"Felt hysteresis", 0.0, 0.6, 0.2, ""},        // TuneFeltHysteresis
            {"Felt relaxation", 0.0, 2.0, 1.0, "x"},      // TuneFeltRelax
            {"Decay / sustain", 0.3, 2.5, 1.0, "x"},      // TuneDecay
            {"Brightness T60", 0.02, 0.30, 0.08, "s"},     // TuneBrightness
            {"Inharmonicity", 0.0, 3.0, 1.0, "x"},        // TuneInharmonicity
            {"Unison detune", 0.0, 3.0, 0.6, "cents"},     // TuneUnisonDetune
            {"Bass growl", 0.0, 0.03, 0.008, "K"},         // TuneBassGrowl
            {"Attack glide", 0.0, 4.0, 1.0, "x"},         // TuneAttackGlide
            {"Treble shimmer", 0.0, 300.0, 90.0, ""},      // TuneTrebleShimmer
            {"Body (soundboard)", 0.0, 1.0, 0.5, ""},      // TuneBody
            {"Body resonance", 0.0, 2.0, 0.5, ""},         // TuneBodyResonance
            {"Master gain", 0.05, 0.5, 0.22, ""},          // TuneMasterGain
        };
        if (param < 0 || param >= TuneCount) param = 0;
        return kSpecs[param];
    }

    double PianoEngine::tuningValue(int param) const
    {
        if (param < 0 || param >= TuneCount) return 0.0;
        return mTune[param];
    }

    Sample PianoEngine::defaultInharmonicity(Sample f0Hz)
    {
        // Mirrors PianoVoice::computeDefaultInharmonicity so the multiplier can scale
        // the pitch curve rather than flatten it to one B across the keyboard.
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample b = 0.00056 * std::pow(100.0 / f0Hz, 1.25);
        if (b < 0.00005) b = 0.00005;
        if (b > 0.02) b = 0.02;
        return b;
    }

    void PianoEngine::applyTuningToVoice(int i)
    {
        if (mNote[i] < 0) return; // never struck -> nothing to voice yet
        const Sample hz = midiToHz(mNote[i]);
        PianoVoice &v = mVoices[i];
        // Multiplier params scale the per-note register default so the keyboard's
        // natural scaling survives; the rest are absolute.
        v.setHammerStiffness(PianoVoice::defaultHammerStiffness(hz) * mTune[TuneHardness]);
        v.setHammerNonlinearExponent(mTune[TuneFeltCurve]);
        v.setHammerHysteresisLoss(mTune[TuneFeltHysteresis]);
        v.setHammerRelaxationDepth(PianoVoice::defaultFeltHysteresis(hz) * mTune[TuneFeltRelax]);
        v.setBaseDecaySeconds(PianoVoice::defaultBaseDecaySeconds(hz) * mTune[TuneDecay]);
        v.setBrightnessDecaySeconds(mTune[TuneBrightness]);
        v.setInharmonicity(defaultInharmonicity(hz) * mTune[TuneInharmonicity]);
        v.setUnisonDetuneCents(mTune[TuneUnisonDetune]);
        v.setTensionCoupling(mTune[TuneBassGrowl]);
        v.setTensionModulation(PianoVoice::defaultTensionModulation(hz) * mTune[TuneAttackGlide]);
        v.setDuplexDriveGain(mTune[TuneTrebleShimmer]);
        v.setBodyMix(mTune[TuneBody]); // README ## 8.2 (M10)
    }

    void PianoEngine::setTuning(int param, double value)
    {
        if (param < 0 || param >= TuneCount) return;
        const TuneSpec s = tuneSpec(param);
        if (value < s.min) value = s.min;
        if (value > s.max) value = s.max;
        mTune[param] = value;
        switch (param)
        {
        case TuneMasterGain:
            mMasterGain = value; // engine-level: takes effect on the next sample
            break;
        case TuneBodyResonance:
            mBridge.setRadiationGain(value); // soundboard modal-resonance level (README ## 8.2)
            break;
        case TuneBody:
            // Series routing: the bridge's radiativity gain matches the string level it
            // replaces, and every voice's direct string is scaled by (1 − body).
            mBridge.setBodyDirectGain(value * PianoVoice::velocityToSignal());
            for (auto &v : mVoices)
                v.setBodyMix(value);
            break;
        default:
            for (int i = 0; i < kVoiceCount; ++i)
                applyTuningToVoice(i); // re-voice all sounding notes live
            break;
        }
    }

    int PianoEngine::allocateVoice()
    {
        // Never-triggered voices first: mAge alone can't tell "idle since startup"
        // apart from "still decaying a note played a while ago" (both just accrue
        // elapsed time), so an explicit never-used check comes before age-stealing.
        for (int i = 0; i < kVoiceCount; ++i)
            if (mNote[i] == -1)
                return i;
        for (int i = 0; i < kVoiceCount; ++i)
            if (mVoices[i].isFinished())
                return i;
        int best = 0;
        for (int i = 1; i < kVoiceCount; ++i)
            if (mAge[i] > mAge[best])
                best = i;
        return best;
    }

    void PianoEngine::refreshDamperHeld(int i)
    {
        bool held = mKeyDown[i] || mSustainPedal || mSostenutoLatched[i];
        mVoices[i].setDamperHeld(held);
        if (!held)
            mVoices[i].noteOff(); // nothing holding it open -> engage the damper now
    }

    void PianoEngine::noteOnMidi(int midiNote, Sample velocity)
    {
        int i = allocateVoice();
        mNote[i] = midiNote;
        mKeyDown[i] = true;
        mSostenutoLatched[i] = false;
        mAge[i] = 0;
        mVoices[i].setUnaCorda(mUnaCorda);
        mVoices[i].setDamperHeld(mSustainPedal); // sustain already down when struck
        mVoices[i].setFrequency(midiToHz(midiNote)); // register defaults first...
        applyTuningToVoice(i);                       // ...then the live voicing on top
        mVoices[i].noteOn(velocity);
    }

    void PianoEngine::noteOff(int midiNote)
    {
        for (int i = 0; i < kVoiceCount; ++i)
        {
            if (mNote[i] == midiNote && mKeyDown[i])
            {
                mKeyDown[i] = false;
                refreshDamperHeld(i);
            }
        }
    }

    void PianoEngine::panic()
    {
        mSustainPedal = false;
        mSostenutoPedal = false;
        mSostenutoLatched.fill(false);
        for (int i = 0; i < kVoiceCount; ++i)
        {
            mKeyDown[i] = false;
            mVoices[i].setDamperHeld(false);
            mVoices[i].noteOff();
        }
    }

    void PianoEngine::setSustainPedal(bool on)
    {
        mSustainPedal = on;
        for (int i = 0; i < kVoiceCount; ++i)
            refreshDamperHeld(i);
    }

    void PianoEngine::setSostenutoPedal(bool on)
    {
        mSostenutoPedal = on;
        if (on)
        {
            for (int i = 0; i < kVoiceCount; ++i)
                if (mKeyDown[i])
                    mSostenutoLatched[i] = true;
        }
        else
        {
            mSostenutoLatched.fill(false);
            for (int i = 0; i < kVoiceCount; ++i)
                refreshDamperHeld(i);
        }
    }

    void PianoEngine::setUnaCorda(bool on)
    {
        mUnaCorda = on;
        for (auto &v : mVoices)
            v.setUnaCorda(on);
    }

    // One sample of the whole engine: every voice, then the shared bridge. Kept in
    // one place so the byte and float paths cannot drift apart.
    void PianoEngine::renderFrame(Sample &outL, Sample &outR)
    {
        Sample sumL = 0.0, sumR = 0.0;
        for (auto &v : mVoices)
        {
            // Skip voices that are fully damped AND inaudible (README ## 13). Every
            // voice in the pool used to be rendered whether it sounded or not, so
            // one note cost almost as much as eight — the single largest waste in
            // the engine during normal playing, where most of the pool is idle.
            // Safe because a damped string is already cut off from bridge feedback,
            // so it cannot be re-excited (REQ-piano-6 is unaffected).
            if (v.isSilent())
                continue;
            sumL += v.out(0.0, 0); // channel 0 first: drives the physics + bridge push
            sumR += v.out(0.0, 1);
        }
        mBridge.tick();
        const Sample rad = mBridge.radiatedOutput();
        sumL += rad;
        sumR += rad;

        // Demo-level mixing headroom + soft limiter (not physical-modeling
        // constants — see src/physical/README.md ## Units for PianoVoice's own
        // per-note calibration, which already stays near +-1 on its own).
        outL = softLimit(sumL * mMasterGain);
        outR = softLimit(sumR * mMasterGain);
    }

    void PianoEngine::renderBlockBytes(std::vector<uint8_t> &out, int frames)
    {
        out.clear();
        out.reserve((size_t)frames * 4); // stereo, 16-bit
        for (int n = 0; n < frames; ++n)
        {
            Sample l, r;
            renderFrame(l, r);
            const int16_t li = toPcm16(l), ri = toPcm16(r);
            out.push_back((uint8_t)(li & 0xFF));
            out.push_back((uint8_t)((li >> 8) & 0xFF));
            out.push_back((uint8_t)(ri & 0xFF));
            out.push_back((uint8_t)((ri >> 8) & 0xFF));
        }
        for (int i = 0; i < kVoiceCount; ++i)
            mAge[i] += frames;
    }

    void PianoEngine::renderBlockFloat(float *interleaved, int frames)
    {
        for (int n = 0; n < frames; ++n)
        {
            Sample l, r;
            renderFrame(l, r);
            interleaved[n * 2] = (float)l;
            interleaved[n * 2 + 1] = (float)r;
        }
        for (int i = 0; i < kVoiceCount; ++i)
            mAge[i] += frames;
    }
}
