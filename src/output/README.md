# output/ — Output sink (double → bit depth → UART audio)

The **only** place in the whole pipeline where `double` is quantized to integer samples.
Everything upstream is `double`; this block converts to the configured output bit depth and
packs interleaved channels for the transport.

> Status: DESIGN. 🆕 = new class.

---

## 🆕 `OutputBlock`

```cpp
class OutputBlock {
public:
    // pulls one rendered double sample per channel and quantizes it
    void writeSample(int channel, double sample);   // clamps to [-1, 1]
    // emits one interleaved frame (all channels) once per sample index
    // bit depth comes from AudioConfig::outputBitDepth()
};
```

- **Bit depth is defined/configurable** via `AudioConfig::outputBitDepth()`
  (default **16-bit**; 8/16/24/32 selectable). This is the "output block to DAC should have
  defined bit depth" requirement — here the DAC is the UART audio stream.
- Quantization: `clamp(x, -1, 1)` then scale to the signed integer range for the chosen
  depth. Dithering is optional and off by default (keeps the benchmark deterministic).
- **Interleaving:** channels interleaved per frame, e.g. `[L0 R0][L1 R1]…`, matching
  `AudioConfig::channelCount()`.

## Audio-over-UART framing

The quantized, interleaved samples are wrapped into audio frames by the transport. The
**frame layout lives in [`host/README.md`](../../host/README.md)** (shared protocol), but
the payload produced here is:

```
[ frame of N samples × C channels, each sample = outputBitDepth bits, little-endian ]
```

`OutputBlock` produces the payload; the firmware's UART layer adds header/length/checksum.

## Why a dedicated block (SRP)

- Synthesis/effects stay in pure `double` and know nothing about bit depth or transport.
- One class owns quantization + interleaving; changing bit depth or channel count touches
  only `AudioConfig` + this block.

## Reuse map

| Need | Reuse |
|---|---|
| Global bit depth / channel count | `base/AudioConfig` |
| Clamp helpers | `util/` (add if missing) |

## Open questions for review

1. **Default bit depth:** 16-bit is the proposed default (good size/quality for the UART
   throughput test). Confirm, or prefer 24/32 for headroom?
2. **Dither:** off by default — agree? (**Answered by `Dither`, below:** `OutputBlock` stays undithered for the
   benchmark; a host writing a file dithers with `Dither` first.)

---

## `Dither` — TPDF dither and quantisation (REQ-out-dither-1)

```cpp
class Dither {
public:
    explicit Dither(int bits = 16, uint32_t seed = kSeed);
    void setBits(int bits);            // 2 … 24
    void reset(uint32_t seed = kSeed); // the noise from the start
    void setEnabled(bool on);          // off: rounding only
    float quantize(float x);           // onto the grid of `bits`
    void process(float *x, int frames);
};
```

What a float mix needs before a host writes it as 16- or 24-bit integers. Composed from the library's `Noise`
(generator/Noise.h, xorshift32) — two of its uniforms make the triangle; nothing new but the rounding. Seeded:
the same input gives the same integers (a render stays pure).

## Math

```
Q = 2^(bits − 1)                                   ; steps per unit (16 bits: 32768)
(D0) x = 0 exactly → y = 0                          ; digital silence stays silence (auto-blanking); no noise drawn
(D1) d = (u₁ + u₂)/2,  u₁, u₂ ~ U[−1, 1) independent ; triangular on (−1, 1): ±1 step — Dither::quantize
(D2) k = ⌊x·Q + d + ½⌋                              ; round onto the grid (d = 0 when disabled)
(D3) k ← clamp(k, −Q, Q − 1);  y = k / Q            ; the integers the depth has
```

**Why a triangle of ±1 step.** With rounding alone the error e = y − x is a function of x (zero on a step, ½ step
half way): distortion, and a sound under ½ step rounds to silence. A dither d added first, with the total error
e = (k − x·Q)/Q, has E[e] = 0 and E[e²] = Δ²/12 + Var(d) for every x exactly when d's distribution is the
convolution of two one-step uniforms (Wannamaker, Lipshitz, Vanderkooy 2000) — the triangle of (D1), whose variance
is Δ²/6, so the floor is Δ²/4 (−101.2 dBFS at 16 bits, white) whatever plays. On a step, the tails past ±½ send
1/8 of the samples one step down and 1/8 one step up. **Why (D0).** An exact zero is digital silence — a render's
intro before the first note, its end after the tail — with nothing in it to decorrelate; dithering it would only
turn −∞ into a −138 dB hiss (24 bits) that a file compare or a meter reads as sound. Any other value, however small,
is dithered.

| setter | unit | symbol |
|---|---|---|
| `setBits` | bits | Q = 2^(bits−1) |
| `reset` / constructor seed | — | the generators' seeds (the second a fixed odd step from the first) |
| `setEnabled` | on/off | d = 0 when off |

Verified: on the grid, in range, clamped (`Dither_writes_on_the_grid_of_its_depth`); mean 0 and power ¼ step² at
four places between steps, rounding alone 0 or ¼ by where x falls (`Dither_error_has_the_same_mean_and_power_whatever_the_signal`);
a 0.4-step sine rounded to silence, dithered measured within 10 % (`Dither_keeps_a_sound_under_one_step`);
seeded (`Dither_is_seeded`); zeros pass, the smallest non-zero is still dithered (`Dither_lets_digital_silence_be`); the integers in a written file counted 1/8 · 3/4 · 1/8 on a step and ½ · ½ half way
(`dither_triangular`).

