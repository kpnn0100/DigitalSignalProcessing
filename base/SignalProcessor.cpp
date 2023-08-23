#include "SignalProcessor.h"
double SignalProcessor::mSampleRate = 48000;
int SignalProcessor::mBufferSize = 128;
void SignalProcessor::notifyPropertyListener()
{
    for (auto listener : mPropertyListenerList)
    {
        listener->onPropertyChange();
    }
}
void SignalProcessor::smoothUpdate(double currentRatio)
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
void SignalProcessor::updateBufferCounter()
{
    mBufferCounter++;

    if (mBufferCounter > mBufferSize)
    {
        mBufferCounter = mBufferSize;
    }
}

bool SignalProcessor::shouldSmoothUpdate()
{
    return mSmoothEnable && mBufferSize > 0;
}

double SignalProcessor::calculateSmoothRatio()
{
    return static_cast<double>(mBufferCounter) / static_cast<double>(mBufferSize);
}

void SignalProcessor::performSmoothUpdate(double ratio)
{
    smoothUpdate(ratio);
}
void SignalProcessor::callUpdate()
{
    mBufferCounter = 0;
    update();
    if (mParent != nullptr)
    {
        mParent->callUpdate();
    }
    notifyPropertyListener();
}

void SignalProcessor::setSampleRate(double sampleRate)
{
    SignalProcessor::mSampleRate = sampleRate;
}

void SignalProcessor::setParent(SignalProcessor* parent)
{
    mParent = parent;
}

void SignalProcessor::addPropertyListener(IPropertyChangeListener* listener)
{
    mPropertyListenerList.push_back(listener);
}

void SignalProcessor::update()
{
    // Default implementation, can be overridden by subclasses
}

double SignalProcessor::out(double in)
{
    
    if (shouldSmoothUpdate())
    {
        updateBufferCounter();
        double ratio = calculateSmoothRatio();
        performSmoothUpdate(ratio);
        notifyPropertyListener();
    }
    if (mBypass)
        return in;
    return process(in);
}

double SignalProcessor::getSampleDelay()
{
    return mSampleDelay;
}

void SignalProcessor::setSmoothEnable(bool smoothEnable)
{
    mSmoothEnable = smoothEnable;
}

void SignalProcessor::setBypass(bool bypass)
{
    mBypass = bypass;
}

SignalProcessor::~SignalProcessor()
{
    // Destructor implementation, if needed
}
