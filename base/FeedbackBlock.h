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
    ~FeedbackBlock();

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