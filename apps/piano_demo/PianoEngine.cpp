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

    // Soft limiter for the summed output: with up to 8 independently-resonant
    // voices sharing one PianoBridge, occasional coincidental in-phase alignment
    // across many partials/voices (a normal multi-voice crest-factor effect, not
    // a bug — measured up to ~20x a single note's peak at rare transients) can
    // momentarily exceed the +-1 range that a single struck note stays within
    // (see src/physical/README.md ## Units). tanh saturates smoothly (near-linear,
    // transparent for normal single/few-note playing; only compresses the rare
    // large transient) instead of hard-clipping into a harsh digital click.
    static Sample softLimit(Sample s) { return std::tanh(s); }

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
        mVoices[i].setFrequency(midiToHz(midiNote));
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

    void PianoEngine::renderBlockBytes(std::vector<uint8_t> &out, int frames)
    {
        out.clear();
        out.reserve((size_t)frames * 4); // stereo, 16-bit
        for (int n = 0; n < frames; ++n)
        {
            Sample sumL = 0.0, sumR = 0.0;
            for (auto &v : mVoices)
            {
                sumL += v.out(0.0, 0); // channel 0 first: drives the physics + bridge push
                sumR += v.out(0.0, 1);
            }
            mBridge.tick();
            Sample rad = mBridge.radiatedOutput();
            sumL += rad;
            sumR += rad;

            // Demo-level mixing headroom + soft limiter (not physical-modeling
            // constants — see src/physical/README.md ## Units for PianoVoice's own
            // per-note calibration, which already stays near +-1 on its own).
            const Sample kMasterGain = 0.6;
            int16_t l = toPcm16(softLimit(sumL * kMasterGain));
            int16_t r = toPcm16(softLimit(sumR * kMasterGain));
            out.push_back((uint8_t)(l & 0xFF));
            out.push_back((uint8_t)((l >> 8) & 0xFF));
            out.push_back((uint8_t)(r & 0xFF));
            out.push_back((uint8_t)((r >> 8) & 0xFF));
        }
        for (int i = 0; i < kVoiceCount; ++i)
            mAge[i] += frames;
    }
}
