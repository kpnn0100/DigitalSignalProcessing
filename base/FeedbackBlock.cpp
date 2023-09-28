#include "FeedbackBlock.h"

FeedbackBlock::FeedbackBlock()
    : mForwardProcessor(nullptr), mFeedbackProcessor(nullptr), mFeedbackGain(1.0), lastOutput(0.0)
{
}

FeedbackBlock::~FeedbackBlock()
{
}

void FeedbackBlock::prepare()
{
    lastOutput = 0.0;
    if (mForwardProcessor != nullptr)
    {
        mForwardProcessor->prepare();
    }
    if (mFeedbackProcessor != nullptr)
    {
        mFeedbackProcessor->prepare();
    }
}

void FeedbackBlock::update()
{
    // Add any update logic here if needed
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
    mFeedbackGain = gain;
}

double FeedbackBlock::process(double in)
{
    if (mForwardProcessor == nullptr || mFeedbackProcessor == nullptr)
    {
        return in;
    }

    double preinput = in + mFeedbackGain * mFeedbackProcessor->out(lastOutput);
    lastOutput = mForwardProcessor->out(preinput);
    return lastOutput;
}
