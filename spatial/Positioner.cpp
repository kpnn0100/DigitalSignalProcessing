#include "Positioner.h"
const double Positioner::SPEED_OF_SOUND = 343.0; // Speed of sound in the medium (assumed in m/s)

const double Positioner::STANDARD_DISTANCE = 1.0; // Standard reference distance for gain calculation (in meters)

Positioner::Positioner()
{
    // Initialize the Delay and Block filters for processing
    mDelayFilter = Delay();
    mDelayFilter.setSmoothEnable(false);
    mSyncTarget = nullptr;
    mBlockFilter.add(&mDelayFilter);
    mBlockFilter.add(&mGainFilter);
    mBlockFilter.setIsParallel(false); // Set the block to process filters serially
}

double Positioner::process(double in)
{
    return mBlockFilter.out(in); // Process the input through the Block filter
}



void Positioner::smoothUpdate(double currentRatio)
{
    updateDelaySample(currentRatio);
    updateGain();
}


void Positioner::saveProperty()
{
    mOldDestination = mCurrentDestination;
    mOldSource = mCurrentSource;
}

void Positioner::setDestination(Coordinate destination)
{
    if (mDestination == destination)
    {
        return;
    }
    saveProperty();
    mDestination = destination; // Set the new destination
    callUpdate();
}

void Positioner::setSource(Coordinate source)
{
    if (mSource == source)
    {
        return;
    }
    saveProperty();
    mSource = source; // Set the new source
    callUpdate();
}

void Positioner::setMaxDistance(double maxDistance)
{
    mMaxDistance = maxDistance; // Set the maximum allowable distance for delay calculation
    // Update the maximum delay for the delay filter based on the maxDistance and sample rate
    mDelayFilter.setMaxDelay((int)(maxDistance / SPEED_OF_SOUND * mSampleRate));
}

void Positioner::updateDelaySample(double currentRatio)
{
    // Calculate expected positions during transition
    mCurrentSource = mOldSource * (1 - currentRatio) + mSource * currentRatio;
    mCurrentDestination = mOldDestination * (1 - currentRatio) + mDestination * currentRatio;
    double distanceToDelay;
    double distance;

    // Calculate the distance for delay and gain adjustment
    if (mSmoothEnable)
    {
        distance = mCurrentSource.distanceTo(mCurrentDestination); // Smooth transition
    }
    else
    {
        distance = mSource.distanceTo(mDestination); // Abrupt transition
    }

    // Calculate the distance to delay conversion and update the delay filter
    distanceToDelay = distance / SPEED_OF_SOUND * mSampleRate;
    mCurrentDistance = distance;
    mDelayFilter.setDelay(distanceToDelay);

}

double Positioner::getDistance()
{
    return mCurrentDistance; // Return the current distance
}

void Positioner::updateGain()
{

    mCurrentGain = STANDARD_DISTANCE / mCurrentDistance; // Calculate the gain adjustment
    mGainFilter.setGain(mCurrentGain); // Update the gain filter with the new gain

}

double Positioner::getGain()
{
    return mCurrentGain; // Return the current gain adjustment
}

void Positioner::update()
{
    if (mSmoothEnable)
    {
        smoothUpdate(0.0);
    }
    else
    {
        updateDelaySample(1.0);
        updateGain();
    }
}

Coordinate Positioner::getCurrentSource()
{
    return mCurrentSource;
}

Coordinate Positioner::getCurrentDestination()
{
    return mCurrentDestination;
}
