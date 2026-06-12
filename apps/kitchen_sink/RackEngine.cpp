#include "RackEngine.h"
#include <cmath>
#include <sstream>

namespace arstro
{
namespace kitchen
{
    // Helper: current sample rate (for ms->samples conversions in setters).
    static double sr() { return AudioConfig::instance().sampleRate(); }

    // The effect catalogue. Each entry: display name, factory, parameter list.
    // Setters cast to the concrete type the factory guarantees.
    const std::vector<NodeType> &nodeTypes()
    {
        static const std::vector<NodeType> types = {
            {"Gain",
             [] { return std::unique_ptr<SignalProcessor>(new Gain(1.0)); },
             {{"gain", 0.0, 2.0, 1.0, [](SignalProcessor &p, double v) { static_cast<Gain &>(p).setGain(v); }}}},

            {"Delay",
             [] { return std::unique_ptr<SignalProcessor>(new Delay(0.0, (int)(2.0 * sr()))); },
             {{"time_ms", 1.0, 1000.0, 200.0,
               [](SignalProcessor &p, double v) { static_cast<Delay &>(p).setDelay(v * 0.001 * sr()); }}}},

            {"LowPass",
             [] { return std::unique_ptr<SignalProcessor>(new LowPassFilter()); },
             {{"cutoff_hz", 50.0, 18000.0, 2000.0,
               [](SignalProcessor &p, double v) { static_cast<LowPassFilter &>(p).setCutoffFrequency(v); }}}},

            {"HighPass",
             [] { return std::unique_ptr<SignalProcessor>(new HighPassFilter()); },
             {{"cutoff_hz", 20.0, 12000.0, 300.0,
               [](SignalProcessor &p, double v) { static_cast<HighPassFilter &>(p).setCutoffFrequency(v); }}}},

            {"Chorus",
             [] { return std::unique_ptr<SignalProcessor>(new Chorus()); },
             {{"rate_hz", 0.05, 8.0, 1.0, [](SignalProcessor &p, double v) { static_cast<Chorus &>(p).setRateHz(v); }},
              {"depth_ms", 0.5, 12.0, 3.0, [](SignalProcessor &p, double v) { static_cast<Chorus &>(p).setDepthMs(v); }},
              {"mix", 0.0, 1.0, 0.5, [](SignalProcessor &p, double v) { static_cast<Chorus &>(p).setMix(v); }}}},

            {"Overdrive",
             [] { return std::unique_ptr<SignalProcessor>(new Overdrive()); },
             {{"drive", 1.0, 20.0, 3.0, [](SignalProcessor &p, double v) { static_cast<Overdrive &>(p).setDrive(v); }},
              {"tone_hz", 500.0, 12000.0, 4000.0, [](SignalProcessor &p, double v) { static_cast<Overdrive &>(p).setToneHz(v); }},
              {"level", 0.0, 1.0, 0.8, [](SignalProcessor &p, double v) { static_cast<Overdrive &>(p).setLevel(v); }}}},

            {"Compressor",
             [] { return std::unique_ptr<SignalProcessor>(new Compressor()); },
             {{"threshold_db", -48.0, 0.0, -18.0, [](SignalProcessor &p, double v) { static_cast<Compressor &>(p).setThresholdDb(v); }},
              {"ratio", 1.0, 20.0, 4.0, [](SignalProcessor &p, double v) { static_cast<Compressor &>(p).setRatio(v); }},
              {"attack_ms", 1.0, 100.0, 10.0, [](SignalProcessor &p, double v) { static_cast<Compressor &>(p).setAttackMs(v); }},
              {"release_ms", 10.0, 500.0, 100.0, [](SignalProcessor &p, double v) { static_cast<Compressor &>(p).setReleaseMs(v); }},
              {"makeup", 1.0, 8.0, 1.0, [](SignalProcessor &p, double v) { static_cast<Compressor &>(p).setMakeupGain(v); }}}},

            {"Repeater",
             [] { return std::unique_ptr<SignalProcessor>(new Repeater()); },
             {{"delay_ms", 20.0, 1000.0, 250.0, [](SignalProcessor &p, double v) { static_cast<Repeater &>(p).setDelayMs(v); }},
              {"feedback", 0.0, 0.99, 0.4, [](SignalProcessor &p, double v) { static_cast<Repeater &>(p).setFeedback(v); }},
              {"tone_hz", 500.0, 12000.0, 4000.0, [](SignalProcessor &p, double v) { static_cast<Repeater &>(p).setToneCutoffHz(v); }},
              {"mix", 0.0, 1.0, 0.3, [](SignalProcessor &p, double v) { static_cast<Repeater &>(p).setMix(v); }}}},

            {"Reverb",
             [] { return std::unique_ptr<SignalProcessor>(new Reverb()); },
             {{"delay_ms", 5.0, 150.0, 60.0, [](SignalProcessor &p, double v) { static_cast<Reverb &>(p).setDelayInMs(v); }},
              {"decay_ms", 100.0, 5000.0, 800.0, [](SignalProcessor &p, double v) { static_cast<Reverb &>(p).setDecayInMs(v); }},
              {"mix", 0.0, 1.0, 0.3, [](SignalProcessor &p, double v) { static_cast<Reverb &>(p).setMix(v); }},
              {"width", 0.0, 1.0, 0.6, [](SignalProcessor &p, double v) { static_cast<Reverb &>(p).setWidth(v); }}}},
        };
        return types;
    }

    RackEngine::RackEngine()
    {
        AudioConfig::instance().setChannelCount(2);
        AudioConfig::instance().setBufferSize(128);
        mOsc.setVoiceCount(1);
        mOsc.setFrequency(220.0);
        mOsc.setAttackMs(10.0);
        mOsc.setDecayMs(120.0);
        mOsc.setSustain(0.7);
        mOsc.setReleaseMs(200.0);
    }

    void RackEngine::setSampleRate(double s) { AudioConfig::instance().setSampleRate(s); }
    void RackEngine::setMasterLevel(double level) { mMaster = level; }

    void RackEngine::setWaveform(int w) { mOsc.setWaveform((Oscillator::Waveform)w); }
    void RackEngine::setFrequency(double hz) { mOsc.setFrequency(hz); }
    void RackEngine::setVoiceCount(int v) { mOsc.setVoiceCount(v); }
    void RackEngine::setDetuneCents(double c) { mOsc.setDetuneCents(c); }
    void RackEngine::setAttackMs(double ms) { mOsc.setAttackMs(ms); }
    void RackEngine::setDecayMs(double ms) { mOsc.setDecayMs(ms); }
    void RackEngine::setSustain(double s) { mOsc.setSustain(s); }
    void RackEngine::setReleaseMs(double ms) { mOsc.setReleaseMs(ms); }

    double RackEngine::midiToHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

    void RackEngine::noteOnMidi(int midi, double velocity)
    {
        mCurrentNote = midi;
        mOsc.setFrequency(midiToHz(midi));
        mOsc.noteOn(velocity);
    }

    void RackEngine::noteOff(int midi)
    {
        if (midi == mCurrentNote) // last-note priority: ignore stale releases
        {
            mOsc.noteOff();
            mCurrentNote = -1;
        }
    }

    int RackEngine::nodeCount() const { return (int)mNodes.size(); }
    int RackEngine::nodeTypeAt(int i) const
    {
        return (i >= 0 && i < (int)mNodeTypeIdx.size()) ? mNodeTypeIdx[i] : -1;
    }

    void RackEngine::addNode(int typeIndex)
    {
        if (typeIndex < 0 || typeIndex >= (int)nodeTypes().size())
            return;
        const NodeType &t = nodeTypes()[typeIndex];
        auto node = t.make();
        // Apply each parameter's default so the node starts in a sane state.
        for (const auto &pd : t.params)
            pd.apply(*node, pd.def);
        mNodes.push_back(std::move(node));
        mNodeTypeIdx.push_back(typeIndex);
    }

    void RackEngine::removeNode(int i)
    {
        if (i < 0 || i >= (int)mNodes.size())
            return;
        mNodes.erase(mNodes.begin() + i);
        mNodeTypeIdx.erase(mNodeTypeIdx.begin() + i);
    }

    void RackEngine::moveNode(int i, int dir)
    {
        int j = i + dir;
        if (i < 0 || i >= (int)mNodes.size() || j < 0 || j >= (int)mNodes.size())
            return;
        std::swap(mNodes[i], mNodes[j]);
        std::swap(mNodeTypeIdx[i], mNodeTypeIdx[j]);
    }

    void RackEngine::setNodeParam(int i, int paramIdx, double value)
    {
        if (i < 0 || i >= (int)mNodes.size())
            return;
        const auto &params = nodeTypes()[mNodeTypeIdx[i]].params;
        if (paramIdx < 0 || paramIdx >= (int)params.size())
            return;
        params[paramIdx].apply(*mNodes[i], value);
    }

    void RackEngine::render(float *interleaved, int frames)
    {
        const int ch = AudioConfig::instance().channelCount();
        for (int n = 0; n < frames; ++n)
        {
            for (int c = 0; c < ch; ++c)
            {
                Sample s = mOsc.out(0.0, c);          // generator (+ ADSR)
                for (auto &node : mNodes)             // user-ordered effect chain
                    s = node->out(s, c);
                interleaved[n * ch + c] = (float)(s * mMaster);
            }
        }
    }

    const float *RackEngine::renderInternal(int frames)
    {
        const int ch = AudioConfig::instance().channelCount();
        mScratch.resize((size_t)frames * ch);
        render(mScratch.data(), frames);
        return mScratch.data();
    }

    std::string RackEngine::nodeTypesJson()
    {
        std::ostringstream o;
        o << "[";
        const auto &types = nodeTypes();
        for (size_t t = 0; t < types.size(); ++t)
        {
            o << (t ? "," : "") << "{\"name\":\"" << types[t].name << "\",\"params\":[";
            for (size_t p = 0; p < types[t].params.size(); ++p)
            {
                const auto &pd = types[t].params[p];
                o << (p ? "," : "") << "{\"name\":\"" << pd.name << "\",\"min\":" << pd.min
                  << ",\"max\":" << pd.max << ",\"def\":" << pd.def << "}";
            }
            o << "]}";
        }
        o << "]";
        return o.str();
    }
}
}
