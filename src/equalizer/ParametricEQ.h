/*
 *  Arstro DSP Library — ParametricEQ: seven switchable bands, each a Biquad (REQ-eq-2).
 *
 *    0 low cut (high-pass) · 1 low shelf · 2,3,4 peaks · 5 high shelf · 6 high cut (low-pass)
 *
 *  A COMPOSITION, not new math (arstro.dsp.implement rule 1): a serial Block of seven Biquads in
 *  that fixed order. A band that is switched off is bypassed (Block skips it). With every gain at
 *  0 dB the shelves and peaks are exactly the identity (RBJ with A = 1 has b = a), so a fresh EQ
 *  is transparent, which is what a user dropping it on a strip expects. The cuts start OFF.
 */
#pragma once
#include "Biquad.h"
#include "../base/Block.h"

namespace arstro
{
    class ParametricEQ : public Block
    {
    public:
        enum Band { LowCut = 0, LowShelf, Peak1, Peak2, Peak3, HighShelf, HighCut, BandCount };

        ParametricEQ();

        Biquad &band(int i) { return mBands[i]; }
        void setBandEnabled(int i, bool on);
        bool bandEnabled(int i) const { return !mBands[i].isBypassed(); }
        void reset();

        /** The whole EQ's response at `hz`: the sum, in dB, of every enabled band's. */
        Sample magnitudeDbAt(Sample hz);

    private:
        Biquad mBands[BandCount];
    };
}
