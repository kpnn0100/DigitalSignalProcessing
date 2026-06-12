#pragma once

#include "LowPassFilter.h"
class HighPassFilter : public LowPassFilter {
private:
    Sample previousInput;
    Sample previousOutput;
    Sample process(Sample in, int channel) override;
public:
    using LowPassFilter::LowPassFilter;
    void prepare() override;
    HighPassFilter();
};
