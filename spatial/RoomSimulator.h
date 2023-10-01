/*
  ==============================================================================

    RoomSimulator.h
    Created: 2 Sep 2023 4:51:55pm
    Author:  PC

  ==============================================================================
*/

#pragma once
#include "PositionSimulator.h"
#include "../reverb/Reverb.h"
class RoomSimulation : public IPropertyChangeListener
{
private:
    static const int CHANNEL_COUNT = 2;
    int mDepth = 0;
    std::vector<std::vector<int>> bounceIndexList;
    std::vector<std::function<void()>> mReflectCalculatorList;
    Coordinate mRoomSize;
    Coordinate mSource;
    Coordinate mDestination;
    double maxDelay = 0.0;
    double minDelay = 0.0;
    gyrus_space::Reverb mReverb[2];
    Block mReverbBlock[2];
    Block mMainFilter[2];
    Block mMainSourceWithMix[2];
    Gain mDryGain;
    Gain mWetGain;
    Gain mReverbGain;
    PositionSimulator mMainSource;
    Gain mOffsetGainForReflect[2];
    
    Block mEffectBlock[2];
    Block mReflectorContainer[2];
    double mDryMix=1.0;
    double mWetMix=1.0;
    std::vector<PositionSimulator> mBounceSource;
    void updateSingleReflector(int x, int y, int z, int index);
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
    void setDepth(int depth);
    void setDecayInMs(double decay);
    void setReverbWet(double wet);
    virtual void onPropertyChange() override;
    void setDryMix(double dryMix);
    void setWetMix(double wetMix);
};