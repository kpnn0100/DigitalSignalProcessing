#include "Gain.h"

Gain::Gain():Gain(1.0)
{

}

Gain::~Gain()
{
}

Gain::Gain(Sample newGain) : SignalProcessor(propertyCount)
{
    mSmoothEnable = false;
    initProperty(gainID, newGain);
}

void Gain::setGain(Sample gain)
{
    setProperty(gainID,gain);
    
}

Sample Gain::process(Sample in, int /*channel*/)
{
    return in * getProperty(gainID);
}

