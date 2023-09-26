#include "Gain.h"

Gain::Gain():Gain(1.0)
{

}

Gain::~Gain()
{
}

Gain::Gain(double newGain)
{
    mCurrentGain = 1.0;
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
        callUpdate();
    }
    else
    {
        mGainFactor = gain;
        mOldGain = gain;
        mCurrentGain = gain;
        callUpdate();
    }
    
}

void Gain::smoothUpdate(double ratio)
{
    mCurrentGain = mOldGain * (1 - ratio) + mGainFactor * ratio;
}

double Gain::process(double in)
{
    return in * mCurrentGain;
}

