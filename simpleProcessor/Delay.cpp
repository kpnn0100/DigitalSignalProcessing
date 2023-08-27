#include "Delay.h"

Delay::Delay()
{
    setSampleDelay(0); // Initialize the sample delay
    setDelay(0); // Set the delay to zero
    setSmoothEnable(false);
}

Delay::Delay(double delay)
{
    setSampleDelay(0); // Initialize the sample delay
    setMaxDelay(delay); // Set the maximum allowable delay
    setDelay(delay); // Set the delay
}

Delay::Delay(double delay, double maxDelay)
{
    setSampleDelay(0); // Initialize the sample delay
    setMaxDelay(maxDelay); // Set the maximum allowable delay
    setDelay(delay); // Set the delay
}

void Delay::setDelay(double newDelay)
{
    mOldDelay = mCurrentDelay;
    if (newDelay > (double)mMaxDelay)
    {
        mainDelay = mMaxDelay; // Cap the delay at the maximum allowable value
    }
    else
    {
        mainDelay = newDelay; // Set the delay to the specified value
    }
    smoothUpdate(1.0);
}

double Delay::process(double in)
{
    if (mCurrentDelay > 0)
    {
        // Calculate the indices and ratio for interpolation
        double index1 = floor(mCurrentDelay);
        double index2 = floor(mCurrentDelay+1);
        double ratio = 1 - ((mCurrentDelay) - index1);

        // Interpolate between delay samples
        double sample1 = delayBuffer[int(index1)];
        double sample2 = delayBuffer[int(index2)] ;
        double outSample = sample1 * ratio + sample2 * (1 - ratio);

        delayBuffer.push_front_and_pop_back(in); // Update the delay buffer
        return outSample; // Return the interpolated output sample
    }
    else
    {
        return in; // No delay applied, return the input as is
    }
}

inline void Delay::setMaxDelay(int maxDelay)
{
    mMaxDelay = maxDelay; // Set the maximum allowable delay
    delayBuffer = CircularList<double>(maxDelay,0.0); // Initialize the delay buffer
}

void Delay::smoothUpdate(double currentRatio)
{
    mCurrentDelay = mOldDelay * (1 - currentRatio) + mainDelay * currentRatio;
}