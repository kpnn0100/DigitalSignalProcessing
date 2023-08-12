#include "SignalProcessor.h"

double SignalProcessor::mSampleRate = 0;

SignalProcessor::SignalProcessor()
{
    callUpdate();
}
void SignalProcessor::setSampleDelay(double newSampleDelay)
{
    mSampleDelay = newSampleDelay;
}

void SignalProcessor::callUpdate()
{
    update();
    if (mParent != nullptr)
    {
        mParent->callUpdate();
    }
}

void SignalProcessor::setSampleRate(double sampleRate)
{
    SignalProcessor::mSampleRate = sampleRate;
    callUpdate();
}

void SignalProcessor::setParent(SignalProcessor* parent)
{
    mParent = parent;
}

void SignalProcessor::update()
{
    // Default implementation, can be overridden by subclasses
}

double SignalProcessor::getSampleDelay()
{
    return mSampleDelay;
}

SignalProcessor::~SignalProcessor()
{
    // Destructor implementation, if needed
}
