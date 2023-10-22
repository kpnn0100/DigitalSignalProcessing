#pragma once

#include "../base/SignalProcessor.h"
class HighPassFilterBase : public SignalProcessor {
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
    HighPassFilterBase();
    explicit HighPassFilterBase(double cutoffFrequency);
    void setCutoffFrequency(double freq);

};
