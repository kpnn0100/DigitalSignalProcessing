#include "Device.h"
#include "../base/AudioConfig.h"
#include "../effects/Chorus.h"
#include "../effects/Compressor.h"
#include "../effects/Limiter.h"
#include "../effects/MultibandCompressor.h"
#include "../effects/Gate.h"
#include "../effects/TransientShaper.h"
#include "../effects/DeEsser.h"
#include "../effects/Saturator.h"
#include "../effects/Bitcrusher.h"
#include "../effects/Phaser.h"
#include "../instrument/Sampler.h"
#include "../effects/Overdrive.h"
#include "../effects/Repeater.h"
#include "../equalizer/ParametricEQ.h"
#include "../equalizer/StateVariableFilter.h"
#include "../instrument/BasicSynth.h"
#include "../instrument/FMSynth.h"
#include "../instrument/MultiSampler.h"
#include "../instrument/GranularSampler.h"
#include "../instrument/Bass808.h"
#include "../instrument/Piano.h"
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
            b.push_back({num(k + ".width", "Width", "ct", 0, 50, d.width), [o](P &p, double v) { (p.*o).width = v; }}); // REQ-synth2-6
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
                // REQ-synth2-7, -8 — appended: a parameter's index is its id in a plugin host
                v.push_back({choice("voice.mode", "Voice Mode", {"poly", "mono", "legato"}, (int)d.mode), [](P &p, double x) { p.mode = (BasicSynth::Mode)(int)x; }});
                v.push_back({num("voice.glide", "Glide", "ms", 0, 2000, d.glide), [](P &p, double x) { p.glide = x; }});
                v.push_back({num("voice.bend", "Bend Range", "st", 0, 24, d.bendRange, false, true), [](P &p, double x) { p.bendRange = x; }});
                v.push_back({num("vibrato.depth", "Vibrato", "st", 0, 2, d.vibrato), [](P &p, double x) { p.vibrato = x; }});
                v.push_back({num("vibrato.rate", "Vibrato Rate", "Hz", 0.1, 20, d.vibratoRate, true), [](P &p, double x) { p.vibratoRate = x; }});
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
            void pitchBend(double a) override { mSynth.pitchBend(a); }
            void modWheel(double a) override { mSynth.modWheel(a); }
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

        // ── the wavetable synth (REQ-wavetable-3): BasicSynth with a table in each oscillator's place ──

        const std::vector<std::string> kTables = {"basic", "pulse", "harmonics", "formant", "sync", "fold", "sample"};

        BasicSynth::Params wavetableDefaults()
        {
            BasicSynth::Params p;
            p.osc1.table = 0;          // Basic, toward the saw
            p.osc1.position = 0.66;
            p.osc1.posEnv = 0.0;
            p.osc1.voices = 3;
            p.osc1.detune = 12.0;
            p.osc1.width = 10.0;
            p.osc2.table = 2;          // Harmonics, silent until raised
            p.osc2.position = 0.3;
            p.osc2.level = 0.0;
            p.osc2.octave = -1;
            p.osc2.fine = 0.0;
            p.cutoff = 6000.0;
            p.envAmount = 1.0;
            return p;
        }

        void tableOscBindings(std::vector<Binding<BasicSynth::Params>> &b, const std::string &k,
                              BasicSynth::Osc BasicSynth::Params::*o, const BasicSynth::Osc &d)
        {
            using P = BasicSynth::Params;
            b.push_back({choice(k + ".table", "Table", kTables, d.table), [o](P &p, double v) { (p.*o).table = (int)v; }});
            b.push_back({num(k + ".position", "Position", "", 0, 1, d.position), [o](P &p, double v) { (p.*o).position = v; }});
            b.push_back({num(k + ".posenv", "Env > Position", "", -1, 1, d.posEnv), [o](P &p, double v) { (p.*o).posEnv = v; }});
            b.push_back({num(k + ".octave", "Octave", "oct", -3, 3, d.octave, false, true), [o](P &p, double v) { (p.*o).octave = (int)v; }});
            b.push_back({num(k + ".semi", "Semi", "st", -12, 12, d.semi, false, true), [o](P &p, double v) { (p.*o).semi = v; }});
            b.push_back({num(k + ".fine", "Fine", "ct", -100, 100, d.fine), [o](P &p, double v) { (p.*o).fine = v; }});
            b.push_back({num(k + ".level", "Level", "", 0, 1, d.level), [o](P &p, double v) { (p.*o).level = v; }});
            b.push_back({num(k + ".voices", "Unison", "", 1, 5, d.voices, false, true), [o](P &p, double v) { (p.*o).voices = (int)v; }});
            b.push_back({num(k + ".detune", "Detune", "ct", 0, 100, d.detune), [o](P &p, double v) { (p.*o).detune = v; }});
            b.push_back({num(k + ".width", "Width", "ct", 0, 50, d.width), [o](P &p, double v) { (p.*o).width = v; }});
        }

        const std::vector<Binding<BasicSynth::Params>> &wavetableBindings()
        {
            using P = BasicSynth::Params;
            static const std::vector<Binding<P>> b = [] {
                const P d = wavetableDefaults();
                std::vector<Binding<P>> v;
                tableOscBindings(v, "osc1", &P::osc1, d.osc1);
                tableOscBindings(v, "osc2", &P::osc2, d.osc2);
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
                v.push_back({choice("voice.mode", "Voice Mode", {"poly", "mono", "legato"}, (int)d.mode), [](P &p, double x) { p.mode = (BasicSynth::Mode)(int)x; }});
                v.push_back({num("voice.glide", "Glide", "ms", 0, 2000, d.glide), [](P &p, double x) { p.glide = x; }});
                v.push_back({num("voice.bend", "Bend Range", "st", 0, 24, d.bendRange, false, true), [](P &p, double x) { p.bendRange = x; }});
                v.push_back({num("vibrato.depth", "Vibrato", "st", 0, 2, d.vibrato), [](P &p, double x) { p.vibrato = x; }});
                v.push_back({num("vibrato.rate", "Vibrato Rate", "Hz", 0.1, 20, d.vibratoRate, true), [](P &p, double x) { p.vibratoRate = x; }});
                return v;
            }();
            return b;
        }

        class WavetableDevice : public Device
        {
        public:
            explicit WavetableDevice(const DeviceType &t) : Device(t), mP(wavetableDefaults()) { mSynth.setParams(mP); applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mSynth.render(io, channels, frames); }
            void noteOn(int n, int v) override { mSynth.noteOn(n, v); }
            void noteOff(int n) override { mSynth.noteOff(n); }
            void allNotesOff() override { mSynth.allNotesOff(); }
            void pitchBend(double a) override { mSynth.pitchBend(a); }
            void modWheel(double a) override { mSynth.modWheel(a); }
            void reset() override { mSynth.reset(); }
            int activeVoices() const override { return mSynth.activeVoices(); }
            // REQ-wavetable-3: a sound given is cut into cycles — the "sample" table
            void setSample(const float *x, long long frames, int channels, double) override
            {
                std::vector<float> mono((size_t)std::max(0LL, frames));
                const int c = std::max(1, channels);
                for (long long i = 0; i < frames; ++i)
                {
                    double sum = 0;
                    for (int k = 0; k < c; ++k) sum += x[(size_t)(i * c + k)];
                    mono[(size_t)i] = (float)(sum / c);
                }
                mSynth.setWavetable(mono.data(), frames);
            }

        protected:
            void apply(int i, double v) override
            {
                wavetableBindings()[i].set(mP, v);
                mSynth.setParams(mP);
            }

        private:
            BasicSynth mSynth;
            BasicSynth::Params mP;
        };

        // ── the FM synth (REQ-fm-3): four operators, eight algorithms ──

        const std::vector<Binding<FMSynth::Params>> &fmBindings()
        {
            using P = FMSynth::Params;
            static const std::vector<Binding<P>> b = [] {
                const P d;
                std::vector<Binding<P>> v;
                v.push_back({choice("algorithm", "Algorithm", {"1", "2", "3", "4", "5", "6", "7", "8"}, d.algorithm - 1),
                             [](P &p, double x) { p.algorithm = (int)x + 1; }});
                v.push_back({num("feedback", "Feedback", "", 0, 1, d.feedback), [](P &p, double x) { p.feedback = x; }});
                for (int k = 0; k < FMSynth::kOps; ++k)
                {
                    const std::string o = "op" + std::to_string(k + 1);
                    const FMSynth::Op &od = d.op[(size_t)k];
                    v.push_back({num(o + ".ratio", "Ratio", "x", 0.5, 16, od.ratio, true), [k](P &p, double x) { p.op[(size_t)k].ratio = x; }});
                    v.push_back({num(o + ".fine", "Fine", "ct", -100, 100, od.fine), [k](P &p, double x) { p.op[(size_t)k].fine = x; }});
                    v.push_back({num(o + ".level", "Level", "", 0, 1, od.level), [k](P &p, double x) { p.op[(size_t)k].level = x; }});
                    v.push_back({num(o + ".attack", "Attack", "ms", 0, 10000, od.attack, true), [k](P &p, double x) { p.op[(size_t)k].attack = x; }});
                    v.push_back({num(o + ".decay", "Decay", "ms", 0, 10000, od.decay, true), [k](P &p, double x) { p.op[(size_t)k].decay = x; }});
                    v.push_back({num(o + ".sustain", "Sustain", "", 0, 1, od.sustain), [k](P &p, double x) { p.op[(size_t)k].sustain = x; }});
                    v.push_back({num(o + ".release", "Release", "ms", 0, 10000, od.release, true), [k](P &p, double x) { p.op[(size_t)k].release = x; }});
                    v.push_back({num(o + ".velocity", "Velocity", "", 0, 1, od.velocity), [k](P &p, double x) { p.op[(size_t)k].velocity = x; }});
                }
                v.push_back({num("volume", "Volume", "dB", -60, 6, d.volumeDb), [](P &p, double x) { p.volumeDb = x; }});
                v.push_back({num("voice.bend", "Bend Range", "st", 0, 24, d.bendRange, false, true), [](P &p, double x) { p.bendRange = x; }});
                v.push_back({num("vibrato.depth", "Vibrato", "st", 0, 2, d.vibrato), [](P &p, double x) { p.vibrato = x; }});
                v.push_back({num("vibrato.rate", "Vibrato Rate", "Hz", 0.1, 20, d.vibratoRate, true), [](P &p, double x) { p.vibratoRate = x; }});
                return v;
            }();
            return b;
        }

        class FMDevice : public Device
        {
        public:
            explicit FMDevice(const DeviceType &t) : Device(t), mP(mSynth.params()) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mSynth.render(io, channels, frames); }
            void noteOn(int n, int v) override { mSynth.noteOn(n, v); }
            void noteOff(int n) override { mSynth.noteOff(n); }
            void allNotesOff() override { mSynth.allNotesOff(); }
            void pitchBend(double a) override { mSynth.pitchBend(a); }
            void modWheel(double a) override { mSynth.modWheel(a); }
            void reset() override { mSynth.reset(); }
            int activeVoices() const override { return mSynth.activeVoices(); }

        protected:
            void apply(int i, double v) override
            {
                fmBindings()[i].set(mP, v);
                mSynth.setParams(mP);
            }

        private:
            FMSynth mSynth;
            FMSynth::Params mP;
        };

        // ── the 808 (REQ-808-2): a tuned bass drum ──

        const std::vector<Binding<Bass808::Params>> &bass808Bindings()
        {
            using P = Bass808::Params;
            static const std::vector<Binding<P>> b = [] {
                const P d;
                std::vector<Binding<P>> v;
                v.push_back({num("punch", "Punch", "st", 0, 36, d.punch), [](P &p, double x) { p.punch = x; }});
                v.push_back({num("punch.time", "Punch Time", "ms", 1, 200, d.punchTime, true), [](P &p, double x) { p.punchTime = x; }});
                v.push_back({num("glide", "Glide", "ms", 0, 1000, d.glide), [](P &p, double x) { p.glide = x; }});
                v.push_back({num("amp.attack", "Attack", "ms", 0, 50, d.attack), [](P &p, double x) { p.attack = x; }});
                v.push_back({num("amp.decay", "Decay", "ms", 20, 8000, d.decay, true), [](P &p, double x) { p.decay = x; }});
                v.push_back({num("amp.release", "Release", "ms", 1, 2000, d.release, true), [](P &p, double x) { p.release = x; }});
                v.push_back({num("click", "Click", "", 0, 1, d.click), [](P &p, double x) { p.click = x; }});
                v.push_back({num("drive", "Drive", "", 0, 1, d.drive), [](P &p, double x) { p.drive = x; }});
                v.push_back({num("tone", "Tone", "Hz", 200, 20000, d.tone, true), [](P &p, double x) { p.tone = x; }});
                v.push_back({num("volume", "Volume", "dB", -60, 12, d.volumeDb), [](P &p, double x) { p.volumeDb = x; }});
                v.push_back({num("velocity", "Velocity", "", 0, 1, d.velocity), [](P &p, double x) { p.velocity = x; }});
                v.push_back({num("voice.bend", "Bend Range", "st", 0, 24, d.bendRange, false, true), [](P &p, double x) { p.bendRange = x; }});
                return v;
            }();
            return b;
        }

        class Bass808Device : public Device
        {
        public:
            explicit Bass808Device(const DeviceType &t) : Device(t), mP(m808.params()) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { m808.render(io, channels, frames); }
            void noteOn(int n, int v) override { m808.noteOn(n, v); }
            void noteOff(int n) override { m808.noteOff(n); }
            void allNotesOff() override { m808.allNotesOff(); }
            void pitchBend(double a) override { m808.pitchBend(a); }
            void reset() override { m808.reset(); }
            int activeVoices() const override { return m808.activeVoices(); }

        protected:
            void apply(int i, double v) override
            {
                bass808Bindings()[i].set(mP, v);
                m808.setParams(mP);
            }

        private:
            Bass808 m808;
            Bass808::Params mP;
        };

        // ── the granular sampler (REQ-granular-2): a sound as a cloud of grains ──

        const std::vector<Binding<GranularSampler::Params>> &granularBindings()
        {
            using P = GranularSampler::Params;
            static const std::vector<Binding<P>> b = [] {
                const P d;
                std::vector<Binding<P>> v;
                v.push_back({num("grain.size", "Size", "ms", 5, 500, d.size, true), [](P &p, double x) { p.size = x; }});
                v.push_back({num("grain.density", "Density", "/s", 1, 200, d.density, true), [](P &p, double x) { p.density = x; }});
                v.push_back({num("grain.position", "Position", "", 0, 1, d.position), [](P &p, double x) { p.position = x; }});
                v.push_back({num("grain.scan", "Scan", "/s", -1, 1, d.scan), [](P &p, double x) { p.scan = x; }});
                v.push_back({num("grain.spray", "Spray", "", 0, 1, d.spray), [](P &p, double x) { p.spray = x; }});
                v.push_back({num("grain.pitch", "Pitch", "st", -24, 24, d.pitch), [](P &p, double x) { p.pitch = x; }});
                v.push_back({num("grain.pspray", "Pitch Spray", "ct", 0, 100, d.pspray), [](P &p, double x) { p.pspray = x; }});
                v.push_back({num("grain.spread", "Spread", "", 0, 1, d.spread), [](P &p, double x) { p.spread = x; }});
                v.push_back({num("root", "Root", "", 0, 127, d.root, false, true), [](P &p, double x) { p.root = (int)x; }});
                v.push_back({num("amp.attack", "Attack", "ms", 0, 10000, d.attack, true), [](P &p, double x) { p.attack = x; }});
                v.push_back({num("amp.decay", "Decay", "ms", 0, 10000, d.decay, true), [](P &p, double x) { p.decay = x; }});
                v.push_back({num("amp.sustain", "Sustain", "", 0, 1, d.sustain), [](P &p, double x) { p.sustain = x; }});
                v.push_back({num("amp.release", "Release", "ms", 0, 10000, d.release, true), [](P &p, double x) { p.release = x; }});
                v.push_back({num("volume", "Volume", "dB", -60, 12, d.volumeDb), [](P &p, double x) { p.volumeDb = x; }});
                v.push_back({num("velocity", "Velocity", "", 0, 1, d.velocity), [](P &p, double x) { p.velocity = x; }});
                return v;
            }();
            return b;
        }

        class GranularDevice : public Device
        {
        public:
            explicit GranularDevice(const DeviceType &t) : Device(t), mP(mGrains.params()) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mGrains.render(io, channels, frames); }
            void noteOn(int n, int v) override { mGrains.noteOn(n, v); }
            void noteOff(int n) override { mGrains.noteOff(n); }
            void allNotesOff() override { mGrains.allNotesOff(); }
            void pitchBend(double a) override { mGrains.pitchBend(a); }
            void reset() override { mGrains.reset(); }
            int activeVoices() const override { return mGrains.activeVoices(); }
            void setSample(const float *x, long long frames, int channels, double rate) override { mGrains.setSample(x, frames, channels, rate); }

        protected:
            void apply(int i, double v) override
            {
                granularBindings()[i].set(mP, v);
                mGrains.setParams(mP);
            }

        private:
            GranularSampler mGrains;
            GranularSampler::Params mP;
        };

        // ── the multi-sample instrument (REQ-multisample-3): an SFZ's regions and the sounds they name ──

        const std::vector<Binding<MultiSampler::Params>> &multiBindings()
        {
            using P = MultiSampler::Params;
            static const std::vector<Binding<P>> b = [] {
                const P d;
                std::vector<Binding<P>> v;
                v.push_back({num("volume", "Volume", "dB", -60, 12, d.volumeDb), [](P &p, double x) { p.volumeDb = x; }});
                v.push_back({num("velocity", "Velocity", "", 0, 1, d.velocity), [](P &p, double x) { p.velocity = x; }});
                v.push_back({num("voice.bend", "Bend Range", "st", 0, 24, d.bendRange, false, true), [](P &p, double x) { p.bendRange = x; }});
                return v;
            }();
            return b;
        }

        class MultiDevice : public Device
        {
        public:
            explicit MultiDevice(const DeviceType &t) : Device(t), mP(mSampler.params()) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mSampler.render(io, channels, frames); }
            void noteOn(int n, int v) override { mSampler.noteOn(n, v); }
            void noteOff(int n) override { mSampler.noteOff(n); }
            void allNotesOff() override { mSampler.allNotesOff(); }
            void pitchBend(double a) override { mSampler.pitchBend(a); }
            void reset() override { mSampler.reset(); }
            int activeVoices() const override { return mSampler.activeVoices(); }
            std::vector<std::string> setDefinition(const std::string &text, std::vector<std::string> &problems) override
            {
                const SfzInstrument sfz = parseSfz(text);
                problems = sfz.problems;
                mSampler.setDefinition(sfz);
                return sfzSounds(sfz);
            }
            void setNamedSample(const std::string &name, const float *x, long long frames, int channels, double rate) override
            {
                mSampler.setSound(name, x, frames, channels, rate);
            }

        protected:
            void apply(int i, double v) override
            {
                multiBindings()[i].set(mP, v);
                mSampler.setParams(mP);
            }

        private:
            MultiSampler mSampler;
            MultiSampler::Params mP;
        };

        // ── the piano (REQ-inst-piano-1): the engine's voicing bank, its ranges and defaults ──

        const std::vector<Binding<Piano::Params>> &pianoBindings()
        {
            using P = Piano::Params;
            static const std::vector<Binding<P>> b = [] {
                std::vector<Binding<P>> v;
                // one row per Tune enumerator the engine documents (PianoEngine::tuneSpec): name, label, unit
                auto tune = [&v](int row, const std::string &name, const std::string &label, const std::string &unit) {
                    const PianoEngine::TuneSpec s = PianoEngine::tuneSpec(row);
                    v.push_back({num(name, label, unit, s.min, s.max, s.def), [row](P &p, double x) { p.tune[(size_t)row] = x; }});
                };
                tune(PianoEngine::TuneHardness, "hammer.hardness", "Hardness", "x");
                tune(PianoEngine::TuneFeltCurve, "hammer.curve", "Felt Curve", "");
                tune(PianoEngine::TuneFeltHysteresis, "hammer.hysteresis", "Hysteresis", "");
                tune(PianoEngine::TuneFeltRelax, "hammer.relax", "Felt Relax", "x");
                tune(PianoEngine::TuneDecay, "strings.decay", "Sustain", "x");
                tune(PianoEngine::TuneBrightness, "strings.brightness", "Brightness", "x");
                tune(PianoEngine::TuneInharmonicity, "strings.inharmonicity", "Stretch", "x");
                tune(PianoEngine::TuneUnisonDetune, "strings.detune", "Unison Detune", "cents");
                tune(PianoEngine::TuneBassGrowl, "strings.growl", "Bass Growl", "");
                tune(PianoEngine::TuneAttackGlide, "strings.glide", "Attack Glide", "x");
                tune(PianoEngine::TuneTrebleShimmer, "strings.shimmer", "Treble Shimmer", "");
                tune(PianoEngine::TuneAftersound, "strings.sing", "Aftersound", "x");
                tune(PianoEngine::TuneBody, "body.mix", "Soundboard", "");
                tune(PianoEngine::TuneBodyResonance, "body.resonance", "Resonance", "");
                v.push_back({choice("pedal.sustain", "Sustain Pedal", {"off", "on"}, 0), [](P &p, double x) { p.sustain = x > 0.5; }});
                v.push_back({choice("pedal.soft", "Soft Pedal", {"off", "on"}, 0), [](P &p, double x) { p.soft = x > 0.5; }});
                tune(PianoEngine::TuneMasterGain, "drive", "Drive", "");
                v.push_back({num("volume", "Volume", "dB", -60, 6, 0), [](P &p, double x) { p.volumeDb = x; }});
                return v;
            }();
            return b;
        }

        class PianoDevice : public Device
        {
        public:
            explicit PianoDevice(const DeviceType &t) : Device(t), mP(mPiano.params()) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mPiano.render(io, channels, frames); }
            void noteOn(int n, int v) override { mPiano.noteOn(n, v); }
            void noteOff(int n) override { mPiano.noteOff(n); }
            void allNotesOff() override { mPiano.allNotesOff(); }
            void reset() override { mPiano.reset(); }
            int activeVoices() const override { return mPiano.activeVoices(); }

        protected:
            void apply(int i, double v) override
            {
                pianoBindings()[(size_t)i].set(mP, v);
                mPiano.setParams(mP);
            }

        private:
            Piano mPiano;
            Piano::Params mP;
        };

        const std::vector<Binding<Sampler::Params>> &samplerBindings()
        {
            using P = Sampler::Params;
            static const std::vector<Binding<P>> b = {
                {choice("mode", "Mode", {"chromatic", "one-shot", "slices"}, 0), [](P &p, double x) { p.mode = (Sampler::Mode)(int)x; }}, // slices: REQ-inst-sampler-2
                {num("root", "Root Key", "", 0, 127, 60, false, true), [](P &p, double x) { p.root = (int)x; }},
                {num("start", "Start", "", 0, 1, 0), [](P &p, double x) { p.start = x; }},
                {num("end", "End", "", 0, 1, 1), [](P &p, double x) { p.end = x; }},
                {choice("reverse", "Reverse", {"off", "on"}, 0), [](P &p, double x) { p.reverse = x > 0.5; }},
                {num("attack", "Attack", "ms", 0, 2000, 0), [](P &p, double x) { p.attackMs = x; }},
                {num("decay", "Decay", "ms", 0, 2000, 0), [](P &p, double x) { p.decayMs = x; }},
                {num("sustain", "Sustain", "", 0, 1, 1), [](P &p, double x) { p.sustain = x; }},
                {num("release", "Release", "ms", 1, 5000, 30, true), [](P &p, double x) { p.releaseMs = x; }},
                {num("level", "Level", "dB", -40, 12, 0), [](P &p, double x) { p.levelDb = x; }},
                {num("velocity", "Velocity", "", 0, 1, 1), [](P &p, double x) { p.velocity = x; }},
                {num("bend", "Bend Range", "st", 0, 24, 2, false, true), [](P &p, double x) { p.bendRange = x; }}, // REQ-inst-expr-1, appended
                {num("slices", "Slices", "", 1, 64, 8, false, true), [](P &p, double x) { p.slices = (int)x; }},     // REQ-inst-sampler-2, appended
            };
            return b;
        }

        class SamplerDevice : public Device
        {
        public:
            explicit SamplerDevice(const DeviceType &t) : Device(t), mP(mSampler.params()) { applyAll(); }
            void process(Sample *const *io, int channels, int frames) override { mSampler.render(io, channels, frames); }
            void noteOn(int n, int v) override { mSampler.noteOn(n, v); }
            void noteOff(int n) override { mSampler.noteOff(n); }
            void pitchBend(double a) override { mSampler.pitchBend(a); }
            void allNotesOff() override { mSampler.allNotesOff(); }
            void reset() override { mSampler.reset(); }
            int activeVoices() const override { return mSampler.activeVoices(); }
            void setSample(const float *x, long long frames, int channels, double rate) override { mSampler.setSample(x, frames, channels, rate); }

        protected:
            void apply(int i, double v) override
            {
                samplerBindings()[i].set(mP, v);
                mSampler.setParams(mP);
            }

        private:
            Sampler mSampler;
            Sampler::Params mP;
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
            void process(Sample *const *io, int channels, int frames) override { runOf(mProc, io, channels, frames, 0); }
            void reset() override { resetOf(mProc, 0); }
            void setKey(const Sample *const *key, int channels) override { keyOf(mProc, key, channels, 0); }
            int latency() const override { return latencyOf(mProc, 0); }

        protected:
            void apply(int i, double v) override { mB[i].set(mProc, v); }

        private:
            // What a processor has, it is used for — found by its members, not listed by type (N34: an effect that
            // links its channels takes the whole frame; one that clears its memory resets; one with a delay says it)
            template <class Q> static auto resetOf(Q &p, int) -> decltype(p.reset(), void()) { p.reset(); }
            template <class Q> static void resetOf(Q &, long) {}
            template <class Q> static auto runOf(Q &p, Sample *const *io, int channels, int frames, int) -> decltype(p.process(io, channels, frames), void())
            {
                p.process(io, channels, frames);
            }
            template <class Q> static void runOf(Q &p, Sample *const *io, int channels, int frames, long) { runChannels(p, io, channels, frames); }
            template <class Q> static auto keyOf(Q &p, const Sample *const *key, int channels, int) -> decltype(p.setKey(key, channels), void()) { p.setKey(key, channels); }
            template <class Q> static void keyOf(Q &, const Sample *const *, int, long) {}
            // REQ-device-8: an effect with a lookahead says how far its output lags; the rest are latency-free
            template <class Q> static auto latencyOf(const Q &p, int) -> decltype(p.latency(), 0) { return p.latency(); }
            template <class Q> static int latencyOf(const Q &, long) { return 0; }
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
                // appended (a parameter's index is its id in a plugin host): detect on the key, not the input
                {choice("sidechain", "Sidechain", {"off", "on"}, 0), [](Compressor &c, double x) { c.setSidechain(x > 0.5); }},
            };
            return b;
        }

        // REQ-fx-multiband-1: three bands, each its own compressor
        const std::vector<Binding<MultibandCompressor>> &multibandBindings()
        {
            using M = MultibandCompressor;
            static const std::vector<Binding<M>> b = [] {
                std::vector<Binding<M>> v;
                v.push_back({num("low.split", "Low Split", "Hz", 20, 1000, 200, true), [](M &m, double x) { m.setSplits(x, m.highSplit()); }});
                v.push_back({num("high.split", "High Split", "Hz", 500, 12000, 2500, true), [](M &m, double x) { m.setSplits(m.lowSplit(), x); }});
                const char *names[3] = {"low", "mid", "high"}, *labels[3] = {"Low", "Mid", "High"};
                for (int k = 0; k < 3; ++k)
                {
                    const std::string n = names[k];
                    v.push_back({num(n + ".threshold", std::string(labels[k]) + " Threshold", "dB", -60, 0, -20), [k](M &m, double x) { m.setThresholdDb(k, x); }});
                    v.push_back({num(n + ".ratio", std::string(labels[k]) + " Ratio", ":1", 1, 20, 3, true), [k](M &m, double x) { m.setRatio(k, x); }});
                    v.push_back({num(n + ".gain", std::string(labels[k]) + " Gain", "dB", -24, 24, 0), [k](M &m, double x) { m.setGainDb(k, x); }});
                }
                v.push_back({num("attack", "Attack", "ms", 0.1, 200, 10, true), [](M &m, double x) { m.setAttackMs(x); }});
                v.push_back({num("release", "Release", "ms", 5, 2000, 150, true), [](M &m, double x) { m.setReleaseMs(x); }});
                v.push_back({num("output", "Output", "dB", -24, 24, 0), [](M &m, double x) { m.setOutputDb(x); }});
                return v;
            }();
            return b;
        }

        // REQ-fx-transient-1: more or less attack, more or less sustain
        const std::vector<Binding<TransientShaper>> &transientBindings()
        {
            static const std::vector<Binding<TransientShaper>> b = {
                {num("attack", "Attack", "%", -100, 100, 0), [](TransientShaper &t, double x) { t.setAttack(x); }},
                {num("sustain", "Sustain", "%", -100, 100, 0), [](TransientShaper &t, double x) { t.setSustain(x); }},
                {num("output", "Output", "dB", -24, 24, 0), [](TransientShaper &t, double x) { t.setOutputDb(x); }},
            };
            return b;
        }

        // REQ-fx-deesser-1: sibilance held down
        const std::vector<Binding<DeEsser>> &deesserBindings()
        {
            static const std::vector<Binding<DeEsser>> b = {
                {num("frequency", "Frequency", "Hz", 2000, 16000, 6000, true), [](DeEsser &d, double x) { d.setFrequency(x); }},
                {num("threshold", "Threshold", "dB", -60, 0, -30), [](DeEsser &d, double x) { d.setThresholdDb(x); }},
                {num("range", "Range", "dB", 0, 24, 12), [](DeEsser &d, double x) { d.setRangeDb(x); }},
                {choice("mode", "Mode", {"split", "wide"}, 0), [](DeEsser &d, double x) { d.setSplit(x < 0.5); }},
            };
            return b;
        }

        // REQ-fx-saturator-1: a curve driven, oversampled
        const std::vector<Binding<Saturator>> &saturatorBindings()
        {
            static const std::vector<Binding<Saturator>> b = {
                {num("drive", "Drive", "dB", 0, 36, 6), [](Saturator &s, double x) { s.setDriveDb(x); }},
                {choice("curve", "Curve", {"soft", "hard", "tube", "fold"}, 0), [](Saturator &s, double x) { s.setCurve((int)x); }},
                {num("mix", "Mix", "", 0, 1, 1), [](Saturator &s, double x) { s.setMix(x); }},
                {num("output", "Output", "dB", -24, 12, 0), [](Saturator &s, double x) { s.setOutputDb(x); }},
            };
            return b;
        }

        // REQ-fx-bitcrusher-1: fewer bits, a lower rate
        const std::vector<Binding<Bitcrusher>> &bitcrusherBindings()
        {
            static const std::vector<Binding<Bitcrusher>> b = {
                {num("bits", "Bits", "", 1, 24, 8, false, true), [](Bitcrusher &c, double x) { c.setBits(x); }},
                {num("rate", "Rate", "Hz", 100, 96000, 96000, true), [](Bitcrusher &c, double x) { c.setRate(x); }},
                {num("mix", "Mix", "", 0, 1, 1), [](Bitcrusher &c, double x) { c.setMix(x); }},
                {num("output", "Output", "dB", -24, 12, 0), [](Bitcrusher &c, double x) { c.setOutputDb(x); }},
            };
            return b;
        }

        // REQ-fx-phaser-1: notches swept by an LFO
        const std::vector<Binding<Phaser>> &phaserBindings()
        {
            static const std::vector<Binding<Phaser>> b = {
                {num("rate", "Rate", "Hz", 0.01, 10, 0.5, true), [](Phaser &p, double x) { p.setRateHz(x); }},
                {num("center", "Center", "Hz", 100, 8000, 800, true), [](Phaser &p, double x) { p.setCenterHz(x); }},
                {num("depth", "Depth", "oct", 0, 4, 2), [](Phaser &p, double x) { p.setDepthOct(x); }},
                {num("stages", "Stages", "", 2, 12, 4, false, true), [](Phaser &p, double x) { p.setStages((int)x); }},
                {num("feedback", "Feedback", "", -0.9, 0.9, 0.3), [](Phaser &p, double x) { p.setFeedback(x); }},
                {num("stereo", "Stereo", "deg", 0, 180, 90), [](Phaser &p, double x) { p.setStereoDeg(x); }},
                {num("mix", "Mix", "", 0, 1, 0.5), [](Phaser &p, double x) { p.setMix(x); }},
            };
            return b;
        }

        // REQ-fx-gate-1: a gate and a downward expander
        const std::vector<Binding<Gate>> &gateBindings()
        {
            static const std::vector<Binding<Gate>> b = {
                {num("threshold", "Threshold", "dB", -80, 0, -40), [](Gate &g, double x) { g.setThresholdDb(x); }},
                {num("ratio", "Ratio", ":1", 1, 100, 100, true), [](Gate &g, double x) { g.setRatio(x); }},
                {num("range", "Range", "dB", 0, 100, 80), [](Gate &g, double x) { g.setRangeDb(x); }},
                {num("attack", "Attack", "ms", 0.01, 100, 1, true), [](Gate &g, double x) { g.setAttackMs(x); }},
                {num("hold", "Hold", "ms", 0, 2000, 20), [](Gate &g, double x) { g.setHoldMs(x); }},
                {num("release", "Release", "ms", 1, 5000, 100, true), [](Gate &g, double x) { g.setReleaseMs(x); }},
                {choice("sidechain", "Sidechain", {"off", "on"}, 0), [](Gate &g, double x) { g.setSidechain(x > 0.5); }},
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

        const std::vector<Binding<Limiter>> &limiterBindings()
        {
            static const std::vector<Binding<Limiter>> b = {
                {num("gain", "Gain", "dB", 0, 24, 0), [](Limiter &l, double x) { l.setGainDb(x); }},
                {num("ceiling", "Ceiling", "dB", -24, 0, -0.3), [](Limiter &l, double x) { l.setCeilingDb(x); }},
                {num("release", "Release", "ms", 1, 1000, 60, true), [](Limiter &l, double x) { l.setReleaseMs(x); }},
                {num("lookahead", "Lookahead", "ms", 0, Limiter::kMaxLookaheadMs, 2), [](Limiter &l, double x) { l.setLookaheadMs(x); }},
                {choice("truePeak", "True Peak", {"off", "on"}, 0), [](Limiter &l, double x) { l.setTruePeak(x > 0.5); }}, // REQ-fx-limiter-2
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
            synth.groupLabels = {{"osc1", "Oscillator 1"}, {"osc2", "Oscillator 2"}, {"filter", "Filter"},
                                 {"fenv", "Filter Envelope"}, {"amp", "Amplifier"}, {"", "Mix & Output"},
                                 {"voice", "Voice"}, {"vibrato", "Vibrato"}}; // REQ-synth2-7, -8
            synth.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<SynthDevice>(self); };
            v.push_back(synth);

            DeviceType wavetable; // REQ-wavetable-3
            wavetable.name = "wavetable"; wavetable.label = "Wavetable"; wavetable.kind = DeviceKind::Instrument;
            wavetable.summary = "Two wavetable oscillators — six built-in tables, or a sound of 2048-point cycles — moved through by a position "
                                "and the filter envelope; band-limited at every pitch; a resonant filter, two envelopes; 16 voices.";
            wavetable.params = specsOf(wavetableBindings());
            wavetable.takesSample = true;
            wavetable.groupLabels = {{"osc1", "Oscillator 1"}, {"osc2", "Oscillator 2"}, {"filter", "Filter"},
                                     {"fenv", "Filter Envelope"}, {"amp", "Amplifier"}, {"", "Mix & Output"},
                                     {"voice", "Voice"}, {"vibrato", "Vibrato"}};
            wavetable.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<WavetableDevice>(self); };
            v.push_back(wavetable);

            DeviceType fm; // REQ-fm-3
            fm.name = "fm"; fm.label = "FM Synth"; fm.kind = DeviceKind::Instrument;
            fm.summary = "Four sine operators by phase modulation — eight algorithms, operator 4 fed back, an envelope each; 16 voices.";
            fm.params = specsOf(fmBindings());
            fm.groupLabels = {{"op1", "Operator 1"}, {"op2", "Operator 2"}, {"op3", "Operator 3"}, {"op4", "Operator 4"},
                              {"", "Algorithm & Output"}, {"voice", "Voice"}, {"vibrato", "Vibrato"}};
            fm.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<FMDevice>(self); };
            v.push_back(fm);

            DeviceType piano;
            piano.name = "piano"; piano.label = "Piano"; piano.kind = DeviceKind::Instrument;
            piano.summary = "A physical piano: felt hammers on stiff, inharmonic strings through a shared bridge and soundboard; "
                            "sustain and soft pedals; 16 voices, a held key never stolen while a released one rings.";
            piano.params = specsOf(pianoBindings());
            piano.groupLabels = {{"hammer", "Hammer"}, {"strings", "Strings"}, {"body", "Body"}, {"pedal", "Pedals"}, {"", "Output"}};
            piano.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<PianoDevice>(self); };
            v.push_back(piano);

            DeviceType drums;
            drums.name = "drums"; drums.label = "Drum Machine"; drums.kind = DeviceKind::Instrument;
            drums.summary = "Twelve synthesized pads on the GM drum notes (36 kick … 56 cowbell, a crash 49 and a ride 51); the closed hat chokes the open hat.";
            drums.params = specsOf(drumBindings());
            drums.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<DrumDevice>(self); };
            {
                // the pads' keys, named for a person (the prefixes are the parameters' names)
                static const char *kLabels[DrumMachine::PadCount] = {"Kick", "Rim", "Snare", "Clap", "Low Tom",
                                                                     "Closed Hat", "Mid Tom", "Open Hat", "High Tom", "Cowbell", "Crash", "Ride"};
                for (int p = 0; p < DrumMachine::PadCount; ++p)
                {
                    drums.noteNames.emplace_back(DrumMachine::noteFor((DrumMachine::Pad)p), kLabels[p]);
                    drums.notePrefixes.emplace_back(DrumMachine::noteFor((DrumMachine::Pad)p), DrumMachine::padName((DrumMachine::Pad)p));
                    drums.groupLabels.emplace_back(DrumMachine::padName((DrumMachine::Pad)p), kLabels[p]); // a pad's group: its name
                }
                drums.groupLabels.emplace_back("", "Output");
                std::sort(drums.noteNames.begin(), drums.noteNames.end());
                std::sort(drums.notePrefixes.begin(), drums.notePrefixes.end());
            }
            v.push_back(drums);

            DeviceType b808; // REQ-808-2
            b808.name = "808"; b808.label = "808"; b808.kind = DeviceKind::Instrument;
            b808.summary = "A tuned bass drum: the note's pitch punched down to at each attack, gliding between held notes; "
                           "a long decay, a click, drive and a tone; one voice.";
            b808.params = specsOf(bass808Bindings());
            b808.groupLabels = {{"punch", "Punch"}, {"amp", "Amplifier"}, {"", "Tone & Output"}, {"voice", "Voice"}};
            b808.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<Bass808Device>(self); };
            v.push_back(b808);

            DeviceType granular; // REQ-granular-2
            granular.name = "granular"; granular.label = "Granular"; granular.kind = DeviceKind::Instrument;
            granular.summary = "A recorded sound as a cloud of short grains — their size, density and place (scanned, sprayed), "
                               "pitched by the note, sprayed in pitch and pan; an envelope; 16 voices.";
            granular.params = specsOf(granularBindings());
            granular.takesSample = true;
            granular.groupLabels = {{"grain", "Grains"}, {"amp", "Amplifier"}, {"", "Output"}};
            granular.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<GranularDevice>(self); };
            v.push_back(granular);

            DeviceType multi; // REQ-multisample-3
            multi.name = "multisample"; multi.label = "Multi-sample"; multi.kind = DeviceKind::Instrument;
            multi.summary = "A recorded instrument read from an SFZ file: its sounds by key and velocity, in turn (round robin) or by "
                            "chance, looped or one-shot, choking each other; an envelope each; 32 voices.";
            multi.params = specsOf(multiBindings());
            multi.takesSample = true;
            multi.takesDefinition = true;
            multi.groupLabels = {{"", "Multi-sample"}, {"voice", "Voice"}};
            multi.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<MultiDevice>(self); };
            v.push_back(multi);

            DeviceType sampler;
            sampler.name = "sampler"; sampler.label = "Sampler"; sampler.kind = DeviceKind::Instrument;
            sampler.summary = "Plays a recorded sound: pitched by the note (chromatic), as recorded (one-shot), or as a kit of its span's equal slices (slices); a span, reverse, an ADSR.";
            sampler.params = specsOf(samplerBindings());
            sampler.takesSample = true;
            sampler.groupLabels = {{"", "Sampler"}};
            sampler.create = [](const DeviceType &self) -> std::unique_ptr<Device> { return std::make_unique<SamplerDevice>(self); };
            v.push_back(sampler);

            DeviceType comp = effectType<Compressor>("compressor", "Compressor", "Feed-forward peak compressor; with Sidechain on it listens to another track (its key).", &compressorBindings);
            comp.takesKey = true;
            v.push_back(comp);
            v.push_back(effectType<ParametricEQ>("eq", "EQ", "Low cut, low shelf, three peaks, high shelf, high cut.", &eqBindings));
            {
                DeviceType gate = effectType<Gate>("gate", "Gate", "A gate and a downward expander: below its threshold turned down by the ratio, held open before it closes; keyed by another track if asked.", &gateBindings); // REQ-fx-gate-1
                gate.takesKey = true;
                v.push_back(gate);
            }
            v.push_back(effectType<Phaser>("phaser", "Phaser", "Notches swept by an LFO: first-order all-passes fed back, mixed with the dry sound.", &phaserBindings)); // REQ-fx-phaser-1
            v.push_back(effectType<Saturator>("saturator", "Saturator", "A curve driven — soft, hard, tube or fold — four times oversampled, its latency reported.", &saturatorBindings)); // REQ-fx-saturator-1
            v.push_back(effectType<Bitcrusher>("bitcrusher", "Bitcrusher", "Fewer bits and a lower rate: each sample rounded to a coarser grid, held for longer.", &bitcrusherBindings)); // REQ-fx-bitcrusher-1
            v.push_back(effectType<DeEsser>("deesser", "De-esser", "Sibilance held down: a high-pass hears it, its level held at the threshold — the band alone turned down (split) or the whole sound (wide).", &deesserBindings)); // REQ-fx-deesser-1
            v.push_back(effectType<TransientShaper>("transient", "Transient Shaper", "More or less attack, more or less sustain, whatever the level: a fast envelope against two slow ones.", &transientBindings)); // REQ-fx-transient-1
            v.push_back(effectType<MultibandCompressor>("multiband", "Multiband", "Three bands split by Linkwitz-Riley crossovers, each compressed alone; flat when nothing is.", &multibandBindings)); // REQ-fx-multiband-1
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
            v.push_back(effectType<Limiter>("limiter", "Limiter", "Brickwall lookahead limiter: the output never exceeds the ceiling.", &limiterBindings));
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
