/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  StringResonator: one damped sinusoidal mode (a matched-Z two-pole resonator).
 *  The single reusable primitive of physical/ — driven at a string's partial
 *  frequency it's a string mode; driven at a soundboard body-mode frequency
 *  (PianoBridge) it's the same math. See src/physical/README.md ## 1.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include <vector>

namespace arstro
{
    class StringResonator : public SignalProcessor
    {
    public:
        enum PropertyIndex
        {
            frequencyID, // Hz -> pole angle theta
            decayID,     // seconds, T60 -> pole radius r
            propertyCount
        };
        StringResonator();

        void setFrequencyHz(Sample hz);
        void setDecaySeconds(Sample t60);

        /** Selects which excitation the input gain G is normalised for (README ## 1).
         *  false (default): sustained drive — G = (1-r^2)sin(theta), unit-ish steady-state
         *  peak. Right for the soundboard modes, which are driven continuously.
         *  true: impulsive strike — G = sin(theta), unit-ish IMPULSE peak, independent of
         *  decay time. Right for struck string partials: how hard a mode is set moving is
         *  set by the hammer force and mode shape, not by how slowly it later decays. */
        void setImpulseNormalized(bool impulse);

        /** Zeroes the per-channel recurrence history. Needed before a reused
         *  (voice-stolen) resonator is driven again — see LongitudinalBank. */
        void reset();


        void update() override;
        void onChannelCountChanged() override;

    protected:
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        // Shared across channels (derived from properties in update()).
        Sample mCosTheta = 1.0;
        Sample mR = 0.0;
        Sample mG = 0.0;
        bool mImpulseNormalized = false;
        // Per-channel history: y[n-1], y[n-2].
        std::vector<Sample> mY1;
        std::vector<Sample> mY2;
    };
}
