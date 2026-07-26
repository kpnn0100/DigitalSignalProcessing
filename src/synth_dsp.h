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
#include "reverb/Reverb.h"

// Generators + envelope
#include "envelope/ADSREnvelope.h"
#include "generator/Oscillator.h"

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

// Output + engine
#include "output/OutputBlock.h"
#include "synth/ParamId.h"
#include "synth/Voice.h"
#include "synth/VoiceManager.h"
#include "synth/SynthEngine.h"
