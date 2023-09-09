/*
  ==============================================================================

    RoomSimulator.h
    Created: 2 Sep 2023 4:51:55pm
    Author:  PC

  ==============================================================================
*/

#pragma once
#include "PositionSimulator.h"
class RoomSimulation : public IPropertyChangeListener
{
private:
    static const int CHANNEL_COUNT = 2;
    static const int WALL_COUNT = 6;
    static const int START_FROM = 0;
    Coordinate mRoomSize;
    Coordinate mSource;
    Coordinate mDestination;
    Block mMainFilter[2];
    Block mMainSourceWithMix[2];
    Gain mDryGain;
    Gain mWetGain;
    PositionSimulator mMainSource;
    Gain mOffsetGainForReflect;
    Block mEffectBlock[2];
    Block mReflectorContainer[2];
    Block mSingleReflect[CHANNEL_COUNT][WALL_COUNT];
    double mDryMix=1.0;
    double mWetMix=1.0;
    PositionSimulator mBounceSource[WALL_COUNT];
    Positioner mBeforeBounce[CHANNEL_COUNT][WALL_COUNT];
    int mNumberOfBounce = 1;
    void updateReflector();

public:
    RoomSimulation();
    void prepare();
    void update();
    SignalProcessor& getFilter(int channel);
    void updateGain();
    void setDestination(Coordinate destination);
    void setSource(Coordinate source);
    void setRoomSize(int dimension, double value);
    void setMaxDistance(double maxDistance);
    void setKeepGain(bool keepGain);
    void setNumberOfBounce(int numberOfBounce);
    virtual void onPropertyChange() override;
    void setDryMix(double dryMix);
    void setWetMix(double wetMix);
};