/*
  ==============================================================================

    LowPassFilterButterWorth.cpp
    Created: 13 Aug 2023 4:17:09pm
    Author:  PC

  ==============================================================================
*/

#include "LowPassFilterButterWorth.h"

double LowPassFilterButterWorth::process(double in)
{
    inputBuffer.push_back(in);
    if (inputBuffer.size() > mOrder) {
        inputBuffer.erase(inputBuffer.begin());
    }
    if (in != 0.0)
    {
        int a = 5;
    }
    double output = 0.0;
    for (int i = 0; i < mOrder; ++i) {
        output += bCoeffs[i] * inputBuffer[mOrder - 1 - i];
    }

    for (int i = 1; i < mOrder; ++i) {
        output -= aCoeffs[i] * outputBuffer[mOrder - 1 - i];
    }

    outputBuffer.push_back(output);
    if (outputBuffer.size() > mOrder) {
        outputBuffer.erase(outputBuffer.begin());
    }

    return output;
}

double LowPassFilterButterWorth::calculatePhaseDelay()
{
    return 0.0;
}

void LowPassFilterButterWorth::update()
{
    bCoeffs.resize(mOrder);
    aCoeffs.resize(mOrder);

    const double wc = 2.0 * M_PI * mCurrentCutoffFrequency;
    const double T = 1.0 / mSampleRate;

    for (int k = 1; k <= mOrder; ++k) {
        const double theta = M_PI * k / mOrder;
        const double alpha = sin(0.5 * theta) / cos(0.5 * theta);
        const double beta = 1.0 + alpha;

        bCoeffs[k - 1] = 1.0 / beta;
        aCoeffs[k - 1] = (1.0 - alpha) / beta;
    }
}


void LowPassFilterButterWorth::reset()
{
    inputBuffer = std::vector<double>(mOrder, 0.0);
    outputBuffer = std::vector<double>(mOrder, 0.0);
}

LowPassFilterButterWorth::LowPassFilterButterWorth()
{
    mOrder = 1;
    reset();
    callUpdate();
}

void LowPassFilterButterWorth::setOrder(int order)
{
    if (mOrder != order)
    {
        mOrder = order;
        reset();
        callUpdate();
    }
}
