/*
 *  Arstro DSP Library — Dither: TPDF dither and quantisation to a bit depth (REQ-out-dither-1).
 *
 *  What a float mix needs before it is written as integers: rounding alone leaves an error that follows the
 *  signal (distortion, and a quiet fade that steps to silence); adding triangular noise of ±1 LSB first makes
 *  the error's mean and power independent of the signal — a constant, white floor of Δ²/4 — so a sound below
 *  one LSB survives as itself under the noise (README ## Math (D1)–(D3)).
 *
 *  An exact zero passes as zero (D0): digital silence — a render's silent intro, its end — stays silence.
 *
 *  Composed: the triangle is the sum of two of the library's `Noise` uniforms (generator/Noise.h, xorshift32);
 *  nothing new but the rounding. Seeded, so the same input gives the same bytes — a render stays pure. Takes
 *  float, as hosts write files; no allocation.
 */
#pragma once
#include "../generator/Noise.h"
#include <cstdint>

namespace arstro
{
    class Dither
    {
    public:
        static constexpr uint32_t kSeed = 0x2545F491u;
        explicit Dither(int bits = 16, uint32_t seed = kSeed);
        void setBits(int bits);                 // 2 … 24: the depth written (a float file is not dithered)
        int bits() const { return mBits; }
        void reset(uint32_t seed = kSeed);      // the noise from the start again
        void setEnabled(bool on) { mOn = on; }  // off: rounding only (D2) — what `bits` alone makes
        /** One sample onto the grid of `bits`: (D1)–(D3); the result × 2^(bits−1) is an integer in [−2^(bits−1), 2^(bits−1) − 1]. */
        float quantize(float x);
        void process(float *x, int frames);     // in place

    private:
        int mBits = 16;
        double mQ = 32768.0;                    // 2^(bits−1): steps per unit
        bool mOn = true;
        Noise mA, mB;
    };
}
