#pragma once

#include "base/SignalProcessor.h"
#include <JuceHeader.h>
using namespace juce;
class LowPassFilter : public SignalProcessor {
private:
    double alpha; // Smoothing factor
    double filteredValue;
    double mCutoffFrequency;
    double mOldCutoffFrequency;
    double mCurrentCutoffFrequency;
    double process(double in) override;
    double calculatePhaseDelay();
public:
    LowPassFilter();
    explicit LowPassFilter(double cutoffFrequency);
    void setCutoffFrequency(double freq);
    void reset();
    void update() override;
    void smoothUpdate(double currentRatio) override;
};
