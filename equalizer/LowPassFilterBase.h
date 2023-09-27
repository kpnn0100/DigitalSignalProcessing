#pragma once

#include "../base/SignalProcessor.h"
class LowPassFilterBase : public SignalProcessor {
protected:
    double alpha; // Smoothing factor
    double filteredValue;
    double mCutoffFrequency;
    double mOldCutoffFrequency;
    double mCurrentCutoffFrequency;
    virtual double calculatePhaseDelay() = 0;
    virtual void reset() = 0;
    void smoothUpdate(double currentRatio) override;
public:
    LowPassFilterBase();
    explicit LowPassFilterBase(double cutoffFrequency);
    void setCutoffFrequency(double freq);

};
