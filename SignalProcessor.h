#pragma once
#include "CircularList.h"
#include <math.h>
class SignalProcessor
{
private:
    double mSampleDelay=0;
    SignalProcessor * mParent;
protected:
    void setSampleDelay(double newSampleDelay)
    {
        mSampleDelay = newSampleDelay;
    }
    void callUpdate()
    {
        update();
        if (mParent != nullptr)
        {
            mParent->callUpdate();
        }
    }
public:
    void setParent(SignalProcessor * parent)
    {
        mParent = parent;
    }
    virtual void update()
    {

    }
    virtual double out(double in) = 0;
    double getSampleDelay()
    {
        return mSampleDelay;
    }

    virtual ~SignalProcessor(){}
};