#!/bin/bash
# Builds and runs the dependency-free synth unit tests (no gtest/libsndfile).
set -e
cd "$(dirname "$0")/.."   # DigitalSignalProcessing/

SRC="base/SignalProcessor.cpp base/AudioConfig.cpp base/SignalGenerator.cpp base/Block.cpp base/FeedbackBlock.cpp \
envelope/ADSREnvelope.cpp generator/Oscillator.cpp \
simpleProcessor/Delay.cpp simpleProcessor/Gain.cpp \
equalizer/LowPassFilter.cpp equalizer/LowPassFilterBase.cpp equalizer/HighPassFilter.cpp equalizer/HighPassFilterBase.cpp \
reverb/Reverb.cpp \
effects/Compressor.cpp effects/Overdrive.cpp effects/Chorus.cpp effects/Repeater.cpp \
synth/Voice.cpp synth/VoiceManager.cpp synth/SynthEngine.cpp"

g++ -std=c++17 -O2 -pthread -I. unittest/synthTests.cpp $SRC -o /tmp/synthTests
/tmp/synthTests
