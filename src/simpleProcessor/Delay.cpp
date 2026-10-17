#include "Delay.h"

Delay::~Delay()
{
}

Delay::Delay() : Delay(0.0,0.0)
{
    
}

Delay::Delay(Sample delay) : Delay(delay,(int)delay)
{

}

Delay::Delay(Sample delay, int maxDelay) : SignalProcessor(propertyCount)
{

    setSampleDelay(0); // Initialize the sample delay
    setMaxDelay(maxDelay); // Set the maximum allowable delay
    initProperty(delayID,delay);
    setSmoothEnable(true);
}

void Delay::setDelay(Sample newDelay)
{
    setProperty(delayID, newDelay);
}

Sample Delay::process(Sample in, int /*channel*/)
{
    if (getProperty(delayID) > 2)
    {

        Sample outSample = read(getProperty(delayID));
        write(in);
        //std::cout << "after delay" << std::endl;
        return outSample; // Return the interpolated output sample
    }
    else
    {
        return in; // No delay applied, return the input as is
    }
}
Sample Delay::read(Sample delay)
{
        // between the samples ⌊delay⌋ and ⌊delay⌋ + 1 back in the buffer (its 0 the newest written), weighted 1 − f
        // and f: interpolated, its gain |(1 − f) + f·e^(−jω)| ≤ 1. It used to carry the line through ⌊delay⌋ − 1 and
        // ⌊delay⌋ on past the newer one — extrapolation, up to 1 + 2f toward Nyquist (Solaris D-18). A whole delay
        // reads the one sample it always did.
        const int index = int(floor(delay));
        const Sample f = delay - index;
        return delayBuffer[index] * (1 - f) + delayBuffer[index + 1] * f;
}
Sample Delay::getCurrentDelay()
{
        return getProperty(delayID);
}
void Delay::write(Sample sample)
{
    // Flush denormals so decaying feedback tails stay in the fast normal range.
    delayBuffer.push_front_and_pop_back(arstroFlush(sample));
}
void Delay::setMaxDelay(int maxDelay)
{
    if (mMaxDelay == maxDelay)
    {
        return;
    }
    if (mMaxDelay == 0)
    {
        delayBuffer = CircularList<Sample>(maxDelay+1,0.0); // Initialize the delay buffer
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
