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
    setDepth(1);
    mBounceSource.resize(bounceIndexList.size());
    mRoomSize = Coordinate(1.0, 1.0, 1.0);
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (i % 2 == 0)
        {
            mMainFilter[i].setName("Room Left");
        }
        else
        {
            mMainFilter[i].setName("Room Right");
        }
        mOffsetGainForReflect[i].setName("offsetGain " + char(i + '0'));
        mReverb[i].setDecayInMs(500.0);
        mOffsetGainForReflect[i].setSmoothEnable(true);
        mReverbBlock[i].setIsParallel(false);
        mMainFilter[i].setIsParallel(true);
        mMainFilter[i].setNeedAverage(false);
        mEffectBlock[i].add(&mOffsetGainForReflect[i]);
        mEffectBlock[i].add(&mWetGain);
        mReflectorContainer[i].setIsParallel(true);
        mReflectorContainer[i].setNeedAverage(false);
        mEffectBlock[i].add(&mReflectorContainer[i]);
        mEffectBlock[i].add(&wetLowCut[i]);
        mEffectBlock[i].add(&wetHighCut[i]);
        mReverbBlock[i].add(&mReverb[i]);
        mReverbBlock[i].add(&mOffsetGainForReflect[i]);
        mReverbBlock[i].add(&mReverbGain);
        for (auto& bounceSource : mBounceSource)
        {
            bounceSource.setKeepGain(false);
            mReflectorContainer[i].add(&bounceSource.getFilter(i));
        }
        mMainSourceWithMix[i].setIsParallel(false);
        mMainSourceWithMix[i].add(&mDryGain);
        mMainSourceWithMix[i].add(&mMainSource.getFilter(i));
        mMainSource.getFilter(i).setName("PositionSimulator of main");
        mReverbBlock[i].setName("Reverb gain block of Room");
        mMainFilter[i].add(&mReverbBlock[i]);
        mMainFilter[i].add(&mMainSourceWithMix[i]);
        mMainFilter[i].add(&mEffectBlock[i]);
    }
    mReverbGain.setName("Reverb gain of Room");
    mWetGain.setName("wet gain of room");
    mDryGain.setName("Dry gain of Room");


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
    if (mBounceSource.size() > 1)
    {
        for (int i = 0; i < CHANNEL_COUNT; i++)
        {
            mReverb[i].setDelayInMs(maxDelay + (maxDelay -minDelay));
        }
    }
    onPropertyChange();
}

SignalProcessor& RoomSimulation::getFilter(int channel)
{
    return mMainFilter[channel];
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
    minDelay = 99999999.0;
    maxDelay = 0.0;
    for (int i = 0; i < CHANNEL_COUNT;i++)
    {
        double delay = mBounceSource[index].getCurrentDelayInMs(i);
        if (maxDelay < delay)
        {
            maxDelay = delay;
        }
        if (minDelay > delay)
        {
            minDelay = delay;
        }
    }

}

void RoomSimulation::updateReflector()
{
    for (int i = 0; i < bounceIndexList.size(); i++)
    {
        updateSingleReflector(bounceIndexList[i][0], bounceIndexList[i][1], bounceIndexList[i][2], i);
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
    updateGain();
}

void RoomSimulation::setDepth(int depth)
{
    if (mDepth != depth)
    {
        mDepth = depth;
        bounceIndexList.clear();
        for (int i = -mDepth; i < mDepth + 1; i++)
        {
            for (int j = -mDepth; j < mDepth + 1; j++)
            {
                for (int k = -mDepth; k < mDepth + 1; k++)
                {
                    if (i == 0 && j == 0 && k == 0)
                    {
                        continue;
                    }
                    if (abs(i) + abs(j) + abs(k) > mDepth)
                    {
                        continue;
                    }
                    bounceIndexList.push_back({ k,j,i });
                }
            }
        }
    }
}

void RoomSimulation::setDecayInMs(double decay)
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        mReverb[i].setDecayInMs(decay);
    }
}

void RoomSimulation::setReverbWet(double wet)
{
    mReverbGain.setGain(wet);
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

void RoomSimulation::setLowCutFrequency(double frequency)
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        wetLowCut[i].setCutoffFrequency(frequency);
        mReverb[i].setLowCutFrequency(frequency);
    }

}

void RoomSimulation::setHighCutFrequency(double frequency)
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        wetHighCut[i].setCutoffFrequency(frequency);
        mReverb[i].setHighCutFrequency(frequency);
    }
}
