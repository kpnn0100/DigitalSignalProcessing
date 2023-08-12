/*
  ==============================================================================

    Gain.cpp
    Created: 12 Aug 2023 7:41:28pm
    Author:  PC

  ==============================================================================
*/

#include "Gain.h"

Gain::Gain()
{
    mGainFactor = 1;
}

Gain::~Gain()
{
}

Gain::Gain(double newGain)
{
    mGainFactor = newGain;
}

void Gain::setGain(double gain)
{
    mGainFactor = gain;
}

double Gain::out(double in)
{
    return in * mGainFactor;
}

