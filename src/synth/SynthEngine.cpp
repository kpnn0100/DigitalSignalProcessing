#include "SynthEngine.h"
#include "ParamId.h"
#include "../base/AudioConfig.h"

namespace arstro
{
    SynthEngine::SynthEngine()
    {
        // Effects chain order: compressor -> overdrive -> chorus -> repeater -> reverb.
        mEffects.setIsParallel(false);
        mEffects.add(&mCompressor);
        mEffects.add(&mOverdrive);
        mEffects.add(&mChorus);
        mEffects.add(&mRepeater);
        mEffects.add(&mReverb);
    }

    void SynthEngine::pushParam(uint16_t id, Sample value)
    {
        Command c;
        c.type = Command::Param;
        c.id = id;
        c.value = value;
        mQueue.push(c);
    }
    void SynthEngine::pushNoteOn(int midiNote, Sample velocity)
    {
        Command c;
        c.type = Command::NoteOn;
        c.note = midiNote;
        c.value = velocity;
        mQueue.push(c);
    }
    void SynthEngine::pushNoteOff(int midiNote)
    {
        Command c;
        c.type = Command::NoteOff;
        c.note = midiNote;
        mQueue.push(c);
    }

    void SynthEngine::drainCommands()
    {
        Command c;
        while (mQueue.pop(c))
        {
            switch (c.type)
            {
            case Command::Param: applyParam(c.id, c.value); break;
            case Command::NoteOn: noteOn(c.note, c.value); break;
            case Command::NoteOff: noteOff(c.note); break;
            }
        }
    }

    void SynthEngine::noteOn(int midiNote, Sample velocity) { mVoices.noteOn(midiNote, velocity); }
    void SynthEngine::noteOff(int midiNote) { mVoices.noteOff(midiNote); }

    void SynthEngine::applyParam(uint16_t id, Sample value)
    {
        uint16_t group = id & 0xFF00;
        uint8_t off = id & 0x00FF;

        if (group == GROUP_OSC1 || group == GROUP_OSC2 || group == GROUP_OSC3)
        {
            int oi = (group >> 8) - 1; // oscillator index from the group
            if (oi < 0 || oi >= Voice::kOscCount)
                return; // e.g. OSC3 params ignored when kOscCount == 2
            for (int v = 0; v < mVoices.voiceCount(); ++v)
            {
                Voice &voice = mVoices.voice(v);
                Oscillator &o = voice.osc(oi);
                switch (off)
                {
                case OSC_VOICE_COUNT: o.setVoiceCount((int)(value + 0.5)); break;
                case OSC_DETUNE: o.setDetuneCents(value); break;
                case OSC_SPREAD: o.setStereoSpreadCents(value); break;
                case OSC_LEVEL: voice.setOscLevel(oi, value); break;
                case OSC_TUNE: voice.setOscTuneSemitones(oi, value); break;
                case OSC_ATTACK: o.setAttackMs(value); break;
                case OSC_DECAY: o.setDecayMs(value); break;
                case OSC_SUSTAIN: o.setSustain(value); break;
                case OSC_RELEASE: o.setReleaseMs(value); break;
                case OSC_WAVEFORM: o.setWaveform((Oscillator::Waveform)(int)(value + 0.5)); break;
                default: break;
                }
            }
            return;
        }

        // Bypass any effect node in the chain (FX_BYPASS offset).
        if (off == FX_BYPASS)
        {
            bool bp = value > 0.5;
            switch (group)
            {
            case GROUP_COMPRESSOR: mCompressor.setBypass(bp); break;
            case GROUP_OVERDRIVE: mOverdrive.setBypass(bp); break;
            case GROUP_CHORUS: mChorus.setBypass(bp); break;
            case GROUP_REPEATER: mRepeater.setBypass(bp); break;
            case GROUP_REVERB: mReverb.setBypass(bp); break;
            default: break;
            }
            return;
        }

        switch (group)
        {
        case GROUP_MASTER:
            if (off == MASTER_LEVEL) mMasterLevel = value;
            else if (off == (MASTER_OSC_SIMD & 0xFF)) Oscillator::setUseSimd(value > 0.5);
            break;
        case GROUP_COMPRESSOR:
            switch (off)
            {
            case CMP_THRESHOLD: mCompressor.setThresholdDb(value); break;
            case CMP_RATIO: mCompressor.setRatio(value); break;
            case CMP_ATTACK: mCompressor.setAttackMs(value); break;
            case CMP_RELEASE: mCompressor.setReleaseMs(value); break;
            case CMP_MAKEUP: mCompressor.setMakeupGain(value); break;
            }
            break;
        case GROUP_OVERDRIVE:
            switch (off)
            {
            case OD_DRIVE: mOverdrive.setDrive(value); break;
            case OD_TONE: mOverdrive.setToneHz(value); break;
            case OD_LEVEL: mOverdrive.setLevel(value); break;
            }
            break;
        case GROUP_CHORUS:
            switch (off)
            {
            case CH_RATE: mChorus.setRateHz(value); break;
            case CH_DEPTH: mChorus.setDepthMs(value); break;
            case CH_BASEDELAY: mChorus.setBaseDelayMs(value); break;
            case CH_MIX: mChorus.setMix(value); break;
            }
            break;
        case GROUP_REPEATER:
            switch (off)
            {
            case RP_DELAY: mRepeater.setDelayMs(value); break;
            case RP_FEEDBACK: mRepeater.setFeedback(value); break;
            case RP_TONE: mRepeater.setToneCutoffHz(value); break;
            case RP_MIX: mRepeater.setMix(value); break;
            }
            break;
        case GROUP_REVERB:
            switch (off)
            {
            case RV_DELAY: mReverb.setDelayInMs(value); break;
            case RV_DECAY: mReverb.setDecayInMs(value); break;
            case RV_LOWCUT: mReverb.setLowCutFrequency(value); break;
            case RV_HIGHCUT: mReverb.setHighCutFrequency(value); break;
            case RV_MIX: mReverb.setMix(value); break;
            case RV_WIDTH: mReverb.setWidth(value); break;
            }
            break;
        default:
            break;
        }
    }

    int SynthEngine::channels() const
    {
        return AudioConfig::instance().channelCount();
    }

    // ---- multi-core block API (voice-split) ----
    static void ensureBufs(std::vector<std::vector<Sample>> &b, int ch, int frames)
    {
        if ((int)b.size() != ch)
            b.assign(ch, std::vector<Sample>(frames));
        for (int c = 0; c < ch; ++c)
            if ((int)b[c].size() < frames)
                b[c].resize(frames);
    }

    // beginBlock: apply queued control + size buffers. Must finish before any
    // renderVoiceHalf() so the parallel section sees stable parameters.
    void SynthEngine::beginBlock(int frames)
    {
        drainCommands();
        int ch = channels();
        ensureBufs(mScratch, ch, frames);
        ensureBufs(mScratchA, ch, frames);
        ensureBufs(mScratchB, ch, frames);
    }

    // renderVoiceHalf: render a DISJOINT subset of voices into its own accumulator
    // (half 0 -> mScratchA, half 1 -> mScratchB), for all channels. Because the
    // two halves touch different Voice objects, the two halves may run on
    // different cores concurrently with no shared object access.
    void SynthEngine::renderVoiceHalf(int half, int frames)
    {
        int n = VoiceManager::kVoiceCount;
        int v0 = half ? n / 2 : 0;
        int v1 = half ? n : n / 2;
        auto &acc = half ? mScratchB : mScratchA;
        int ch = channels();
        for (int c = 0; c < ch; ++c)
        {
            Sample *buf = acc[c].data();
            arstroVecZero(buf, frames);
            mVoices.renderBlockRange(buf, frames, c, v0, v1);
        }
    }

    // finishBlock: sum the two voice halves, run the shared effects chain + master
    // (single core — no concurrent access to the shared effect objects), advance ages.
    void SynthEngine::finishBlock(int frames)
    {
        int ch = channels();
        for (int c = 0; c < ch; ++c)
        {
            Sample *dst = mScratch[c].data();
            const Sample *a = mScratchA[c].data();
            const Sample *b = mScratchB[c].data();
            for (int i = 0; i < frames; ++i)
                dst[i] = a[i] + b[i];
            mEffects.processBlock(dst, frames, c); // comp->od->chorus->repeater->reverb
            arstroVecMulC(dst, mMasterLevel, frames);  // master
        }
        mVoices.advance(frames);
    }

    void SynthEngine::packBytes(std::vector<uint8_t> &outBytes, int frames)
    {
        int ch = channels();
        outBytes.clear();
        std::vector<Sample> frame(ch);
        for (int f = 0; f < frames; ++f)
        {
            for (int c = 0; c < ch; ++c)
                frame[c] = mScratch[c][f];
            mOutput.appendFrame(outBytes, frame.data());
        }
    }

    void SynthEngine::renderBlockDouble(std::vector<Sample> &out, int frames)
    {
        beginBlock(frames);
        renderVoiceHalf(0, frames);
        renderVoiceHalf(1, frames);
        finishBlock(frames);
        int ch = channels();
        out.resize((size_t)frames * ch);
        for (int f = 0; f < frames; ++f)
            for (int c = 0; c < ch; ++c)
                out[(size_t)f * ch + c] = mScratch[c][f];
    }

    void SynthEngine::renderBlockBytes(std::vector<uint8_t> &outBytes, int frames)
    {
        beginBlock(frames);
        renderVoiceHalf(0, frames);
        renderVoiceHalf(1, frames);
        finishBlock(frames);
        packBytes(outBytes, frames);
    }
}
