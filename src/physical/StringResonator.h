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

        // Zeroes the y[n-1]/y[n-2] history on every channel. Required before
        // reusing this resonator for a new note (voice-stealing): changing
        // frequency/decay coefficients while old history is still nonzero feeds
        // stale state through new coefficients and can produce a large transient
        // (this is what "reset before retune" protects against).
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
        // Per-channel history: y[n-1], y[n-2].
        std::vector<Sample> mY1;
        std::vector<Sample> mY2;
    };
}
