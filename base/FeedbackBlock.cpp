#include "FeedbackBlock.h"

FeedbackBlock::FeedbackBlock() : SignalProcessor(propertyCount)
    , mForwardProcessor(nullptr), mFeedbackProcessor(nullptr), lastOutput(0.0)
{
}

FeedbackBlock::~FeedbackBlock()
{
}

void FeedbackBlock::onInit()
{
    lastOutput = 0.0;
    if (mForwardProcessor != nullptr)
    {
        mForwardProcessor->onInit();
    }
    if (mFeedbackProcessor != nullptr)
    {
        mFeedbackProcessor->onInit();
    }
}

void FeedbackBlock::onPropertyUpdated()
{
    // Add any onPropertyUpdated logic here if needed
}

void FeedbackBlock::setForwardProcessor(SignalProcessor* forwardProcessor)
{
    mForwardProcessor = forwardProcessor;
    mForwardProcessor->setParent(this);
}

void FeedbackBlock::setFeedbackProcessor(SignalProcessor* feedbackProcessor)
{
    mFeedbackProcessor = feedbackProcessor;
    mFeedbackProcessor->setParent(this);
}

void FeedbackBlock::setFeedbackGain(double gain)
{
    setProperty(feedbackGainID, gain);
}

double FeedbackBlock::process(double in)
{
    if (mForwardProcessor == nullptr || mFeedbackProcessor == nullptr)
    {
        return in;
    }

    double preinput = in + getProperty(feedbackGainID) * mFeedbackProcessor->out(lastOutput);
    lastOutput = mForwardProcessor->out(preinput);
    return lastOutput;
}
