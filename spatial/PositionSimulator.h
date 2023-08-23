/*
 *  Gyrus Space DSP Library
 *  Author: kpnn0100
 *  Organization: Gyrus Space
 *
 *  Description:
 *  This is an open-source Digital Signal Processing (DSP) library developed by Gyrus Space.
 *  It provides various functions and utilities for processing digital signals and audio data.
 *  The library is free to use and open for contributions from the community.
 *
 *  License: MIT License
 *  GitHub Repository: https://github.com/kpnn0100/DigitalSignalProcessing
 *
 *  Disclaimer: This library is provided as-is without any warranties. The author and organization
 *  shall not be held liable for any damages or liabilities arising from the use of this library.
 */
#pragma once

#include "../spatial/Positioner.h"
#include "../simpleProcessor/Delay.h"
#include "../simpleProcessor/Gain.h"
#include "../base/Block.h"
#include "../base/SignalProcessor.h"
#include "../equalizer/LowPassFilter.h"
#include "../util/Util.h"
#include "../util/IPropertyChangeListener.h"

#include <cmath>



class PositionSimulator : public IPropertyChangeListener
{
private:
    
    static constexpr double CUTOFF_FREQUENCY = 500;
    static constexpr double EAR_DISTANCE = 0.20;
    static constexpr double MAX_SHADOW_RATIO = 1.0;
    static constexpr double MIN_SHADOW_RATIO = 0.5;
    static constexpr double MAX_RATIO_DEGREE = 150.0;
    static constexpr int CHANNEL_COUNT = 2;
    bool mKeepGain = false;
    Coordinate mSource, mDestination, mCurrentSource, mCurrentDestination;
	Block mMainFilter[2];
	Positioner mPositioner[2];
	Gain mOffsetGain[2];
    Block mAcousticShadowBlock[2];
    Block mLowpassAndGainBlock[2];
	LowPassFilter mAcousticShadowFilter[2];
    Gain mRatioGainForLowpass[2];
    Gain mParallelGainForLowpass[2];
    double degreeToRatio(double degree, int channel);
public:
    PositionSimulator();
    void prepare();
	SignalProcessor& getFilter(int channel);
    /**
     * @brief Sets the destination position in space.
     *
     * @param destination The new destination coordinate.
     */
    void setDestination(Coordinate destination);

    /**
     * @brief Sets the source position in space.
     *
     * @param source The new source coordinate.
     */
    void setSource(Coordinate source);

    /**
     * @brief Sets the maximum allowable distance for delay calculation.
     *
     * @param maxDistance The maximum distance value.
     */
    void setMaxDistance(double maxDistance);

    /**
     * @brief Gets the current distance between the source and destination.
     *
     * @return The current distance value.
     */
    void onPropertyChange() override;
    void setKeepGain(bool keepGain);
};