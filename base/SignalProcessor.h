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
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
 /**
  * @brief A base signal processing module
  *
  */
class SignalProcessor
{
private:
    /**< Pointer to the parent SignalProcessor, if any. */
    SignalProcessor* mParent = nullptr; 

protected:
    /**< The sample delay for the signal processor. 
        This is for sync the delay when using parallel processor
    */
    double mSampleDelay = 0; 
    /**< The global sample rate for all signal processors. */
    static double mSampleRate; 
    /**< The global BufferSize for all signal processors. */
    static int mBufferSize;
    /**< Buffer counter for smooth change of update */
    int mBufferCounter;
    /**
     * @brief Sets the sample delay for the signal processor.
     *
     * @param newSampleDelay The new sample delay to be set.
     */
    void setSampleDelay(double newSampleDelay);

    /**
     * @brief Calls the update method of the signal processor and its parent.
     * should call this when something affect its parent, ex: changing sampleDelay
     */
    void callUpdate();
    /**
     * @brief overide this for sample processing.
     *
     * @param in The input signal value.
     * @return The processed output signal value.
     */
    
    virtual double process(double out) = 0;

    virtual void smoothUpdate();
public:
    /**
     * @brief Default constructor for the SignalProcessor class.
     *
     * Initializes the signal processor and calls the update method.
     */
    SignalProcessor();
    /**
     * @brief Sets the buffer size for the signal processor and triggers an update.
     *
     * @param sampleRate The new buffer size to be set.
     */
    void setBufferSize(double bufferSize);
    /**
     * @brief Sets the sample rate for the signal processor and triggers an update.
     *
     * @param sampleRate The new sample rate to be set.
     */
    void setSampleRate(double sampleRate);

    /**
     * @brief Sets the parent SignalProcessor for this instance.
     *
     * @param parent Pointer to the parent SignalProcessor.
     */
    void setParent(SignalProcessor* parent);

    /**
     * @brief Virtual method to update the state of the signal processor.
     *
     * This method should be overridden by subclasses to perform specific update operations.
     */
    virtual void update();
        /**
     * @brief Pure virtual method to process an input signal and produce an output signal.
     *
     * @param in The input signal value.
     * @return The processed output signal value.
     */
    double out(double in);
    /**
     * @brief Returns the sample delay for the signal processor.
     *
     * @return The sample delay value.
     */
    double getSampleDelay();

    /**
     * @brief Destructor for the SignalProcessor class.
     *
     * This destructor can be overridden by subclasses if needed.
     */
    virtual ~SignalProcessor();
};
