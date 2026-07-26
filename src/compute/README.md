# compute/ — Parallel work scheduling

The block-grain work scheduler engines render through. Full design, dependency analysis and the
GPU verdict: [`../../docs/parallel-architecture.md`](../../docs/parallel-architecture.md).
Requirements: `REQ-compute-1` … `REQ-compute-6`.

## Why new classes (rule 1 — reuse check)

Searched first, and two things already existed and are **reused rather than replaced**:

- `base/platform/Platform.h` already declares itself "the ONLY platform-specific code", with
  `platform::Thread` implemented over `std::thread` (desktop) and `xTaskCreatePinnedToCore`
  (ESP32). `ThreadPoolExecutor` is built entirely on it and adds **no** OS calls of its own, so
  porting still means implementing that one class (`REQ-compute-2`).
- `synth/SynthEngine::renderVoiceHalf()` already split the voice pool into disjoint halves for
  two cores. That shape was correct; it was hardcoded to exactly two and never given an executor,
  so both halves ran back to back on the caller. It is now the two-shard case of
  `renderVoiceShard(shard, shardCount, frames)` and keeps working unchanged.

What genuinely did not exist is the *scheduler*: something that runs N disjoint shards on N
threads, blocks until they finish, allocates nothing, and can be swapped for a serial
implementation so tests can prove the two agree. That is this module.

## Class map

| Class | Base | Responsibility |
|---|---|---|
| `ParallelExecutor` | — | The interface engines depend on: `run(task, ctx, shards)`, `concurrency()`, `shouldParallelise(frames)`. |
| `SerialExecutor` | `ParallelExecutor` | Runs shards in a loop. The default, always available, and the reference the bit-identity test compares against. |
| `ThreadPoolExecutor` | `ParallelExecutor` | Persistent workers over `platform::Thread`; lock-free shard claiming, condition-variable dispatch/join. |
| `ComputeConfig` | — | Single authority for "which executor is in use", mirroring `AudioConfig`'s role (`docs/design.md`, DIP). Defaults to serial. |

## Math

There is no DSP math in this module — it computes nothing. What it does have is a **cost model**,
and every constant below traces to it.

### 1. Speedup bound (Amdahl)

With serial fraction `s` and `p` workers:

```
S(p) = 1 / ( s + (1 − s)/p )                S(∞) = 1/s
```

Measured for the piano graph (bridge alone vs the full graph, 8 sustained voices, best of 5
interleaved passes): `s = 45.9 ms / 345.5 ms = 0.133`, giving `S(8) = 4.14×` and a hard ceiling of
`1/s = 7.52×`. This is why `run()` is never handed more shards than `concurrency()`: past the
shard count the term `(1−s)/p` stops shrinking and only overhead grows.

### 2. Overhead and the block-size floor

Including fan-out/join cost `O(p)`:

```
S_real(p) = T / ( s·T + (1−s)·T/p + O(p) )
```

Parallelism pays only while `O(p) ≪ (1−s)·T·(1 − 1/p)`. With `O(p) ≈ 5–20 µs` and a 128-frame
block of the piano graph costing `T ≈ 0.51 ms`, overhead is ~1–4 %. For `SynthEngine`'s much
cheaper voices (`T ≈ 0.13 ms`) it is 4–15 %, and **measured, the parallel path is break-even at
best and 0.68× at 64 frames** — see `docs/parallel-architecture.md` §2.0. Hence:

```
kMinFramesForParallel = 64          ParallelExecutor::shouldParallelise()
```

a deliberately coarse proxy: the real threshold is work-per-block, and frames is the only part of
it the executor can see.

### 3. Determinism (`REQ-compute-3`)

Floating-point addition is not associative:

```
(a + b) + c  ≠  a + (b + c)        in general
```

so the *order* accumulators are summed must not depend on which worker finished first. Each shard
writes its own accumulator and `SynthEngine::finishBlock()` sums them by ascending shard index,
making the output a pure function of the shard count — not of thread timing. A unit test renders
the same sequence through both executors and requires the bytes to be **identical**, not close.

### 4. Shard boundaries

`n` voices over `k` shards, with the first `n mod k` shards taking one extra voice:

```
v0(i) = i·⌊n/k⌋ + min(i, n mod k)
v1(i) = v0(i) + ⌊n/k⌋ + (i < n mod k ? 1 : 0)
```

so no shard is ever more than one voice heavier than another, and the union over `i` is exactly
`[0, n)` for every `k` — which a unit test checks by requiring identical output for `k = 1…8`.

## Parameters

| Class | Member | Meaning |
|---|---|---|
| `ParallelExecutor` | `kMinFramesForParallel` = 64 | block-size floor (§2) |
| `ThreadPoolExecutor` | `kAutoWorkers` = −1 | size the pool to the hardware, minus one for the caller |
| | `kMaxWorkers` = 31 | upper clamp |
| | ctor `workers = 0` | explicitly no worker threads — a real single-core configuration, and `run()` then executes inline |
| `SynthEngine` | `kMaxVoiceShards` = 8 | `VoiceManager::kVoiceCount`; more shards than voices is pointless |

## Threading contract

- `run()` **blocks** until every shard is finished. There is no async variant on purpose: an
  audio block has a hard deadline, so there is nothing useful to do while waiting.
- Shards must touch **disjoint** state. Nothing is locked because nothing is shared — a stronger
  guarantee than locking correctly.
- The dispatch mutex is held only for a handful of integer operations, never while DSP work runs.
  `REQ-compute-4` was amended to permit it after an all-atomic handshake deadlocked; the failure
  and the fix are documented in `docs/parallel-architecture.md` §3.2 because it is a mistake that
  is very easy to make again.
- `ComputeConfig::setExecutor()` is **setup-time only** — not synchronised, exactly like
  `AudioConfig`'s setters. Set it before the audio thread starts.
