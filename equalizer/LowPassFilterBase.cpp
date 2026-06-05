/*
  ==============================================================================

    LowPassFilterBase.cpp
    Created: 27 Aug 2023 1:48:27pm
    Author:  PC

  ==============================================================================
*/

#include "LowPassFilterBase.h"
LowPassFilterBase::LowPassFilterBase() : LowPassFilterBase(10000.0) {}

LowPassFilterBase::LowPassFilterBase(Sample cutoffFrequency) : SignalProcessor(propertyCount)
{
    initProperty(cutoffFreqID,cutoffFrequency);
    mSmoothEnable = false;
    callUpdate();
}
void LowPassFilterBase::setCutoffFrequency(Sample freq)
{
    setProperty(cutoffFreqID, freq);
}