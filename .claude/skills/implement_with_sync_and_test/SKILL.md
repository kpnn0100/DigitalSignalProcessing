---
name: implement_with_sync_and_test
description: Use when implementing, changing, removing, or fixing ANY feature in the Arstro DSP library (this repo — namespace `arstro`, the channel-aware synth in synth_dsp.h). Enforces a V-model loop that keeps DOCS, DESIGN (PlantUML), CODE, UNIT TESTS (100% coverage), and the PYTHON integration tests in sync: check/create the doc first, update the .puml, implement, write + run unit tests, then run the Python feature tests. Invoke for tasks like "add an effect", "change the envelope", "fix the filter", "add a synth param".
---

# implement_with_sync_and_test

The single workflow for changing this repo. Nothing ships unless **doc, design, code, and
tests move together**. Skipping a stage is the bug.

## 0. Orient (facts about this repo)

- **Layout.** Library sources live under `src/` (one dir per module: `src/base`,
  `src/effects`, …). Aggregate headers are `src/synth_dsp.h` (channel-aware, supported) and
  `src/arstro_dsp.h` (legacy). Tests: `unittest/` (C++) + `tests/` (Python). Docs: `docs/` +
  per-module `README.md`. The demo app is `apps/wav_demo/`.
- **Namespace** `arstro`. Scalar type is `Sample` (`src/base/Sample.h`, `double`; `float`
  under `ARSTRO_USE_FLOAT`). Never hardcode `double`/`float` — use `Sample`.
- **Global config** is the single source of truth: `arstro::AudioConfig::instance()`
  (`sampleRate()`, `bufferSize()`, `channelCount()`, `outputBitDepth()`). Don't store your
  own copy of any of these.
- **Base class** `SignalProcessor` (`src/base/`): per-sample `process(Sample in, int channel)`,
  public `out(in, ch)`. Parameter smoothing is delegated to `ParameterSet` /
  `SmoothedParameter` (`src/base/SmoothedParameter.h`) — params are shared across channels,
  **running state (filter memory, phase, delay lines) is per-channel**, resized in
  `onChannelCountChanged()`.
- **No libsndfile** in this environment. The legacy `src/arstro_dsp.h` + `apps/wav_demo`
  (spatial, sndfile) do NOT build here. Work and test against the channel-aware subset:
  **`src/synth_dsp.h`**. The `src/spatial/` and `src/util/` modules are legacy/excluded.
- Each module dir under `src/` has a `README.md` — that is the doc surface.
  `docs/architecture.puml` is the design surface.
- **Build:** `cmake -S . -B build && cmake --build build && (cd build && ctest)` runs both
  suites; or use the scripts in §1 directly.

## 1. The V-model loop (do in order, every time)

Left side = specify & design (top→down). Right side = build & verify (bottom→up). Each right
stage validates the same-level left stage.

1. **DOC FIRST.** Find the doc for the area (the module `README.md`, or `docs/`). If it
   exists, read it and update it for the change. **If it doesn't exist, create it** before
   writing code. State the feature's contract: inputs, params (with units), channel behavior,
   what "correct" means.
2. **DESIGN (PlantUML).** Update `docs/architecture.puml` to reflect the new/changed classes
   and relationships *before* coding. Keep it the truth — a class in code with no box in the
   puml is an unsynced design.
3. **IMPLEMENT.** Follow the inheritance contract (§2). Match surrounding style (brace
   placement, `Sample`, doc-comments). Keep SOLID: new behavior = new subclass (OCP); one
   responsibility per class (SRP).
4. **UNIT TEST → 100% coverage.** Behavioural tests go in `unittest/synthTests.cpp`;
   branch/guard/edge-coverage tests go in `unittest/coverageTests.cpp` (harness
   `unittest/MiniTest.h`: `CHECK`, `CHECK_NEAR`; `main()` lives in synthTests.cpp, registry is
   shared). Cover every new branch/param/edge. Register any new `.cpp` in BOTH
   `unittest/buildSynthTests.sh` and the `synth_tests` target in `CMakeLists.txt`. If a line is
   genuinely unreachable (dead defensive guard, uncalled interface method), delete the dead
   code rather than fake a test for it.
5. **RUN unit tests** — `bash unittest/buildSynthTests.sh`. Must be 0 failed. To check coverage:
   build the same sources with `g++ --coverage -O0` and run `gcov` — every synth-path source
   must report `Lines executed:100.00%`.
6. **INTEGRATION TEST (Python, automated).** Add/refresh a feature test under `tests/`
   (see §3). The C++ render harness emits audio (raw/WAV bytes, no sndfile); Python asserts
   the DSP behaves (amplitude, frequency via FFT, envelope shape, filter attenuation, etc.).
7. **RUN integration** — `python3 tests/run_integration.py`. Must pass.
8. **SYNC CHECK** (§4). If any artifact lags, the task is not done.

## 2. How to add a processor (the contract)

```cpp
// in namespace arstro
class MyEffect : public SignalProcessor {
public:
    enum PropertyIndex { gainID, mixID, propertyCount };
    MyEffect() : SignalProcessor(propertyCount) { /* initProperty(gainID, 1.0); */ }
    void  setGain(Sample g) { setProperty(gainID, g); }   // wrap setProperty
    void  update() override { /* recompute coeffs from getProperty(...) */ }
    void  onChannelCountChanged() override { /* resize per-channel state */ }
protected:
    Sample process(Sample in, int channel) override {     // the DSP, per channel
        return in * getProperty(gainID);                  // getProperty = smoothed value
    }
};
```

Rules: inherit `SignalProcessor` (or `SignalGenerator` for sound sources, or `Block` for
composites); declare a `PropertyIndex` enum ending in `propertyCount`; pass it to the base
ctor; wrap params in named setters; recompute derived coefficients in `update()`; keep all
mutable running state **per channel**. Generators implement `generate(channel)` (envelope is
applied by the base).

## 3. Python integration tests (`tests/`)

- `tests/render_harness.cpp` — tiny C++ main built against `synth_dsp.h` that constructs a
  graph from CLI args / a small scenario and writes a WAV (header written by hand, no
  libsndfile). Built by `tests/run_integration.py`.
- `tests/run_integration.py` — compiles the harness, runs scenarios, loads the WAV with the
  stdlib `wave` module, and asserts feature behavior numerically (RMS/peak, zero-crossing or
  FFT frequency, monotonic ADSR segments, high/low-pass attenuation ratio, determinism).
  Self-contained: stdlib only (`wave`, `struct`, `math`); NumPy optional.

When you add a feature, add a scenario + assertion here. A feature with no integration
assertion is unverified.

## 4. Definition of done — the sync checklist

- [ ] Module `README.md` / `docs/` describes the feature (created if it was missing).
- [ ] `docs/architecture.puml` matches the code (classes, params, relationships).
- [ ] Code compiles in the synth path: `bash unittest/buildSynthTests.sh`.
- [ ] Unit tests cover **every** new line/branch; `0 failed`.
- [ ] `python3 tests/run_integration.py` passes with an assertion for the new behavior.
- [ ] Names/branding stay `arstro` (no `gyrus`/`GS_`/`gs` prefixes).

If you changed code but not the doc/puml/tests (or vice-versa), you are not done.
