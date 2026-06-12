/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  ParamId: the shared, versioned parameter-id map. Host, firmware and the Python
 *  GUI must all agree on these ids. The high byte selects a group; the low byte
 *  selects a parameter within the group. Envelope (ADSR) parameters live inside
 *  each oscillator group, because the envelope is part of the generator.
 */
#pragma once
#include <cstdint>

namespace arstro
{
    static constexpr uint16_t PARAM_VERSION = 1;

    // Group bases (high byte).
    enum ParamGroup : uint16_t
    {
        GROUP_MASTER = 0x0000,
        GROUP_OSC1 = 0x0100,
        GROUP_OSC2 = 0x0200,
        GROUP_OSC3 = 0x0300,
        GROUP_COMPRESSOR = 0x0400,
        GROUP_OVERDRIVE = 0x0500,
        GROUP_CHORUS = 0x0600,
        GROUP_REPEATER = 0x0700,
        GROUP_REVERB = 0x0800,
    };

    // Master
    enum { MASTER_LEVEL = GROUP_MASTER + 0, MASTER_OSC_SIMD = GROUP_MASTER + 1 };

    // Per-oscillator offsets (add to GROUP_OSCn). ADSR included here on purpose.
    enum
    {
        OSC_VOICE_COUNT = 0, // 1..5
        OSC_DETUNE = 1,      // cents
        OSC_SPREAD = 2,      // stereo cents
        OSC_LEVEL = 3,       // linear
        OSC_TUNE = 4,        // semitone offset for this oscillator
        OSC_ATTACK = 5,      // ms
        OSC_DECAY = 6,       // ms
        OSC_SUSTAIN = 7,     // 0..1
        OSC_RELEASE = 8,     // ms
    };

    // Shared effect offset: bypass a node in the chain (1 = bypassed/skipped).
    // Valid for the 5 effect groups (compressor..reverb). High offset avoids
    // colliding with the per-effect parameter offsets below.
    enum { FX_BYPASS = 10 };

    // Compressor offsets
    enum { CMP_THRESHOLD = 0, CMP_RATIO = 1, CMP_ATTACK = 2, CMP_RELEASE = 3, CMP_MAKEUP = 4 };
    // Overdrive offsets
    enum { OD_DRIVE = 0, OD_TONE = 1, OD_LEVEL = 2 };
    // Chorus offsets
    enum { CH_RATE = 0, CH_DEPTH = 1, CH_BASEDELAY = 2, CH_MIX = 3 };
    // Repeater offsets
    enum { RP_DELAY = 0, RP_FEEDBACK = 1, RP_TONE = 2, RP_MIX = 3 };
    // Reverb offsets
    enum { RV_DELAY = 0, RV_DECAY = 1, RV_LOWCUT = 2, RV_HIGHCUT = 3, RV_MIX = 4, RV_WIDTH = 5 };
}
