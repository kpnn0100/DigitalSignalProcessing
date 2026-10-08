/*
 *  Arstro DSP Library — Biquad: one second-order IIR section, RBJ "Audio EQ Cookbook" designs.
 *
 *  The one primitive every EQ band is built from (REQ-eq-1): low-pass, high-pass, band-pass,
 *  notch, peaking, low shelf, high shelf. Transposed direct form II, per-channel state, shared
 *  coefficients. Frequency, Q and gain are smoothed SignalProcessor properties (a sweep does not
 *  click); the TYPE is not a property, because ramping between "low-pass" and "peak" is not a
 *  thing. The math, and where each line of it is used, is in equalizer/README.md ## Math.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include <vector>

namespace arstro
{
    class Biquad : public SignalProcessor
    {
    public:
        enum Type { LowPass = 0, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf };
        enum PropertyIndex
        {
            frequencyID, // Hz
            qID,         // quality factor
            gainDbID,    // dB — Peak and the shelves only
            propertyCount
        };

        /** Normalised coefficients (a0 divided out). */
        struct Coeffs
        {
            Sample b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        };

        Biquad();
        Biquad(Type type, Sample hz, Sample q, Sample gainDb = 0.0);

        void setType(Type type);
        void setFrequency(Sample hz);
        void setQ(Sample q);
        void setGainDb(Sample db);
        Type type() const { return mType; }

        /** Clear the filter memory (a seek, a new note). */
        void reset();

        void update() override;
        void onSampleRateChanged() override;
        void onChannelCountChanged() override;

        /** The RBJ design for these settings (frequency clamped to [10 Hz, 0.49·fs], Q to [0.1, 40]). */
        static Coeffs design(Type type, Sample hz, Sample q, Sample gainDb, Sample sampleRate);
        /** |H(e^{jω})| in dB at `hz` — what a band does to a steady sine there. */
        static Sample magnitudeDb(const Coeffs &c, Sample hz, Sample sampleRate);
        /** The response of this filter's TARGET settings at `hz` (for curves and tests). */
        Sample magnitudeDbAt(Sample hz);

        const Coeffs &coeffs() const { return mC; }

    protected:
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        Type mType = Peak;
        Coeffs mC;
        std::vector<Sample> mZ1, mZ2; // per-channel TDF-II state
    };
}
