#include "LowPassFilter.h"

void LowPassFilter::reset()
{
    filteredValue = 0.0;
}
void LowPassFilter::update() {
    double dt = 1.0 / mSampleRate;
    double RC = 1.0 / (2.0 * M_PI * getProperty(cutoffFreqID));
    // Calculate the smoothing factor (alpha) for the filter
    alpha = dt / (RC + dt);
    setSampleDelay(calculatePhaseDelay());
    if (isnan(alpha) || isinf(alpha))
    {
        alpha = 1.0;
    }
}


double LowPassFilter::process(double in) {
    filteredValue = alpha * in + (1.0 - alpha) * filteredValue;
    return filteredValue;
}

double LowPassFilter::calculatePhaseDelay()
{
    double RC = 1.0 / (2.0 * M_PI * getProperty(cutoffFreqID));
    double averagePhaseDelaySamples = 0.5 * RC * mSampleRate;
    return averagePhaseDelaySamples;
}

void LowPassFilter::prepare()
{
    reset();
}


