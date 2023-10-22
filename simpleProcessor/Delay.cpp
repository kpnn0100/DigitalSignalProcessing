#include "Delay.h"

Delay::~Delay()
{
}

Delay::Delay() : Delay(0.0,0.0)
{
    
}

Delay::Delay(double delay) : Delay(delay,(int)delay)
{

}

Delay::Delay(double delay, int maxDelay) : SignalProcessor(propertyCount)
{

    setSampleDelay(0); // Initialize the sample delay
    setMaxDelay(maxDelay); // Set the maximum allowable delay
    initProperty(delayID,delay);
    setSmoothEnable(true);
}

void Delay::setDelay(double newDelay)
{
    setProperty(delayID, newDelay);
}

double Delay::process(double in)
{
    if (getProperty(delayID) > 2)
    {

        double outSample = read(getProperty(delayID));
        write(in);
        //std::cout << "after delay" << std::endl;
        return outSample; // Return the interpolated output sample
    }
    else
    {
        return in; // No delay applied, return the input as is
    }
}
double Delay::read(double delay)
{
        double index1 = floor(delay-1);
        double index2 = floor(delay);
        double ratio = 1 - ((delay) - index1);
        double sample1 = delayBuffer[int(index1)];
        double sample2 = delayBuffer[int(index2)] ;
        double outSample = sample1 * ratio + sample2 * (1 - ratio);
        return outSample;
}
double Delay::getCurrentDelay()
{
        return getProperty(delayID);
}
void Delay::write(double sample)
{
    delayBuffer.push_front_and_pop_back(sample); // Update the delay buffer
}
inline void Delay::setMaxDelay(int maxDelay)
{
    if (mMaxDelay == maxDelay)
    {
        return;
    }
    if (mMaxDelay == 0)
    {
        delayBuffer = CircularList<double>(maxDelay+1,0.0); // Initialize the delay buffer
    }
    else
    {
        while (delayBuffer.size()<maxDelay)
        {
            delayBuffer.push_back(0.0);
        }
        while (delayBuffer.size()>maxDelay)
        {
            delayBuffer.pop_back();
        }
    }
    mMaxDelay = maxDelay; // Set the maximum allowable delay
    //std::cout << this << std::endl;
    
}

void Delay::update()
{
    
}
