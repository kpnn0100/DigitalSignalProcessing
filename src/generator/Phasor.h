/*
 *  Arstro DSP Library — Phasor: a bare phase accumulator in [0, 1) (REQ-drum-1).
 *
 *  The primitive under the drum voices' sweeping sines and metallic squares. Why not
 *  `Oscillator`: a drum's pitch moves on EVERY sample (a kick falls several octaves in ~50 ms),
 *  and `Oscillator::setFrequency` recomputes the increment of every unison voice on every channel
 *  with a `pow` each — ten `pow`s a sample to change one frequency. A phasor takes the increment
 *  from its caller and costs one add. Not band-limited: its users either read a sine from it, or
 *  run its squares straight into band-pass filters where the aliasing is part of the metal.
 *  Math: generator/README.md ## Phasor.
 */
#pragma once
#include "../base/Sample.h"
#include <cmath>

namespace arstro
{
    class Phasor
    {
    public:
        /** The phase for this sample, then advance by `inc` cycles (= hz / fs). */
        Sample advance(Sample inc)
        {
            const Sample p = mPhase;
            mPhase += inc;
            mPhase -= std::floor(mPhase); // (P1) wrap to [0,1) — also for a negative or >1 increment
            return p;
        }
        Sample sine(Sample inc) { return std::sin(6.283185307179586 * advance(inc)); }
        Sample square(Sample inc) { return advance(inc) < 0.5 ? 1.0 : -1.0; }
        void reset(Sample phase = 0.0) { mPhase = phase; }
        Sample phase() const { return mPhase; }

    private:
        Sample mPhase = 0.0;
    };
}
