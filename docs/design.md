# Arstro DSP — Design Overview

> Design surface for the Arstro DSP library. Kept in sync with the code by the
> `implement_with_sync_and_test` skill (V-model: doc → design → code → unit test →
> integration test, all moving together).

## What this is

A modular, channel-aware Digital Signal Processing library (namespace `arstro`) and an
ESP32-targeted polyphonic synth built on top of it. Sources live under `src/`. The buildable,
tested core is the **synth subset** declared in [`../src/synth_dsp.h`](../src/synth_dsp.h).
The legacy `src/spatial/` modules and the libsndfile demo (`apps/wav_demo/`) are not part of
the verified build here.

## Layers

| Layer | Home (under `src/`) | Responsibility |
|-------|------|----------------|
| Config | `base/AudioConfig` | Single source of truth: sample rate, buffer size, channel count, bit depth. |
| Core | `base/SignalProcessor` | Per-sample dispatch `process(in, ch)` + smoothing schedule. |
| Params | `base/SmoothedParameter`, `ParameterSet` | Click-free parameter ramps (extracted from the core for SRP). |
| Composites | `base/Block`, `FeedbackBlock` | Per-channel chains; serial/parallel routing; feedback. |
| Sources | `base/SignalGenerator` → `generator/Oscillator` | Enveloped sound sources (own their ADSR). |
| Modules | `simpleProcessor/`, `equalizer/`, `reverb/`, `effects/`, `envelope/` | Concrete processors. |
| Engine | `synth/SynthEngine`, `VoiceManager`, `Voice` | Polyphony → shared effects → output. |
| Platform | `base/platform/` | Thin thread/mutex adapter (the only OS-specific code). |

See [`architecture.puml`](architecture.puml) for the class diagram (render with PlantUML).

## Key design rules (and the SOLID rationale)

- **One config authority (DIP).** Nothing stores its own sample rate / channel count;
  everything reads `AudioConfig::instance()`.
- **Shared params, per-channel state (LSP).** Parameters/targets are shared across channels
  and smoothed once per frame; running state (filter memory, oscillator phase, delay lines)
  is per-channel, resized in `onChannelCountChanged()`. Every processor honors
  `out(in, channel)` identically.
- **Smoothing is its own concern (SRP).** `SignalProcessor` decides *when* a ramp advances;
  `ParameterSet`/`SmoothedParameter` know *how* a value ramps. (This was inlined in the base
  before v1.0.0 — extracting it is the headline refactor.)
- **Extend by subclassing (OCP).** New waveforms/effects are new `SignalProcessor` /
  `SignalGenerator` subclasses; the base contract is closed for modification.
- **Audio thread never locks.** Control changes cross threads via `LockFreeQueue` (SPSC),
  drained at block boundaries.

## Concurrency

One audio (render) thread + one control (comms) thread. Graph construction happens before the
audio thread starts. Multi-core rendering splits the voice pool into disjoint halves
(`renderVoiceHalf`), then sums + runs shared effects single-core (`finishBlock`).

## Verification

- **Unit:** `bash unittest/buildSynthTests.sh` (dependency-free `MiniTest.h`). Split into
  behavioural tests (`synthTests.cpp`) and branch/guard coverage (`coverageTests.cpp`) —
  **100% line coverage** of every synth-path source (verified with `g++ --coverage` + `gcov`).
- **Integration:** `python3 tests/run_integration.py` (builds a WAV-emitting C++ harness,
  asserts feature behavior numerically — see [`../tests/`](../tests/)).
- **Both at once:** `cmake -S . -B build && cmake --build build && (cd build && ctest)`.
