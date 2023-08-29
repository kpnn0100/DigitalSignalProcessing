/*
  ==============================================================================

    LowPassFilterBase.cpp
    Created: 27 Aug 2023 1:48:27pm
    Author:  PC

  ==============================================================================
*/

#include "LowPassFilterBase.h"
LowPassFilterBase::LowPassFilterBase() : LowPassFilterBase(10000.0) {}

LowPassFilterBase::LowPassFilterBase(double cutoffFrequency) {
    mCutoffFrequency = cutoffFrequency;
    mOldCutoffFrequency = cutoffFrequency;
    mCurrentCutoffFrequency = cutoffFrequency;
    mSmoothEnable = false;
    callUpdate();
}
void LowPassFilterBase::setCutoffFrequency(double freq)
{
    if (mCutoffFrequency != freq)
    {

        mCutoffFrequency = freq;
        mOldCutoffFrequency = mCurrentCutoffFrequency;
        if (mSmoothEnable)
        {
            smoothUpdate(0.0);
        }
        else
        {
            smoothUpdate(1.0);
        }

    }

}
void LowPassFilterBase::smoothUpdate(double currentRatio)
{
    double oldCutoffLog = log(mOldCutoffFrequency);
    double newCutoffLog = log(mCutoffFrequency);
    mCurrentCutoffFrequency = exp(oldCutoffLog * (1 - currentRatio) + newCutoffLog * currentRatio);
    update();
}