#include "Device.h"
#include "../base/AudioConfig.h"
#include "../effects/Chorus.h"
#include "../effects/Compressor.h"
#include "../effects/Overdrive.h"
#include "../effects/Repeater.h"
#include "../equalizer/ParametricEQ.h"
#include "../equalizer/StateVariableFilter.h"
#include "../instrument/BasicSynth.h"
#include "../instrument/DrumMachine.h"
#include "../reverb/Reverb.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    // ── ParamSpec / DeviceType / Device ─────────────────────────────────────────────────────

    double ParamSpec::clamp(double v) const
    {
        if (!std::isfinite(v)) return def;
        const double lo = isChoice() ? 0.0 : min;
        const double hi = isChoice() ? (double)choices.size() - 1.0 : max;
        v = std::clamp(v, lo, hi);
        return (integer || isChoice()) ? std::round(v) : v;
    }

    int DeviceType::paramIndex(const std::string &n) const
    {
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].name == n) return (int)i;
        return -1;
    }

    Device::Device(const DeviceType &type) : mType(&type)
    {
        for (const auto &p : type.params) mValues.push_back(p.clamp(p.def));
    }

    double Device::param(int index) const
    {
        return (index >= 0 && index < (int)mValues.size()) ? mValues[index] : 0.0;
    }

    bool Device::setParam(int index, double value)
    {
        if (index < 0 || index >= (int)mValues.size()) return false;
        mValues[index] = mType->params[index].clamp(value);
        apply(index, mValues[index]);
        return true;
    }

    bool Device::setParam(const std::string &name, double value) { return setParam(mType->paramIndex(name), value); }

    void Device::applyAll()
    {
        for (int i = 0; i < (int)mValues.size(); ++i) apply(i, mValues[i]);
    }

    namespace
    {
        // (R1) dB → linear amplitude
        double lin(double db) { return std::pow(10.0, db / 20.0); }

        ParamSpec num(const std::string &name, const std::string &label, const std::string &unit, double lo,
                      double hi, double def, bool logScale = false, bool integer = false)
        {
            ParamSpec p;
            p.name = name; p.label = label; p.unit = unit;
            p.min = lo; p.max = hi; p.def = def;
            p.logScale = logScale; p.integer = integer;
            return p;
        }
        ParamSpec choice(const std::string &name, const std::string &label, std::vector<std::string> values, int def)
        {
            ParamSpec p;
            p.name = name; p.label = label;
            p.choices = std::move(values);
            p.min = 0; p.max = (double)p.choices.size() - 1; p.def = def;
            return p;
        }

        /** One parameter and how it reaches its DSP object. */
        template <class T> struct Binding
        {
            ParamSpec spec;
            std::function<void(T &, double)> set;
        };
        template <class T> std::vector<ParamSpec> specsOf(const std::vector<Binding<T>> &b)
        {
            std::vector<ParamSpec> out;
            for (const auto &x : b) out.push_back(x.spec);
            return out;
        }

        /** Run every channel the library is configured for through a per-sample processor. Channel by
         *  channel, as the library's own block path does (reverb/README.md "why decorrelation"). */
        void runChannels(SignalProcessor &p, Sample *const *io, int channels, int frames)
        {
            const int n = std::min(channels, AudioConfig::instance().channelCount());
            for (int c = 0; c < n; ++c) p.processBlock(io[c], frames, c);
        }

        // ── instruments ─────────────────────────────────────────────────────────────────────

        const std::vector<std::string> kWaves = {"sine", "saw", "square", "triangle"};
        const std::vector<std::string> kModes = {"lowpass", "bandpass", "highpass", "notch"};

        void oscBindings(std::vector<Binding<BasicSynth::Params>> &b, const std::string &k,
                         BasicSynth::Osc BasicSynth::Params::*o, const BasicSynth::Osc &d)
        {
            using P = BasicSynth::Params;
            b.push_back({choice(k + ".wave", "Wave", kWaves, (int)d.wave), [o](P &p, double v) { (p.*o).wave = (BasicSynth::Wave)(int)v; }});
            b.push_back({num(k + ".octave", "Octave", "oct", -3, 3, d.octave, false, true), [o](P &p, double v) { (p.*o).octave = (int)v; }});
            b.push_back({num(k + ".semi", "Semi", "st", -12, 12, d.semi, false, true), [o](P &p, double v) { (p.*o).semi = v; }});
            b.push_back({num(k + ".fine", "Fine", "ct", -100, 100, d.fine), [o](P &p, double v) { (p.*o).fine = v; }});
            b.push_back({num(k + ".level", "Level", "", 0, 1, d.level), [o](P &p, double v) { (p.*o).level = v; }});
            b.push_back({num(k + ".voices", "Unison", "", 1, 5, d.voices, false, true), [o](P &p, double v) { (p.*o).voices = (int)v; }});
            b.push_back({num(k + ".detune", "Detune", "ct", 0, 100, d.detune), [o](P &p, double v) { (p.*o).detune = v; }});
        }
        void envBindings(std::vector<Binding<BasicSynth::Params>> &b, const std::string &k,
                         BasicSynth::Env BasicSynth::Params::*e, const BasicSynth::Env &d)
        {
            using P = BasicSynth::Params;
            b.push_back({num(k + ".attack", "Attack", "ms", 0, 10000, d.attack, true), [e](P &p, double v) { (p.*e).attack = v; }});
            b.push_back({num(k + ".decay", "Decay", "ms", 0, 10000, d.decay, true), [e](P &p, double v) { (p.*e).decay = v; }});
            b.push_back({num(k + ".sustain", "Sustain", "", 0, 1, d.sustain), [e](P &p, double v) { (p.*e).sustain = v; }});
            b.push_back({num(k + ".release", "Release", "ms", 0, 10000, d.release, true), [e](P &p, double v) { (p.*e).release = v; }});
        }

        const std::vector<Binding<BasicSynth::Params>> &synthBindings()
        {
            using P = BasicSynth::Params;
            static const std::vector<Binding<P>> b = [] {
                const P d; // the instrument's own defaults ARE the registry's
                std::vector<Binding<P>> v;
                oscBindings(v, "osc1", &P::osc1, d.osc1);
                oscBindings(v, "osc2", &P::osc2, d.osc2);
                v.push_back({num("noise", "Noise", "", 0, 1, d.noise), [](P &p, double x) { p.noise = x; }});
                v.push_back({choice("filter.mode", "Mode", kModes, (int)d.filterMode), [](P &p, double x) { p.filterMode = (StateVariableFilter::Mode)(int)x; }});
                v.push_back({num("filter.cutoff", "Cutoff", "Hz", 20, 20000, d.cutoff, true), [](P &p, double x) { p.cutoff = x; }});
                v.push_back({num("filter.res", "Resonance", "", 0, 1, d.resonance), [](P &p, double x) { p.resonance = x; }});
                v.push_back({num("filter.env", "Env Amount", "oct", -8, 8, d.envAmount), [](P &p, double x) { p.envAmount = x; }});
                v.push_back({num("filter.keytrack", "Key Track", "", 0, 1, d.keytrack), [](P &p, double x) { p.keytrack = x; }});
                envBindings(v, "fenv", &P::filterEnv, d.filterEnv);
                envBindings(v, "amp", &P::ampEnv, d.ampEnv);
                v.push_back({num("volume", "Volume", "dB", -60, 6, d.volumeDb), [](P &p, double x) { p.volumeDb = x; }});
                v.push_back({num("velocity", "Velocity", "", 0, 1, d.velocity), [](P &p, double x) { p.velocity = x; }});
                return v;
            }();
            return b;
        }

        class SynthDevice : public Device
        {
        public:
            explicit SynthDevice(const DeviceType &t) : Device(t), mP(mSynth.params()) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mSynth.render(io, channels, frames); }
            void noteOn(int n, int v) override { mSynth.noteOn(n, v); }
            void noteOff(int n) override { mSynth.noteOff(n); }
            void allNotesOff() override { mSynth.allNotesOff(); }
            void reset() override { mSynth.reset(); }
            int activeVoices() const override { return mSynth.activeVoices(); }

        protected:
            void apply(int i, double v) override
            {
                synthBindings()[i].set(mP, v);
                mSynth.setParams(mP);
            }

        private:
            BasicSynth mSynth;
            BasicSynth::Params mP;
        };

        const std::vector<Binding<DrumMachine>> &drumBindings()
        {
            static const std::vector<Binding<DrumMachine>> b = [] {
                std::vector<Binding<DrumMachine>> v;
                for (int i = 0; i < DrumMachine::PadCount; ++i)
                {
                    const auto pad = (DrumMachine::Pad)i;
                    const std::string k = DrumMachine::padName(pad);
                    const auto d = DrumMachine::defaults(pad);
                    // each binding edits one field of one pad's PadParams
                    auto field = [pad](Sample DrumMachine::PadParams::*f) {
                        return [pad, f](DrumMachine &m, double x) { auto p = m.pad(pad); p.*f = x; m.setPad(pad, p); };
                    };
                    v.push_back({num(k + ".tune", "Tune", "st", -12, 12, d.tune), field(&DrumMachine::PadParams::tune)});
                    v.push_back({num(k + ".decay", "Decay", "ms", 10, 4000, d.decay, true), field(&DrumMachine::PadParams::decay)});
                    v.push_back({num(k + ".tone", "Tone", "", 0, 1, d.tone), field(&DrumMachine::PadParams::tone)});
                    v.push_back({num(k + ".level", "Level", "dB", -60, 6, d.levelDb), field(&DrumMachine::PadParams::levelDb)});
                    v.push_back({num(k + ".pan", "Pan", "", -1, 1, d.pan), field(&DrumMachine::PadParams::pan)});
                }
                v.push_back({num("volume", "Volume", "dB", -60, 6, -6.0), [](DrumMachine &m, double x) { m.setVolumeDb(x); }});
                return v;
            }();
            return b;
        }

        class DrumDevice : public Device
        {
        public:
            explicit DrumDevice(const DeviceType &t) : Device(t) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mDrums.render(io, channels, frames); }
            void noteOn(int n, int v) override { mDrums.noteOn(n, v); }
            void noteOff(int n) override { mDrums.noteOff(n); }
            void reset() override { mDrums.reset(); }
            int activeVoices() const override { return mDrums.activeVoices(); }

        protected:
            void apply(int i, double v) override { drumBindings()[i].set(mDrums, v); }

        private:
            DrumMachine mDrums;
        };

        // ── effects: a SignalProcessor and its bindings ─────────────────────────────────────

        template <class Proc> class EffectDevice : public Device
        {
        public:
            EffectDevice(const DeviceType &t, const std::vector<Binding<Proc>> &b) : Device(t), mB(b) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { runChannels(mProc, io, channels, frames); }
            void reset() override { resetOf(mProc); }

        protected:
            void apply(int i, double v) override { mB[i].set(mProc, v); }

        private:
            // Each processor clears its memory its own way; one without a reset keeps its state.
            static void resetOf(ParametricEQ &p) { p.reset(); }
            template <class Q> static void resetOf(Q &) {}
            Proc mProc;
            const std::vector<Binding<Proc>> &mB;
        };

        const std::vector<Binding<Compressor>> &compressorBindings()
        {
            static const std::vector<Binding<Compressor>> b = {
                {num("threshold", "Threshold", "dB", -60, 0, -18), [](Compressor &c, double x) { c.setThresholdDb(x); }},
                {num("ratio", "Ratio", ":1", 1, 20, 4, true), [](Compressor &c, double x) { c.setRatio(x); }},
                {num("attack", "Attack", "ms", 0.1, 200, 5, true), [](Compressor &c, double x) { c.setAttackMs(x); }},
                {num("release", "Release", "ms", 5, 2000, 100, true), [](Compressor &c, double x) { c.setReleaseMs(x); }},
                {num("makeup", "Makeup", "dB", 0, 24, 0), [](Compressor &c, double x) { c.setMakeupGain(lin(x)); }},
            };
            return b;
        }

        const std::vector<Binding<ParametricEQ>> &eqBindings()
        {
            using E = ParametricEQ;
            static const std::vector<Binding<E>> b = [] {
                std::vector<Binding<E>> v;
                const std::vector<std::string> onOff = {"off", "on"};
                auto freq = [](int band) { return [band](E &e, double x) { e.band(band).setFrequency(x); }; };
                auto gain = [](int band) { return [band](E &e, double x) { e.band(band).setGainDb(x); }; };
                auto q = [](int band) { return [band](E &e, double x) { e.band(band).setQ(x); }; };
                v.push_back({choice("lowcut.on", "Low Cut", onOff, 0), [](E &e, double x) { e.setBandEnabled(E::LowCut, x > 0.5); }});
                v.push_back({num("lowcut.freq", "Low Cut", "Hz", 20, 1000, 30, true), freq(E::LowCut)});
                v.push_back({num("lowshelf.freq", "Low Shelf", "Hz", 20, 1000, 100, true), freq(E::LowShelf)});
                v.push_back({num("lowshelf.gain", "Low Shelf", "dB", -18, 18, 0), gain(E::LowShelf)});
                const int peaks[3] = {E::Peak1, E::Peak2, E::Peak3};
                const double hz[3] = {250, 1000, 4000};
                for (int k = 0; k < 3; ++k)
                {
                    const std::string n = "peak" + std::to_string(k + 1);
                    v.push_back({num(n + ".freq", "Peak " + std::to_string(k + 1), "Hz", 20, 20000, hz[k], true), freq(peaks[k])});
                    v.push_back({num(n + ".gain", "Gain", "dB", -18, 18, 0), gain(peaks[k])});
                    v.push_back({num(n + ".q", "Q", "", 0.1, 10, 1, true), q(peaks[k])});
                }
                v.push_back({num("highshelf.freq", "High Shelf", "Hz", 1000, 20000, 8000, true), freq(E::HighShelf)});
                v.push_back({num("highshelf.gain", "High Shelf", "dB", -18, 18, 0), gain(E::HighShelf)});
                v.push_back({choice("highcut.on", "High Cut", onOff, 0), [](E &e, double x) { e.setBandEnabled(E::HighCut, x > 0.5); }});
                v.push_back({num("highcut.freq", "High Cut", "Hz", 1000, 20000, 18000, true), freq(E::HighCut)});
                return v;
            }();
            return b;
        }

        const std::vector<Binding<Reverb>> &reverbBindings()
        {
            static const std::vector<Binding<Reverb>> b = {
                {num("size", "Size", "ms", 10, 300, 100, true), [](Reverb &r, double x) { r.setDelayInMs(x); }},
                {num("decay", "Decay", "ms", 100, 10000, 1500, true), [](Reverb &r, double x) { r.setDecayInMs(x); }},
                {num("lowcut", "Low Cut", "Hz", 20, 2000, 100, true), [](Reverb &r, double x) { r.setLowCutFrequency(x); }},
                {num("highcut", "High Cut", "Hz", 1000, 20000, 8000, true), [](Reverb &r, double x) { r.setHighCutFrequency(x); }},
                {num("width", "Width", "", 0, 1, 0.6), [](Reverb &r, double x) { r.setWidth(x); }},
                {num("mix", "Mix", "", 0, 1, 0.25), [](Reverb &r, double x) { r.setMix(x); }},
            };
            return b;
        }

        const std::vector<Binding<Repeater>> &delayBindings()
        {
            static const std::vector<Binding<Repeater>> b = {
                {num("time", "Time", "ms", 1, 2000, 375, true), [](Repeater &r, double x) { r.setDelayMs(x); }},
                {num("feedback", "Feedback", "", 0, 0.95, 0.4), [](Repeater &r, double x) { r.setFeedback(x); }},
                {num("tone", "Tone", "Hz", 200, 20000, 4000, true), [](Repeater &r, double x) { r.setToneCutoffHz(x); }},
                {num("mix", "Mix", "", 0, 1, 0.3), [](Repeater &r, double x) { r.setMix(x); }},
            };
            return b;
        }

        const std::vector<Binding<Chorus>> &chorusBindings()
        {
            static const std::vector<Binding<Chorus>> b = {
                {num("rate", "Rate", "Hz", 0.05, 10, 1.5, true), [](Chorus &c, double x) { c.setRateHz(x); }},
                {num("depth", "Depth", "ms", 0, 10, 3), [](Chorus &c, double x) { c.setDepthMs(x); }},
                {num("delay", "Delay", "ms", 1, 40, 12), [](Chorus &c, double x) { c.setBaseDelayMs(x); }},
                {num("mix", "Mix", "", 0, 1, 0.5), [](Chorus &c, double x) { c.setMix(x); }},
            };
            return b;
        }

        const std::vector<Binding<Overdrive>> &driveBindings()
        {
            static const std::vector<Binding<Overdrive>> b = {
                {num("drive", "Drive", "x", 1, 50, 2, true), [](Overdrive &o, double x) { o.setDrive(x); }},
                {num("tone", "Tone", "Hz", 500, 20000, 5000, true), [](Overdrive &o, double x) { o.setToneHz(x); }},
                {num("level", "Level", "dB", -24, 12, 0), [](Overdrive &o, double x) { o.setLevel(lin(x)); }},
            };
            return b;
        }

        /** The Filter effect: the synth's SVF on a strip, with a dry/wet mix. */
        class FilterDevice : public Device
        {
        public:
            explicit FilterDevice(const DeviceType &t) : Device(t) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override
            {
                const int n = std::min(channels, AudioConfig::instance().channelCount());
                for (int c = 0; c < n; ++c)
                    for (int i = 0; i < frames; ++i)
                    {
                        const Sample x = io[c][i];
                        io[c][i] = mMix * mSvf.out(x, c) + (1.0 - mMix) * x; // (R2) dry/wet
                    }
            }
            void reset() override { mSvf.reset(); }

        protected:
            void apply(int i, double v) override
            {
                switch (i)
                {
                case 0: mSvf.setMode((StateVariableFilter::Mode)(int)v); break;
                case 1: mSvf.setCutoff(v); break;
                case 2: mSvf.setResonance(v); break;
                default: mMix = v; break;
                }
            }

        private:
            StateVariableFilter mSvf;
            Sample mMix = 1.0;
        };

        template <class Proc>
        DeviceType effectType(const std::string &name, const std::string &label, const std::string &summary,
                              const std::vector<Binding<Proc>> &(*bindings)())
        {
            DeviceType t;
            t.name = name; t.label = label; t.summary = summary;
            t.kind = DeviceKind::Effect;
            t.params = specsOf(bindings());
            t.create = [bindings](const DeviceType &self) -> std::unique_ptr<Device> {
                return std::make_unique<EffectDevice<Proc>>(self, bindings());
            };
            return t;
        }
    }

    const std::vector<DeviceType> &DeviceRegistry::types()
    {
        static const std::vector<DeviceType> all = [] {
            std::vector<DeviceType> v;
            DeviceType synth;
            synth.name = "synth"; synth.label = "Basic Synth"; synth.kind = DeviceKind::Instrument;
            synth.summary = "Two oscillators and noise through a resonant filter and an amplitude envelope; 16 voices.";
            synth.params = specsOf(synthBindings());
            synth.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<SynthDevice>(self); };
            v.push_back(synth);

            DeviceType drums;
            drums.name = "drums"; drums.label = "Drum Machine"; drums.kind = DeviceKind::Instrument;
            drums.summary = "Ten synthesized pads on the GM drum notes (36 kick … 56 cowbell); the closed hat chokes the open hat.";
            drums.params = specsOf(drumBindings());
            drums.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<DrumDevice>(self); };
            {
                // the pads' keys, named for a person (the prefixes are the parameters' names)
                static const char *kLabels[DrumMachine::PadCount] = {"Kick", "Rim", "Snare", "Clap", "Low Tom",
                                                                     "Closed Hat", "Mid Tom", "Open Hat", "High Tom", "Cowbell"};
                for (int p = 0; p < DrumMachine::PadCount; ++p)
                    drums.noteNames.emplace_back(DrumMachine::noteFor((DrumMachine::Pad)p), kLabels[p]);
                std::sort(drums.noteNames.begin(), drums.noteNames.end());
            }
            v.push_back(drums);

            v.push_back(effectType<Compressor>("compressor", "Compressor", "Feed-forward peak compressor.", &compressorBindings));
            v.push_back(effectType<ParametricEQ>("eq", "EQ", "Low cut, low shelf, three peaks, high shelf, high cut.", &eqBindings));
            v.push_back(effectType<Reverb>("reverb", "Reverb", "Feedback-delay-network reverb, decorrelated stereo.", &reverbBindings));
            v.push_back(effectType<Repeater>("delay", "Delay", "Feedback echo, each repeat darker.", &delayBindings));
            v.push_back(effectType<Chorus>("chorus", "Chorus", "LFO-modulated short delay.", &chorusBindings));
            v.push_back(effectType<Overdrive>("drive", "Drive", "tanh waveshaper with a tone low-pass.", &driveBindings));

            DeviceType filter;
            filter.name = "filter"; filter.label = "Filter"; filter.kind = DeviceKind::Effect;
            filter.summary = "The synth's resonant state-variable filter, with a dry/wet mix.";
            filter.params = {choice("mode", "Mode", kModes, 0), num("cutoff", "Cutoff", "Hz", 20, 20000, 1000, true),
                             num("res", "Resonance", "", 0, 1, 0.3), num("mix", "Mix", "", 0, 1, 1)};
            filter.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<FilterDevice>(self); };
            v.push_back(filter);
            return v;
        }();
        return all;
    }

    const DeviceType *DeviceRegistry::find(const std::string &name)
    {
        for (const auto &t : types())
            if (t.name == name) return &t;
        return nullptr;
    }

    std::unique_ptr<Device> DeviceRegistry::create(const std::string &name)
    {
        const DeviceType *t = find(name);
        return t ? t->create(*t) : nullptr;
    }
}
