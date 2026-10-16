#include "Sampler.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Sampler::Sampler()
    {
        for (int i = 0; i < kVoices; ++i) mVoices.push_back(std::make_unique<Voice>());
        mRamp.assign(4096, 0.0); // a host's blocks up to 4096 frames never allocate in render()
        setParams(mP);
    }

    void Sampler::applyEnvelope(Voice &v) const
    {
        v.env.setAttackMs(mP.attackMs);
        v.env.setDecayMs(mP.decayMs);
        v.env.setSustain(mP.sustain);
        v.env.setReleaseMs(mP.releaseMs);
    }

    void Sampler::setParams(const Params &p)
    {
        mP = p;
        mP.start = std::clamp(mP.start, 0.0, 1.0);
        mP.end = std::clamp(mP.end, 0.0, 1.0);
        for (auto &v : mVoices) applyEnvelope(*v);
    }

    void Sampler::setSample(const float *x, long long frames, int channels, double rate)
    {
        reset();
        mFrames = x && channels > 0 && frames > 0 ? frames : 0;
        mRate = rate > 0.0 ? rate : 48000.0;
        mL.assign((size_t)mFrames, 0.0f);
        mR.assign((size_t)mFrames, 0.0f);
        for (long long i = 0; i < mFrames; ++i)
        {
            mL[(size_t)i] = x[(size_t)(i * channels)];
            mR[(size_t)i] = x[(size_t)(i * channels + (channels > 1 ? 1 : 0))]; // mono: both sides
        }
    }

    void Sampler::spanOf(long long &a, long long &b) const
    {
        // (S2) the span [a, b) of the recorded frames
        double s = mP.start, e = mP.end;
        if (e < s) std::swap(s, e);
        a = (long long)std::floor(s * (double)mFrames);
        b = (long long)std::ceil(e * (double)mFrames);
        a = std::clamp<long long>(a, 0, mFrames);
        b = std::clamp<long long>(b, a, mFrames);
    }

    void Sampler::noteOn(int note, int velocity)
    {
        if (mFrames <= 0 || velocity <= 0) { if (velocity <= 0) noteOff(note); return; }
        Voice *v = nullptr;
        for (auto &x : mVoices)
            if (x->active && x->note == note) v = x.get();       // the same note retriggers
        if (!v)
            for (auto &x : mVoices)
                if (!x->active) { v = x.get(); break; }           // else an idle voice
        if (!v)
        {
            v = mVoices[0].get();                                 // else the oldest is stolen
            for (auto &x : mVoices)
                if (x->age < v->age) v = x.get();
        }
        long long a, b;
        spanOf(a, b);
        if (b <= a) return;
        v->a = v->b = -1;
        if (mP.mode == Slices)
        {
            // (S7) the key root + k plays slice k of the span, as recorded, to the slice's end; a key outside them, nothing
            const int n = std::max(1, mP.slices), k = note - mP.root;
            if (k < 0 || k >= n) return;
            const long long len = b - a, sa = a + len * k / n, sb = a + len * (k + 1) / n;
            if (sb <= sa) return;
            a = sa;
            b = sb;
            v->a = sa;
            v->b = sb;
        }
        // (S1) the rate: by the note's distance from the root (chromatic), and the file's rate against ours
        const double fs = AudioConfig::instance().sampleRate();
        const double pitch = mP.mode == Chromatic ? std::pow(2.0, (note - mP.root) / 12.0) : 1.0;
        const double ratio = pitch * mRate / fs;
        v->active = v->held = true;
        v->note = note;
        v->age = ++mAge;
        v->pos = mP.reverse ? (double)(b - 1) : (double)a;
        v->inc = mP.reverse ? -ratio : ratio;
        // (S4) velocity here; the level as the block renders (D-8: set or automated, it moves a sounding note)
        const double vel = std::clamp(velocity, 1, 127) / 127.0;
        v->gain = 1.0 - mP.velocity + mP.velocity * vel;
        v->env.noteOn(1.0); // from the attack's start (ADSREnvelope restarts on every note-on)
    }

    void Sampler::pitchBend(Sample amount)
    {
        mBendRatio = std::pow(2.0, std::clamp((double)amount, -1.0, 1.0) * mP.bendRange / 12.0);
    }

    void Sampler::noteOff(int note)
    {
        if (mP.mode == OneShot || mP.mode == Slices) return; // a one-shot (and a slice) plays to its end
        for (auto &v : mVoices)
            if (v->active && v->held && v->note == note)
            {
                v->held = false;
                v->env.noteOff();
            }
    }

    void Sampler::allNotesOff()
    {
        for (auto &v : mVoices)
            if (v->active && v->held)
            {
                v->held = false;
                v->env.noteOff();
            }
    }

    void Sampler::reset()
    {
        for (auto &v : mVoices)
        {
            v->active = v->held = false;
            v->note = -1; // its envelope restarts with the next note-on
        }
        mLevelGain = -1.0;
    }

    void Sampler::render(Sample *const *out, int channels, int frames)
    {
        if (mFrames <= 0) return;
        long long a, b;
        spanOf(a, b);
        // (S4) the level, linear across the block from where the last one ended
        if ((int)mRamp.size() < frames) mRamp.assign(frames, 0.0);
        const double lv = std::pow(10.0, mP.levelDb / 20.0);
        if (mLevelGain < 0.0) mLevelGain = lv;
        for (int i = 0; i < frames; ++i) mRamp[(size_t)i] = mLevelGain + (lv - mLevelGain) * (double)(i + 1) / (double)frames;
        mLevelGain = lv;
        for (auto &vp : mVoices)
        {
            Voice &v = *vp;
            if (!v.active) continue;
            const long long va = v.a >= 0 ? v.a : a, vb = v.b >= 0 ? v.b : b; // (S7) a slice keeps its own span
            for (int i = 0; i < frames; ++i)
            {
                // (S5) a voice ends when it leaves its span, or when its release has finished
                if (v.pos < (double)va || v.pos > (double)(vb - 1) || (!v.held && v.env.isFinished()))
                {
                    v.active = false;
                    break;
                }
                // (S3) linear interpolation between the two recorded frames around the position
                const long long k = (long long)std::floor(v.pos);
                const double f = v.pos - (double)k;
                const long long k1 = std::min(k + 1, vb - 1);
                const double l = (1.0 - f) * mL[(size_t)k] + f * mL[(size_t)k1];
                const double r = (1.0 - f) * mR[(size_t)k] + f * mR[(size_t)k1];
                const double g = v.gain * v.env.nextValue(0) * mRamp[(size_t)i];
                out[0][i] += l * g;
                if (channels > 1) out[1][i] += r * g;
                v.pos += v.inc * mBendRatio; // (S6) the wheel moves every voice's rate
            }
        }
    }

    int Sampler::activeVoices() const
    {
        int n = 0;
        for (const auto &v : mVoices) n += v->active ? 1 : 0;
        return n;
    }
}
