# arstro_kitchen_sink

An interactive bench for auditioning the Arstro DSP processors. Two front-ends share the same
C++ DSP:

1. **Visual web UI (WASM)** — `web/` + `RackEngine`: pick a signal generator, edit its ADSR
   envelope visually, build an effect **chain** (add / remove / reorder), tweak every node's
   parameters with knobs, and watch a live **waveform + spectrum**. Plays through the browser's
   Web Audio (your system default output). See "Visual web UI" below.
2. **Headless CLI** — `main.cpp`: the full `SynthEngine` instrument streamed to stdout/WAV (for
   servers/CI). See "Headless CLI" below.

---

## Visual web UI (WASM)

The real Arstro DSP is compiled to WebAssembly with Emscripten and driven by a small HTML/Canvas
UI. Architecture:

```
 web/index.html + app.js + style.css        (UI: chain builder, ADSR editor, scopes)
            │  embind
            ▼
 apps/kitchen_sink/web_bindings.cpp          (JS ⇄ C++ glue)
            ▼
 apps/kitchen_sink/RackEngine                (generator → ordered effect chain; render)
            ▼
 src/ … the Arstro DSP library (real processors)
            ▼
 Web Audio ScriptProcessorNode + AnalyserNode → system default output
```

`RackEngine` owns one `Oscillator` (with its ADSR) feeding a **user-ordered list of effect
nodes**. The node catalogue (types + each param's name/min/max/default) is published to JS as
JSON (`nodeTypesJson`), so the UI builds its palette and knobs dynamically — adding a processor
type to the catalogue automatically gives it UI (OCP).

### Build & run

Install Emscripten once (`emcc`), then:

```bash
cd apps/kitchen_sink
./build_web.sh                 # emits web/arstro.js + web/arstro.wasm
cd web && python3 -m http.server 8000
# open http://localhost:8000  — click "Start audio", then play with A–K keys
```

> The C++ side (`RackEngine`) is verified natively in CI (CTest `rack_smoke`); the WASM build and
> browser UI must be built/run on your machine (this repo's CI has no `emcc`, browser, or audio
> device).

### UI features

- **Generator:** waveform, frequency, unison voices/detune, and a **draggable ADSR editor**
  (attack/decay/sustain/release shown as an envelope curve).
- **Chain:** a palette of processors (Gain, Delay, LowPass, HighPass, Chorus, Overdrive,
  Compressor, Repeater, Reverb); add to the rack, drag to reorder, remove. Each node shows knobs
  generated from its parameter descriptors.
- **Visualizers:** real-time waveform scope + FFT spectrum (Web Audio `AnalyserNode`).
- **Keyboard:** A–K row plays a chromatic octave; notes are monophonic (bench-style).

---

## Headless CLI

## Audio I/O

No extra link dependencies: the app writes raw **interleaved S16LE PCM to stdout**, so you pipe
it into whatever the system default player is. On a typical Linux box:

```bash
# build (see top-level README)
cmake --build build --target arstro_kitchen_sink

# live play through the default device:
./build/arstro_kitchen_sink | aplay  -f S16_LE -c 2 -r 48000
./build/arstro_kitchen_sink | paplay --raw --format=s16le --channels=2 --rate=48000   # PulseAudio/PipeWire
```

Offline render (no audio device needed — useful headless or for inspecting output):

```bash
./build/arstro_kitchen_sink --wav out.wav 5     # 5 seconds of a demo chord
```

> Design note: output is behind an `AudioSink` abstraction (`StdoutSink` / `WavSink`), so adding
> a direct-device backend (ALSA/PortAudio) later is a new sink class, not a rewrite (OCP).

## Live control (stdin, one command per line — works interactively or piped)

| Command | Effect |
|---------|--------|
| `on <note> [vel]` | note on (MIDI note, velocity 0..1) |
| `off <note>` | note off |
| `panic` | all notes off |
| `demo` | trigger a C-major chord |
| `<name> <value>` | set a named parameter (below) |
| `p <id> <value>` | set a raw param id (hex ok, e.g. `p 0x0804 0.3`) |
| `help` | list parameter names |
| `quit` | exit |

Named parameters include: `master.level`, `osc1.voices/detune/spread/attack/decay/sustain/release`,
`comp.threshold/ratio/bypass`, `od.drive/tone/level/bypass`, `chorus.rate/depth/mix/bypass`,
`repeat.delay/feedback/mix/bypass`, `reverb.decay/mix/width/bypass` (run `help` for the full list).

Example session:

```bash
{ echo "reverb.mix 0.5"; echo "reverb.width 1"; echo "demo"; sleep 3; echo "quit"; } \
  | ./build/arstro_kitchen_sink | aplay -f S16_LE -c 2 -r 48000
```

## Tested by

`tests/test_kitchen_sink.py` (CTest target `kitchen_sink_smoke`): asserts the offline WAV render
is a non-silent stereo file and that live stdout streaming produces PCM and terminates on `quit`.
Real-device playback is not exercised in CI (headless).
