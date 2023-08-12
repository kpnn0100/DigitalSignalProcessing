#pragma once
#include "SignalProcessor.h"
#include <cmath>
class Delay : public SignalProcessor
{
private:
    CircularList<double> delayBuffer;
    double mainDelay;
    int mMaxDelay;
public:
    Delay();
    Delay(double delay);
    Delay(double delay, double maxDelay);
    void setDelay(double newDelay);
    double out(double in) override;
    void setMaxDelay(int maxDelay);
};

