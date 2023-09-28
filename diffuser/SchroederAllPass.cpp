#include "SchroederAllPass.h"

SchroederAllPass::SchroederAllPass() : SchroederAllPass(10.0)
{
}

SchroederAllPass::SchroederAllPass(double initialDelayInMilliseconds)
{
    mMainDelay.setMaxDelay(2400);

    // Set the initial delay using the provided delay time
    setDelayInMs(initialDelayInMilliseconds);
}

void SchroederAllPass::setFeedbackGain(double gain)
{
    mFeedbackCoefficient = gain;
}


double SchroederAllPass::process(double in)
{
    // double sum1 = last + in;
    // // Calculate the output by applying the allpass filter equation
    // double forward = sum1 * mFeedbackCoefficient;
    // double afterDelay = mMainDelay.out(sum1);
    // last = afterDelay * (-mFeedbackCoefficient);
    // double output =forward + afterDelay;
    // return output;

    double delayedSample = mMainDelay.out((in + (last * mFeedbackCoefficient))/2);
    last = delayedSample;
    double feedFordwardSample  =in * (-mFeedbackCoefficient);
    return (delayedSample + feedFordwardSample);
    
}

void SchroederAllPass::setDelayInMs(double delayInMilliseconds)
{
    // Calculate the desired delay in samples based on the given delay in milliseconds
    mDelayInMs = delayInMilliseconds;
    float desiredDelayInSamples = delayInMilliseconds * mSampleRate / 1000.0;

    mMainDelay.setDelay(desiredDelayInSamples);
}
void SchroederAllPass::onSampleRateChanged()
{
    setDelayInMs(mDelayInMs);
}