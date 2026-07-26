# Arstro DSP — Parallel & Accelerated Compute Architecture

How this library uses more than one core, and what a port to another platform (or to a GPU)
actually has to implement. Requirements: `REQ-compute-1` … `REQ-compute-6` in
[`requirements.md`](requirements.md).

---

## 0. Start here: measure before you parallelise

This document exists because the interactive `examples/piano` app felt laggy. It is worth
recording what the measurement showed, because the conclusion was **not** "we need more cores":

| block size | 8 voices sounding | share of the real-time budget |
|---|---|---|
| 64 frames (1.33 ms) | 0.263 ms | **19.7 %** |
| 128 frames (2.67 ms) | 0.511 ms | **19.1 %** |
| 512 frames (10.7 ms) | 1.838 ms | **17.2 %** |

The DSP was using under a fifth of one core. The lag was **latency, not throughput**:
`snd_pcm_set_params(..., 50000)` asked ALSA for a 50 ms buffer, and the app wrote 512-frame
blocks, so a keypress reached the speaker 50–60 ms later. A pianist notices past ~15 ms.

**The lesson is architectural, not incidental.** Parallelism buys *throughput*. Perceived lag is
*latency*. They are different axes, and adding threads to fix latency makes things worse — more
synchronisation per block, and (if the pool is misconfigured) more jitter.

So why build this at all? Because the two axes meet at one place: **low latency forces small
blocks, and small blocks are where headroom is thinnest.** Per-block overhead is amortised over
fewer samples (19.7 % at 64 frames vs 17.2 % at 512), and the fixed costs — command drain, buffer
setup, worker wake-up — do not shrink. Parallelism is what buys back the headroom to run a
64-frame block with a larger voice pool, or with M8/M9's added physics on top. It is headroom for
growth, not a fix for lag.

---

## 1. Dependency analysis — what is actually parallel

Parallelism is not a property of a codebase, it is a property of its **dependency graph**. The
synth graph has three levels, and they differ enormously.

```
                 ┌── voice 0 ──┐
   note events ──┼── voice 1 ──┼──► sum ──► shared effects ──► master ──► output
                 ┼── …        ─┤              (serial)
                 └── voice N ──┘
                        ▲                │
                        └──── bridge ────┘   (piano only: cross-voice coupling)
```

### 1.1 Across samples — **no parallelism, ever**

Every resonator in `physical/` and every filter in `equalizer/` is a recurrence:

```
y[n] = a1·y[n-1] − a2·y[n-2] + G·x[n]
```

`y[n]` cannot be computed before `y[n-1]`. This is not an implementation limit; it is the
definition of an IIR filter. **No backend, CPU or GPU, can parallelise the time axis of this
library.** Anything that claims to is either changing the filter or changing the output.

### 1.2 Across voices — **parallel, and this is the one worth taking**

Voices own disjoint state (`Voice`, `PianoVoice`), so at a given sample they are independent.
`synth/SynthEngine` already encodes this: `renderVoiceHalf(half, frames)` renders a disjoint
subset into its own accumulator precisely so two cores can run it, with `finishBlock()` doing the
shared effects on one core. That design was already correct — it was simply never given an
executor, so both halves ran back to back on the calling thread.

### 1.3 Across partials inside one voice — parallel, but too fine to pay

A bass `PianoVoice` runs ~250 two-pole recurrences per sample. They are mutually independent,
so this looks like the most parallel level available. It is also the *worst* one to use, because
the synchronisation grain is one sample:

```
work per sample per voice  ≈ 250 resonators × ~5 flops   ≈ 0.3 µs
thread barrier cost        ≈ 1–2 µs
```

A barrier costs 3–6× the work it coordinates. **The grain must be the block, not the sample**,
which rules out per-sample fan-out for both threads and GPUs (§4). Within a sample the right tool
for partial-level parallelism is SIMD, not threads — same instruction stream, no synchronisation
at all. `StringPartialBank`'s flattened coefficient arrays (M2) were built for exactly that and
are already vectorisable.

---

## 2. The scheduling law, and what it caps us at

With serial fraction `s` (the part that cannot be spread across workers) and `p` workers,
Amdahl's law bounds the speedup of one block:

```
S(p) = 1 / ( s + (1 − s)/p )
S(∞) = 1/s
```

For the piano graph the serial part is the shared bridge plus the mix, **measured directly** (the
bridge run alone against the full graph, 8 sustained voices, best of 5 interleaved passes):

```
bridge alone           45.9 ms
voices + bridge       345.5 ms
s = 45.9 / 345.5    = 0.133
```

| `p` | `S(p)` ceiling | note |
|---|---|---|
| 2 | 1.77× | the shape `renderVoiceHalf` already had |
| 4 | 2.83× | |
| 8 | 4.14× | one worker per voice |
| 16 | 5.35× | this machine's thread count; diminishing hard |
| ∞ | 7.52× | `1/s` — the bridge is the wall |

Two things follow, and both are design decisions rather than observations:

1. **Do not spawn more workers than shards.** Past 8 there is nothing left to give a worker;
   past 16 (`nproc`) they contend — measured, 15 workers is consistently *slower* than 3 (§2.0).
   The executor therefore clamps to `min(shardCount, hardware_concurrency)`.
2. **The serial fraction is the thing to attack**, not the worker count. Going 8 → 16 workers buys
   29 %; halving the bridge cost buys more. That is a DSP change (fewer soundboard modes, or a
   cheaper bridge topology), which is why it is recorded here rather than solved by scheduling.

### 2.0 Measured: on `SynthEngine` today, parallelism does NOT pay

Predictions are cheap, so here is the measurement — `SynthEngine`, 8 oscillator voices, best of 3
passes over 2 s of audio, serial vs `ThreadPoolExecutor` at several worker counts:

| frames | 1 worker | 3 workers | 7 workers | 15 workers |
|---|---|---|---|---|
| 64 | 0.85× | 0.79× | 0.83× | **0.68×** |
| 128 | 1.00× | 0.99× | 0.97× | 0.87× |
| 256 | 1.02× | 1.10× | 1.08× | 1.00× |
| 512 | 0.98× | 1.10× | 1.10× | 1.09× |

**Break-even at best, a 32 % loss at 64 frames.** This is not a defect in the executor — it is
§2.1's floor showing up exactly where §2.1 says it will. A 2 s `SynthEngine` render takes ~48 ms
(≈41× real time), so a 128-frame block is only ~0.13 ms of work; against a ~5–20 µs fan-out and
join, there is nothing to win, and at 64 frames the overhead dominates outright.

The conclusion to carry forward: **the block-size floor is really a work-per-block floor.**
`kMinFramesForParallel` is a proxy for it, and a coarse one — a cheap engine at 512 frames can
still be below the threshold while an expensive one at 64 frames is above it. The piano graph is
~4× more expensive per block (0.51 ms at 128 frames) and is where this layer would first pay,
which is why §2's Amdahl ceiling is computed for *it* and not for the synth.

### 2.1 Overhead: why the block size sets the floor

Real speedup is
`S(p) = T_serial / (s·T + (1−s)·T/p + O(p))`, where `O(p)` is fan-out + join. A block of 128
frames at 48 kHz costs ~0.5 ms of work; a wake-up + join is ~2–10 µs, so overhead is ~1–2 % —
acceptable. At 16 frames the same overhead is ~10 % and parallelism starts losing. **Below ~64
frames, run serially**; the executor exposes `shouldParallelise(frames)` so this is one decision
in one place rather than a scattered heuristic.

---

## 3. The adapter seam — what a port implements

The library already declares that `base/platform/Platform.h` is "the ONLY platform-specific
code", with `platform::Thread` backed by `std::thread` on desktop and `xTaskCreatePinnedToCore`
on ESP32. **That is the seam, and it stays the seam.** The compute layer is built *on* it, so a
new platform implements one class it already had to implement anyway:

```
  ┌─────────────────────────────────────────────┐
  │ engines: SynthEngine, PianoEngine           │  no OS calls, no thread code
  ├─────────────────────────────────────────────┤
  │ compute/ParallelExecutor  (interface)       │  ← what engines depend on
  │   • SerialExecutor      always available    │
  │   • ThreadPoolExecutor  built on platform:: │
  ├─────────────────────────────────────────────┤
  │ base/platform/Platform.h  Thread, Mutex     │  ← THE porting seam (already existed)
  └─────────────────────────────────────────────┘
```

| To port to… | implement | already done? |
|---|---|---|
| Linux / macOS / Windows | `platform::Thread` via `std::thread` | yes, header-only |
| ESP32 / FreeRTOS | `platform::Thread` via `xTaskCreatePinnedToCore` | declared; firmware supplies the `.cpp` |
| bare metal / no threads | nothing — use `SerialExecutor` | yes, the default |
| a GPU / DSP accelerator | **not this interface** — see §4 | — |

`ParallelExecutor` is deliberately tiny, and deliberately *not* `std::function`-based:

```cpp
using Task = void (*)(void *ctx, int shard);
virtual void run(Task task, void *ctx, int shards) = 0;   // blocking; allocates nothing
```

A raw function pointer plus a `void*` context is used because `std::function` heap-allocates when
it captures more than a couple of words, and **nothing on the audio thread may allocate**
(`docs/design.md`: "Audio thread never locks", same reasoning). The cost is one cast at the top of
each task body; the benefit is a hard guarantee.

### 3.2 Lock-free where it counts, a mutex where it does not

Shard **claiming** runs once per shard and is lock-free (one `fetch_add` on a shared cursor).
Dispatch and join take a short mutex + condition variable. That split is deliberate, and the
second half of it was learned the hard way:

> An all-atomic handshake was implemented first, and deadlocked. A worker that woke *late* for
> job N — after `run()` had already returned — would claim against job **N+1**'s cursor using its
> stale snapshot. Its final failed claim still advanced the cursor, so one shard of the new job
> was never run by anyone: `mRemaining` never reached zero and `run()` waited forever. It
> reproduced only when a job with *fewer* shards was followed by one with more, which is why it
> survived the first round of testing and showed up 18 jobs into a stress run.
>
> The fix is to count the drainers a job **invites** (caller + every worker, all woken by
> `notify_all`) rather than counting them as they *arrive*. Then a worker cannot be late for a
> job it was not counted into, and the previous job cannot return until every worker has retired.

The mutex is held only for a handful of integer operations, never while DSP work runs, and never
by a thread that could be descheduled mid-computation — so it cannot stall the audio thread the
way a general-purpose lock could. `REQ-compute-4` was amended to say exactly this.

### 3.1 Why the executor is an interface and not `#ifdef`

`Platform.h` selects its backend with `#if defined(ESP_PLATFORM)`. That is right for a leaf
primitive with one implementation per platform. It is wrong for the executor, because a single
build wants **several** executors at once:

- `SerialExecutor` for unit tests, so results are bit-reproducible;
- `ThreadPoolExecutor` for the live app;
- both, in the same binary, for the test that asserts they agree (§5).

So the executor is a runtime-polymorphic interface, selected through `ComputeConfig::instance()` —
the same single-authority pattern `AudioConfig` already establishes for sample rate and channel
count (`docs/design.md`: "One config authority (DIP)").

---

## 4. GPU: where it pays, where it cannot, and the honest verdict

A GPU is not a faster CPU; it is a throughput device with a latency floor. Both properties decide
the answer here.

### 4.1 The arithmetic

```
work per block (128 frames, 8 voices) ≈ 1944 resonators × 128 samples × ~5 flops ≈ 1.2 MFLOP
blocks per second                      = 48000/128 = 375
sustained rate required                ≈ 0.47 GFLOP/s
```

An integrated Radeon (this machine: Ryzen 9 6900HX) does ~1–3 TFLOP/s. **The compute is ~0.02 % of
an iGPU.** Throughput is emphatically not the problem, which already tells us a GPU is not the
answer to a throughput question we do not have.

The binding constraint is dispatch latency:

```
kernel launch + host round trip   ≈ 20–100 µs
block budget at 128 frames        = 2670 µs      -> 1–4 %   tolerable
block budget at 64 frames         = 1333 µs      -> 2–8 %   marginal
per-SAMPLE dispatch (§1.3)        = 20.8 µs      -> 100–500 %  impossible
```

So a GPU can only ever be handed **a whole block at a time** — never a sample. That is the crux,
and it collides with the physics:

### 4.2 The blocker: intra-block feedback

To hand a block to a GPU, every resonator's *input for the whole block* must be known before the
block starts. Two paths violate that:

- **Hammer contact (§6 of `physical/README.md`).** The felt compresses against the string's
  *current* displacement, so force at sample `n` depends on output at `n−1`. Fully sequential.
- **Bridge feedback (§8).** Each voice reads the bridge's previous-sample response.

The second is already one sample delayed and can be relaxed to one *block* delayed cheaply. The
first cannot be relaxed — it is the mechanism M3 exists to model, and breaking it would undo the
milestone.

But hammer contact lasts **2.08–5.38 ms** (measured, M6), i.e. 100–260 samples, and only at
note-on. Which gives the decomposition:

```
voice state = ATTACK  (hammer in contact)  -> sequential, CPU, ~1-2 blocks per note
              DECAY   (felt has left)      -> input is bridge feedback alone
                                           -> block-parallel, GPU-eligible
```

Since a note decays for seconds and attacks for milliseconds, **the overwhelming majority of the
work is GPU-eligible** — and it is exactly the part that scales with polyphony.

### 4.3 Why the GPU adapter is a different interface

`ParallelExecutor::run` takes a C++ function pointer. A GPU cannot execute one. Any real
accelerator backend needs a **fixed, narrow kernel contract** it can implement ahead of time:

```
advanceResonatorBank(a1[], a2[], y1[], y2[], drive[], M resonators,
                     input[B], output[B])        // B samples, one thread per resonator
```

This is the one primitive that dominates the profile, and it is the only thing worth
accelerating. Two interfaces, then, at two grains — and conflating them is the design mistake to
avoid:

| interface | grain | parallel over | implementable on |
|---|---|---|---|
| `ParallelExecutor` | block × voice-shard | task | CPU threads only |
| *(future)* `ResonatorKernel` | block × resonator | data | SIMD, GPU, DSP hw |

### 4.4 Verdict — and what is NOT being shipped

**No GPU backend is shipped here, deliberately.** This machine has an integrated Radeon
(`/dev/dri/renderD128`) but **no OpenCL, CUDA, or Vulkan toolchain installed**, so a GPU backend
could not be compiled, let alone tested. Rule 4 of `arstro.dsp.implement` says an untested formula
is not shipped, and that applies with more force to an untestable backend: a GPU path that has
never run is worse than no GPU path, because it looks like a feature.

What is shipped is the analysis above and the seam in §3, so the decision is recorded rather than
rediscovered. And the honest conclusion is that **for this workload a GPU is the wrong tool** —
0.47 GFLOP/s of inherently serial-in-time recurrences, on a device whose advantage is throughput
and whose penalty is latency. It becomes interesting at a very different scale (hundreds of
voices, or offline bounce rendering where latency is free), and §4.2's attack/decay split is how
it would be reached.

---

## 5. Correctness: parallelism must not change the output

A parallel renderer that sounds different from the serial one is a bug, not a speedup — and it
would silently invalidate every numeric acceptance criterion M0–M7 established. The design keeps
this checkable:

- **Shards touch disjoint state.** Voice `i` is written by exactly one worker. No locks are needed
  because nothing is shared, which is a stronger guarantee than locking correctly.
- **Summation order is fixed.** Each shard accumulates into its *own* buffer and `finishBlock`
  sums those buffers in shard index order — never in completion order. Floating-point addition is
  not associative, so completion-order summation would make output depend on thread timing and
  make every test flaky.
- **The equivalence is asserted, not assumed.** A unit test renders the same note sequence through
  `SerialExecutor` and `ThreadPoolExecutor` and requires the results **bit-identical**.

That last point is why the two executors must coexist in one binary (§3.1).

---

## 6. Summary of decisions

| decision | reason |
|---|---|
| Shard across **voices**, at **block** grain | the only level where work per sync is ≥ 100× the sync cost (§1) |
| Ship it opt-in and OFF by default | measured, it does not yet pay on `SynthEngine` (§2.0); it is headroom, and headroom that is on by default is a regression |
| Never across samples | IIR recurrence; not an implementation limit (§1.1) |
| Serial below ~64 frames | fan-out overhead exceeds the gain (§2.1) |
| Workers = `min(shards, hardware_concurrency)` | Amdahl gives nothing beyond shard count (§2) |
| Build on `platform::Thread`, don't replace it | it was already declared the only OS-specific code (§3) |
| Function pointer + `void*`, not `std::function` | no audio-thread allocation (§3) |
| Runtime interface, not `#ifdef` | one binary needs serial + threaded simultaneously (§3.1) |
| Fixed shard-order summation | float addition isn't associative; timing must not change output (§5) |
| No GPU backend shipped | no toolchain to test it, and the workload is latency-bound not throughput-bound (§4.4) |
