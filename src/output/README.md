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
2. **Dither:** off by default — agree?
