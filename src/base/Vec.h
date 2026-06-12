/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  Vec: thin block (vector) helpers for the block-processing path.
 *
 *  NOTE on ESP-DSP: these are intentionally plain loops. The dual-core render
 *  calls them concurrently from both cores (one per channel), and ESP-DSP's
 *  dsps_* assembly routines are NOT reentrant across cores — calling them from
 *  both cores at once corrupts output. They also gave negligible benefit here,
 *  because the heavy DSP (PolyBLEP saws, ADSR, feedback effects) is custom
 *  per-sample code that doesn't map onto dsps_add/mul anyway. ESP-DSP is still
 *  linked and available for future single-threaded block work (e.g. biquad
 *  filters via dsps_biquad_f32_aes3/arp4), just not on this concurrent hot path.
 */
#pragma once
#include "Sample.h"

namespace arstro
{
    // dst[i] = 0
    static inline void arstroVecZero(Sample *dst, int n)
    {
        for (int i = 0; i < n; ++i) dst[i] = (Sample)0;
    }

    // dst[i] += a[i]
    static inline void arstroVecAdd(Sample *dst, const Sample *a, int n)
    {
        for (int i = 0; i < n; ++i) dst[i] += a[i];
    }

    // dst[i] *= c
    static inline void arstroVecMulC(Sample *dst, Sample c, int n)
    {
        for (int i = 0; i < n; ++i) dst[i] *= c;
    }

    // dst[i] += a[i] * c
    static inline void arstroVecAddScaled(Sample *dst, const Sample *a, Sample c, int n)
    {
        for (int i = 0; i < n; ++i) dst[i] += a[i] * c;
    }
}
