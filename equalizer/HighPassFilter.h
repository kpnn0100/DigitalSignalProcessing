#pragma once

#include "LowPassFilter.h"
class HighPassFilter : public LowPassFilter {
private:
    double previousInput;
    double previousOutput;
    double process(double in) override;
public:
    using LowPassFilter::LowPassFilter;
    void onInit() override;
    HighPassFilter();
};
