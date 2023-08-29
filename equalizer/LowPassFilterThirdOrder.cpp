/*
  ==============================================================================

    LowPassFilterThirdOrder.cpp
    Created: 27 Aug 2023 8:27:00pm
    Author:  PC

  ==============================================================================
*/

#include "LowPassFilterThirdOrder.h"

void LowPassFilterThirdOrder::reset()
{
    filteredValue = 0.0;

    x.resize(3);
    y.resize(3);
    a.resize(3);
    for (int i = 0; i < 3; ++i) {
        x[i] = 0.0;
        y[i] = 0.0;
    }
}
LowPassFilterThirdOrder::LowPassFilterThirdOrder()
{
    reset();
}
void LowPassFilterThirdOrder::update() {
    // Calculate the filter coefficients
    double wc = 2.0 * M_PI * mCurrentCutoffFrequency;
    double T = 1.0 / mSampleRate;
    double alpha = wc * T / 2.0;
    double beta = 2.0 - alpha;

    a.resize(3);
    a[0] = (4.0 - beta) / (4.0 + beta);
    a[1] = (1.0 - alpha) * (1.0 - alpha) / (2.0 + beta);
    a[2] = (1.0 - alpha) * (1.0 - alpha) / (2.0 + beta);
}

double LowPassFilterThirdOrder::process(double in) {
    double output = a[0] * in + a[1] * x[0] + a[2] * x[1] - a[1] * y[0] - a[2] * y[1];

    x[2] = x[1];
    x[1] = x[0];
    x[0] = in;

    y[2] = y[1];
    y[1] = y[0];
    y[0] = output;

    return output;
}

double LowPassFilterThirdOrder::calculatePhaseDelay()
{
    return 0;
}


