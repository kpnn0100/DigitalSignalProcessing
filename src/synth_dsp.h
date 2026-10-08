#pragma once
// ESP32DigitalSynth aggregate header — the channel-aware synth subset of the
// Arstro library. (The legacy arstro_dsp.h still includes the spatial
// modules, which are not yet migrated to the channel-aware base.)

// Foundation
#include "base/AudioConfig.h"
#include "base/SignalProcessor.h"
#include "base/SignalGenerator.h"
#include "base/Block.h"
#include "base/FeedbackBlock.h"
#include "base/LockFreeQueue.h"
#include "base/platform/Platform.h"

// Reused building blocks
#include "simpleProcessor/Delay.h"
#include "simpleProcessor/Gain.h"
#include "equalizer/LowPassFilter.h"
#include "equalizer/HighPassFilter.h"
#include "equalizer/Biquad.h"
#include "equalizer/ParametricEQ.h"
#include "equalizer/StateVariableFilter.h"
#include "reverb/Reverb.h"

// Generators + envelope
#include "envelope/ADSREnvelope.h"
#include "generator/Oscillator.h"
#include "generator/Noise.h"
#include "generator/Phasor.h"
#include "envelope/DecayEnvelope.h"

// Physical modeling (struck-string / piano) — see src/physical/README.md
#include "physical/StringResonator.h"
#include "physical/StringPartialBank.h"
#include "physical/HammerExciter.h"
#include "physical/PianoBridge.h"
#include "physical/LongitudinalBank.h"

// Parallel & accelerated compute — see docs/parallel-architecture.md
#include "compute/ParallelExecutor.h"
#include "compute/ThreadPoolExecutor.h"
#include "compute/ComputeConfig.h"
#include "physical/PianoVoice.h"

// Effects
#include "effects/Compressor.h"
#include "effects/Overdrive.h"
#include "effects/Chorus.h"
#include "effects/Repeater.h"

// Instruments a host plays (Solaris) — see instrument/README.md
#include "instrument/Instrument.h"
#include "instrument/BasicSynth.h"
#include "instrument/DrumMachine.h"

// Output + engine
#include "output/OutputBlock.h"
#include "synth/ParamId.h"
#include "synth/Voice.h"
#include "synth/VoiceManager.h"
#include "synth/SynthEngine.h"
