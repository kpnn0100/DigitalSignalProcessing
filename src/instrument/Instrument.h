/*
 *  Arstro DSP Library — Instrument: what a host (Solaris) plays (REQ-inst-1).
 *
 *  A note-driven sound source that renders whole blocks. Deliberately small: notes in, samples
 *  ADDED into the host's buffers out. Sample-accurate timing is the host's job — it splits a block
 *  at every note event and calls `render` for each piece (Solaris R-DSP-5) — so an instrument never
 *  needs to know about time stamps, and every instrument gets sample accuracy for free.
 *
 *  Rendering is deterministic: the same notes at the same samples from a `reset()` give the same
 *  output, on every machine (Solaris R-DSP-4).
 */
#pragma once
#include "../base/Sample.h"

namespace arstro
{
    class Instrument
    {
    public:
        virtual ~Instrument() = default;

        /** Start `note` (0..127) at `velocity` (1..127). */
        virtual void noteOn(int note, int velocity) = 0;
        /** Release `note`. A note that is not sounding is ignored. */
        virtual void noteOff(int note) = 0;
        /** Release everything (transport stop): tails ring out. */
        virtual void allNotesOff() = 0;
        /** Silence now and clear all state (a seek): the next render starts from nothing. */
        virtual void reset() = 0;
        /** ADD `frames` samples into `out[0..channels)`. The caller zeroes the buffers. */
        virtual void render(Sample *const *out, int channels, int frames) = 0;
        /** Voices still sounding (0 = the instrument is silent and may be skipped). */
        virtual int activeVoices() const = 0;
    };
}
