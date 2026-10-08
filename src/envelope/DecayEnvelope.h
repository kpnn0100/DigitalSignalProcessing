/*
 *  Arstro DSP Library — DecayEnvelope: a struck sound's envelope — a short linear attack, then an
 *  exponential fall (REQ-decay-1).
 *
 *  Not an ADSR: a drum has no sustain and no note-off, and its decay is exponential (a damped
 *  resonance loses a fixed fraction of its energy per cycle), which the ADSR's linear stages are
 *  not. Its time constant is stated the way a drum machine's knob is — "decay" = the time to fall
 *  60 dB. A value type with `next()`: times are converted to samples at `trigger()`, from
 *  AudioConfig, so a sample-rate change between notes is honoured. Math: envelope/README.md
 *  ## DecayEnvelope (D1)…(D3).
 */
#pragma once
#include "../base/Sample.h"

namespace arstro
{
    class DecayEnvelope
    {
    public:
        void setAttackMs(Sample ms) { mAttackMs = ms < 0 ? 0 : ms; }
        void setDecayMs(Sample ms) { mDecayMs = ms < 1 ? 1 : ms; }
        Sample decayMs() const { return mDecayMs; }

        /** Start a hit at `peak` (0..1). */
        void trigger(Sample peak = 1.0);
        /** Fall to silence over `ms` from wherever it is (a hi-hat choked by the closed hat). */
        void choke(Sample ms);
        /** The gain for this sample; advances one sample. */
        Sample next();
        /** Below −80 dB of its peak (or never triggered): the voice may stop. */
        bool isFinished() const { return mStage == Idle; }
        Sample level() const { return mLevel; }

    private:
        enum Stage { Idle, Attack, Decay };
        Stage mStage = Idle;
        Sample mAttackMs = 0.0, mDecayMs = 300.0;
        Sample mLevel = 0.0, mPeak = 1.0, mAttackInc = 0.0, mRatio = 0.0, mFloor = 0.0;
    };
}
