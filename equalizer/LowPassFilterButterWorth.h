#pragma once
#include <cmath>
#include <vector>
#include "base/SignalProcessor.h"

class LowPassFilterButterWorth : public SignalProcessor {
private:

    void update() override;
    double process(double in) override;
    double cutoffFreq;
    double slope;
    double b[3];
    double a[3];
    double z[2];
public:
    LowPassFilterButterWorth();
    LowPassFilterButterWorth(double cutoffFreq, double slope);
    
    void reset();
    void setCutoffFrequency(double freq);
};
