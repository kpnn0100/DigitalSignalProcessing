#include "Delay.h"

Delay::Delay()
{
    setSampleDelay(0); // Initialize the sample delay
    setDelay(0); // Set the delay to zero
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
    if (newDelay > (double)mMaxDelay)
    {
        mainDelay = mMaxDelay; // Cap the delay at the maximum allowable value
    }
    else
    {
        mainDelay = newDelay; // Set the delay to the specified value
    }
}

double Delay::out(double in)
{
    if (mainDelay > 0)
    {
        // Calculate the indices and ratio for interpolation
        double index1 = floor(mainDelay - 1);
        double index2 = floor(mainDelay);
        double ratio = 1 - ((mainDelay - 1) - index1);

        // Interpolate between delay samples
        double outSample = delayBuffer[int(index1)] * ratio + delayBuffer[int(index2)] * (1 - ratio);

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
