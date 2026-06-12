/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  SmoothedParameter / ParameterSet: the parameter-smoothing concern, extracted
 *  from SignalProcessor (Single Responsibility). SignalProcessor used to own the
 *  three-value (last/target/current) storage AND the interpolation math inline;
 *  now it delegates to ParameterSet and only decides *when* a ramp advances.
 *
 *  Responsibility split:
 *   - SmoothedParameter : how ONE value ramps from `last` toward `target`.
 *   - ParameterSet      : the indexed collection + bulk ramp operations.
 *   - SignalProcessor   : *scheduling* (advance once per frame, on channel 0).
 */
#pragma once
#include "Sample.h"
#include <vector>

/**
 * @brief One smoothly-interpolated parameter.
 *
 * Three values are tracked so a parameter change is audible as a ramp rather
 * than a click:
 *   - target  : the value most recently requested (set instantly).
 *   - last    : the value at the start of the current ramp.
 *   - current : the value the audio path actually reads this sample.
 */
class SmoothedParameter
{
public:
    Sample last = 0.0;
    Sample target = 0.0;
    Sample current = 0.0;

    /** Set all three to the same value (no ramp — used at init). */
    void init(Sample value) { last = target = current = value; }

    /** Request a new destination. @return true if the target actually moved. */
    bool setTarget(Sample value)
    {
        if (target == value)
            return false;
        target = value;
        return true;
    }

    /** Anchor a new ramp at wherever `current` is right now. */
    void beginRamp() { last = current; }

    /** Linear-interpolate `current` between `last` and `target`. ratio in [0,1]. */
    void interpolate(Sample ratio)
    {
        current = last * (static_cast<Sample>(1) - ratio) + ratio * target;
    }

    /** Jump straight to `target` (ramp finished, or smoothing disabled). */
    void snap() { last = current = target; }
};

/**
 * @brief Fixed-size collection of SmoothedParameters addressed by integer id
 * (the subclass's PropertyIndex enum). Bundles the bulk operations so
 * SignalProcessor delegates instead of hand-rolling loops.
 */
class ParameterSet
{
public:
    void resize(int count) { mParams.resize(count); }
    int size() const { return static_cast<int>(mParams.size()); }

    void init(int id, Sample value) { mParams[id].init(value); }
    bool setTarget(int id, Sample value) { return mParams[id].setTarget(value); }

    Sample current(int id) const { return mParams[id].current; }
    Sample target(int id) const { return mParams[id].target; }

    /** Anchor every parameter's ramp at its current value. */
    void beginRamp()
    {
        for (auto &p : mParams)
            p.beginRamp();
    }

    /** Advance every parameter's ramp by `ratio`. */
    void interpolate(Sample ratio)
    {
        for (auto &p : mParams)
            p.interpolate(ratio);
    }

    /** Force every parameter to its target (no smoothing). */
    void snapAll()
    {
        for (auto &p : mParams)
            p.snap();
    }

private:
    std::vector<SmoothedParameter> mParams;
};
