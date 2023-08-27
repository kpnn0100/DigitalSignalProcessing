#include "LowPassFilter.h"
#include <cmath>

LowPassFilter::LowPassFilter() : LowPassFilter(10000.0) {}

LowPassFilter::LowPassFilter(double cutoffFrequency) {
    /*logger = new FileLogger(juce::String("G:\\Dev\\plugin\\DSPTester\\Builds\\VisualStudio2022\\x64\\Debug\\VST3\\log.txt"), "log");*/
    mCutoffFrequency = cutoffFrequency;
    mOldCutoffFrequency = cutoffFrequency;
    mCurrentCutoffFrequency = cutoffFrequency;
    mSmoothEnable = false;
    reset();
    callUpdate();
}
void LowPassFilter::setCutoffFrequency(double freq)

{

    if (mCutoffFrequency != freq)
    {

        mCutoffFrequency = freq;
        mOldCutoffFrequency = mCurrentCutoffFrequency;
        if (mSmoothEnable)
        {
            smoothUpdate(0.0);
        }
        else
        {
            smoothUpdate(1.0);
        }
        
    }

}
void LowPassFilter::reset()
{
    filteredValue = 0.0;
}
void LowPassFilter::update() {
    double dt = 1.0 / mSampleRate;
    double RC = 1.0 / (2.0 * M_PI * mCurrentCutoffFrequency);
    // Calculate the smoothing factor (alpha) for the filter
    alpha = dt / (RC + dt);
    setSampleDelay(calculatePhaseDelay());
    if (isnan(alpha) || isinf(alpha))
    {
        alpha = 1.0;
    }
    
}

void LowPassFilter::smoothUpdate(double currentRatio)
{
    double oldCutoffLog = log(mOldCutoffFrequency);
    double newCutoffLog = log(mCutoffFrequency);
    mCurrentCutoffFrequency = exp(oldCutoffLog * (1 - currentRatio) + newCutoffLog * currentRatio);
    update();
}


double LowPassFilter::process(double in) {
    filteredValue = alpha * in + (1.0 - alpha) * filteredValue;
    return filteredValue;
}

double LowPassFilter::calculatePhaseDelay()
{
    double RC = 1.0 / (2.0 * M_PI * mCurrentCutoffFrequency);
    double averagePhaseDelaySamples = 0.5 * RC * mSampleRate;
    return averagePhaseDelaySamples;
}


