#pragma once

#include "base/SignalProcessor.h"

class LowPassFilter : public SignalProcessor {
private:
    double alpha; // Smoothing factor
    double filteredValue;
    double mCutoffFrequency;
double process(double in) override;
public:
    LowPassFilter();
    explicit LowPassFilter(double cutoffFrequency);
    void setCutoffFrequency(double freq);
    void reset();
    void update() override;
    
};
