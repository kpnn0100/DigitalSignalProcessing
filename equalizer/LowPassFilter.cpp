#include "LowPassFilter.h"

void LowPassFilter::reset()
{
    filteredValue = 0.0;
}
LowPassFilter::LowPassFilter()
{
    reset();
}
void LowPassFilter::update() {
    Sample dt = 1.0 / mSampleRate;
    Sample RC = 1.0 / (2.0 * M_PI * getProperty(cutoffFreqID));
    // Calculate the smoothing factor (alpha) for the filter
    alpha = dt / (RC + dt);
    //setSampleDelay(calculatePhaseDelay());
    if (isnan(alpha) || isinf(alpha))
    {
        alpha = 1.0;
    }
}


Sample LowPassFilter::process(Sample in, int /*channel*/) {
    filteredValue = gsFlush(alpha * in + (1.0 - alpha) * filteredValue);
    return filteredValue;
}

Sample LowPassFilter::calculatePhaseDelay()
{
    Sample RC = 1.0 / (2.0 * M_PI * getProperty(cutoffFreqID));
    Sample averagePhaseDelaySamples = 0.5 * RC * mSampleRate;
    return averagePhaseDelaySamples;
}

void LowPassFilter::prepare()
{
    reset();
}


