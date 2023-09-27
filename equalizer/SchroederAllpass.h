#pragma once
#include "../base/SignalProcessor.h"
#include "../simpleProcessor/Delay.h"

class SchroederAllpass : public SignalProcessor
{
private:
    Delay mMainDelay;
    int mDelayIndex;
    double mDelayInMs;
    double last = 0.0;
    double mFeedbackCoefficient;
    int mMaxDelay; // Maximum delay in samples

public:
    SchroederAllpass();
    SchroederAllpass(double initialDelayInMilliseconds);

    void update() override;
    double process(double in) override;
    void setDelayInMs(double delayInMilliseconds);
};
