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
    int mDepth = 1;
    std::vector<std::function<void()>> mReflectCalculatorList;
    Coordinate mRoomSize;
    Coordinate mSource;
    Coordinate mDestination;
    Block mMainFilter[2];
    Block mMainSourceWithMix[2];
    Gain mDryGain;
    Gain mWetGain;
    PositionSimulator mMainSource;
    Gain mOffsetGainForReflect[2];
    Block mEffectBlock[2];
    Block mReflectorContainer[2];
    double mDryMix=1.0;
    double mWetMix=1.0;
    std::vector<PositionSimulator> mBounceSource;
    void updateSingleReflector(int x, int y, int z, int index);
    void updateReflector();
    int getIndex(int x, int y, int z) const;
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
    void setDepth(int depth);
    virtual void onPropertyChange() override;
    void setDryMix(double dryMix);
    void setWetMix(double wetMix);
};