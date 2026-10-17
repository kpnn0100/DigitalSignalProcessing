/*
 *  Arstro DSP Library — Gate: a gate and a downward expander (REQ-fx-gate-1). Below its threshold a sound is turned
 *  down by the ratio, never by more than the range; it holds open before it closes; its gain smoothed (opening at the
 *  attack, closing at the release); its level linked across channels, or a key's (another track opens it).
 *  effects/README.md, Math — Gate (GT1)–(GT5). Whole frames; no allocation.
 */
#pragma once
#include "../base/Sample.h"

namespace arstro
{
    class Gate
    {
    public:
        Gate();
        void setThresholdDb(Sample db) { mT = db; }
        void setRatio(Sample r) { mR = r < 1 ? 1 : r; }
        void setRangeDb(Sample db) { mRange = db < 0 ? 0 : db; }
        void setAttackMs(Sample ms) { mAttackMs = ms; coeffs(); }
        void setHoldMs(Sample ms) { mHoldMs = ms < 0 ? 0 : ms; coeffs(); }
        void setReleaseMs(Sample ms) { mReleaseMs = ms; coeffs(); }
        void setSidechain(bool on) { mSidechain = on; }
        /** The key for the NEXT process call (keyed): `channels` buffers of that block's frames; nullptr = silence. */
        void setKey(const Sample *const *key, int channels) { mKey = key; mKeyChannels = key ? channels : 0; }
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** The gain it applies now, dB (for tests). */
        Sample gainDb() const { return mGain; }

    private:
        void coeffs();
        Sample mT = -40.0, mR = 100.0, mRange = 80.0, mAttackMs = 1.0, mHoldMs = 20.0, mReleaseMs = 100.0;
        bool mSidechain = false;
        const Sample *const *mKey = nullptr;
        int mKeyChannels = 0;
        double mEnv = 0.0, mGain = 0.0, mDetA = 0.0, mDetR = 0.0, mOpen = 0.0, mClose = 0.0;
        long long mHoldLeft = 0, mHold = 0;
    };
}
