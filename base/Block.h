/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  Block: a composite SignalProcessor holding a chain PER CHANNEL.
 *
 *  Channel-aware: each channel has its own ordered list of processors, so within
 *  one block channel 0 and channel 1 may run different chains (e.g. a different
 *  delay time on R than L). add(proc) appends to every channel; add(proc,channel)
 *  appends to one. process(in, channel) runs that channel's chain.
 *
 *  Modes: serial (default) feeds each processor's output into the next; parallel
 *  sums every processor's output (optionally averaged).
 */
#pragma once
#include "SignalProcessor.h"
#include <vector>
using namespace std;

class Block : public SignalProcessor
{
protected:
    bool isParallel = false;
    bool mNeedAverage = true;
    // One processor chain per channel: mChains[channel] = ordered processors.
    vector<vector<SignalProcessor *>> mChains;

    void ensureChannels();

public:
    Block();
    virtual ~Block();

    void prepare() override;
    void update() override;
    void onChannelCountChanged() override;

    void setIsParallel(bool newState);
    void setNeedAverage(bool needAverage);

    /** Append a processor to EVERY channel's chain. */
    void add(SignalProcessor *newProcessor);
    /** Append a processor to ONE channel's chain. */
    void add(SignalProcessor *newProcessor, int channel);
    /** Remove a processor from every channel's chain. */
    void remove(SignalProcessor *processor);

    Sample process(Sample in, int channel) override;
    void processBlock(Sample *buf, int frames, int channel) override;

    /** Channel 0's chain (compatibility accessor). */
    vector<SignalProcessor *> &getProcessorList();
};
