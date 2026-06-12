// Reference here https://signalsmith-audio.co.uk/writing/2021/lets-write-a-reverb/
#pragma once
#include "../simpleProcessor/Delay.h"
#include "../equalizer/HighPassFilter.h"
#include "../equalizer/LowPassFilter.h"
#include <array>
#include <cstdlib>
#include <cmath>

// Use like `Householder<Sample, 8>::inPlace(data)` - size must be ≥ 1
template <typename Sample, int size>
class Householder
{
    static constexpr Sample multiplier{-2.0 / size};

public:
    static void inPlace(Sample *arr)
    {
        Sample sum = 0;
        for (int i = 0; i < size; ++i)
        {
            sum += arr[i];
        }

        sum *= multiplier;

        for (int i = 0; i < size; ++i)
        {
            arr[i] += sum;
        }
    };
};

// Use like `Hadamard<Sample, 8>::inPlace(data)` - size must be a power of 2
template <typename Sample, int size>
class Hadamard
{
public:
    static inline void recursiveUnscaled(Sample *data)
    {
        if (size <= 1)
            return;
        constexpr int hSize = size / 2;

        // Two (unscaled) Hadamards of half the size
        Hadamard<Sample, hSize>::recursiveUnscaled(data);
        Hadamard<Sample, hSize>::recursiveUnscaled(data + hSize);

        // Combine the two halves using sum/difference
        for (int i = 0; i < hSize; ++i)
        {
            Sample a = data[i];
            Sample b = data[i + hSize];
            data[i] = (a + b);
            data[i + hSize] = (a - b);
        }
    }

    static inline void inPlace(Sample *data)
    {
        recursiveUnscaled(data);

        // The scaling factor is constant for a given size — compute once, not
        // per sample (this used to call sqrt on every Hadamard mix).
        static const Sample scalingFactor = (Sample)std::sqrt(1.0 / size);
        for (int c = 0; c < size; ++c)
        {
            data[c] *= scalingFactor;
        }
    }
};
Sample randomInRange(Sample low, Sample high);

struct SingleChannelFeedback {
	Sample delayMs = 80;
	Sample decayGain = 0.85;

	int delaySamples;
	Delay delay;
	
	void configure(Sample sampleRate) {
		delaySamples = delayMs*0.001*sampleRate;
		delay.setMaxDelay(delaySamples + 1);
		delay.prepare(); // Start with all 0s
	}
	
	Sample process(Sample input) {
		Sample delayed = delay.read(delaySamples);
		
		Sample sum = input + delayed*decayGain;
		delay.write(sum);
		
		return delayed;
	}
};


template<int channels=8>
struct MultiChannelFeedback {
	using Array = std::array<Sample, channels>;

	Sample delayMs = 150;
	Sample decayGain = 0.85;

	std::array<int, channels> delaySamples;
	std::array<Delay, channels> delays;
	
	void configure(Sample sampleRate) {
		Sample delaySamplesBase = delayMs*0.001*sampleRate;
		for (int c = 0; c < channels; ++c) {
			// Distribute delay times exponentially between delayMs and 2*delayMs
			Sample r = c*1.0/channels;
			delaySamples[c] = std::pow(2, r)*delaySamplesBase;
			
			delays[c].setMaxDelay(delaySamples[c] + 1);
			delays[c].prepare();
		}
	}
	
	Array process(Array input) {
		Array delayed;
		for (int c = 0; c < channels; ++c) {
			delayed[c] = delays[c].read(delaySamples[c]);
		}
		
		for (int c = 0; c < channels; ++c) {
			Sample sum = input[c] + delayed[c]*decayGain;
			delays[c].write(sum);
		}
		
		return delayed;
	}
};

template<int channels=8>
struct MultiChannelMixedFeedback {
	using Array = std::array<Sample, channels>;
	Sample delayMs = 150;
	Sample decayGain = 0.85;
	// Default cutoffs. These MUST be applied (configure) so each filter's coeff is
	// computed: the filter base ctor's update() is dispatched to the base no-op
	// (virtual-during-construction), leaving the coefficient uninitialized until a
	// cutoff is set — which made the reverb tail non-deterministic.
	Sample lowCutHz = 20.0;
	Sample highCutHz = 18000.0;
	HighPassFilter mLowCutFilter[channels];
	LowPassFilter mHighCutFilter[channels];
	std::array<int, channels> delaySamples;
	std::array<Delay, channels> delays;
	void setLowCutFrequency(Sample frequency)
	{
		lowCutHz = frequency;
		for (int c = 0; c < channels; ++c) {
			mLowCutFilter[c].setCutoffFrequency(frequency);
		}
	}
	void setHighCutFrequency(Sample frequency)
	{
		highCutHz = frequency;
		for (int c = 0; c < channels; ++c) {
			mHighCutFilter[c].setCutoffFrequency(frequency);
		}
	}
	void configure(Sample sampleRate) {
		Sample delaySamplesBase = delayMs*0.001*sampleRate;
		for (int c = 0; c < channels; ++c) {
			Sample r = c*1.0/channels;
			delaySamples[c] = std::pow(2, r)*delaySamplesBase;
			delays[c].setMaxDelay(sampleRate);
			delays[c].prepare();
			// Disable per-sample smoothing on the in-loop filters: otherwise every
			// .out() call re-runs the coefficient update() (divisions + M_PI),
			// which dominated the reverb cost (8 filters x both channels / sample).
			mLowCutFilter[c].setSmoothEnable(false);
			mHighCutFilter[c].setSmoothEnable(false);
			// Apply cutoffs so the filter coefficients are deterministically set.
			mLowCutFilter[c].setCutoffFrequency(lowCutHz);
			mHighCutFilter[c].setCutoffFrequency(highCutHz);
		}
	}
	
	Array process(Array input) {
		Array delayed;
		for (int c = 0; c < channels; ++c) {
			delayed[c] = delays[c].read(delaySamples[c]);
		}
		for (int c = 0; c < channels; ++c) {
			delayed[c] = mLowCutFilter[c].out(delayed[c]);
			delayed[c] = mHighCutFilter[c].out(delayed[c]);
		}
		// Mix using a Householder matrix
		Array mixed = delayed;
		Householder<Sample, channels>::inPlace(mixed.data());
		
		for (int c = 0; c < channels; ++c) {
			Sample sum = input[c] + mixed[c]*decayGain;
			delays[c].write(sum);
		}
		
		return delayed;
	}
};

template<int channels=8, int diffusionSteps=4>
struct BasicReverb {
	using Array = std::array<Sample, channels>;
	
	MultiChannelMixedFeedback<channels> feedback;
	Sample dry, wet;
    Sample mDelay;
    Sample mRt60;
	BasicReverb(Sample roomSizeMs, Sample rt60, Sample dry=0, Sample wet=1) : dry(dry), wet(wet) {
		mDelay = roomSizeMs;
        mRt60 = rt60;
	}
	void setLowCutFrequency(Sample frequency)
	{
		feedback.setLowCutFrequency(frequency);
	}
	void setHighCutFrequency(Sample frequency)
	{
		feedback.setHighCutFrequency(frequency);
	}
	void configure(Sample sampleRate) {
        feedback.delayMs = mDelay;

		// How long does our signal take to go around the feedback loop?
		Sample typicalLoopMs = mDelay*1.5;
		// How many times will it do that during our RT60 period?
		Sample loopsPerRt60 = mRt60/(typicalLoopMs*0.001);
		// This tells us how many dB to reduce per loop
		Sample dbPerCycle = -60/loopsPerRt60;

		feedback.decayGain = std::pow(10, dbPerCycle*0.05);
		feedback.configure(sampleRate);
		// diffuser.configure(sampleRate);
	}
	
	Array process(Array input) {
		// Array diffuse = diffuser.process(input);
		Array longLasting = feedback.process(input);
		Array output;
		for (int c = 0; c < channels; ++c) {
			output[c] = dry*input[c] + wet*longLasting[c];
		}
		return output;
	}
};