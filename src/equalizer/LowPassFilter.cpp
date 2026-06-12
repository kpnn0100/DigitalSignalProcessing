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
    if (isnan(alpha) || isinf(alpha))
    {
        alpha = 1.0;
    }
}


Sample LowPassFilter::process(Sample in, int /*channel*/) {
    filteredValue = arstroFlush(alpha * in + (1.0 - alpha) * filteredValue);
    return filteredValue;
}

void LowPassFilter::prepare()
{
    reset();
}


