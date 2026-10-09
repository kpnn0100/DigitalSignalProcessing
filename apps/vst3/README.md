# apps/vst3 — Basic Synth and Drum Machine as VST3 instruments

`ArstroBasicSynth.vst3` and `ArstroDrumMachine.vst3`: the library's own `BasicSynth` and
`DrumMachine`, created by `DeviceRegistry::create`, behind Steinberg's VST3 interfaces. No sound lives
here (REQ-vst-2). Requirements: `docs/requirements.md` REQ-device-7, REQ-vst-1…5; Solaris R-VST.

## Build, test, install

```bash
# once per machine — the SDK lives outside this repo (MIT since 3.8)
git clone --recursive --depth 1 --shallow-submodules --branch v3.8.1_build_84 \
    https://github.com/steinbergmedia/vst3sdk ~/sdk/vst3sdk

cmake -S . -B build                          # finds ~/sdk/vst3sdk, or -DVST3_SDK_ROOT=… / $VST3_SDK_ROOT
cmake --build build -j
ctest --test-dir build -R vst3               # the SDK's validator on both + vst3_equivalence
cmake --build build --target vst3_install    # copies both bundles to ~/.vst3, where hosts look
```

With no SDK the configure step prints `arstro VST3: no SDK at …` and builds none of this. The SDK's
own CMake needs 3.25, so `CMakeLists.txt` compiles the SDK's sources itself — from the SDK's own lists
(`cmake/modules/SMTG_VST3_SDK.cmake`, the validator's `CMakeLists.txt`) — and none of its global
settings reach the rest of the build. Bundles land in `build/vst3/<Name>.vst3/Contents/x86_64-linux/`.

## What a host sees

| | |
|---|---|
| classes | an audio processor (category `Instrument` + `Synth`, or `Instrument` + `Drum`) and its controller, vendor "Arstro", `kDistributable` |
| buses | one event input (notes), one stereo audio output; any other arrangement refused |
| sample sizes | 32- and 64-bit |
| parameters | one per registry parameter: id = its index in the type, title = its label, unit, steps (an integer's range or a choice's count − 1), default — all from the spec; normalised by `normalizedFromValue` / `valueFromNormalized` (REQ-device-7); a choice is a list |
| timing | the block is split at every note and every parameter point at its sample offset; a note-on at velocity 0 is a note-off |
| state | text: `arstro-device 1`, `type=<t>`, `<name>=<value>` per parameter (the values a Solaris `.slp` stores); another type's state is refused, an unknown name skipped |
| notes from its editor | `arstro.note` messages (`pitch`, `velocity`, 0 = off) to the processor, played at the next block (REQ-vst-7) — the editor's pads |
| editor | built from this repo alone: none — the host draws its generic parameter view. Built in the arstro umbrella: Solaris's own, in the Arstro look (`apps/solaris/plugins`, R-VST-7), through the seam in `Editor.h` (REQ-vst-6) |

## The editor seam (REQ-vst-6)

`Controller::createView("editor")` returns `createEditor(*this)` when the build defines
`ARSTRO_VST3_EDITOR` — the umbrella compiles its editor into the plugin targets and sets it — and
nothing otherwise. An editor uses only `Controller::type()`, `plainValue(i)` and `editBegin` /
`editPerform` / `editEnd` (engineering units; `editPerform` normalises by `normalizedFromValue` and
tells the host), so it can never disagree with the parameters' mapping. `vst3_editorhost <bundle>` —
the SDK's editor host, built here when X11 is found — opens a plugin's editor in a real host window.

## Frozen ids

`factory.cpp` holds each plugin's processor and controller class ids. A host saves them in every
project that uses the plugin: **never change them**. A parameter's id is its index in the registry
type — so a new parameter is APPENDED to its type, never inserted (a host's automation is keyed by it).

## The audio thread

`setupProcessing` allocates (the device, two scratch buffers, the change list); `process` does not. A
state the host sets from its UI thread is handed over under a mutex the audio thread only `try_lock`s.
Parameters the host reads back (`getState`) are atomics written by `process`.

## Verified

- `vst3_validate_synth`, `vst3_validate_drums` — the SDK's validator: 47 tests, 0 failed, each.
- `vst3_equivalence` — an offline host loads each bundle, sets its state as text, plays notes landing
  mid-block and a parameter automated mid-block at 100-sample blocks; the device rendered the way
  Solaris renders it (warm-up included) gives the same floats — 0 of 96 000 differ — and is not
  silence; the state reads back as the registry's text. A wrapper that ignores the notes' sample
  offsets fails it (95 998 differ).
