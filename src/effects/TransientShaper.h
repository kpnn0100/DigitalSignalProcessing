/*
 *  Arstro DSP Library — TransientShaper: more or less attack, more or less sustain, whatever the level
 *  (REQ-fx-transient-1). A fast envelope against two slow ones that follow it — one only up (the attack: how far the
 *  sound has just risen), one only down (the sustain: how far it has just fallen); each difference in dB, scaled by
 *  its amount, is the gain. Linked across channels. effects/README.md, Math — TransientShaper (TS1)–(TS5). Whole
 *  frames; no allocation.
 */
#pragma once
#include "../base/Sample.h"

namespace arstro
{
    class TransientShaper
    {
    public:
        TransientShaper();
        /** −100 … +100 %: −100 takes the rise away, +100 doubles it (in dB). */
        void setAttack(Sample pct) { mA = clampPct(pct) / 100.0; }
        /** −100 … +100 %: −100 takes the tail away, +100 holds it toward the slow fall's. */
        void setSustain(Sample pct) { mS = clampPct(pct) / 100.0; }
        void setOutputDb(Sample db) { mOut = db; }
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** The gain it applies now, dB, its output gain apart (for tests). */
        Sample gainDb() const { return mGain; }

        static constexpr double kFastAttackMs = 1.0, kFastReleaseMs = 100.0, kSlowAttackMs = 30.0, kSlowReleaseMs = 1000.0;
        static constexpr double kLimitDb = 24.0;

    private:
        static Sample clampPct(Sample p) { return p < -100 ? -100 : p > 100 ? 100 : p; }
        double mA = 0, mS = 0, mOut = 0;
        double mFa = 0, mFr = 0, mSa = 0, mSr = 0; // (TS2)–(TS3) per-sample constants
        double mFast = 0, mRise = 0, mFall = 0, mGain = 0;
    };
}
