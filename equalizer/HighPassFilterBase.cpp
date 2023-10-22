/*
  ==============================================================================

    LowPassFilterBase.cpp
    Created: 27 Aug 2023 1:48:27pm
    Author:  PC

  ==============================================================================
*/

#include "HighPassFilterBase.h"
HighPassFilterBase::HighPassFilterBase() : HighPassFilterBase(4000.0) {}

HighPassFilterBase::HighPassFilterBase(double cutoffFrequency) : SignalProcessor(propertyCount)
{
    initProperty(cutoffFreqID, cutoffFrequency);
    mSmoothEnable = false;
    callUpdate();
}
void HighPassFilterBase::setCutoffFrequency(double freq)
{
    setProperty(cutoffFreqID, freq);
}