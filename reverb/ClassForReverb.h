// Reference here https://signalsmith-audio.co.uk/writing/2021/lets-write-a-reverb/
#pragma once
#include "../simpleProcessor/Delay.h"
#include <array>
#include <cstdlib>
#include <cmath>

// Use like `Householder<double, 8>::inPlace(data)` - size must be ≥ 1
template <typename Sample, int size>
class Householder
{
    static constexpr Sample multiplier{-2.0 / size};

public:
    static void inPlace(Sample *arr)
    {
        double sum = 0;
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

// Use like `Hadamard<double, 8>::inPlace(data)` - size must be a power of 2
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
            double a = data[i];
            double b = data[i + hSize];
            data[i] = (a + b);
            data[i + hSize] = (a - b);
        }
    }

    static inline void inPlace(Sample *data)
    {
        recursiveUnscaled(data);

        Sample scalingFactor = std::sqrt(1.0 / size);
        for (int c = 0; c < size; ++c)
        {
            data[c] *= scalingFactor;
        }
    }
};
double randomInRange(double low, double high);

struct SingleChannelFeedback {
	double delayMs = 80;
	double decayGain = 0.85;

	int delaySamples;
	Delay delay;
	
	void configure(double sampleRate) {
		delaySamples = delayMs*0.001*sampleRate;
		delay.setMaxDelay(delaySamples + 1);
		delay.prepare(); // Start with all 0s
	}
	
	double process(double input) {
		double delayed = delay.read(delaySamples);
		
		double sum = input + delayed*decayGain;
		delay.write(sum);
		
		return delayed;
	}
};


template<int channels=8>
struct MultiChannelFeedback {
	using Array = std::array<double, channels>;

	double delayMs = 150;
	double decayGain = 0.85;

	std::array<int, channels> delaySamples;
	std::array<Delay, channels> delays;
	
	void configure(double sampleRate) {
		double delaySamplesBase = delayMs*0.001*sampleRate;
		for (int c = 0; c < channels; ++c) {
			// Distribute delay times exponentially between delayMs and 2*delayMs
			double r = c*1.0/channels;
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
			double sum = input[c] + delayed[c]*decayGain;
			delays[c].write(sum);
		}
		
		return delayed;
	}
};

template<int channels=8>
struct MultiChannelMixedFeedback {
	using Array = std::array<double, channels>;
	double delayMs = 150;
	double decayGain = 0.85;

	std::array<int, channels> delaySamples;
	std::array<Delay, channels> delays;
	
	void configure(double sampleRate) {
		double delaySamplesBase = delayMs*0.001*sampleRate;
		for (int c = 0; c < channels; ++c) {
			double r = c*1.0/channels;
			delaySamples[c] = std::pow(2, r)*delaySamplesBase;
			delays[c].setMaxDelay(sampleRate);
			delays[c].prepare();
		}
	}
	
	Array process(Array input) {
		Array delayed;
		for (int c = 0; c < channels; ++c) {
			delayed[c] = delays[c].read(delaySamples[c]);
		}
		
		// Mix using a Householder matrix
		Array mixed = delayed;
		Householder<double, channels>::inPlace(mixed.data());
		
		for (int c = 0; c < channels; ++c) {
			double sum = input[c] + mixed[c]*decayGain;
			delays[c].write(sum);
		}
		
		return delayed;
	}
};

template<int channels=8, int diffusionSteps=4>
struct BasicReverb {
	using Array = std::array<double, channels>;
	
	MultiChannelMixedFeedback<channels> feedback;
	double dry, wet;
    double mDelay;
    double mRt60;
	BasicReverb(double roomSizeMs, double rt60, double dry=0, double wet=1) : dry(dry), wet(wet) {
		mDelay = roomSizeMs;
        mRt60 = rt60;
	}
	
	void configure(double sampleRate) {
        feedback.delayMs = mDelay;

		// How long does our signal take to go around the feedback loop?
		double typicalLoopMs = mDelay*1.5;
		// How many times will it do that during our RT60 period?
		double loopsPerRt60 = mRt60/(typicalLoopMs*0.001);
		// This tells us how many dB to reduce per loop
		double dbPerCycle = -60/loopsPerRt60;

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