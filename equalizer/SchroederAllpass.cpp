#include "SchroederAllpass.h"

SchroederAllpass::SchroederAllpass() : SchroederAllpass(10.0)
{
}

SchroederAllpass::SchroederAllpass(double initialDelayInMilliseconds)
{
    mMainDelay.setMaxDelay(2400);
    mDelayIndex = 0;

    // Set the initial delay using the provided delay time
    setDelayInMs(initialDelayInMilliseconds);
}

double SchroederAllpass::process(double in)
{
    double sum1 = last + in;
    // Calculate the output by applying the allpass filter equation
    double forward = sum1 * mFeedbackCoefficient;
    double afterDelay = mMainDelay.out(sum1);
    last = -afterDelay * mFeedbackCoefficient;
    double output =forward + afterDelay;
    return output;
}

void SchroederAllpass::setDelayInMs(double delayInMilliseconds)
{
    // Calculate the desired delay in samples based on the given delay in milliseconds
    float desiredDelayInSamples = delayInMilliseconds * mSampleRate / 1000.0;

    mMainDelay.setDelay(desiredDelayInSamples);
    mFeedbackCoefficient = 0.5;
}
