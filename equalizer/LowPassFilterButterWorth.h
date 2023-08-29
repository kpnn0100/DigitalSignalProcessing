#pragma once
#include <cmath>
#include <vector>
#include "LowPassFilterBase.h"

class LowPassFilterButterWorth : public LowPassFilterBase {
private:
    int mOrder = 1;
    std::vector<double> inputBuffer;
    std::vector<double> outputBuffer;
    std::vector<double> bCoeffs;
    std::vector<double> aCoeffs;
    double process(double in) override;
    double calculatePhaseDelay();
    void update() override;
    double binomialCoeff(int n, int k);
    virtual void reset() override;
public:
    LowPassFilterButterWorth();
    void setOrder(int order);
};
