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
#include "../base/SignalProcessor.h"
#include <cmath>

/**
 * @brief A signal processing module for introducing a delay to the input signal.
 *
 * The Delay class allows delaying the input signal by a specified number of samples.
 * 
 * This will have linear interpolation between delay samples
 */
class Delay : public SignalProcessor
{
protected:
    CircularList<double> delayBuffer; /**< Circular buffer to store delayed samples. */
    double mainDelay; /**< The main delay value for the input signal. */
    int mMaxDelay; /**< The maximum allowable delay value. */
    double mOldDelay;
    double mCurrentDelay;
    /**
     * @brief Processes the input signal with the applied delay.
     *
     * @param in The input signal value.
     * @return The output signal value after applying the delay.
     */
    double process(double in) override;
public:
    /**
     * @brief Default constructor for the Delay class.
     *
     * Initializes the delay buffer and sets the delay to zero.
     */
    Delay();

    /**
     * @brief Constructor for the Delay class with a specified delay.
     *
     * Initializes the delay buffer and sets the delay to the specified value.
     *
     * @param delay The delay value in samples.
     */
    Delay(double delay);

    /**
     * @brief Constructor for the Delay class with a specified delay and maximum delay.
     *
     * Initializes the delay buffer, sets the delay to the specified value,
     * and sets the maximum allowable delay.
     *
     * @param delay The delay value in samples.
     * @param maxDelay The maximum allowable delay value.
     */
    Delay(double delay, int maxDelay);

    /**
     * @brief Sets the delay for the Delay module.
     *
     * @param newDelay The new delay value in samples.
     */
    void setDelay(double newDelay);

    /**
     * @brief Sets the maximum allowable delay value.
     *
     * @param maxDelay The maximum delay value in samples.
     */
    void setMaxDelay(int maxDelay);
    void smoothUpdate(double currentRatio) override;
    void update() override;
};
