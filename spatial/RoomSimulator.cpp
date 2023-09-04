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
    mRoomSize = Coordinate(1.0, 1.0, 1.0);
    mMainSource.addGainListener([this]() {
        this->updateGain();
        });
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        mMainFilter[i].setIsParallel(true);
        mMainFilter[i].setNeedAverage(false);
        mEffectBlock[i].add(&mOffsetGainForReflect);
        mReflectorContainer[i].setIsParallel(true);
        mReflectorContainer[i].setNeedAverage(false);
        mEffectBlock[i].add(&mReflectorContainer[i]);
        for (int j = START_FROM; j < WALL_COUNT; j++)
        {
            mBeforeBounce[i][j].setMaxDistance(100.0);
            mBounceSource[j].setMaxDistance(100.0);
            mSingleReflect[i][j].add(&mBeforeBounce[i][j]);
            mSingleReflect[i][j].add(&mBounceSource[j].getFilter(i)); 
            mReflectorContainer[i].add(&mSingleReflect[i][j]);
            mBeforeBounce[i][j].setKeepGain(true);
            if (i == CHANNEL_COUNT-1 && j == WALL_COUNT-1)
                mBeforeBounce[i][j].addPropertyListener(this);
        }

        mMainFilter[i].add(&mMainSource.getFilter(i));
        mMainFilter[i].add(&mEffectBlock[i]);
    }
}

void RoomSimulation::prepare()
{
    mMainSource.prepare();
    for (int j = 0; j < WALL_COUNT; j++)
    {
        mBounceSource[j].prepare();
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

void RoomSimulation::updateReflector()
{
    for (int j = START_FROM; j < WALL_COUNT; j++)
    {

        mBounceSource[j].setDestination(mDestination);
        Coordinate middle;
        middle.set(j % 3 , 0 + j / 3 * mRoomSize.get(j%3));
        double mainSide = mSource.get(j % 3) - middle.get(j % 3);
        double ratioSide = mDestination.get(j % 3) - middle.get(j % 3);
        if ((mainSide + ratioSide) == 0)
        {
            return;
        }
        double ratio = mainSide /(mainSide+ ratioSide);
        double midSide = mSource.get((j + 1) % 3)*(1-ratio) + mDestination.get((j + 1) % 3)*ratio;
        double nextSide = mSource.get((j + 2) % 3) * (1 - ratio) + mDestination.get((j + 2) % 3) * ratio;
        middle.set((j+1) % 3, midSide);
        middle.set((j + 2) % 3, nextSide);
        
        mBounceSource[j].setSource(middle);
        
        for (int i = 0; i < CHANNEL_COUNT; i++)
        {
            mBeforeBounce[i][j].setSource(mSource);
            mBeforeBounce[i][j].setDestination(middle);
        }
    }
    
}

void RoomSimulation::updateGain()
{
    mOffsetGainForReflect.setGain(mMainSource.getCurrentGain());
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

    for (int j = START_FROM; j < WALL_COUNT; j++)
    {
        mBounceSource[j].setMaxDistance(maxDistance);
        for (int i = 0; i < CHANNEL_COUNT; i++)
        {
            mBeforeBounce[i][j].setMaxDistance(maxDistance);
        }
    }

}

void RoomSimulation::setKeepGain(bool keepGain)
{
    mMainSource.setKeepGain(keepGain);
}

void RoomSimulation::setNumberOfBounce(int numberOfBounce)
{
    mNumberOfBounce = numberOfBounce;
}

void RoomSimulation::onPropertyChange()
{
    for (int j = START_FROM; j < WALL_COUNT; j++)
    {
        double distance = mBeforeBounce[0][j].getDistance();
        mBounceSource[j].setOffsetDistance(distance);
    }
}
