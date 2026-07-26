/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  ComputeConfig: the single authority for "which executor does rendering use",
 *  mirroring AudioConfig's role for sample rate / channel count / bit depth
 *  (docs/design.md — "One config authority (DIP)"). Engines ask this rather than
 *  owning an executor, so switching the whole graph between serial and threaded
 *  is one call and needs no engine API changes.
 *
 *  Default is SerialExecutor: parallelism is opt-in (REQ-compute-5), and a
 *  platform with no threads is fully functional without touching this.
 *
 *  Set the executor during setup, before the audio thread starts — the setter is
 *  not synchronised, exactly like AudioConfig's setters.
 */
#pragma once
#include "ParallelExecutor.h"

namespace arstro
{
    class ComputeConfig
    {
    public:
        static ComputeConfig &instance()
        {
            static ComputeConfig c;
            return c;
        }

        /** Never null. Defaults to the built-in serial executor. */
        ParallelExecutor &executor() const { return *mExecutor; }

        /** Passing nullptr restores the serial default, which is what tests use
         *  to return to a known state. The pointee must outlive its use. */
        void setExecutor(ParallelExecutor *e) { mExecutor = e ? e : &mSerial; }

        /** Convenience: are we currently set up to parallelise at this block size? */
        bool shouldParallelise(int frames) const { return mExecutor->shouldParallelise(frames); }

    private:
        ComputeConfig() = default;
        SerialExecutor mSerial;
        ParallelExecutor *mExecutor = &mSerial;
    };
}
