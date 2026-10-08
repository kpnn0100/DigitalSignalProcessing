#include "ParametricEQ.h"

namespace arstro
{
    ParametricEQ::ParametricEQ()
    {
        // Band defaults: the cuts off at the edges of hearing; everything else unity.
        struct Def { Biquad::Type type; Sample hz, q; bool on; };
        static const Def defs[BandCount] = {
            {Biquad::HighPass, 30.0, 0.7071, false},
            {Biquad::LowShelf, 100.0, 0.7071, true},
            {Biquad::Peak, 250.0, 1.0, true},
            {Biquad::Peak, 1000.0, 1.0, true},
            {Biquad::Peak, 4000.0, 1.0, true},
            {Biquad::HighShelf, 8000.0, 0.7071, true},
            {Biquad::LowPass, 18000.0, 0.7071, false},
        };
        for (int i = 0; i < BandCount; ++i)
        {
            mBands[i].setType(defs[i].type);
            mBands[i].setSmoothEnable(false); // the constructor's values are not a sweep
            mBands[i].setFrequency(defs[i].hz);
            mBands[i].setQ(defs[i].q);
            mBands[i].setSmoothEnable(true);
            mBands[i].setBypass(!defs[i].on);
            add(&mBands[i]);
        }
    }

    void ParametricEQ::setBandEnabled(int i, bool on)
    {
        if (i >= 0 && i < BandCount)
            mBands[i].setBypass(!on);
    }

    void ParametricEQ::reset()
    {
        for (auto &b : mBands)
            b.reset();
    }

    Sample ParametricEQ::magnitudeDbAt(Sample hz)
    {
        // Cascaded sections multiply, so their dB responses add.
        Sample db = 0.0;
        for (auto &b : mBands)
            if (!b.isBypassed())
                db += b.magnitudeDbAt(hz);
        return db;
    }
}
