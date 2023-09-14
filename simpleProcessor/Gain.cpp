#include "Gain.h"

Gain::Gain():Gain(1.0)
{

}

Gain::~Gain()
{
}

Gain::Gain(double newGain)
{
    mSmoothEnable = false;
    mOldGain = 1.0;
    mGainFactor = newGain;
}

void Gain::setGain(double gain)
{
    if (mSmoothEnable)
    {
        mOldGain = mCurrentGain;
        mGainFactor = gain;
    }
    else
    {
        mGainFactor = gain;
        mOldGain = gain;
        mCurrentGain = gain;
    }
    
}

void Gain::smoothUpdate(double ratio)
{
    mCurrentGain = mOldGain * (1 - ratio) + mGainFactor * ratio;
}

double Gain::process(double in)
{
    return in * mGainFactor;
}

