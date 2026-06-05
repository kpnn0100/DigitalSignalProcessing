#include "Block.h"
#include "AudioConfig.h"
#include <iostream>

Block::Block()
{
    setSmoothEnable(false);
    ensureChannels();
}

Block::~Block()
{
}

void Block::ensureChannels()
{
    int n = gyrus_space::AudioConfig::instance().channelCount();
    if ((int)mChains.size() < n)
        mChains.resize(n);
}

void Block::onChannelCountChanged()
{
    ensureChannels();
}

void Block::prepare()
{
    for (auto &chain : mChains)
        for (auto processor : chain)
            processor->prepare();
}

void Block::update()
{
    // Serial sample-delay = sum along channel 0's chain (used for latency sync).
    if (mChains.empty())
        return;
    Sample delay = 0;
    for (auto processor : mChains[0])
        delay += processor->getSampleDelay();
    setSampleDelay(delay);
}

void Block::setIsParallel(bool newState)
{
    isParallel = newState;
}

void Block::setNeedAverage(bool needAverage)
{
    mNeedAverage = needAverage;
}

void Block::add(SignalProcessor *newProcessor)
{
    ensureChannels();
    for (auto &chain : mChains)
        chain.push_back(newProcessor);
    newProcessor->setParent(this);
    callUpdate();
}

void Block::add(SignalProcessor *newProcessor, int channel)
{
    ensureChannels();
    if (channel < 0 || channel >= (int)mChains.size())
        return;
    mChains[channel].push_back(newProcessor);
    newProcessor->setParent(this);
    callUpdate();
}

void Block::remove(SignalProcessor *processor)
{
    bool found = false;
    for (auto &chain : mChains)
    {
        for (int i = 0; i < (int)chain.size(); i++)
        {
            if (chain[i] == processor)
            {
                chain.erase(chain.begin() + i);
                found = true;
                break;
            }
        }
    }
    if (!found)
        std::cout << "There no processor in the list matched" << std::endl;
}

Sample Block::process(Sample in, int channel)
{
    if (channel < 0 || channel >= (int)mChains.size())
        return in;
    auto &chain = mChains[channel];
    Sample out = isParallel ? 0.0 : in;
    for (int i = 0; i < (int)chain.size(); i++)
    {
        if (isParallel)
        {
            Sample y = chain[i]->out(in, channel);
            out += mNeedAverage ? y / (Sample)chain.size() : y;
        }
        else
        {
            out = chain[i]->out(out, channel);
        }
    }
    return out;
}

void Block::processBlock(Sample *buf, int frames, int channel)
{
    if (isParallel)
    {
        SignalProcessor::processBlock(buf, frames, channel);
        return;
    }
    if (channel < 0 || channel >= (int)mChains.size())
        return;
    for (auto p : mChains[channel])
        if (!p->isBypassed()) // bypassed nodes are skipped entirely (zero cost)
            p->processBlock(buf, frames, channel);
}

vector<SignalProcessor *> &Block::getProcessorList()
{
    ensureChannels();
    return mChains[0];
}
