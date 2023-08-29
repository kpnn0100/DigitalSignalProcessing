/*
  ==============================================================================

    LowPassFilterSixthOrder.cpp
    Created: 27 Aug 2023 8:45:02pm
    Author:  PC

  ==============================================================================
*/

#include "LowPassFilterSixthOrder.h"


void LowPassFilterSixthOrder::reset()
{
    filteredValue = 0.0;

    x.resize(7);
    y.resize(7);
    a.resize(7);
    for (int i = 0; i < 7; ++i) {
        x[i] = 0.0;
        y[i] = 0.0;
    }
}
LowPassFilterSixthOrder::LowPassFilterSixthOrder()
{
    reset();
}
void LowPassFilterSixthOrder::update() {
    // Calculate the filter coefficients
    double wc = 2.0 * M_PI * mCurrentCutoffFrequency;
    double T = 1.0 / mSampleRate;
    double alpha = wc * T / 2.0;
    double beta = 2.0 - alpha;

    a.resize(7);
    a[0] = (64.0 - 6.0 * beta) / (64.0 + 6.0 * beta);
    a[1] = 6.0 * (beta - 16.0) / (64.0 + 6.0 * beta);
    a[2] = 15.0 * (16.0 - alpha) * (16.0 - alpha) / (64.0 + 6.0 * beta);
    a[3] = 20.0 * (beta - 16.0) / (64.0 + 6.0 * beta);
    a[4] = 15.0 * (16.0 - alpha) * (16.0 - alpha) / (64.0 + 6.0 * beta);
    a[5] = 6.0 * (beta - 16.0) / (64.0 + 6.0 * beta);
    a[6] = (64.0 - 6.0 * beta) / (64.0 + 6.0 * beta);
}

double LowPassFilterSixthOrder::process(double in) {
    double output = 0.0;
    for (int i = 0; i < 7; ++i) {
        output += a[i] * x[i] - a[i] * y[i];
    }

    for (int i = 6; i > 0; --i) {
        x[i] = x[i - 1];
        y[i] = y[i - 1];
    }

    x[0] = in;
    y[0] = output;

    return output;
}

double LowPassFilterSixthOrder::calculatePhaseDelay()
{
    return 0;
}


