#include "Positioner.h"
const double Positioner::SPEED_OF_SOUND = 343.0; // Speed of sound in the medium (assumed in m/s)

const double Positioner::STANDARD_DISTANCE = 1.0; // Standard reference distance for gain calculation (in meters)

Positioner::Positioner()
{
    // Initialize the Delay and Block filters for processing
    mDelayFilter = Delay();
    mSyncTarget = nullptr;
    mBlockFilter.add(&mDelayFilter);
    mBlockFilter.add(&mGainFilter);
    mBlockFilter.add(&mOffsetGainFilter);
    mBlockFilter.setIsParallel(false); // Set the block to process filters serially
}

double Positioner::process(double in)
{
    return mBlockFilter.out(in); // Process the input through the Block filter
}

void Positioner::setSmoothChange(bool smoothChange)
{
    mSmoothChange = smoothChange; // Set the position change smoothness flag
}


void Positioner::smoothUpdate()
{
    updateDelaySample();
    updateGain();
}

void Positioner::setKeepGain(bool keepGain)
{
    isKeepGain = keepGain;
}


void Positioner::setDestination(Coordinate destination)
{
    mOldDestination = mCurrentDestination;
    mDestination = destination; // Set the new destination
    callUpdate();
}

void Positioner::setSource(Coordinate source)
{
    mOldSource = mCurrentSource;
    mSource = source; // Set the new source
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
    double ratio;
    if (mBufferSize == 0)
    {
        ratio = 1.0; // No previous transition
    }
    else
    {
        ratio = (double)mBufferCounter / (double)mBufferSize;
    }

    // Calculate expected positions during transition
    mCurrentSource = mOldSource * (1 - ratio) + mSource * ratio;
    mCurrentDestination = mOldDestination * (1 - ratio) + mDestination * ratio;

    double distanceToDelay;
    double distance;

    // Calculate the distance for delay and gain adjustment
    if (mSmoothChange)
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
    if (isKeepGain)
    {
        mGainFilter.setGain(1.0);
    }
    else
    {
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
