#include "Oscillator.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace gyrus_space
{
    static inline Sample polyBlep(Sample t, Sample dt)
    {
        // Corrects the saw discontinuity to suppress aliasing.
        if (dt <= 0.0)
            return 0.0;
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        else if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    bool Oscillator::sUseSimd = false;

    Oscillator::Oscillator() : SignalGenerator(propertyCount)
    {
        setSmoothEnable(false);
        initProperty(voiceCountID, 1);
        initProperty(detuneID, 15.0); // 15 cents total spread by default
        initProperty(spreadID, 0.0);
        mVoiceCents.assign(kMaxVoices, 0.0);
        ensureChannels();
        recalcVoiceCents();
        recalcIncrements();
    }

    void Oscillator::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        size_t need = (size_t)n * kMaxVoices;
        if (mPhase.size() != need)
        {
            mPhase.assign(need, 0.0);
            mInc.assign(need, 0.0);
            mPhaseU.assign(need, 0u);
            mIncU.assign(need, 0u);
        }
    }

    void Oscillator::setVoiceCount(int voices)
    {
        if (voices < 1) voices = 1;
        if (voices > kMaxVoices) voices = kMaxVoices;
        setProperty(voiceCountID, voices);
    }
    void Oscillator::setDetuneCents(Sample cents) { setProperty(detuneID, cents); }
    void Oscillator::setStereoSpreadCents(Sample cents) { setProperty(spreadID, cents); }

    void Oscillator::setFrequency(Sample hz)
    {
        SignalGenerator::setFrequency(hz);
        recalcIncrements();
    }

    void Oscillator::update()
    {
        recalcVoiceCents();
        recalcIncrements();
    }

    void Oscillator::onSampleRateChanged()
    {
        recalcIncrements();
    }

    void Oscillator::onChannelCountChanged()
    {
        ensureChannels();
        recalcIncrements();
    }

    void Oscillator::recalcVoiceCents()
    {
        int vc = (int)(getProperty(voiceCountID) + 0.5);
        if (vc < 1) vc = 1;
        if (vc > kMaxVoices) vc = kMaxVoices;
        // Cache voice count + normalization so generate() needs no getProperty
        // and no per-sample sqrt (this was ~the dominant oscillator cost).
        mVoiceCount = vc;
        mNorm = (Sample)(1.0 / std::sqrt((double)vc));
        Sample detune = getProperty(detuneID);
        mVoiceCents.assign(kMaxVoices, 0.0);
        if (vc == 1)
        {
            mVoiceCents[0] = 0.0;
            return;
        }
        // Spread voices evenly across [-detune/2, +detune/2].
        for (int v = 0; v < vc; ++v)
        {
            Sample r = (Sample)v / (Sample)(vc - 1); // 0..1
            mVoiceCents[v] = (r - 0.5) * detune;
        }
    }

    void Oscillator::recalcIncrements()
    {
        ensureChannels();
        Sample sr = AudioConfig::instance().sampleRate();
        int ch = AudioConfig::instance().channelCount();
        Sample spread = getProperty(spreadID);
        for (int c = 0; c < ch; ++c)
        {
            // Symmetric per-channel offset for stereo width.
            Sample chOffset = (ch > 1) ? ((Sample)c - (ch - 1) / 2.0) * spread : 0.0;
            for (int v = 0; v < kMaxVoices; ++v)
            {
                Sample cents = mVoiceCents[v] + chOffset;
                Sample freq = mFrequency * std::pow(2.0, cents / 1200.0);
                double cyclesPerSample = (double)freq / sr;
                mInc[(size_t)c * kMaxVoices + v] = (Sample)cyclesPerSample;
                // fixed-point increment for the SIMD path (phase in full uint32 range)
                double f = cyclesPerSample - std::floor(cyclesPerSample); // wrap to [0,1)
                mIncU[(size_t)c * kMaxVoices + v] = (uint32_t)(f * 4294967296.0);
            }
        }
    }

    // Branchless block oscillator: each voice is a fixed-point phase accumulator
    // (uint32 add wraps naturally = mod 1.0), naive saw = (int32)phase * 2^-31.
    // No per-sample branches -> the inner loops vectorize; the cross-voice mix and
    // envelope are a single linear pass. Trades PolyBLEP anti-aliasing for speed.
    void Oscillator::addBlockSimd(Sample *buf, int frames, int channel, Sample level)
    {
        constexpr int kMaxBlock = 1024;
        if (frames > kMaxBlock) { SignalGenerator::addBlock(buf, frames, channel, level); return; }
        const int vc = mVoiceCount;
        const size_t base = (size_t)channel * kMaxVoices;
        Sample acc[kMaxBlock];
        for (int i = 0; i < frames; ++i) acc[i] = (Sample)0;
        const Sample k = (Sample)(1.0 / 2147483648.0); // 1/2^31
        for (int v = 0; v < vc; ++v)
        {
            uint32_t ph = mPhaseU[base + v];
            const uint32_t inc = mIncU[base + v];
            for (int i = 0; i < frames; ++i)
            {
                acc[i] += (Sample)((int32_t)ph) * k; // saw in [-1,1), branchless
                ph += inc;                           // natural wrap = mod
            }
            mPhaseU[base + v] = ph;
        }
        const Sample g = level * mNorm;
        for (int i = 0; i < frames; ++i)
            buf[i] += g * acc[i] * mEnvelope.nextValue(channel);
    }

    void Oscillator::addBlock(Sample *buf, int frames, int channel, Sample level)
    {
        if (sUseSimd)
            addBlockSimd(buf, frames, channel, level);
        else
            SignalGenerator::addBlock(buf, frames, channel, level); // scalar PolyBLEP
    }

    Sample Oscillator::generate(int channel)
    {
        const int vc = mVoiceCount;
        Sample sum = 0.0;
        Sample *phase = &mPhase[(size_t)channel * kMaxVoices];
        const Sample *inc = &mInc[(size_t)channel * kMaxVoices];
        for (int v = 0; v < vc; ++v)
        {
            Sample dt = inc[v];
            Sample t = phase[v];
            sum += 2.0 * t - 1.0 - polyBlep(t, dt);
            t += dt;
            if (t >= 1.0)
                t -= 1.0;
            phase[v] = t;
        }
        return sum * mNorm; // precomputed 1/sqrt(voiceCount)
    }
}
