/*
 *  Arstro DSP Library — Imager: a stereo image made wider or narrower, and mono below a frequency (REQ-fx-imager-1).
 *  Mid and side ((L + R)/2, (L − R)/2); the side scaled by the width; optionally the side's lows taken away by a
 *  Linkwitz-Riley high-pass (library Biquads, Butterworth pairs — the multiband's crossover) and the mid sent through
 *  the matching all-pass, so above the frequency the image is the width's and below it mono. Channels in pairs
 *  (0, 1), (2, 3) …; a lone last one passes untouched. effects/README.md, Math — Imager (IM1)–(IM4). Whole frames;
 *  no allocation.
 */
#pragma once
#include "../base/Sample.h"
#include "../equalizer/Biquad.h"

namespace arstro
{
    class Imager
    {
    public:
        Imager();
        /** 0 … 200 %: 0 mono, 100 as it is, 200 the side doubled. */
        void setWidth(Sample pct) { mW = (pct < 0 ? 0 : pct > 200 ? 200 : pct) / 100.0; }
        void setMono(bool on) { mMono = on; }
        void setMonoHz(Sample hz);
        void process(Sample *const *io, int channels, int frames);
        void reset();

    private:
        double mW = 1.0, mHz = 120.0;
        bool mMono = false;
        // (IM2) the side's high-pass and the mid's all-pass (LP + HP), each two Butterworth sections; one channel of
        // each per stereo pair
        Biquad mSideHp[2], mMidLp[2], mMidHp[2];
    };
}
