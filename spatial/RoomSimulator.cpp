/*
  ==============================================================================

    RoomSimulator.cpp
    Created: 2 Sep 2023 4:51:55pm
    Author:  PC

  ==============================================================================
*/

#include "RoomSimulator.h"

RoomSimulation::RoomSimulation()
{
    //make a square surround main room
    int numberOfBounce;
    if (mDepth == 0)
    {
        numberOfBounce = 0;
    }
    else
    {
        numberOfBounce = (mDepth * 2 + 1) * (mDepth * 2 + 1) * (mDepth * 2 + 1) - 1;
    }
    mBounceSource.resize(numberOfBounce);
    mRoomSize = Coordinate(1.0, 1.0, 1.0);

    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        mOffsetGainForReflect[i].setSmoothEnable(true);
        mMainFilter[i].setIsParallel(true);
        mMainFilter[i].setNeedAverage(false);
        mEffectBlock[i].add(&mOffsetGainForReflect[i]);
        mEffectBlock[i].add(&mWetGain);
        mReflectorContainer[i].setIsParallel(true);
        mReflectorContainer[i].setNeedAverage(false);
        mEffectBlock[i].add(&mReflectorContainer[i]);
        for (auto& bounceSource : mBounceSource)
        {
            bounceSource.setKeepGain(false);
            mReflectorContainer[i].add(&bounceSource.getFilter(i));
        }
        mMainSourceWithMix[i].setIsParallel(false);
        mMainSourceWithMix[i].add(&mMainSource.getFilter(i));
        mMainSourceWithMix[i].add(&mDryGain);
        mMainFilter[i].add(&mMainSourceWithMix[i]);
        mMainFilter[i].add(&mEffectBlock[i]);
    }

}

void RoomSimulation::prepare()
{
    mMainSource.prepare();
    for (auto& bounceSource : mBounceSource)
    {
        bounceSource.prepare();
    }
}

void RoomSimulation::update()
{
    updateGain();
    updateReflector();
    onPropertyChange();
}

SignalProcessor& RoomSimulation::getFilter(int channel)
{
    return mMainFilter[channel];
}
int RoomSimulation::getIndex(int x, int y, int z) const
{
    int index = (x+ mDepth) + (y+ mDepth)*3 + (z+ mDepth)*3*3;
    if (index > mDepth + mDepth*3 + mDepth*3*3)
    {
        index -= 1;
    }
    return index;
}
void RoomSimulation::updateSingleReflector(int x, int y, int z,int index)
{
    //int index = getIndex(x, y, z);
    mBounceSource[index].setSource(mSource);
    Coordinate destination = mDestination;
    int coor[] = { x,y,z };
    for (int i = 0 ; i< 3; i++)
    {
        if (coor[i] % 2 == 0)
        {
            double newValue = destination.get(i) + ((double)coor[i]) * mRoomSize.get(i);
            destination.set(i, newValue);
        }
        else
        {
            double flipValue = mRoomSize.get(i) - destination.get(i);
            double newValue = flipValue + ((double)coor[i]) * mRoomSize.get(i);
            double flipNewValue = mSource.get(i) - (newValue - mSource.get(i));
            destination.set(i, flipNewValue);
        }    
    }
    mBounceSource[index].setDestination(destination);
}

void RoomSimulation::updateReflector()
{
    for (int i = -mDepth; i < mDepth +1; i++)
    {
        for (int j = -mDepth; j < mDepth + 1; j++)
        {
            for (int k = -mDepth; k < mDepth + 1; k++)
            {
                if (i == 0 && j == 0 && k == 0)
                {
                    continue;
                }
                updateSingleReflector(k, j, i, getIndex(k,j,i));
            }
        }
    }
    
}



void RoomSimulation::updateGain()
{
    for (int i = 0; i<CHANNEL_COUNT; i ++)
        mOffsetGainForReflect[i].setGain(mMainSource.getCurrentGain());
}

void RoomSimulation::setDestination(Coordinate destination)
{
    if (!(mDestination == destination))
    {
        mDestination = destination;
        mMainSource.setDestination(destination);
        update();

    }
    
}

void RoomSimulation::setSource(Coordinate source)
{
    if (!(mSource == source))
    {
        mSource = source;
        mMainSource.setSource(source);
        update();
    }   
}

void RoomSimulation::setRoomSize(int dimension, double value)
{
    if (mRoomSize.get(dimension) != value)
    {
        mRoomSize.set(dimension, value);
        update();
    }
    
}

void RoomSimulation::setMaxDistance(double maxDistance)
{
    mMainSource.setMaxDistance(maxDistance);

    for (auto& bounceSource : mBounceSource)
    {
        bounceSource.setMaxDistance(maxDistance);
    }

}

void RoomSimulation::setKeepGain(bool keepGain)
{
    mMainSource.setKeepGain(keepGain);
}

void RoomSimulation::setDepth(int depth)
{
    mDepth = depth;
}

void RoomSimulation::onPropertyChange()
{

}

void RoomSimulation::setDryMix(double dryMix)
{
    mDryMix = dryMix;
    mDryGain.setGain(dryMix);
}

void RoomSimulation::setWetMix(double wetMix)
{
    mWetMix = wetMix;
    mWetGain.setGain(wetMix);
}
