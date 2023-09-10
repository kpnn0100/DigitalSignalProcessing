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
#include "../util/IPropertyChangeListener.h"
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>

 /**
  * @brief A base signal processing module
  *
  * This class serves as the base for various signal processing modules in the Gyrus Space DSP Library.
  * It provides functionality for processing digital signals and audio data with support for
  * buffer size, sample rate, and smoothing updates.
  */
class SignalProcessor
{
private:
    /**< Pointer to the parent SignalProcessor, if any. */
    SignalProcessor* mParent = nullptr;
    bool mBypass = false;
    // Helper methods for smoothing and buffer management
    bool updateBufferCounter();
    bool shouldSmoothUpdate();
    double calculateSmoothRatio();
    void performSmoothUpdate(double ratio);

protected:
    std::vector<IPropertyChangeListener*> mPropertyListenerList;
    bool mSmoothEnable = true;
    /**< The sample delay for the signal processor.
        This is for synchronizing the delay when using parallel processors.
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
     * This should be called when something affects its parent, for example, changing sampleDelay.
     */
    void callUpdate();

    /**
     * @brief Virtual method for processing an input signal.
     *
     * This method should be overridden by subclasses to perform specific signal processing operations.
     *
     * @param in The input signal value.
     * @return The processed output signal value.
     */
    virtual double process(double in) = 0;

    /**
     * @brief Notifies property listeners of property changes.
     */
    void notifyPropertyListener();

    /**
     * @brief Performs a smooth update of the signal processor's state.
     *
     * @param currentRatio The current ratio used for smoothing the update.
     */
    virtual void smoothUpdate(double currentRatio);

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
     * @param bufferSize The new buffer size to be set.
     */
    static void setBufferSize(double bufferSize);

    /**
     * @brief Sets the sample rate for the signal processor and triggers an update.
     *
     * @param sampleRate The new sample rate to be set.
     */
    static void setSampleRate(double sampleRate);

    /**
     * @brief Sets the parent SignalProcessor for this instance.
     *
     * @param parent Pointer to the parent SignalProcessor.
     */
    void setParent(SignalProcessor* parent);

    /**
     * @brief Adds a property listener to the signal processor.
     *
     * @param listener Pointer to the property change listener.
     */
    void addPropertyListener(IPropertyChangeListener* listener);

    /**
     * @brief Virtual method to update the state of the signal processor.
     *
     * This method should be overridden by subclasses to perform specific update operations.
     */
    virtual void update();
    /**
     * @brief Prepare the processor before getting into processing
     *
     * This method should be overridden by subclasses to perform specific prepare operations.
     */
    virtual void prepare();
    /**
     * @brief Processes an input signal and produces an output signal.
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
     * @brief Enables or disables smooth updates for the signal processor.
     *
     * @param smoothEnable Boolean indicating whether smooth updates should be enabled.
     */
    void setSmoothEnable(bool smoothEnable);

    /**
     * @brief Enables or disables bypass for the signal processor.
     *
     * @param bypass Boolean indicating whether bypass should be enabled.
     */
    void setBypass(bool bypass);
    /**
     * @brief Destructor for the SignalProcessor class.
     *
     * This destructor can be overridden by subclasses if needed.
     */
    virtual ~SignalProcessor();
};
