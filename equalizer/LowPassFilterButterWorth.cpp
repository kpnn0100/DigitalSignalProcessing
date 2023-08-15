/*
  ==============================================================================

    LowPassFilterButterWorth.cpp
    Created: 13 Aug 2023 4:17:09pm
    Author:  PC

  ==============================================================================
*/

#include "LowPassFilterButterWorth.h"

LowPassFilterButterWorth::LowPassFilterButterWorth() : LowPassFilterButterWorth(10000.0, 12.0) {}

LowPassFilterButterWorth::LowPassFilterButterWorth(double cutoffFreq, double slope)
    : cutoffFreq(cutoffFreq), slope(slope) {
    callUpdate();
    reset();
}

double LowPassFilterButterWorth::process(double in) {
    // Direct Form I implementation
    double output = b[0] * in + z[0];
    z[0] = b[1] * in - a[1] * output + z[1];
    z[1] = b[2] * in - a[2] * output;
    return output;
}

void LowPassFilterButterWorth::reset() {
    z[0] = 0.0;
    z[1] = 0.0;
}

void LowPassFilterButterWorth::setCutoffFrequency(double freq)

{
    if (cutoffFreq != freq)
    {
        cutoffFreq = freq;
        callUpdate();
        reset();
    }

}

void LowPassFilterButterWorth::update() {
    double wc = 2.0 * M_PI * cutoffFreq / mSampleRate;
    double A = std::pow(10.0, slope / 40.0);
    double beta = 0.5 * (1.0 - std::sqrt(1.0 - 1.0 / (A * A)));

    b[0] = (1.0 - std::exp(-beta * wc)) / (2.0 - 2.0 * std::cos(wc));
    b[1] = 2.0 * b[0];
    b[2] = b[0];
    a[1] = -2.0 * std::exp(-beta * wc) * std::cos(wc);
    a[2] = std::exp(-2.0 * beta * wc);

    // Normalize filter coefficients
    b[0] /= a[2];
    b[1] /= a[2];
    b[2] /= a[2];
    a[1] /= a[2];
}
