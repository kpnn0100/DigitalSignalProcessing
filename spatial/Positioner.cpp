#include "Positioner.h"
const double Positioner::SPEED_OF_SOUND = 343.0;

const double Positioner::STANDARD_DISTANCE = 1.0;
Positioner::Positioner()
{
    mDelayFilter = Delay();
    mBlockFilter.add(&mDelayFilter);
    mBlockFilter.add(&mGainFilter);
    mDelayFilter.setParent(&mBlockFilter);
    mGainFilter.setParent(&mBlockFilter);
    mBlockFilter.setIsParallel(false);
}
double Positioner::out(double in)
{
    mCounter++;
    isNeedUpdate = true;
    updateDelaySample();
    
	return mBlockFilter.out(in);
}

void Positioner::setSmoothChange(bool smoothChange)
{
    mSmoothChange = smoothChange;
}
void Positioner::updateCounter()
{
    if (isNeedUpdate)
    {
        mOldDestination = mDestination;
        mOldSource = mSource;
        mLastCounter = mCounter;
        mCounter = 0;
        isNeedUpdate = false;
    }
    

}
void Positioner::setDestination(Coordinate destination)
{
    updateCounter();
    mDestination = destination;
    updateDelaySample();

}

void Positioner::setSource(Coordinate source)
{
    updateCounter();
    mSource = source;
    updateDelaySample();
}

void Positioner::setMaxDistance(double maxDistance)
{
    mMaxDistance = maxDistance;
    mDelayFilter.setMaxDelay((int)(maxDistance / SPEED_OF_SOUND * mSampleRate));
}



void Positioner::updateDelaySample()
{
    double ratio;
    if (mCounter > mLastCounter)
    {
        ratio = 1.0;
    }
    else
    if (mLastCounter == 0)
    {
        ratio = 1.0;
    }
    else
    {
        ratio = (double)(mCounter) / (double)mLastCounter;
    }
    if (mCounter > 478)
    {
        int a = 3;
    }
    Coordinate expectedSource = mOldSource * (1-ratio) + mSource * ratio;
    Coordinate expectedDestination = mOldDestination * (1 - ratio) + mDestination * ratio;
    double distanceToDelay;
    double distance;
    if (mSmoothChange)
    {
        distance = expectedSource.distanceTo(expectedDestination);
    }
    else
    {
        distance = mSource.distanceTo(mDestination);
    }
    distanceToDelay = distance / SPEED_OF_SOUND * mSampleRate;
    mCurrentDistance = distance;
    mDelayFilter.setDelay(distanceToDelay);
    updateGain();
}

double Positioner::getDistance()
{
    return mCurrentDistance;
}

void Positioner::updateGain()
{
    mCurrentGain = STANDARD_DISTANCE / mCurrentDistance;
    mGainFilter.setGain(mCurrentGain);
}

double Positioner::getGain()
{
    return mCurrentGain;
}

void Positioner::update()
{
    isNeedUpdate = true;
    mLastCounter = 0;
    mCounter = 0;
}


