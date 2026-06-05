#include "FeedbackBlock.h"
#include "AudioConfig.h"

FeedbackBlock::FeedbackBlock() : SignalProcessor(propertyCount)
    , mForwardProcessor(nullptr), mFeedbackProcessor(nullptr)
{
    ensureChannels();
}

FeedbackBlock::~FeedbackBlock()
{
}

void FeedbackBlock::ensureChannels()
{
    int n = gyrus_space::AudioConfig::instance().channelCount();
    if ((int)lastOutput.size() != n)
        lastOutput.assign(n, 0.0);
}

void FeedbackBlock::onChannelCountChanged()
{
    ensureChannels();
}

void FeedbackBlock::prepare()
{
    for (auto &v : lastOutput)
        v = 0.0;
    if (mForwardProcessor != nullptr)
        mForwardProcessor->prepare();
    if (mFeedbackProcessor != nullptr)
        mFeedbackProcessor->prepare();
}

void FeedbackBlock::update()
{
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

void FeedbackBlock::setFeedbackGain(Sample gain)
{
    setProperty(feedbackGainID, gain);
}

Sample FeedbackBlock::process(Sample in, int channel)
{
    if (mForwardProcessor == nullptr || mFeedbackProcessor == nullptr)
        return in;
    if (channel < 0 || channel >= (int)lastOutput.size())
        return in;

    Sample preinput = in + getProperty(feedbackGainID) *
                               mFeedbackProcessor->out(lastOutput[channel], channel);
    lastOutput[channel] = mForwardProcessor->out(preinput, channel);
    return lastOutput[channel];
}
