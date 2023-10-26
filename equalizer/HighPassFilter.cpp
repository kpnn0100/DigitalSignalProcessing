#include "HighPassFilter.h"

double HighPassFilter::process(double in) {
    // High-pass filter equation
    double output = in - previousInput + (1.0-alpha) * previousOutput;

    // Update the previous input and output
    previousInput = in;
    previousOutput = output;

    return output;
}

void HighPassFilter::onInit()
{
    previousInput = 0.0;
    previousOutput = 0.0;
}

HighPassFilter::HighPassFilter()
{
    onInit();
}


