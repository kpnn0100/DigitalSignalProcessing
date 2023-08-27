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
#include "CircularList.h"
#include "SignalProcessor.h"
#include "../simpleProcessor/Delay.h"
#include <vector>
using namespace std;

/**
 * @brief A block for processing signals using a collection of SignalProcessors.
 *
 * This class represents a block that can process signals using a collection of SignalProcessors.
 * It provides the ability to add SignalProcessors and manage their processing behavior.
 */
class Block : public SignalProcessor
{
protected:
    bool isParallel = false; /**< Flag indicating whether the processors in the block run in parallel. */
    vector<SignalProcessor*> processorList; /**< List of SignalProcessors in the block. */
    vector<Delay> delaySyncMachine; /**< List of Delay instances for sample delay synchronization. */
    static constexpr double OUTPUT_MIN = -1.0;
    static constexpr double OUTPUT_MAX = 1.0;
public:
    /**
     * @brief Updates the state of the block and its contained processors.
     *
     * Update the delay machine for all children processor
     */
    void update();

    /**
     * @brief Sets the parallel processing mode of the block.
     * 
     * @param newState The new state of the parallel processing mode (true for parallel, false for serial).
     */
    void setIsParallel(bool newState);

    /**
     * @brief Adds a SignalProcessor to the block and sets this as its parent.
     *
     * @param newProcessor Pointer to the SignalProcessor to be added.
     */
    void add(SignalProcessor* newProcessor);

    /**
     * @brief Processes an input signal through the block's processors.
     *
     * @param in The input signal value.
     * @return The processed output signal value.
     */
    virtual double process(double in) override;
};
