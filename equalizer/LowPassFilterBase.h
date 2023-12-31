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
    double alpha; // Smoothing factor

    virtual double calculatePhaseDelay() = 0;
    virtual void reset() = 0;
public:
    double filteredValue;
    enum PropertyIndex {
        cutoffFreqID,
        propertyCount
    };
    LowPassFilterBase();
    explicit LowPassFilterBase(double cutoffFrequency);
    void setCutoffFrequency(double freq);

};
