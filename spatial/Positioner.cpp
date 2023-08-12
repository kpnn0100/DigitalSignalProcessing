#include "Positioner.h"
const double Positioner::SPEED_OF_SOUND = 343.0; // Speed of sound in the medium (assumed in m/s)

const double Positioner::STANDARD_DISTANCE = 1.0; // Standard reference distance for gain calculation (in meters)

Positioner::Positioner()
{
    // Initialize the Delay and Block filters for processing
    mDelayFilter = Delay();
    mBlockFilter.add(&mDelayFilter);
    mBlockFilter.add(&mGainFilter);
    mBlockFilter.setIsParallel(false); // Set the block to process filters serially
}

double Positioner::out(double in)
{
    mCounter++; // Increment the position transition counter
    isNeedUpdate = true; // Indicate that parameter update is needed
    updateDelaySample(); // Update the delay sample

    return mBlockFilter.out(in); // Process the input through the Block filter
}

void Positioner::setSmoothChange(bool smoothChange)
{
    mSmoothChange = smoothChange; // Set the position change smoothness flag
}

void Positioner::updateCounter()
{
    if (isNeedUpdate)
    {
        // Update the previous source and destination positions for smooth transition
        mOldDestination = mDestination;
        mOldSource = mSource;
        mLastCounter = mCounter;
        mCounter = 0; // Reset the counter
        isNeedUpdate = false; // Reset the update flag
    }
}

void Positioner::setDestination(Coordinate destination)
{
    updateCounter(); // Update previous positions and counters for transition
    mDestination = destination; // Set the new destination
    updateDelaySample(); // Update the delay sample based on the new position
}

void Positioner::setSource(Coordinate source)
{
    updateCounter(); // Update previous positions and counters for transition
    mSource = source; // Set the new source
    updateDelaySample(); // Update the delay sample based on the new position
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

    // Calculate the interpolation ratio based on position transition counters
    if (mCounter > mLastCounter)
    {
        ratio = 1.0; // Transition is ongoing
    }
    else if (mLastCounter == 0)
    {
        ratio = 1.0; // No previous transition
    }
    else
    {
        ratio = (double)(mCounter) / (double)mLastCounter; // Calculate the ratio of transition
    }

    // Calculate expected positions during transition
    Coordinate expectedSource = mOldSource * (1 - ratio) + mSource * ratio;
    Coordinate expectedDestination = mOldDestination * (1 - ratio) + mDestination * ratio;

    double distanceToDelay;
    double distance;

    // Calculate the distance for delay and gain adjustment
    if (mSmoothChange)
    {
        distance = expectedSource.distanceTo(expectedDestination); // Smooth transition
    }
    else
    {
        distance = mSource.distanceTo(mDestination); // Abrupt transition
    }

    // Calculate the distance to delay conversion and update the delay filter
    distanceToDelay = distance / SPEED_OF_SOUND * mSampleRate;
    mCurrentDistance = distance;
    mDelayFilter.setDelay(distanceToDelay);

    updateGain(); // Update the gain adjustment
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
    isNeedUpdate = true; // Indicate the need for parameter update
    mLastCounter = 0; // Reset the last counter
    mCounter = 0; // Reset the counter
}
