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
#include "../base/CircularList.h"
#include "../base/SignalProcessor.h"

/**
 * @brief A signal processing module for applying gain to an input signal.
 *
 * The Gain class allows amplifying or attenuating an input signal by a specified gain factor.
 */
class Gain : public SignalProcessor
{
protected:
    double mOldGain;
    double mCurrentGain;
    double mGainFactor; /**< The gain factor to apply to the input signal. */
    /**
     * @brief Processes the input signal by applying the gain factor.
     *
     * @param in The input signal value.
     * @return The output signal value after applying the gain.
     */
    virtual double process(double in) override;
public:
    /**
     * @brief Default constructor for the Gain class.
     *
     * Initializes the gain factor to 1.0 (no change).
     */
    Gain();

    /**
     * @brief Destructor for the Gain class.
     */
    ~Gain();

    /**
     * @brief Constructor for the Gain class with a specified gain factor.
     *
     * @param newGain The gain factor to apply to the input signal.
     */
    Gain(double newGain);

    /**
     * @brief Sets the gain factor for the Gain module.
     *
     * @param newGain The new gain factor to be set.
     */
    void setGain(double newGain);
    void smoothUpdate(double ratio);

};
