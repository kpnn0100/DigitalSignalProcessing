#pragma once

#include "LowPassFilterBase.h"
class LowPassFilter : public LowPassFilterBase {
private:
    double process(double in) override;
    double calculatePhaseDelay();
    void update() override;
    void reset() override;
public:
    using LowPassFilterBase::LowPassFilterBase;
};
