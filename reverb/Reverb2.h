#pragma once
#include <cstdlib>
#include <cmath>
//from here https://github.com/Signalsmith-Audio/reverb-example-code/blob/main/reverb-example-code.h
// Use like `Householder<double, 8>::inPlace(data)` - size must be ≥ 1
template<typename Sample, int size>
class Householder {
	static constexpr Sample multiplier{-2.0/size};
public:
	static void inPlace(Sample *arr) {
		double sum = 0;
		for (int i = 0; i < size; ++i) {
			sum += arr[i];
		}

		sum *= multiplier;
		
		for (int i = 0; i < size; ++i) {
			arr[i] += sum;
		}
	};
};
// Use like `Hadamard<double, 8>::inPlace(data)` - size must be a power of 2
template<typename Sample, int size>
class Hadamard {
public: 
	static inline void recursiveUnscaled(Sample * data) {
		if (size <= 1) return;
		constexpr int hSize = size/2;

		// Two (unscaled) Hadamards of half the size
		Hadamard<Sample, hSize>::recursiveUnscaled(data);
		Hadamard<Sample, hSize>::recursiveUnscaled(data + hSize);

		// Combine the two halves using sum/difference
		for (int i = 0; i < hSize; ++i) {
			double a = data[i];
			double b = data[i + hSize];
			data[i] = (a + b);
			data[i + hSize] = (a - b);
		}
	}

	static inline void inPlace(Sample * data) {
		recursiveUnscaled(data);
		
		Sample scalingFactor = std::sqrt(1.0/size);
		for (int c = 0; c < size; ++c) {
			data[c] *= scalingFactor;
		}
	}
};
template<int channels=8>
struct DiffusionStep {
	using Array = std::array<double, channels>;
	double delayMsRange = 50;
	
	std::array<int, channels> delaySamples;
	std::array<Delay, channels> delays;
	std::array<bool, channels> flipPolarity;
	
	void configure(double sampleRate) {
		double delaySamplesRange = delayMsRange*0.001*sampleRate;
		for (int c = 0; c < channels; ++c) {
			double rangeLow = delaySamplesRange*c/channels;
			double rangeHigh = delaySamplesRange*(c + 1)/channels;
			delaySamples[c] = randomInRange(rangeLow, rangeHigh);
			delays[c].resize(delaySamples[c] + 1);
			delays[c].reset();
			flipPolarity[c] = rand()%2;
		}
	}
	
	Array process(Array input) {
		// Delay
		Array delayed;
		for (int c = 0; c < channels; ++c) {
			delays[c].write(input[c]);
			delayed[c] = delays[c].read(delaySamples[c]);
		}
		
		// Mix with a Hadamard matrix
		Array mixed = delayed;
		Hadamard<double, channels>::inPlace(mixed.data());

		// Flip some polarities
		for (int c = 0; c < channels; ++c) {
			if (flipPolarity[c]) mixed[c] *= -1;
		}

		return mixed;
	}
};

template<int channels=8, int stepCount=4>
struct DiffuserHalfLengths {
	using Array = std::array<double, channels>;

	using Step = DiffusionStep<channels>;
	std::array<Step, stepCount> steps;

	DiffuserHalfLengths(double diffusionMs) {
		for (auto &step : steps) {
			diffusionMs *= 0.5;
			step.delayMsRange = diffusionMs;
		}
	}
	
	void configure(double sampleRate) {
		for (auto &step : steps) step.configure(sampleRate);
	}
	
	Array process(Array samples) {
		for (auto &step : steps) {
			samples = step.process(samples);
		}
		return samples;
	}
};