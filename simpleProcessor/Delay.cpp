#include "Delay.h"

Delay::Delay() : Delay(0.0,0.0)
{
    
}

Delay::Delay(double delay) : Delay(delay,(int)delay)
{

}

Delay::Delay(double delay, int maxDelay)
{
    mOldDelay = 0;
    mCurrentDelay = 0;
    setSampleDelay(0); // Initialize the sample delay
    setMaxDelay(maxDelay); // Set the maximum allowable delay
    setDelay(delay); // Set the delay
    setSmoothEnable(true);
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
    callUpdate();
}

double Delay::process(double in)
{
    if (mCurrentDelay > 2)
    {
        //std::cout << this << std::endl;
        //std::cout << "before delay" << std::endl;
        // Calculate the indices and ratio for interpolation
        double index1 = floor(mCurrentDelay-1);
        double index2 = floor(mCurrentDelay);
        double ratio = 1 - ((mCurrentDelay) - index1);

        // Interpolate between delay samples
        //std::cout << mCurrentDelay << std::endl;
        //std::cout << "before access" << std::endl;
        //std::cout << delayBuffer.size() << std::endl;
        //std::cout << int(index1) << std::endl;
        double sample1 = delayBuffer[int(index1)];
        double sample2 = delayBuffer[int(index2)] ;
        double outSample = sample1 * ratio + sample2 * (1 - ratio);
        delayBuffer.push_front_and_pop_back(in); // Update the delay buffer
        //std::cout << "after delay" << std::endl;
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
    //std::cout << this << std::endl;
    delayBuffer = CircularList<double>(maxDelay+1,0.0); // Initialize the delay buffer
}

void Delay::smoothUpdate(double currentRatio)
{
    mCurrentDelay = mOldDelay * (1 - currentRatio) + mainDelay * currentRatio;
}

void Delay::update()
{
    
}
