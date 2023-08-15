#include "SignalProcessor.h"

double SignalProcessor::mSampleRate = 0;
int SignalProcessor::mBufferSize = 0;
void SignalProcessor::smoothUpdate()
{
}

SignalProcessor::SignalProcessor()
{
    callUpdate();
}
void SignalProcessor::setBufferSize(double bufferSize)
{
    mBufferSize = bufferSize;
}
void SignalProcessor::setSampleDelay(double newSampleDelay)
{
    mSampleDelay = newSampleDelay;
    if (mParent != nullptr)
    {
        mParent->callUpdate();
    }
}

void SignalProcessor::callUpdate()
{
    mBufferCounter = 0;
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

double SignalProcessor::out(double in)
{
    mBufferCounter++;
    smoothUpdate();
    if (mBufferCounter > mBufferSize)
    {
        mBufferCounter = mBufferSize;
    }
    return process(in);
}

double SignalProcessor::getSampleDelay()
{
    return mSampleDelay;
}

SignalProcessor::~SignalProcessor()
{
    // Destructor implementation, if needed
}
