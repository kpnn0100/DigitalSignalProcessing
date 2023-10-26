#include "Positioner.h"
const double Positioner::SPEED_OF_SOUND = 343.0; // Speed of sound in the medium (assumed in m/s)

const double Positioner::STANDARD_DISTANCE = 1.0; // Standard reference distance for gain calculation (in meters)

Positioner::Positioner() : SignalProcessor(propertyCount)
{
    // Initialize the Delay and Block filters for processing
    setSmoothEnable(false);
    initProperty(offsetDistanceId, 0.0);
    mBlockFilter.setParent(this);
    mDelayFilter = Delay(5000);
    mDelayFilter.setSmoothEnable(true);
    mBlockFilter.add(&mDelayFilter);
    mBlockFilter.add(&mGainFilter);
    mBlockFilter.setIsParallel(false); // Set the block to process filters serially
    mDelaySample = 0.0;

    callUpdate();
}

double Positioner::process(double in)
{
    return mBlockFilter.out(in); // Process the input through the Block filter
}




void Positioner::setKeepGain(bool keepGain)
{
    mKeepGain = keepGain;
}

void Positioner::smoothUpdate(double currentRatio)
{

}


void Positioner::setOffsetDistance(double offsetDistance)
{
    setProperty(offsetDistanceId, offsetDistance);
}

void Positioner::setDestination(Coordinate destination)
{
    mDestination = destination;
    callUpdate();
}

void Positioner::setSource(Coordinate source)
{
    mSource = source;
    callUpdate();
}

void Positioner::setMaxDistance(double maxDistance)
{
    mMaxDistance = maxDistance; // Set the maximum allowable distance for delay calculation
    // Update the maximum delay for the delay filter based on the maxDistance and sample rate
    mDelayFilter.setMaxDelay((int)(maxDistance / SPEED_OF_SOUND * mSampleRate));
}

void Positioner::updateDelaySample()
{
    // Calculate expected positions during transition

    double distanceToDelay;
    double distance;
     distance = mSource.distanceTo(mDestination); // Smooth transition

    // Calculate the distance to delay conversion and onPropertyUpdated the delay filter
    distanceToDelay = distance / SPEED_OF_SOUND * mSampleRate;
    mCurrentDistance = distance;
    mDelayFilter.setDelay(distanceToDelay);

}

double Positioner::getDistance()
{
    return mSource.distanceTo(mDestination); // Return the current distance
}

double Positioner::getDelayInMs()
{
    return (mSource.distanceTo(mDestination)+getProperty(offsetDistanceId))/SPEED_OF_SOUND * 1000.0;
}

void Positioner::updateGain()
{
    if (mKeepGain)
    {
        mCurrentGain = 1.0;
        mGainFilter.setGain(1.0);
        return;
    }
    if (mCurrentDistance == 0.0)
    {
        mCurrentGain = 1.0;
        mGainFilter.setGain(1.0);
    }
    else
    {
        mCurrentGain = STANDARD_DISTANCE / (mCurrentDistance+ getProperty(offsetDistanceId)); // Calculate the gain adjustment
        mGainFilter.setGain(mCurrentGain); // Update the gain filter with the new gain
    }


}

double Positioner::getGain()
{
    return mCurrentGain; // Return the current gain adjustment
}

void Positioner::onPropertyUpdated()
{
    updateDelaySample();
    updateGain();
}

double Positioner::getTargetGain()
{

    if (mKeepGain)
    {
        return 1.0;
    }
    else
    {
        return STANDARD_DISTANCE / (mSource.distanceTo(mDestination) + getPropertyTargetValue(offsetDistanceId));
        
     }
}

Coordinate Positioner::getCurrentSource()
{

    return mSource;
}

Coordinate Positioner::getCurrentDestination()
{
    return mDestination;
}

Coordinate Positioner::getSource()
{
    return mSource;
}

Coordinate Positioner::getDestination()
{
    return mDestination;
}
