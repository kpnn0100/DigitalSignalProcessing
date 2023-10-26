#include "SignalProcessor.h"
double SignalProcessor::mSampleRate = 48000;
int SignalProcessor::mBufferSize = 128;
std::vector<SignalProcessor*> SignalProcessor::signalProcessorInstanceList;
SignalProcessor::SignalProcessor() : SignalProcessor(0)
{
}
SignalProcessor::SignalProcessor(int propertyCount)
{
    mPropertyList.resize(propertyCount);
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

void SignalProcessor::onPropertyChanged(int propertyID,double value)
{
}

void SignalProcessor::smoothUpdate(double currentRatio)
{


}

void SignalProcessor::propertyInterpolation(double currentRatio)
{
    if (mBufferSize == mBufferCounter)
    {
        for (int i = 0; i< mPropertyList.size();i++)
        {
            mPropertyList[i].last = mPropertyList[i].target;
            mPropertyList[i].current = mPropertyList[i].target;
        }
        return;
    }
    for (int i = 0; i< mPropertyList.size();i++)
    {
        mPropertyList[i].current =mPropertyList[i].last*(1-currentRatio) +  currentRatio*mPropertyList[i].target;
    }
}

void SignalProcessor::initProperty(int propertyId, double value)
{
    mPropertyList[propertyId].current = value;
    mPropertyList[propertyId].last = value;
    mPropertyList[propertyId].target = value;
}

void SignalProcessor::setBufferSize(double bufferSize)
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
void SignalProcessor::setProperty(int propertyId, double value)
{
    if (mPropertyList[propertyId].target != value)
    {
        savePropertyState();
        mPropertyList[propertyId].target = value;
        
        //reset state for other property
        callUpdate();
        onPropertyChanged(propertyId, value);
    }

}
double SignalProcessor::getProperty(int propertyId)
{
    return mPropertyList[propertyId].current;
}
double SignalProcessor::getPropertyTargetValue(int propertyId)
{
    return mPropertyList[propertyId].target;
}
void SignalProcessor::onSampleRateChanged()
{
}
void SignalProcessor::setSampleDelay(double newSampleDelay)
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
    for (int i = 0; i < mPropertyList.size(); i++)
    {
        mPropertyList[i].last = mPropertyList[i].current;
    }
}

void SignalProcessor::setName(std::string name)
{
    nameOfFilter = name;
}

inline double SignalProcessor::calculateSmoothRatio()
{
    return static_cast<double>(mBufferCounter) / static_cast<double>(mBufferSize);
}

inline void SignalProcessor::performSmoothUpdate(double ratio)
{
    propertyInterpolation(ratio);
    smoothUpdate(ratio);
    onPropertyUpdated();
}
void SignalProcessor::notifyAllSignalProcessor()
{
    for (int i =0 ; i< signalProcessorInstanceList.size();i++)
    {
        signalProcessorInstanceList[i]->onSampleRateChanged();
    }
}
inline void SignalProcessor::callUpdate()
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
    onPropertyUpdated();
    notifyPropertyListener();
}

void SignalProcessor::setSampleRate(double sampleRate)
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

void SignalProcessor::onPropertyUpdated()
{
    // Default implementation, can be overridden by subclasses
}

/**
* @brief Prepare the processor before get into processing;
*
* This method should be overridden by subclasses to perform specific onPropertyUpdated operations.
*/

inline void SignalProcessor::onInit()
{

}

double SignalProcessor::out(double in)
{
    
    if (shouldSmoothUpdate())
    {
        if (updateBufferCounter())
        {
            if (mBufferCounter == 479)
            {
                int break_here = 2;
            }
            double ratio = calculateSmoothRatio();
            performSmoothUpdate(ratio);
            notifyPropertyListener();
        }
        else
        {
            int breakHere = 2;
        }
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


