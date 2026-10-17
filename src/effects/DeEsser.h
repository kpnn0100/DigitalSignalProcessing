/*
 *  Arstro DSP Library — DeEsser: sibilance held down (REQ-fx-deesser-1). A high-pass (Biquad::design, Butterworth)
 *  hears the band; its level, linked across channels, is held at the threshold — never cut by more than the range;
 *  split: a high shelf at the same corner, its gain the cut (the sound under the corner left as it is); wide: the
 *  whole sound turned down. Nothing cut: the input, to the bit. effects/README.md, Math — DeEsser (DE1)–(DE5). Whole
 *  frames; no allocation (up to kMaxChannels; beyond them a channel takes the wide gain).
 */
#pragma once
#include "../base/Sample.h"

namespace arstro
{
    class DeEsser
    {
    public:
        static constexpr int kMaxChannels = 16;
        static constexpr double kAttackMs = 1.0, kReleaseMs = 50.0;
        DeEsser();
        void setFrequency(Sample hz);
        void setThresholdDb(Sample db) { mT = db; }
        void setRangeDb(Sample db) { mRange = db < 0 ? 0 : db; }
        /** true: only the band is turned down (split); false: the whole sound (wide). */
        void setSplit(bool on) { mSplit = on; }
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** The cut it applies now, dB ≥ 0 (for tests). */
        Sample cutDb() const { return mCut; }

    private:
        void design();
        double mF = 6000, mT = -30, mRange = 12;
        bool mSplit = true;
        double mB0 = 1, mB1 = 0, mB2 = 0, mA1 = 0, mA2 = 0; // (DE1) the band
        double mZ1[kMaxChannels] = {}, mZ2[kMaxChannels] = {};
        double mS[5] = {1, 0, 0, 0, 0}, mShelfCut = 0;     // (DE4) the shelf: b0 b1 b2 a1 a2, at mShelfCut
        double mY1[kMaxChannels] = {}, mY2[kMaxChannels] = {};
        double mAtk = 0, mRel = 0, mEnv = 0, mCut = 0;
    };
}
