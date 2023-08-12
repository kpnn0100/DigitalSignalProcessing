#pragma once

#include "Delay.h"
#include "Block.h"
#include "SignalProcessor.h"
#include "Util.h"
#include "Gain.h"
#include <cmath>
class Positioner : public SignalProcessor
{
private:
    Coordinate mSource;
    Coordinate mDestination;
    Coordinate mOldSource;
    Coordinate mOldDestination;
    double mDelaySample;
    double mMaxDistance;
    static const double SPEED_OF_SOUND;
    bool isNeedUpdate;
    bool mSmoothChange = true;
    int mCounter;
    int mLastCounter;
    double mCurrentDistance;
    double mCurrentGain;
    static const double STANDARD_DISTANCE; //m
    Block mBlockFilter;
    Delay mDelayFilter;
    Gain mGainFilter;
    void updateCounter();
public:
    Positioner();
    double out(double in) override;
    void setSmoothChange(bool smoothChange);
    void setSource(Coordinate source);
    void setDestination(Coordinate destination);
    void setMaxDistance(double maxDistance);
    double getDistance();
    double getGain();
    void updateDelaySample();
    void updateGain();
    void update() override;
};

