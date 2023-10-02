#include "Positioner.h"
const double Positioner::SPEED_OF_SOUND = 343.0; // Speed of sound in the medium (assumed in m/s)

const double Positioner::STANDARD_DISTANCE = 1.0; // Standard reference distance for gain calculation (in meters)

Positioner::Positioner() : SignalProcessor(propertyCount)
{
    // Initialize the Delay and Block filters for processing
    setSmoothEnable(true);
    mDelayFilter = Delay(5000);
    mDelayFilter.setSmoothEnable(false);
    mBlockFilter.add(&mDelayFilter);
    mBlockFilter.add(&mGainFilter);
    mBlockFilter.setIsParallel(false); // Set the block to process filters serially
    mDelaySample = 0.0;

    smoothUpdate(1.0);
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
    updateDelaySample();
    updateGain();
}


void Positioner::setOffsetDistance(double offsetDistance)
{
    setProperty(offsetDistanceId, offsetDistance);
}

void Positioner::setDestination(Coordinate destination)
{
    for (int i = 0 ; i<3; i ++)
    {
        setProperty(destinationXId+ i, destination.get(i));
    }
}

void Positioner::setSource(Coordinate source)
{
    for (int i = 0 ; i<3; i ++)
    {
        setProperty(sourceXId+ i, source.get(i));
    }
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
    Coordinate source;
    Coordinate destination;
    for (int i = 0 ; i< 3; i++)
    {
        source.set(i, getProperty(sourceXId+i));
        destination.set(i, getProperty(destinationXId+i));
    }
    double distanceToDelay;
    double distance;
     distance = source.distanceTo(destination); // Smooth transition

    // Calculate the distance to delay conversion and update the delay filter
    distanceToDelay = distance / SPEED_OF_SOUND * mSampleRate;
    mCurrentDistance = distance;
    mDelayFilter.setDelay(distanceToDelay);

}

double Positioner::getDistance()
{
    Coordinate source;
    Coordinate destination;
    for (int i = 0 ; i< 3; i++)
    {
        source.set(i, getProperty(sourceXId+i));
        destination.set(i, getProperty(destinationXId+i));
    }
    return source.distanceTo(destination); // Return the current distance
}

double Positioner::getDelayInMs()
{
    Coordinate source;
    Coordinate destination;
    for (int i = 0 ; i< 3; i++)
    {
        source.set(i, getProperty(sourceXId+i));
        destination.set(i, getProperty(destinationXId+i));
    }
    return (source.distanceTo(destination)+getProperty(offsetDistanceId))/SPEED_OF_SOUND * 1000.0;
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

void Positioner::update()
{

}

double Positioner::getTargetGain()
{
    Coordinate source;
    Coordinate destination;
    for (int i = 0 ; i< 3; i++)
    {
        source.set(i, getPropertyTargetValue(sourceXId+i));
        destination.set(i, getPropertyTargetValue(destinationXId+i));
    }
    if (mKeepGain)
    {
        return 1.0;
    }
    else
    {
        return STANDARD_DISTANCE / (source.distanceTo(destination) + getPropertyTargetValue(offsetDistanceId));
        
     }
}

Coordinate Positioner::getCurrentSource()
{
    Coordinate source;
    for (int i = 0 ; i< 3; i++)
    {
        source.set(i, getProperty(sourceXId+i));
    }
    return source;
}

Coordinate Positioner::getCurrentDestination()
{
    Coordinate destination;
    for (int i = 0 ; i< 3; i++)
    {
        destination.set(i, getProperty(destinationXId+i));
    }
    return destination;
}

Coordinate Positioner::getSource()
{
    Coordinate source;
    for (int i = 0 ; i< 3; i++)
    {
        source.set(i, getPropertyTargetValue(sourceXId+i));
    }
    return source;
}

Coordinate Positioner::getDestination()
{
    Coordinate destination;
    for (int i = 0 ; i< 3; i++)
    {
        destination.set(i, getPropertyTargetValue(destinationXId+i));
    }
    return destination;
}
