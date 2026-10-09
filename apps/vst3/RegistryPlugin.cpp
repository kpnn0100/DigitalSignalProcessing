#include "RegistryPlugin.h"
#include "Editor.h"
#include "base/AudioConfig.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace arstro
{
namespace vst3
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    namespace
    {
        std::string readAll(IBStream *s)
        {
            std::string out;
            char buf[512];
            int32 got = 0;
            while (s && s->read(buf, (int32)sizeof buf, &got) == kResultOk && got > 0) out.append(buf, (size_t)got);
            return out;
        }
        bool writeAll(IBStream *s, const std::string &text)
        {
            int32 put = 0;
            return s && s->write(const_cast<char *>(text.data()), (int32)text.size(), &put) == kResultOk && put == (int32)text.size();
        }
    }

    std::string stateText(const DeviceType &t, const std::vector<double> &values)
    {
        std::string s = "arstro-device 1\ntype=" + t.name + "\n";
        for (size_t i = 0; i < t.params.size() && i < values.size(); ++i) s += t.params[i].name + "=" + paramToText(t.params[i], values[i]) + "\n";
        return s;
    }

    bool readState(const DeviceType &t, const std::string &text, std::vector<double> &values)
    {
        std::istringstream in(text);
        std::string line;
        if (!std::getline(in, line) || line != "arstro-device 1") return false;
        if (!std::getline(in, line) || line != "type=" + t.name) return false; // a drum kit's state is not a synth's
        std::vector<double> next = values;
        while (std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const int i = t.paramIndex(line.substr(0, eq));
            if (i < 0) continue; // a later version's parameter: skipped, the rest still load
            double v = 0;
            if (paramFromText(t.params[(size_t)i], line.substr(eq + 1), v)) next[(size_t)i] = v;
        }
        values.swap(next);
        return true;
    }

    // ── the processor ───────────────────────────────────────────────────────────────────────

    Processor::Processor(const PluginId &id) : mType(DeviceRegistry::find(id.type))
    {
        setControllerClass(id.controller);
        const size_t n = mType ? mType->params.size() : 0;
        mValues.reset(new std::atomic<double>[n]);
        for (size_t i = 0; i < n; ++i) mValues[i].store(mType->params[i].def);
    }

    tresult PLUGIN_API Processor::initialize(FUnknown *context)
    {
        const tresult r = AudioEffect::initialize(context);
        if (r != kResultOk) return r;
        if (!mType) return kResultFalse;
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("Notes"), 1);
        return kResultOk;
    }

    tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement *inputs, int32 numIns, SpeakerArrangement *outputs, int32 numOuts)
    {
        // an instrument: no audio in, one stereo out — nothing else
        if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo) return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
        return kResultFalse;
    }

    tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize)
    {
        return symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64 ? kResultTrue : kResultFalse;
    }

    void Processor::makeDevice()
    {
        mDevice = DeviceRegistry::create(mType->name);
        for (int i = 0; i < (int)mType->params.size(); ++i) mDevice->setParam(i, mValues[i].load());
    }

    tresult PLUGIN_API Processor::setupProcessing(ProcessSetup &setup)
    {
        // not on the audio thread: the one place this plugin allocates
        AudioConfig &cfg = AudioConfig::instance();
        if (cfg.sampleRate() != (Sample)setup.sampleRate) cfg.setSampleRate((Sample)setup.sampleRate);
        const size_t n = (size_t)std::max<int32>(setup.maxSamplesPerBlock, 1);
        mL.assign(n, 0.0);
        mR.assign(n, 0.0);
        mChanges.clear();
        mChanges.reserve(kMaxChanges);
        makeDevice();
        return AudioEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API Processor::setActive(TBool state)
    {
        if (state && !mDevice) return kResultFalse; // setupProcessing first
        // no warm-up block: an instrument writes its parameters straight through (no ramp to finish) —
        // vst3_equivalence renders the reference WITH Solaris's warm-up and still matches to the sample
        if (mDevice) mDevice->reset();
        return AudioEffect::setActive(state);
    }

    void Processor::apply(const Change &c)
    {
        switch (c.kind)
        {
        case 0:
            mDevice->setParam(c.index, c.value);
            mValues[(size_t)c.index].store(mDevice->param(c.index));
            break;
        case 1:
            if (c.value <= 0.0) mDevice->noteOff(c.index); // a note-on at velocity 0 is a note-off (MIDI's rule)
            else mDevice->noteOn(c.index, (int)c.value);
            break;
        default:
            mDevice->noteOff(c.index);
            break;
        }
    }

    void Processor::render(int from, int n)
    {
        if (n <= 0) return;
        Sample *io[2] = {mL.data() + from, mR.data() + from};
        mDevice->process(io, 2, n);
    }

    tresult PLUGIN_API Processor::process(ProcessData &data)
    {
        if (!mDevice) return kResultFalse;
        // a state the host set from its UI thread: taken when free, never waited for
        if (mPendingLock.try_lock())
        {
            if (mPendingSet)
            {
                for (int i = 0; i < (int)mPending.size(); ++i)
                {
                    mDevice->setParam(i, mPending[(size_t)i]);
                    mValues[(size_t)i].store(mDevice->param(i));
                }
                mPendingSet = false;
            }
            mPendingLock.unlock();
        }
        // every parameter point and note, at its sample offset
        mChanges.clear();
        const int32 frames = std::max<int32>(0, std::min<int32>(data.numSamples, (int32)mL.size()));
        if (IParameterChanges *pc = data.inputParameterChanges)
            for (int32 q = 0; q < pc->getParameterCount(); ++q)
                if (IParamValueQueue *queue = pc->getParameterData(q))
                {
                    const ParamID id = queue->getParameterId();
                    if (id >= (ParamID)mType->params.size()) continue;
                    for (int32 k = 0; k < queue->getPointCount() && (int)mChanges.size() < kMaxChanges; ++k)
                    {
                        int32 at = 0;
                        ParamValue norm = 0;
                        if (queue->getPoint(k, at, norm) != kResultOk) continue;
                        mChanges.push_back(Change{std::clamp<int32>(at, 0, frames), 0, (int)id, valueFromNormalized(mType->params[id], norm)});
                    }
                }
        if (IEventList *ev = data.inputEvents)
            for (int32 k = 0; k < ev->getEventCount() && (int)mChanges.size() < kMaxChanges; ++k)
            {
                Event e{};
                if (ev->getEvent(k, e) != kResultOk) continue;
                const int32 at = std::clamp<int32>(e.sampleOffset, 0, frames);
                if (e.type == Event::kNoteOnEvent)
                    mChanges.push_back(Change{at, 1, e.noteOn.pitch, (double)std::clamp<long>(std::lround(e.noteOn.velocity * 127.0f), 0, 127)});
                else if (e.type == Event::kNoteOffEvent)
                    mChanges.push_back(Change{at, 2, e.noteOff.pitch, 0.0});
            }
        std::stable_sort(mChanges.begin(), mChanges.end(), [](const Change &a, const Change &b) { return a.offset < b.offset; });

        // the block, split at each change (the device ADDS into silence)
        std::fill(mL.begin(), mL.begin() + frames, 0.0);
        std::fill(mR.begin(), mR.begin() + frames, 0.0);
        int pos = 0;
        for (const Change &c : mChanges)
        {
            render(pos, c.offset - pos);
            pos = std::max(pos, (int)c.offset);
            apply(c);
        }
        render(pos, frames - pos);

        if (data.numOutputs < 1 || data.outputs[0].numChannels < 2 || frames == 0) return kResultOk;
        AudioBusBuffers &out = data.outputs[0];
        if (data.symbolicSampleSize == kSample64)
            for (int32 i = 0; i < frames; ++i) { out.channelBuffers64[0][i] = mL[(size_t)i]; out.channelBuffers64[1][i] = mR[(size_t)i]; }
        else
            for (int32 i = 0; i < frames; ++i) { out.channelBuffers32[0][i] = (float)mL[(size_t)i]; out.channelBuffers32[1][i] = (float)mR[(size_t)i]; }
        out.silenceFlags = 0;
        return kResultOk;
    }

    tresult PLUGIN_API Processor::setState(IBStream *state)
    {
        std::vector<double> v((size_t)mType->params.size());
        for (size_t i = 0; i < v.size(); ++i) v[i] = mValues[i].load();
        if (!readState(*mType, readAll(state), v)) return kResultFalse;
        for (size_t i = 0; i < v.size(); ++i) mValues[i].store(mType->params[i].clamp(v[i]));
        std::lock_guard<std::mutex> g(mPendingLock);
        mPending = v;
        mPendingSet = true;
        return kResultOk;
    }

    tresult PLUGIN_API Processor::getState(IBStream *state)
    {
        std::vector<double> v((size_t)mType->params.size());
        for (size_t i = 0; i < v.size(); ++i) v[i] = mValues[i].load();
        return writeAll(state, stateText(*mType, v)) ? kResultOk : kResultFalse;
    }

    // ── the controller ──────────────────────────────────────────────────────────────────────

    Controller::Controller(const PluginId &id) : mType(DeviceRegistry::find(id.type)) {}

    tresult PLUGIN_API Controller::initialize(FUnknown *context)
    {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk) return r;
        if (!mType) return kResultFalse;
        for (size_t i = 0; i < mType->params.size(); ++i) parameters.addParameter(new RegistryParameter(mType->params[i], (ParamID)i));
        return kResultOk;
    }

    IPlugView *PLUGIN_API Controller::createView(FIDString name)
    {
#if ARSTRO_VST3_EDITOR
        if (name && FIDStringsEqual(name, ViewType::kEditor)) return createEditor(*this);
#else
        (void)name;
#endif
        return nullptr; // built alone: the host draws its generic parameter view
    }

    double Controller::plainValue(int i) const
    {
        if (i < 0 || i >= (int)mType->params.size()) return 0.0;
        return valueFromNormalized(mType->params[(size_t)i], const_cast<Controller *>(this)->getParamNormalized((ParamID)i));
    }

    void Controller::editBegin(int i) { beginEdit((ParamID)i); }

    void Controller::editPerform(int i, double plain)
    {
        if (i < 0 || i >= (int)mType->params.size()) return;
        const ParamValue n = normalizedFromValue(mType->params[(size_t)i], plain);
        setParamNormalized((ParamID)i, n); // the controller's own copy, then the host (and through it the processor)
        performEdit((ParamID)i, n);
    }

    void Controller::editEnd(int i) { endEdit((ParamID)i); }

    tresult PLUGIN_API Controller::setComponentState(IBStream *state)
    {
        std::vector<double> v(mType->params.size());
        for (size_t i = 0; i < v.size(); ++i) v[i] = mType->params[i].def;
        if (!readState(*mType, readAll(state), v)) return kResultFalse;
        for (size_t i = 0; i < v.size(); ++i) setParamNormalized((ParamID)i, normalizedFromValue(mType->params[i], v[i]));
        return kResultOk;
    }

    // ── a parameter ─────────────────────────────────────────────────────────────────────────

    RegistryParameter::RegistryParameter(const ParamSpec &spec, ParamID id) : mSpec(spec)
    {
        StringConvert::convert(spec.label, info.title);
        StringConvert::convert(spec.label, info.shortTitle);
        StringConvert::convert(spec.unit, info.units);
        info.id = id;
        info.stepCount = paramSteps(spec);
        info.defaultNormalizedValue = normalizedFromValue(spec, spec.def);
        info.unitId = kRootUnitId;
        info.flags = ParameterInfo::kCanAutomate | (spec.isChoice() ? ParameterInfo::kIsList : 0);
        setNormalized(info.defaultNormalizedValue);
    }

    void RegistryParameter::toString(ParamValue valueNormalized, String128 string) const
    {
        StringConvert::convert(paramToText(mSpec, valueFromNormalized(mSpec, valueNormalized)), string);
    }

    bool RegistryParameter::fromString(const TChar *string, ParamValue &valueNormalized) const
    {
        std::string t = StringConvert::convert(string);
        // "900 Hz" as well as "900": the unit a host may echo back is allowed
        if (!mSpec.unit.empty() && t.size() > mSpec.unit.size() && t.compare(t.size() - mSpec.unit.size(), mSpec.unit.size(), mSpec.unit) == 0)
            t.erase(t.size() - mSpec.unit.size());
        while (!t.empty() && t.back() == ' ') t.pop_back();
        while (!t.empty() && t.front() == ' ') t.erase(t.begin());
        double v = 0;
        if (!paramFromText(mSpec, t, v)) return false;
        valueNormalized = normalizedFromValue(mSpec, v);
        return true;
    }

    ParamValue RegistryParameter::toPlain(ParamValue valueNormalized) const { return valueFromNormalized(mSpec, valueNormalized); }
    ParamValue RegistryParameter::toNormalized(ParamValue plainValue) const { return normalizedFromValue(mSpec, plainValue); }
}
}
