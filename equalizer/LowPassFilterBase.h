/*
 *  Gyrus Space DSP Library
 *  Author: kpnn0100
 *  Organization: Gyrus Space
 *
 *  Description:
 *  This is an open-source Digital Signal Processing (DSP) library developed by Gyrus Space.
 *  It provides various functions and utilities for processing digital signals and audio data.
 *  The library is free to use and open for contributions from the community.
 *
 *  License: MIT License
 *  GitHub Repository: https://github.com/kpnn0100/DigitalSignalProcessing
 *
 *  Disclaimer: This library is provided as-is without any warranties. The author and organization
 *  shall not be held liable for any damages or liabilities arising from the use of this library.
 */
#pragma once

#include "../base/SignalProcessor.h"
class LowPassFilterBase : public SignalProcessor {
protected:
    Sample alpha = 1.0; // filter coefficient (safe default until a cutoff is set)

    virtual Sample calculatePhaseDelay() = 0;
    virtual void reset() = 0;
public:
    Sample filteredValue;
    enum PropertyIndex {
        cutoffFreqID,
        propertyCount
    };
    LowPassFilterBase();
    explicit LowPassFilterBase(Sample cutoffFrequency);
    void setCutoffFrequency(Sample freq);

};
