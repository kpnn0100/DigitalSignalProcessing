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
/**
 * @brief A feedback loop block for signal processing.
 *
 * This class extends SignalProcessor to create a block that can handle feedback loops.
 * It provides functionality to set the feedback gain and connect to both forward and feedback sources.
 */


/*                             ------------------
*                 ___          |                 |
input----------->| + |---->----|mForwardProcessor|--------------> output
                  ---          |_________________|         |
                   ^                                       |
                   |            ---------------------      |
                   |------<-----|                    |-----|
                                | mFeedbackProcessor |
                                |____________________|
*/
#pragma once
#include "SignalProcessor.h"
class FeedbackBlock : public SignalProcessor
{
public:
    FeedbackBlock();
    virtual ~FeedbackBlock();
    void prepare() override;
    void update();
    void setForwardProcessor(SignalProcessor* forwardProcessor);
    void setFeedbackProcessor(SignalProcessor* feedbackProcessor);
    void setFeedbackGain(double gain);
    double process(double in) override;
private:
    SignalProcessor* mForwardProcessor;
    SignalProcessor* mFeedbackProcessor;
    enum PropertyIndex {
        feedbackGainID,
        propertyCount
    };
    double lastOutput;
};