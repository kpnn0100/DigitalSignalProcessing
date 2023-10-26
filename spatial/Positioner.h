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

#include "../simpleProcessor/Delay.h"
#include "../simpleProcessor/Gain.h"
#include "../base/Block.h"
#include "../base/SignalProcessor.h"
#include "../util/Util.h"

#include <cmath>

/**
 * @brief A signal processing module for simulating sound source movement in space.
 *
 * The Positioner class simulates the movement of a sound source in space using delay and gain filters.
 * It calculates the necessary delay and gain adjustments based on the source and destination positions.
 */
class Positioner : public SignalProcessor
{
private:

    Coordinate mSource;
    Coordinate mDestination;
    double mDelaySample; /**< The delay sample value. */
    double mMaxDistance; /**< The maximum distance for delay calculation. */
    static const double SPEED_OF_SOUND; /**< The speed of sound in the medium. */
    double mCurrentDistance; /**< The current distance between source and destination. */
    double mCurrentGain; /**< The current gain adjustment factor. */
    static const double STANDARD_DISTANCE; /**< The standard reference distance. */
    Block mBlockFilter; /**< Block for managing delay and gain filters. */
    Delay mDelayFilter; /**< Delay filter for adjusting sound delay. */
    Gain mGainFilter; /**< Gain filter for adjusting sound gain. */
    bool mKeepGain = false;
    /**
     * @brief Processes the input signal while simulating sound source movement.
     *
     * @param in The input signal value.
     * @return The processed output signal value.
     */
    double process(double in) override;
public:
    enum PropertyIndex {
        sourceXId,
        sourceYId,
        sourceZId,
        destinationXId,
        destinationYId,
        destinationZId,
        offsetDistanceId,
        propertyCount
    };
    void setOffsetDistance(double offsetDistance);
    void setKeepGain(bool keepGain);
    void smoothUpdate(double currentRatio) override;
    /**
     * @brief Default constructor for the Positioner class.
     */
    Positioner();


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
    double getDistance();
    double getDelayInMs();
    /**
     * @brief Gets the current gain adjustment factor.
     *
     * @return The current gain factor.
     */
    double getGain();

    /**
     * @brief Updates the delay sample and delay filter settings.
     */
    void updateDelaySample();

    /**
     * @brief Updates the gain adjustment factor.
     */
    void updateGain();

    /**
     * @brief Overrides the base class onPropertyUpdated method.
     *
     * This method is called to reset position onPropertyUpdated parameters.
     */
    void onPropertyUpdated() override;
    double getTargetGain();
    Coordinate getCurrentSource();
    Coordinate getCurrentDestination();

    Coordinate getSource();
    Coordinate getDestination();
};
