/*
  ==============================================================================

    LowPassFilterBase.cpp
    Created: 27 Aug 2023 1:48:27pm
    Author:  PC

  ==============================================================================
*/

#include "LowPassFilterBase.h"
LowPassFilterBase::LowPassFilterBase() : LowPassFilterBase(10000.0) {}

LowPassFilterBase::LowPassFilterBase(double cutoffFrequency) : SignalProcessor(propertyCount)
{
    initProperty(cutoffFreqID,cutoffFrequency);
    mSmoothEnable = false;
    callUpdate();
}
void LowPassFilterBase::setCutoffFrequency(double freq)
{
    setProperty(cutoffFreqID, freq);
}