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

    // One sample of the whole engine: every voice, then the shared bridge. Kept in
    // one place so the byte and float paths cannot drift apart.
    void PianoEngine::renderFrame(Sample &outL, Sample &outR)
    {
        Sample sumL = 0.0, sumR = 0.0;
        for (auto &v : mVoices)
        {
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
        outL = softLimit(sumL * kMasterGain);
        outR = softLimit(sumR * kMasterGain);
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
