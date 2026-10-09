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
| Sources | `base/SignalGenerator` → `generator/Oscillator`; `generator/Noise`, `generator/Phasor` | Enveloped sound sources (own their ADSR); seeded noise and a bare phase accumulator as plain primitives. |
| Modules | `simpleProcessor/`, `equalizer/`, `reverb/`, `effects/`, `envelope/` | Concrete processors — incl. `equalizer/Biquad` + `ParametricEQ` (RBJ EQ), `effects/Compressor` (with a sidechain key) and `effects/Limiter` (brickwall, lookahead), `equalizer/StateVariableFilter` (resonant, per-sample sweepable), `envelope/DecayEnvelope` (struck-sound T60 envelope). See [`../src/equalizer/README.md`](../src/equalizer/README.md). |
| Physical modeling | `physical/` | Struck-string synthesis (piano): modal resonators, nonlinear hammer contact, shared bridge/sympathetic coupling. See [`../src/physical/README.md`](../src/physical/README.md) for the full math. Not sample playback. |
| Engine | `synth/SynthEngine`, `VoiceManager`, `Voice` | Polyphony → shared effects → output (the ESP32 firmware synth). |
| Devices | `device/Device`, `DeviceRegistry` | Every instrument and effect behind one face; every parameter described once (name, unit, range, default, choices) — what a host reads instead of restating; its normalised 0…1 and its text derived once (`ParamMapping`). See [`../src/device/README.md`](../src/device/README.md). |
| VST3 plugins | `apps/vst3` (`Processor`, `Controller`, `RegistryParameter`) | Basic Synth and Drum Machine as VST3 instruments, wrapping the registry's devices; built only when the SDK is present. See [`../apps/vst3/README.md`](../apps/vst3/README.md). |
| Instruments | `instrument/Instrument` → `BasicSynth`, `DrumMachine` | Note-driven sources a host (the Solaris DAW) plays: blocks added into the host's buffers, host-side sample-accurate splitting. See [`../src/instrument/README.md`](../src/instrument/README.md). |
| Compute | `compute/` | Block-grain work scheduling: `ParallelExecutor` (interface) + `SerialExecutor` (default) + `ThreadPoolExecutor`, selected via `ComputeConfig`. No OS calls — built on the platform adapter. See [`parallel-architecture.md`](parallel-architecture.md). |
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
audio thread starts. Multi-core rendering splits the voice pool into disjoint **shards**
(`renderVoiceShard(shard, shardCount, frames)`, generalising the original two-way
`renderVoiceHalf`), then sums + runs shared effects single-core (`finishBlock`).

`renderBlockBytesParallel()` drives that split through `ComputeConfig::instance().executor()`.
Three rules govern it, and all three are requirements (`REQ-compute-*`) rather than conventions:

- **Parallel output is bit-identical to serial.** Shards touch disjoint state and `finishBlock`
  sums the accumulators in fixed shard order — float addition is not associative, so
  completion-order summation would make the audio depend on thread timing. Asserted by both a
  unit test and an integration check.
- **Parallelism is opt-in and off by default.** Measured, it does not yet pay for `SynthEngine`'s
  oscillator voices (break-even at 512 frames, 0.68× at 64) — a block is simply too little work
  to amortise fan-out. It is headroom for heavier graphs, not a free win.
- **Latency ≠ throughput.** Threads buy throughput; perceived lag is set by the output buffer.
  Adding threads to fix lag makes it worse.

Porting: implement `platform::Thread` and nothing else. A platform with no threads uses
`SerialExecutor` and is fully functional.

## Verification

- **Unit:** `bash unittest/buildSynthTests.sh` (dependency-free `MiniTest.h`). Split into
  behavioural tests (`synthTests.cpp`) and branch/guard coverage (`coverageTests.cpp`) —
  **100% line coverage** of every synth-path source (verified with `g++ --coverage` + `gcov`).
- **Integration:** `python3 tests/run_integration.py` (builds a WAV-emitting C++ harness,
  asserts feature behavior numerically — see [`../tests/`](../tests/)).
- **Both at once:** `cmake -S . -B build && cmake --build build && (cd build && ctest)`.
