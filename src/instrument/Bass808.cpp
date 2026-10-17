#include "Bass808.h"
#include "../base/AudioConfig.h"
#include "../equalizer/Biquad.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        double fsNow() { return std::max(8000.0, (double)AudioConfig::instance().sampleRate()); }
    }

    Bass808::Bass808() { setParams(mP); }

    void Bass808::design()
    {
        // (B5) the tone: Biquad's own low-pass design, run here as a bare section
        const Biquad::Coeffs c = Biquad::design(Biquad::LowPass, std::clamp(mP.tone, 200.0, 20000.0), 0.7071, 0.0, fsNow());
        b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2;
    }

    void Bass808::setParams(const Params &p)
    {
        mP = p;
        mEnv.setAttackMs(mP.attack);
        mEnv.setDecayMs(mP.decay);
        design();
    }

    void Bass808::attack(int note, int velocity)
    {
        // a new hit: the punch from the top, the decay from its peak, the click from its start
        mActive = true;
        mT = 0;
        mVel = std::clamp(velocity, 1, 127) / 127.0;
        mFrom = mTo = note;
        mGlideLeft = mGlideLen = 0;
        mNoise.setSeed(0x808u);
        mPhase = Phasor(); // every hit from phase 0, as a struck resonator starts
        mEnv.setDecayMs(mP.decay);
        mEnv.trigger(1.0);
    }

    void Bass808::glideTo(int note)
    {
        // (B1) from where the pitch is now to the note, linear in semitones over the glide's time
        const double now = mGlideLen > 0 ? mTo - (mTo - mFrom) * (double)mGlideLeft / (double)mGlideLen : mTo;
        mFrom = now;
        mTo = note;
        mGlideLen = mGlideLeft = (long long)std::llround(mP.glide * fsNow() / 1000.0);
    }

    void Bass808::noteOn(int note, int velocity)
    {
        if (note < 0 || note > 127) return;
        const bool held = mStackSize > 0 && mActive;
        // (B6) the held keys: the newest on top
        int at = -1;
        for (int i = 0; i < mStackSize; ++i)
            if (mStack[i] == note) at = i;
        if (at >= 0) { std::copy(mStack + at + 1, mStack + mStackSize, mStack + at); --mStackSize; }
        if (mStackSize < 16) mStack[mStackSize++] = note;
        if (held) glideTo(note); // legato: no new attack
        else attack(note, velocity);
    }

    void Bass808::noteOff(int note)
    {
        int at = -1;
        for (int i = 0; i < mStackSize; ++i)
            if (mStack[i] == note) at = i;
        if (at < 0) return;
        const bool top = at == mStackSize - 1;
        std::copy(mStack + at + 1, mStack + mStackSize, mStack + at);
        --mStackSize;
        if (!top) return;
        if (mStackSize > 0) glideTo(mStack[mStackSize - 1]); // back to the key under it
        else if (mActive) mEnv.choke(std::max(1.0, mP.release)); // (B2) let go: choked over the release
    }

    void Bass808::allNotesOff()
    {
        mStackSize = 0;
        if (mActive) mEnv.choke(std::max(1.0, mP.release));
    }

    void Bass808::reset()
    {
        mActive = false;
        mStackSize = 0;
        mEnv = DecayEnvelope(); // idle, at zero
        mEnv.setAttackMs(mP.attack);
        mEnv.setDecayMs(mP.decay);
        mPhase = Phasor();
        z1 = z2 = 0.0;
        mBend = 0.0;
    }

    void Bass808::pitchBend(Sample amount) { mBend = std::clamp((double)amount, -1.0, 1.0); }

    void Bass808::render(Sample *const *out, int channels, int frames)
    {
        if (!mActive) return;
        const double fs = fsNow(), tau = std::max(1.0, mP.punchTime * fs / 1000.0 / 4.6);
        const double k = 0.01 + 20.0 * std::clamp(mP.drive, 0.0, 1.0), norm = 1.0 / std::tanh(k); // (B4) 0: linear to 0.003 %
        const double level = std::pow(10.0, mP.volumeDb / 20.0) * (1.0 - mP.velocity + mP.velocity * mVel);
        const double clickTau = 0.0015 * fs;
        for (int i = 0; i < frames; ++i)
        {
            // (B1) the glide's place, the punch on it, the bend
            const double g = mGlideLen > 0 ? mTo - (mTo - mFrom) * (double)mGlideLeft / (double)mGlideLen : mTo;
            if (mGlideLeft > 0) --mGlideLeft;
            mPitch = g + mP.punch * std::exp(-(double)mT / tau);
            const double f = 440.0 * std::exp2((mPitch + mBend * mP.bendRange - 69.0) / 12.0);
            const double x = std::sin(6.283185307179586 * mPhase.advance(f / fs));
            // (B2) the decay; (B3) the click
            const double e = mEnv.next();
            const double c = mP.click > 0.0 ? mP.click * mNoise.next() * std::exp(-(double)mT / clickTau) : 0.0;
            // (B4) the drive, normalised; (B5) the tone
            const double d = std::tanh(k * (x * e + c)) * norm;
            const double y = b0 * d + z1;
            z1 = b1 * d - a1 * y + z2;
            z2 = b2 * d - a2 * y;
            const Sample s = (Sample)(y * level);
            for (int ch = 0; ch < channels; ++ch) out[ch][i] += s;
            ++mT;
            if (mEnv.isFinished() && mStackSize == 0)
            {
                // over at this very sample, however the host chops time: nothing of it carries into the next hit
                mActive = false;
                z1 = z2 = 0.0;
                break;
            }
        }
    }
}
