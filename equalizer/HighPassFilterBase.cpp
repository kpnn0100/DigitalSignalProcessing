/*
  ==============================================================================

    LowPassFilterBase.cpp
    Created: 27 Aug 2023 1:48:27pm
    Author:  PC

  ==============================================================================
*/

#include "HighPassFilterBase.h"
HighPassFilterBase::HighPassFilterBase() : HighPassFilterBase(4000.0) {}

HighPassFilterBase::HighPassFilterBase(Sample cutoffFrequency) : SignalProcessor(propertyCount)
{
    initProperty(cutoffFreqID, cutoffFrequency);
    mSmoothEnable = false;
    callUpdate();
}
void HighPassFilterBase::setCutoffFrequency(Sample freq)
{
    setProperty(cutoffFreqID, freq);
}