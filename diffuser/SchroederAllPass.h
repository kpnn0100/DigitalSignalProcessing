#pragma once
#include "../base/SignalProcessor.h"
#include "../simpleProcessor/Delay.h"

class SchroederAllPass : public SignalProcessor
{
private:
    Delay mMainDelay;
    int mDelayIndex;
    double mDelayInMs;
    double last = 0.0;
    double mFeedbackCoefficient = 0.5;
public:
    SchroederAllPass();
    SchroederAllPass(double initialDelayInMilliseconds);
    void setFeedbackGain(double gain);
    void onSampleRateChanged() override;
    double process(double in) override;
    void setDelayInMs(double delayInMilliseconds);
};
