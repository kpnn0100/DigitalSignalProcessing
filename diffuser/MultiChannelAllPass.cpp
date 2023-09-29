#include "MultiChannelAllPass.h"

MultiChannelAllPass::MultiChannelAllPass()
{
    
}

void MultiChannelAllPass::setChannelCount(int channelCount)
{
    mChannelCount = channelCount;
}

void MultiChannelAllPass::setDelayInMs(double delay)
{
    mDelayInMs = delay;
    setDelay(mDelayInMs*mSampleRate/1000.0);
}

double MultiChannelAllPass::process(double in)
{
    if (mCurrentDelay > 2)
    {
        double outSample =0;
        double sampleRatio =(double)mChannelCount;
        for (int i = 0; i<mChannelCount;i++)
        {
            double delayOfThis = (mCurrentDelay-1)/mChannelCount*(i+1);
            double index1 = floor(delayOfThis);
            double index2 = floor(delayOfThis+1);
            double ratio = 1 - ((delayOfThis) - index1);

            double sample1 = delayBuffer[int(index1)];
            double sample2 = delayBuffer[int(index2)] ;
            double thisOut = sample1 * ratio + sample2 * (1 - ratio);
            double invert =-1.0;
            if (i%3==1)
            {
                invert = 1.0;
            }
            outSample += (thisOut/sampleRatio)*invert;
        }
        delayBuffer.push_front_and_pop_back(in); // Update the delay buffer
        return outSample; // Return the interpolated output sample
    }
    else
    {
        return in; // No delay applied, return the input as is
    }
}

void MultiChannelAllPass::onSampleRateChanged()
{
    setDelayInMs(mDelayInMs);
}
