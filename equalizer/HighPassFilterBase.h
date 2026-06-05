#pragma once

#include "../base/SignalProcessor.h"
class HighPassFilterBase : public SignalProcessor {
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
    HighPassFilterBase();
    explicit HighPassFilterBase(Sample cutoffFrequency);
    void setCutoffFrequency(Sample freq);

};
