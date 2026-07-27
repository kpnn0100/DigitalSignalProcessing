#include "DuplexBank.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    constexpr int DuplexBank::kAliquotHarmonic[];

    DuplexBank::DuplexBank() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false); // struck note: parameters jump at note-on, no knob-click risk
        initProperty(fundamentalID, 523.25);
        // README ## 8.1: kappa_dup scales the string's motion into the aliquot drive.
        // Calibrated (not derived) — like §12.2's kappa, every SI term is inside the
        // normalised unit system, so it is set against the level a real duplex shimmer
        // sits at: measured ~-33 dB (C6) to -27 dB (C5) below the note peak, present
        // but well under a second voice.
        initProperty(driveGainID, 90.0);
        update();
    }

    void DuplexBank::setFundamentalHz(Sample hz) { setProperty(fundamentalID, hz); }
    void DuplexBank::setDriveGain(Sample kappa) { setProperty(driveGainID, kappa); }

    Sample DuplexBank::registerGainFor(Sample f0Hz)
    {
        // README ## 8.1: ~0 in the bass, rising to 1 by C5 — where the duplex is a
        // real, functional part of the instrument.
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample g = std::pow(f0Hz / kDuplexRefHz, kRegisterExponent);
        if (g > 1.0) g = 1.0;
        return g;
    }

    Sample DuplexBank::aliquotHz(Sample f0Hz, int index)
    {
        // README ## 8.1: tuned to the HARMONIC n*f0 (not the string's inharmonic f_n) —
        // a separate short segment whose pitch is set by its own geometry, landing a
        // few cents off the speaking partial, which is the shimmer's beat.
        if (index < 0 || index >= kMaxStrings) return 0.0;
        if (f0Hz < 1.0) f0Hz = 1.0;
        return (Sample)kAliquotHarmonic[index] * f0Hz;
    }

    void DuplexBank::reset()
    {
        for (int i = 0; i < kMaxStrings; ++i)
            mSegments[i].reset();
    }

    void DuplexBank::update()
    {
        const Sample f0 = getProperty(fundamentalID);
        const Sample sr = AudioConfig::instance().sampleRate();
        const Sample nyquistLimit = kNyquistFraction * sr;

        mRegisterGain = registerGainFor(f0);
        // README ## 8.1: below the crossover the bank is switched OFF, not merely
        // quiet — no resonators run, so the bass pays nothing for a treble effect.
        mActive = (mRegisterGain >= kMinAudibleGain);
        if (!mActive)
        {
            mStringCount = 0;
            return;
        }

        mStringCount = 0;
        for (int i = 0; i < kMaxStrings; ++i)
        {
            const Sample fi = aliquotHz(f0, i);
            if (fi >= nyquistLimit) // the top notes' higher aliquots fall out near Nyquist
                continue;
            mSegments[i].setFrequencyHz(fi);
            mSegments[i].setDecaySeconds(kSegmentT60);
            // Higher aliquots carry less energy (1/n, the same mode-shape falloff the
            // string drive gets, ## 4). Incoherent-sum normalisation as in ## 8, so
            // changing kMaxStrings cannot rescale the output level.
            mSegWeight[i] = (1.0 / (Sample)kAliquotHarmonic[i]) / std::sqrt((Sample)kMaxStrings);
            mStringCount = i + 1;
        }
    }

    Sample DuplexBank::process(Sample stringMotion, int channel)
    {
        // Mono physics (REQ-piano-13): only channel 0 advances the bank.
        if (channel != 0 || !mActive)
            return 0.0;

        // README ## 8.1: a driven read-out of the speaking string — the resonators
        // ring at their aliquot frequencies where the string carries energy there,
        // and sustain (high Q) past the bridge-damped speaking partial. Feed-forward:
        // the result is radiated, never returned into the string drive.
        const Sample drive = stringMotion * getProperty(driveGainID);
        Sample sum = 0.0;
        for (int i = 0; i < mStringCount; ++i)
            sum += mSegWeight[i] * mSegments[i].out(drive, 0);

        return arstroFlush(sum * mRegisterGain);
    }
}
