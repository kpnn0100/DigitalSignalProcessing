/*
 *  Arstro Kitchen Sink — interactive bench for auditioning the DSP processors
 *  through the system's default audio output.
 *
 *  It builds the full Arstro instrument (SynthEngine: oscillators -> compressor ->
 *  overdrive -> chorus -> repeater -> reverb -> output) and lets you play notes and
 *  tweak every processor's parameters live, then streams audio to the default
 *  audio device.
 *
 *  Audio I/O (no extra link deps): raw interleaved S16LE PCM is written to stdout;
 *  pipe it into the system's default player. On this box `aplay`/`paplay` exist:
 *
 *      arstro_kitchen_sink | aplay  -f S16_LE -c 2 -r 48000
 *      arstro_kitchen_sink | paplay --raw --format=s16le --channels=2 --rate=48000
 *
 *  Or render offline to a WAV (no device needed):
 *      arstro_kitchen_sink --wav out.wav 5      # 5 seconds of the demo
 *
 *  Control (one command per line on stdin, also works piped):
 *      on <note> [vel]   note on   (e.g. `on 60 0.8`)
 *      off <note>        note off
 *      panic             all notes off
 *      <name> <value>    set a named parameter (see `help`)
 *      p <id> <value>    set a raw param id (hex ok: 0x0800)
 *      demo              trigger a short chord
 *      help              list parameter names
 *      quit              exit
 */
#include "../../src/synth_dsp.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <memory>
#include <unistd.h>
#include <sys/select.h>

using namespace arstro;

// ───────────────────────── audio sinks (SOLID: one role each) ─────────────────────────

struct AudioSink
{
    virtual ~AudioSink() = default;
    virtual void write(const std::vector<uint8_t> &pcm) = 0;
    virtual void close() {}
};

// Streams raw S16LE PCM to stdout — pipe into the default player (aplay/paplay).
struct StdoutSink : AudioSink
{
    void write(const std::vector<uint8_t> &pcm) override
    {
        std::fwrite(pcm.data(), 1, pcm.size(), stdout);
        std::fflush(stdout);
    }
};

// Buffers PCM and writes a 16-bit WAV on close — offline, no device needed.
struct WavSink : AudioSink
{
    std::string path;
    int sr, ch;
    std::vector<uint8_t> buf;
    WavSink(std::string p, int sampleRate, int channels) : path(std::move(p)), sr(sampleRate), ch(channels) {}
    void write(const std::vector<uint8_t> &pcm) override { buf.insert(buf.end(), pcm.begin(), pcm.end()); }
    void close() override
    {
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) { std::perror("fopen"); return; }
        const uint32_t dataBytes = (uint32_t)buf.size();
        const uint32_t byteRate = (uint32_t)sr * ch * 2;
        auto u32 = [&](uint32_t v){ std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](uint16_t v){ std::fwrite(&v, 2, 1, f); };
        std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
        std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16((uint16_t)ch);
        u32((uint32_t)sr); u32(byteRate); u16((uint16_t)(ch * 2)); u16(16);
        std::fwrite("data", 1, 4, f); u32(dataBytes);
        std::fwrite(buf.data(), 1, buf.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "wrote %s (%u bytes)\n", path.c_str(), dataBytes);
    }
};

// ───────────────────────── named parameter map ─────────────────────────

static const std::unordered_map<std::string, uint16_t> &paramNames()
{
    static const std::unordered_map<std::string, uint16_t> m = {
        {"master.level", MASTER_LEVEL},
        {"osc1.voices", GROUP_OSC1 + OSC_VOICE_COUNT}, {"osc1.detune", GROUP_OSC1 + OSC_DETUNE},
        {"osc1.spread", GROUP_OSC1 + OSC_SPREAD}, {"osc1.attack", GROUP_OSC1 + OSC_ATTACK},
        {"osc1.decay", GROUP_OSC1 + OSC_DECAY}, {"osc1.sustain", GROUP_OSC1 + OSC_SUSTAIN},
        {"osc1.release", GROUP_OSC1 + OSC_RELEASE},
        {"osc2.voices", GROUP_OSC2 + OSC_VOICE_COUNT}, {"osc2.detune", GROUP_OSC2 + OSC_DETUNE},
        {"comp.threshold", GROUP_COMPRESSOR + CMP_THRESHOLD}, {"comp.ratio", GROUP_COMPRESSOR + CMP_RATIO},
        {"comp.bypass", GROUP_COMPRESSOR + FX_BYPASS},
        {"od.drive", GROUP_OVERDRIVE + OD_DRIVE}, {"od.tone", GROUP_OVERDRIVE + OD_TONE},
        {"od.level", GROUP_OVERDRIVE + OD_LEVEL}, {"od.bypass", GROUP_OVERDRIVE + FX_BYPASS},
        {"chorus.rate", GROUP_CHORUS + CH_RATE}, {"chorus.depth", GROUP_CHORUS + CH_DEPTH},
        {"chorus.mix", GROUP_CHORUS + CH_MIX}, {"chorus.bypass", GROUP_CHORUS + FX_BYPASS},
        {"repeat.delay", GROUP_REPEATER + RP_DELAY}, {"repeat.feedback", GROUP_REPEATER + RP_FEEDBACK},
        {"repeat.mix", GROUP_REPEATER + RP_MIX}, {"repeat.bypass", GROUP_REPEATER + FX_BYPASS},
        {"reverb.decay", GROUP_REVERB + RV_DECAY}, {"reverb.mix", GROUP_REVERB + RV_MIX},
        {"reverb.width", GROUP_REVERB + RV_WIDTH}, {"reverb.bypass", GROUP_REVERB + FX_BYPASS},
    };
    return m;
}

static void printHelp()
{
    std::fprintf(stderr, "commands: on <note> [vel] | off <note> | panic | demo | <name> <value> | p <id> <value> | quit\n");
    std::fprintf(stderr, "named parameters:\n");
    for (auto &kv : paramNames())
        std::fprintf(stderr, "  %-16s (id 0x%04X)\n", kv.first.c_str(), kv.second);
}

// Apply a single control line to the engine (returns false on "quit").
static bool handleCommand(SynthEngine &eng, const std::string &line)
{
    std::istringstream ss(line);
    std::string cmd;
    if (!(ss >> cmd)) return true;
    if (cmd == "quit" || cmd == "q") return false;
    if (cmd == "help") { printHelp(); return true; }
    if (cmd == "on")    { int n; double v = 0.8; ss >> n; ss >> v; eng.noteOn(n, v); return true; }
    if (cmd == "off")   { int n; ss >> n; eng.noteOff(n); return true; }
    if (cmd == "panic") { for (int n = 0; n < 128; ++n) eng.noteOff(n); return true; }
    if (cmd == "demo")  { int ch[4] = {60, 64, 67, 71}; for (int n : ch) eng.noteOn(n, 0.7); return true; }
    if (cmd == "p")     { unsigned id; double v; ss >> std::hex >> id >> std::dec >> v; eng.applyParam((uint16_t)id, v); return true; }
    // otherwise: a named parameter
    auto it = paramNames().find(cmd);
    if (it != paramNames().end()) { double v; ss >> v; eng.applyParam(it->second, v); }
    else std::fprintf(stderr, "unknown command: %s (try `help`)\n", cmd.c_str());
    return true;
}

static bool gStdinEof = false;

// Non-blocking: read any complete stdin lines available right now.
// Returns false on an explicit `quit`. Sets gStdinEof when input closes.
static bool drainStdin(SynthEngine &eng)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv{0, 0};
    static std::string pending;
    while (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0 && FD_ISSET(STDIN_FILENO, &fds))
    {
        char chunk[256];
        ssize_t got = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (got == 0) { gStdinEof = true; return true; } // EOF: caller plays a short tail then stops
        if (got < 0) return true;
        pending.append(chunk, got);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos)
        {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!handleCommand(eng, line)) return false;
        }
        FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds); tv = {0, 0};
    }
    return true;
}

int main(int argc, char **argv)
{
    const int sr = 48000, channels = 2, frames = 128;
    AudioConfig::instance().setSampleRate(sr);
    AudioConfig::instance().setChannelCount(channels);
    AudioConfig::instance().setOutputBitDepth(16);

    // Parse args: [--wav <file> [seconds]]
    std::string wavPath;
    double seconds = 0.0; // 0 = run until stdin EOF/quit (live mode)
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--wav" && i + 1 < argc) { wavPath = argv[++i]; seconds = (i + 1 < argc) ? std::atof(argv[++i]) : 4.0; }
        else if (a == "--help") { printHelp(); return 0; }
    }

    SynthEngine eng;
    std::unique_ptr<AudioSink> sink;
    bool offline = !wavPath.empty();
    if (offline) { sink.reset(new WavSink(wavPath, sr, channels)); eng.noteOn(60, 0.7); eng.noteOn(64, 0.7); eng.noteOn(67, 0.7); }
    else         { sink.reset(new StdoutSink()); std::fprintf(stderr, "arstro kitchen sink — %d Hz %dch; type `help`. Pipe stdout to aplay/paplay.\n", sr, channels); }

    std::vector<uint8_t> block;
    const long long total = (long long)(seconds * sr);
    const long long tailLen = 2 * sr;               // play a 2 s tail after stdin closes
    long long done = 0, tailDone = 0;
    bool running = true;
    while (running)
    {
        if (!offline)
        {
            running = drainStdin(eng);              // live: poll control input
            if (gStdinEof && (tailDone += frames) >= tailLen) break;
        }
        eng.renderBlockBytes(block, frames);        // drains queued commands + renders
        sink->write(block);
        done += frames;
        if (offline && done >= total) break;        // offline: fixed duration
    }
    sink->close();
    return 0;
}
