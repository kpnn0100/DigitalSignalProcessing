#pragma once

#include "../base/SignalProcessor.h"
class LowPassFilterBase : public SignalProcessor {
protected:
    double alpha; // Smoothing factor
    double filteredValue;
        enum PropertyIndex {
        cutoffFreqID,
        propertyCount
    };
    virtual double calculatePhaseDelay() = 0;
    virtual void reset() = 0;
public:
    LowPassFilterBase();
    explicit LowPassFilterBase(double cutoffFrequency);
    void setCutoffFrequency(double freq);

};
