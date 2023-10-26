/*
  ==============================================================================

    PositionSimulator.cpp
    Created: 17 Aug 2023 8:55:34pm
    Author:  PC

  ==============================================================================
*/

#include "PositionSimulator.h"

PositionSimulator::PositionSimulator()
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (i % 2 == 0)
        {
            mMainFilter[i].setName("PositionSimulator Left");
        }
        else
        {
            mMainFilter[i].setName("PositionSimulator Right");
        }
        
        mMainFilter[i].setIsParallel(false);
        mOffsetGain[i].setSmoothEnable(true);
        mMainFilter[i].add(mOffsetGain + i);
        mMainFilter[i].add(mPositioner + i);
        mAcousticShadowFilter[i].setCutoffFrequency(CUTOFF_FREQUENCY);
        mMainFilter[i].add(mAcousticShadowBlock + i);
        mAcousticShadowBlock[i].setIsParallel(true);
        mAcousticShadowBlock[i].add(mLowpassAndGainBlock + i);
        mAcousticShadowBlock[i].add(mParallelGainForLowpass + i);
        mLowpassAndGainBlock[i].setIsParallel(false);
        mLowpassAndGainBlock[i].add(mAcousticShadowFilter + i);
        mLowpassAndGainBlock[i].add(mRatioGainForLowpass + i);
        mParallelGainForLowpass[i].setSmoothEnable(true);
        mRatioGainForLowpass[i].setSmoothEnable(true);
        mAcousticShadowFilter[i].callRecursiveUpdate();
    }
}
void PositionSimulator::setOffsetDistance(double offsetDistance)
{
    mOffsetDistance = offsetDistance;
    for (auto & positioner : mPositioner)
    {
        positioner.setOffsetDistance(mOffsetDistance);
    }
}
void PositionSimulator::onInit()
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        mMainFilter[i].onInit();
    }
}
double PositionSimulator::degreeToRatio(double degree, int channel)
{
    double ratio;
    if (channel == 1)
        degree = 180.0 - MAX_RATIO_DEGREE;
    if (degree <= MAX_RATIO_DEGREE)
    {
        if (degree >= MAX_RATIO_DEGREE - 180.0)
        {
            ratio = ( degree - ( MAX_RATIO_DEGREE - 180.0 )) / 180.0;
        }
        else
        {
            ratio = 1.0 -  ( degree - (MAX_RATIO_DEGREE - 360.0)) / 180.0;
        }
    }
    else
    {
        if (degree <= MAX_RATIO_DEGREE + 180.0)
        {
            ratio = 1.0 - (degree - MAX_RATIO_DEGREE) / 180.0;
        }
        else
        {
            ratio = (degree - (MAX_RATIO_DEGREE + 180.0)) / 180.0;
        }
    }
    ratio = MIN_SHADOW_RATIO + (MAX_SHADOW_RATIO - MIN_SHADOW_RATIO) * ratio;
    return ratio;
}

void PositionSimulator::setCurrentGain(double gain)
{
    if (gain != mCurrentGain)
    {
        mCurrentGain = gain;
        onCurrentGainChanged();
    }
}

void PositionSimulator::addGainListener(std::function<void()> callback)
{
    mGainListenerList.push_back(callback);
}

void PositionSimulator::onCurrentGainChanged()
{
    for (auto listener : mGainListenerList)
    {
        listener();
    }
}


SignalProcessor& PositionSimulator::getFilter(int channel)
{
    return mMainFilter[channel];
}

void PositionSimulator::setDestination(Coordinate destination)
{
    if (!(mDestination == destination))
    {
        mDestination = destination;
        for (int i = 0; i < CHANNEL_COUNT; i++)
        {
            mPositioner[i].setDestination(destination - Coordinate(EAR_DISTANCE / 2, 0, 0) + Coordinate(EAR_DISTANCE, 0, 0) * i);
        }
        updateGain();
    }
}

void PositionSimulator::setSource(Coordinate source)
{
    if (!(mSource == source))
    {
        mSource = source;
        for (int i = 0; i < CHANNEL_COUNT; i++)
        {
            mPositioner[i].setSource(source);
        }
        updateGain();
    }
}

void PositionSimulator::setMaxDistance(double maxDistance)
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        mPositioner[i].setMaxDistance(maxDistance);
    }
    updateGain();
}

void PositionSimulator::updateGain()
{
    double offsetGain = 1.0 / std::max(mPositioner[0].getTargetGain(), mPositioner[1].getTargetGain());
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (mKeepGain)
        {
            (mOffsetGain + i)->setGain(offsetGain);
            setCurrentGain(offsetGain);
        }
        else
        {
            (mOffsetGain + i)->setGain(1.0);
            setCurrentGain(1.0);
        }
        double ratio = degreeToRatio(mPositioner[i].getDestination().angleToOnXZPlane(mPositioner[i].getSource()), i);
        mParallelGainForLowpass[i].setGain(ratio);
        mRatioGainForLowpass[i].setGain((1 - ratio) * 2);
    }
}

void PositionSimulator::onPropertyChange()
{

}

double PositionSimulator::getCurrentGain()
{
    return mCurrentGain;
}

double PositionSimulator::getCurrentDelayInMs(int channel)
{
    return mPositioner[channel].getDelayInMs();
}

void PositionSimulator::setKeepGain(bool keepGain)
{
    if (keepGain != mKeepGain)
    {
        mKeepGain = keepGain;
        updateGain();
    }

}
