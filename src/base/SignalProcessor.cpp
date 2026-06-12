#include "SignalProcessor.h"
Sample SignalProcessor::mSampleRate = 48000;
int SignalProcessor::mBufferSize = 128;
std::vector<SignalProcessor*> SignalProcessor::signalProcessorInstanceList;
SignalProcessor::SignalProcessor() : SignalProcessor(0)
{
}
SignalProcessor::SignalProcessor(int propertyCount)
{
    mParams.resize(propertyCount);
    signalProcessorInstanceList.push_back(this);
    mBufferCounter = 0;
    setSampleDelay(0);
}

SignalProcessor::~SignalProcessor()
{
    for (int i = 0; i < signalProcessorInstanceList.size(); i++)
    {
        if (signalProcessorInstanceList[i] == this)
        {
            signalProcessorInstanceList.erase(signalProcessorInstanceList.begin() + i);
        }
    }
}

void SignalProcessor::notifyPropertyListener()
{
    for (auto listener : mPropertyListenerList)
    {
        listener->onPropertyChange();
    }
}

void SignalProcessor::onPropertyChanged(int propertyID,Sample value)
{
}

void SignalProcessor::smoothUpdate(Sample currentRatio)
{


}

void SignalProcessor::propertyInterpolation(Sample currentRatio)
{
    if (mBufferSize == mBufferCounter)
    {
        mParams.snapAll();
        return;
    }
    mParams.interpolate(currentRatio);
}

void SignalProcessor::initProperty(int propertyId, Sample value)
{
    mParams.init(propertyId, value);
}

void SignalProcessor::setBufferSize(Sample bufferSize)
{
    mBufferSize = bufferSize;
}
void SignalProcessor::callRecursiveUpdate()
{
    callUpdate();
    if (mParent != nullptr)
    {
        mParent->callRecursiveUpdate();
    }
}
void SignalProcessor::setProperty(int propertyId, Sample value)
{
    if (mParams.target(propertyId) != value)
    {
        savePropertyState();        // anchor the ramp at the current values
        mParams.setTarget(propertyId, value);

        //reset state for other property
        callUpdate();
        onPropertyChanged(propertyId, value);
    }

}
Sample SignalProcessor::getProperty(int propertyId)
{
    return mParams.current(propertyId);
}
Sample SignalProcessor::getPropertyTargetValue(int propertyId)
{
    return mParams.target(propertyId);
}
void SignalProcessor::onSampleRateChanged()
{
}
void SignalProcessor::onChannelCountChanged()
{
}
void SignalProcessor::notifyChannelCountChanged()
{
    for (int i = 0; i < signalProcessorInstanceList.size(); i++)
    {
        signalProcessorInstanceList[i]->onChannelCountChanged();
    }
}
void SignalProcessor::setSampleDelay(Sample newSampleDelay)
{
    mSampleDelay = newSampleDelay;
    if (mParent != nullptr)
    {
        mParent->callUpdate();
    }
}
inline bool SignalProcessor::updateBufferCounter()
{
    mBufferCounter++;

    if (mBufferCounter > mBufferSize)
    {
        mBufferCounter = mBufferSize;
        return false;
    }
    return true;
}

bool SignalProcessor::shouldSmoothUpdate()
{
    return mSmoothEnable && mBufferSize > 0;
}

void SignalProcessor::savePropertyState()
{
    mParams.beginRamp();
}

void SignalProcessor::setName(std::string name)
{
    nameOfFilter = name;
}

inline Sample SignalProcessor::calculateSmoothRatio()
{
    return static_cast<Sample>(mBufferCounter) / static_cast<Sample>(mBufferSize);
}

inline void SignalProcessor::performSmoothUpdate(Sample ratio)
{
    propertyInterpolation(ratio);
    smoothUpdate(ratio);
    update();
}
void SignalProcessor::notifyAllSignalProcessor()
{
    for (int i =0 ; i< signalProcessorInstanceList.size();i++)
    {
        signalProcessorInstanceList[i]->onSampleRateChanged();
    }
}
void SignalProcessor::callUpdate()
{
    mBufferCounter = 0;
    if (shouldSmoothUpdate())
    {
        performSmoothUpdate(0.0);
    }
    else
    {
        mBufferCounter = mBufferSize;
        performSmoothUpdate(1.0);
    }
    update();
    notifyPropertyListener();
}

void SignalProcessor::setSampleRate(Sample sampleRate)
{
    SignalProcessor::mSampleRate = sampleRate;
    notifyAllSignalProcessor();
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

/**
* @brief Prepare the processor before get into processing;
*
* This method should be overridden by subclasses to perform specific update operations.
*/

void SignalProcessor::prepare()
{

}

void SignalProcessor::processBlock(Sample *buf, int frames, int channel)
{
    // Default block path: per-sample. Hot modules override for a tight/SIMD loop.
    for (int i = 0; i < frames; ++i)
        buf[i] = out(buf[i], channel);
}

Sample SignalProcessor::out(Sample in, int channel)
{
    // Property smoothing advances once per frame: only on channel 0. Every
    // channel then reads the same smoothed parameter values. Per-channel running
    // state lives in the subclass and advances on every process() call.
    if (channel == 0 && shouldSmoothUpdate())
    {
        if (updateBufferCounter())
        {
            Sample ratio = calculateSmoothRatio();
            performSmoothUpdate(ratio);
            notifyPropertyListener();
        }
    }
    if (mBypass)
        return in;
    return process(in, channel);
}

Sample SignalProcessor::getSampleDelay()
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


