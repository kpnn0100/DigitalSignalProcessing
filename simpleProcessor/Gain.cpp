#include "Gain.h"

Gain::Gain():Gain(1.0)
{

}

Gain::~Gain()
{
}

Gain::Gain(double newGain) : SignalProcessor(propertyCount)
{
    mSmoothEnable = false;
    initProperty(gainID, newGain);
}

void Gain::setGain(double gain)
{
    setProperty(gainID,gain);
    
}

double Gain::process(double in)
{
    return in * getProperty(gainID);
}

