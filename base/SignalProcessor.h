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

/*
 *  Created by kpnn0100
 *  Version 1.0: stable property Interpolation feature 
 */
#pragma once

#include "CircularList.h"
#include "../util/Property.h"
#include "../util/IPropertyChangeListener.h"
#include "IStateEvent.h"
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
#include <string>

 /**
  * @brief A base signal processing module
  *
  * This class serves as the base for various signal processing modules in the Gyrus Space DSP Library.
  * It provides functionality for processing digital signals and audio data with support for
  * buffer size, sample rate, and smoothing updates.
  */
class SignalProcessor : public IStateEvent
{
private:
    /**< Pointer to the parent SignalProcessor, if any. */
    static std::vector<SignalProcessor*> signalProcessorInstanceList;
    std::string nameOfFilter;
    SignalProcessor* mParent = nullptr;
    bool mBypass = false;
    // Helper methods for smoothing and buffer management
    bool updateBufferCounter();
    
    double calculateSmoothRatio();
    void performSmoothUpdate(double ratio);
    static void notifyAllSignalProcessor();
protected:
    std::vector<IPropertyChangeListener*> mPropertyListenerList;
    std::vector<Property> mPropertyList;
    bool mSmoothEnable = true;
    /**< The sample delay for the signal processor.
        This is for synchronizing the delay when using parallel processors.
    */
    double mSampleDelay = 0;
    /**< The global sample rate for all signal processors. */
    static double mSampleRate;
    /**< The global BufferSize for all signal processors. */
    static int mBufferSize;
    /**< Buffer counter for smooth change of onPropertyUpdated */
    int mBufferCounter;
    /**
     * @brief Sets the sample delay for the signal processor.
     *
     * @param newSampleDelay The new sample delay to be set.
     */
    void setSampleDelay(double newSampleDelay);

    /**
     * @brief Calls the onPropertyUpdated method of the signal processor and its parent.
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

    virtual void onPropertyChanged(int propertyID, double value);

    /**
     * @brief Performs a smooth onPropertyUpdated of the signal processor's state.
     *
     * @param currentRatio The current ratio used for smoothing the onPropertyUpdated.
     */
    virtual void smoothUpdate(double currentRatio);
    void propertyInterpolation(double currentRatio);
    void initProperty(int propertyId, double value);
public:
    SignalProcessor();
    /**
     * @brief constructor for the SignalProcessor class.
     * @param number of property in derived class
     * Initializes the signal processor and calls the onPropertyUpdated method.
     */
    SignalProcessor(int propertyCount);

    /**
     * @brief Sets the buffer size for the signal processor and triggers an onPropertyUpdated.
     *
     * @param bufferSize The new buffer size to be set.
     */
    static void setBufferSize(double bufferSize);

    /**
     * @brief Sets the sample rate for the signal processor and triggers an onPropertyUpdated.
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
     * @brief Virtual method to onPropertyUpdated to apply the change of property.
     *
     * This method should be overridden by subclasses to perform specific onPropertyUpdated operations.
     */
    virtual void onPropertyUpdated();
    /**
     * @brief Prepare the processor before getting into processing
     *
     * This method should be overridden by subclasses to perform specific onInit operations.
     */
    virtual void onInit();
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
    void callRecursiveUpdate();
    void setProperty(int propertyId, double value);
    double getProperty(int propertyId);
    double getPropertyTargetValue(int propertyId);
    bool shouldSmoothUpdate();
    void savePropertyState();
    void setName(std::string name);
    virtual void onSampleRateChanged();
    /**
     * @brief Destructor for the SignalProcessor class.
     *
     * This destructor can be overridden by subclasses if needed.
     */
    virtual ~SignalProcessor();
};
