/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  Sample: the library's sample/parameter scalar type.
 *
 *  Default is `double` (the project rule). Define GS_USE_FLOAT to build a
 *  single-precision variant — used by the ESP32-S3 firmware to compare against
 *  software-emulated `double` (the S3 FPU is single-precision). The host/tests
 *  stay `double` unless GS_USE_FLOAT is defined.
 */
#pragma once

#ifdef GS_USE_FLOAT
typedef float Sample;
#else
typedef double Sample;
#endif

/*
 *  Denormal flush. Feedback loops (reverb/echo tails, one-pole filters) decay
 *  toward zero into subnormal values, which the ESP32-S3 FPU handles in slow
 *  software (often the dominant cost on silence). Snapping tiny values to exactly
 *  0 (inaudible, < -300 dB) keeps everything in the fast normal range.
 */
static inline Sample gsFlush(Sample x)
{
    return (x < (Sample)1e-15 && x > (Sample)-1e-15) ? (Sample)0 : x;
}
