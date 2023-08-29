/*
  ==============================================================================

    LowPassFilterThirdOrder.h
    Created: 27 Aug 2023 8:27:00pm
    Author:  PC

  ==============================================================================
*/

#pragma once

#include "LowPassFilterBase.h"
class LowPassFilterSixthOrder : public LowPassFilterBase {
private:
    std::vector<double> a;   // Coefficients for the filter's difference equation
    std::vector<double> x;   // Input history
    std::vector<double> y;   // Output history
    double process(double in) override;
    double calculatePhaseDelay();
    void update() override;
    void reset() override;
public:
    LowPassFilterSixthOrder();
};
