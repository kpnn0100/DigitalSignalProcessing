#include "Dither.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Dither::Dither(int bits, uint32_t seed)
    {
        setBits(bits);
        reset(seed);
    }

    void Dither::setBits(int bits)
    {
        mBits = std::clamp(bits, 2, 24);
        mQ = std::ldexp(1.0, mBits - 1);
    }

    void Dither::reset(uint32_t seed)
    {
        // two generators from one seed: the second's seed a fixed odd step away, so the uniforms are independent
        mA.setSeed(seed);
        mB.setSeed(seed * 0x9E3779B1u + 0x7F4A7C15u);
    }

    float Dither::quantize(float x)
    {
        // (D1) d = (u1 + u2)/2, u ~ U[−1, 1): triangular on (−1, 1) — in steps of the grid
        const double d = mOn ? 0.5 * ((double)mA.next() + (double)mB.next()) : 0.0;
        // (D2) round onto the grid; (D3) clamp to the integers the depth has
        const double k = std::clamp(std::floor((double)x * mQ + d + 0.5), -mQ, mQ - 1.0);
        return (float)(k / mQ);
    }

    void Dither::process(float *x, int frames)
    {
        for (int i = 0; i < frames; ++i) x[i] = quantize(x[i]);
    }
}
