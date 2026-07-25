/*
 *  Arstro Piano Demo — the "simple UI" for auditioning PianoVoice (REQ-piano-10).
 *
 *  Same headless-bench pattern as apps/kitchen_sink/main.cpp: raw S16LE PCM to
 *  stdout (pipe into aplay/paplay), or an offline WAV render. No new UI
 *  framework — a small CLI is the pragmatic "simple UI" here (kitchen_sink's own
 *  browser front end needs emscripten, which this environment doesn't have; see
 *  src/physical/README.md's PianoEngine note).
 *
 *  Audio I/O:
 *      arstro_piano_demo | aplay  -f S16_LE -c 2 -r 48000
 *      arstro_piano_demo | paplay --raw --format=s16le --channels=2 --rate=48000
 *      arstro_piano_demo --wav out.wav            # canned demo sequence (~13.5s)
 *      arstro_piano_demo --wav out.wav 5           # same, capped/padded to 5s
 *
 *  Control (one command per line on stdin, also works piped):
 *      on <note> [vel]      note on   (e.g. `on 60 0.8`; note = MIDI note number)
 *      off <note>           note off
 *      sustain on|off       sustain (damper) pedal
 *      sostenuto on|off     sostenuto pedal
 *      una on|off           una corda (soft) pedal
 *      panic                all notes off, pedals cleared
 *      demo                 play the canned anatomy-showcase sequence
 *      help                 list commands
 *      quit                 exit
 */
#include "PianoEngine.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <memory>
#include <unistd.h>
#include <sys/select.h>

using namespace arstro;

// ───────────────────────── audio sinks ─────────────────────────
// Same small IO helper shape as apps/kitchen_sink/main.cpp — app-level plumbing,
// duplicated locally rather than shared (it isn't part of the DSP library).

struct AudioSink
{
    virtual ~AudioSink() = default;
    virtual void write(const std::vector<uint8_t> &pcm) = 0;
    virtual void close() {}
};

struct StdoutSink : AudioSink
{
    void write(const std::vector<uint8_t> &pcm) override
    {
        std::fwrite(pcm.data(), 1, pcm.size(), stdout);
        std::fflush(stdout);
    }
};

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

// ───────────────────────── command handling ─────────────────────────

static void printHelp()
{
    std::fprintf(stderr,
        "commands: on <note> [vel] | off <note> | sustain on|off | sostenuto on|off | "
        "una on|off | panic | demo | quit\n");
}

static bool handleCommand(PianoEngine &eng, const std::string &line)
{
    std::istringstream ss(line);
    std::string cmd;
    if (!(ss >> cmd)) return true;
    if (cmd == "quit" || cmd == "q") return false;
    if (cmd == "help") { printHelp(); return true; }
    if (cmd == "on")       { int n; double v = 0.8; ss >> n; ss >> v; eng.noteOnMidi(n, v); return true; }
    if (cmd == "off")      { int n; ss >> n; eng.noteOff(n); return true; }
    if (cmd == "sustain")  { std::string v; ss >> v; eng.setSustainPedal(v == "on"); return true; }
    if (cmd == "sostenuto"){ std::string v; ss >> v; eng.setSostenutoPedal(v == "on"); return true; }
    if (cmd == "una")      { std::string v; ss >> v; eng.setUnaCorda(v == "on"); return true; }
    if (cmd == "panic")    { eng.panic(); return true; }
    std::fprintf(stderr, "unknown command: %s (try `help`)\n", cmd.c_str());
    return true;
}

static bool gStdinEof = false;

static bool drainStdin(PianoEngine &eng)
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
        if (got == 0) { gStdinEof = true; return true; }
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

// ───────────────────────── canned demo sequence ─────────────────────────
// Showcases each anatomy stage in turn: a struck bass note (inharmonicity/
// warmth), a sustained chord (pedal), sympathetic resonance (silently-held
// octave), and una corda vs. normal strike contrast. ~13.5s total.

struct DemoEvent { double atSeconds; std::string cmd; };

static const std::vector<DemoEvent> &demoScript()
{
    static const std::vector<DemoEvent> script = {
        {0.0, "on 48 0.9"},                                  // bass C3, struck hard
        {1.5, "off 48"},
        {2.0, "on 60 0.7"}, {2.0, "on 64 0.7"}, {2.0, "on 67 0.7"}, // C4-E4-G4 chord
        {3.5, "sustain on"},
        {3.6, "off 60"}, {3.6, "off 64"}, {3.6, "off 67"},    // pedal keeps it ringing
        {5.5, "sustain off"},                                 // dampers engage
        {6.0, "on 69 0.0"},                                   // silently depress A4 (velocity 0)
        {6.05, "on 57 0.9"},                                  // strike A3 (octave) -> sympathetic A4
        {7.5, "off 57"},
        {8.0, "off 69"},
        {8.5, "una on"},
        {8.6, "on 60 0.9"},                                   // soft-pedal strike
        {9.6, "off 60"},
        {10.0, "una off"},
        {10.1, "on 60 0.9"},                                  // same note, normal, for contrast
        {11.1, "off 60"},
    };
    return script;
}

static void runDemo(PianoEngine &eng, AudioSink &sink, int sr, int frames, double totalSeconds)
{
    const auto &script = demoScript();
    size_t next = 0;
    const long long total = (long long)(totalSeconds * sr);
    std::vector<uint8_t> block;
    for (long long done = 0; done < total; done += frames)
    {
        double t = (double)done / sr;
        while (next < script.size() && script[next].atSeconds <= t)
        {
            handleCommand(eng, script[next].cmd);
            ++next;
        }
        eng.renderBlockBytes(block, frames);
        sink.write(block);
    }
}

int main(int argc, char **argv)
{
    const int sr = 48000, frames = 128;
    AudioConfig::instance().setSampleRate(sr);
    AudioConfig::instance().setChannelCount(2);
    AudioConfig::instance().setOutputBitDepth(16);

    std::string wavPath;
    double seconds = 13.5; // default: full canned demo length
    bool haveWav = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--wav" && i + 1 < argc) { wavPath = argv[++i]; haveWav = true; if (i + 1 < argc) seconds = std::atof(argv[++i]); }
        else if (a == "--help") { printHelp(); return 0; }
    }

    PianoEngine eng;

    if (haveWav)
    {
        WavSink sink(wavPath, sr, 2);
        runDemo(eng, sink, sr, frames, seconds);
        sink.close();
        return 0;
    }

    StdoutSink sink;
    std::fprintf(stderr, "arstro piano demo — %d Hz 2ch; type `help`. Pipe stdout to aplay/paplay.\n", sr);
    std::vector<uint8_t> block;
    const long long tailLen = 2 * sr;
    long long tailDone = 0;
    bool running = true;
    while (running)
    {
        running = drainStdin(eng);
        if (gStdinEof && (tailDone += frames) >= tailLen) break;
        eng.renderBlockBytes(block, frames);
        sink.write(block);
    }
    sink.close();
    return 0;
}
