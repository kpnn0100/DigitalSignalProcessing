/*
 *  Arstro DSP Library — Noise: seeded white noise (REQ-noise-1).
 *
 *  A primitive, not a voice: a value type with `next()`, like a phase accumulator, so a drum voice
 *  or a synth can own one without inheriting an envelope it does not want. Deterministic by
 *  construction — the same seed gives the same samples on every run and every machine, which is
 *  what lets a render be a pure function of its project (REQ-noise-1; Solaris R-DSP-4). No clock,
 *  no global generator. The math is in generator/README.md ## Noise.
 */
#pragma once
#include "../base/Sample.h"
#include <cstdint>

namespace arstro
{
    class Noise
    {
    public:
        explicit Noise(uint32_t seed = 0x9E3779B9u) { setSeed(seed); }

        /** A zero seed would lock xorshift at zero forever; it is replaced by a fixed constant. */
        void setSeed(uint32_t seed) { mState = seed ? seed : 0x9E3779B9u; }

        /** Uniform in [-1, 1). */
        Sample next()
        {
            // (N1) xorshift32 (Marsaglia 2003), period 2^32 − 1.
            uint32_t x = mState;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            mState = x;
            // (N2) the top 24 bits → [0,1) → [-1,1).
            return (Sample)(x >> 8) * (Sample)(2.0 / 16777216.0) - (Sample)1.0;
        }

    private:
        uint32_t mState;
    };
}
