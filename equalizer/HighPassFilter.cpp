#include "HighPassFilter.h"

Sample HighPassFilter::process(Sample in, int /*channel*/) {
    // High-pass filter equation
    Sample output = in - previousInput + (1.0-alpha) * previousOutput;

    // Update the previous input and output (flush denormals on decay)
    previousInput = in;
    previousOutput = gsFlush(output);

    return previousOutput;
}

void HighPassFilter::prepare()
{
    previousInput = 0.0;
    previousOutput = 0.0;
}

HighPassFilter::HighPassFilter()
{
    prepare();
}


