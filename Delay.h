#pragma once
#include "SignalProcessor.h"
#include <cmath>
class Delay : public SignalProcessor
{
private:
    CircularList<double> delayBuffer;
    double mainDelay;
    int mMaxDelay;
public:
    Delay();
    Delay(double delay);
    Delay(double delay, double maxDelay);
    void setDelay(double newDelay);
    double out(double in) override;
    void setMaxDelay(int maxDelay)
    {
        mMaxDelay = maxDelay;
        while (delayBuffer.size()<mMaxDelay)
        {
            delayBuffer.push_back(0.0);
        }
    }
};

#include "Delay.h"

Delay::Delay()
{
    setSampleDelay(0);
    setDelay(0);
}

Delay::Delay(double delay)
{
    setSampleDelay(0);
    setMaxDelay(delay);
    setDelay(delay);
}
Delay::Delay(double delay, double maxDelay)
{
    setSampleDelay(0);
    setMaxDelay(maxDelay);
    setDelay(delay);
}

void Delay::setDelay(double newDelay)
{
    if (newDelay>(double)mMaxDelay)
    {
        mainDelay = mMaxDelay;
    }
    else
    {
        mainDelay = newDelay;
    }

}
double Delay::out(double in)
{
    if (mainDelay>0)
    {
        //example delay = 1.3
        //index 1 = 0
        //index 2 = 1;
        double index1 = floor(mainDelay-1);
        double index2 = floor(mainDelay);
        //ratio of index1 is 0.7, index2 is 0.3
        double ratio = 1- ((mainDelay-1) - index1);
        //access to delay sample
        //outsample in this case is more index1 than  index2
        double outSample = delayBuffer[int(index1)]*ratio + delayBuffer[int(index2)]*(1-ratio);
        delayBuffer.push_front_and_pop_back(in);
        return outSample;
    } else return in;
}