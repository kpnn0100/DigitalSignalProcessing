#include "LowPassFilter.h"
#include <cmath>

LowPassFilter::LowPassFilter() : LowPassFilter(10000.0) {}

LowPassFilter::LowPassFilter(double cutoffFrequency) {
    mCutoffFrequency = cutoffFrequency;
    reset();
    callUpdate();
}
void LowPassFilter::setCutoffFrequency(double freq)

{
    if (mCutoffFrequency != freq)
    {
        mCutoffFrequency = freq;
        callUpdate();
    }

}
void LowPassFilter::reset()
{
    filteredValue = 0.0;
}
void LowPassFilter::update() {
    double dt = 1.0 / mSampleRate;
    double RC = 1.0 / (2.0 * M_PI * mCutoffFrequency);
    // Calculate the smoothing factor (alpha) for the filter
    alpha = dt / (RC + dt);
    
}

double LowPassFilter::process(double in) {
    filteredValue = alpha * in + (1 - alpha) * filteredValue;
    return filteredValue;
}
