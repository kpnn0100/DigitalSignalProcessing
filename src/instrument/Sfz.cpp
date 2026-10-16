#include "Sfz.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace arstro
{
    namespace
    {
        std::string stripComments(const std::string &t)
        {
            std::string o;
            o.reserve(t.size());
            for (size_t i = 0; i < t.size(); ++i)
            {
                if (t.compare(i, 2, "//") == 0) { while (i < t.size() && t[i] != '\n') ++i; o += '\n'; continue; }
                if (t.compare(i, 2, "/*") == 0)
                {
                    const size_t e = t.find("*/", i + 2);
                    i = e == std::string::npos ? t.size() : e + 1;
                    o += ' ';
                    continue;
                }
                o += t[i];
            }
            return o;
        }
        std::string trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace((unsigned char)s[a])) ++a;
            while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
            return s.substr(a, b - a);
        }
        bool number(const std::string &s, double &v)
        {
            char *end = nullptr;
            v = std::strtod(s.c_str(), &end);
            return end && end != s.c_str() && *end == '\0';
        }
        using Opcodes = std::map<std::string, std::string>;

        void apply(SfzRegion &r, const Opcodes &ops, SfzInstrument &out)
        {
            for (const auto &kv : ops)
            {
                const std::string &k = kv.first, &v = kv.second;
                double x = 0;
                auto bad = [&] { out.problems.push_back("`" + k + "=" + v + "` is not a value"); };
                auto key = [&](int &dst) { const int n = sfzKey(v); if (n < 0) bad(); else dst = n; };
                if (k == "sample") r.sample = v;
                else if (k == "lokey") key(r.lokey);
                else if (k == "hikey") key(r.hikey);
                else if (k == "pitch_keycenter") key(r.keycenter);
                else if (k == "key") { int n = -1; key(n); if (n >= 0) r.lokey = r.hikey = r.keycenter = n; }
                else if (k == "lovel") { if (number(v, x)) r.lovel = (int)x; else bad(); }
                else if (k == "hivel") { if (number(v, x)) r.hivel = (int)x; else bad(); }
                else if (k == "lorand") { if (number(v, x)) r.lorand = x; else bad(); }
                else if (k == "hirand") { if (number(v, x)) r.hirand = x; else bad(); }
                else if (k == "seq_length") { if (number(v, x)) r.seqLength = std::max(1, (int)x); else bad(); }
                else if (k == "seq_position") { if (number(v, x)) r.seqPosition = std::max(1, (int)x); else bad(); }
                else if (k == "tune") { if (number(v, x)) r.tune = x; else bad(); }
                else if (k == "transpose") { if (number(v, x)) r.transpose = (int)x; else bad(); }
                else if (k == "volume") { if (number(v, x)) r.volume = x; else bad(); }
                else if (k == "pan") { if (number(v, x)) r.pan = std::clamp(x, -100.0, 100.0); else bad(); }
                else if (k == "offset") { if (number(v, x)) r.offset = (long long)x; else bad(); }
                else if (k == "end") { if (number(v, x)) r.end = (long long)x; else bad(); }
                else if (k == "loop_mode")
                {
                    if (v == "no_loop") r.loop = SfzRegion::NoLoop;
                    else if (v == "one_shot") r.loop = SfzRegion::OneShot;
                    else if (v == "loop_continuous") r.loop = SfzRegion::LoopContinuous;
                    else if (v == "loop_sustain") r.loop = SfzRegion::LoopSustain;
                    else bad();
                }
                else if (k == "loop_start" || k == "loopstart") { if (number(v, x)) r.loopStart = (long long)x; else bad(); }
                else if (k == "loop_end" || k == "loopend") { if (number(v, x)) r.loopEnd = (long long)x; else bad(); }
                else if (k == "ampeg_attack") { if (number(v, x)) r.attack = std::max(0.0, x); else bad(); }
                else if (k == "ampeg_decay") { if (number(v, x)) r.decay = std::max(0.0, x); else bad(); }
                else if (k == "ampeg_sustain") { if (number(v, x)) r.sustain = std::clamp(x, 0.0, 100.0); else bad(); }
                else if (k == "ampeg_release") { if (number(v, x)) r.release = std::max(0.0, x); else bad(); }
                else if (k == "group") { if (number(v, x)) r.group = (int)x; else bad(); }
                else if (k == "off_by") { if (number(v, x)) r.offBy = (int)x; else bad(); }
                else if (k != "default_path") out.problems.push_back("`" + k + "` is not read (yet) — ignored");
            }
        }
    }

    int sfzKey(const std::string &s)
    {
        double x = 0;
        if (number(s, x)) return x >= 0 && x <= 127 ? (int)x : -1;
        if (s.size() < 2) return -1;
        static const int kBase[7] = {9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
        const char c = (char)std::tolower((unsigned char)s[0]);
        if (c < 'a' || c > 'g') return -1;
        int n = kBase[c - 'a'];
        size_t i = 1;
        if (s[i] == '#') { ++n; ++i; }
        else if (s[i] == 'b' && i + 1 < s.size()) { --n; ++i; }
        if (!number(s.substr(i), x)) return -1;
        const int key = n + 12 * ((int)x + 1); // c4 = 60
        return key >= 0 && key <= 127 ? key : -1;
    }

    SfzInstrument parseSfz(const std::string &text)
    {
        // (Z1) headers and `name=value` opcodes; a value runs to the next `name=` or header
        SfzInstrument out;
        const std::string t = stripComments(text);
        Opcodes global, group, region, control;
        enum { None, Control, Global, Group, Region } at = None;
        std::string defaultPath;
        auto closeRegion = [&] {
            if (at != Region) return;
            SfzRegion r;
            Opcodes all = global;
            for (const auto &kv : group) all[kv.first] = kv.second;
            for (const auto &kv : region) all[kv.first] = kv.second;
            apply(r, all, out);
            if (!r.sample.empty()) r.sample = defaultPath + r.sample;
            if (r.sample.empty()) out.problems.push_back("a region without a sample — ignored");
            else out.regions.push_back(r);
            region.clear();
        };
        // tokens: headers <…> and runs of text between them
        size_t i = 0;
        while (i < t.size())
        {
            if (t[i] == '<')
            {
                const size_t e = t.find('>', i);
                if (e == std::string::npos) break;
                const std::string h = t.substr(i + 1, e - i - 1);
                closeRegion();
                if (h == "region") at = Region;
                else if (h == "group") { at = Group; group.clear(); }
                else if (h == "global") { at = Global; global.clear(); }
                else if (h == "control") at = Control;
                else { at = None; out.problems.push_back("<" + h + "> is not read (yet) — ignored"); }
                i = e + 1;
                continue;
            }
            const size_t next = t.find('<', i);
            const std::string body = t.substr(i, next == std::string::npos ? std::string::npos : next - i);
            i = next == std::string::npos ? t.size() : next;
            // the opcodes in this run: each `name=` starts one, its value until the next `name=`
            std::vector<std::pair<size_t, size_t>> starts; // (name start, '=' position)
            for (size_t j = 0; j < body.size(); ++j)
                if (body[j] == '=')
                {
                    size_t k = j;
                    while (k > 0 && (std::isalnum((unsigned char)body[k - 1]) || body[k - 1] == '_')) --k;
                    if (k < j && (k == 0 || std::isspace((unsigned char)body[k - 1]))) starts.emplace_back(k, j);
                }
            for (size_t n = 0; n < starts.size(); ++n)
            {
                const std::string name = body.substr(starts[n].first, starts[n].second - starts[n].first);
                const size_t vEnd = n + 1 < starts.size() ? starts[n + 1].first : body.size();
                const std::string value = trim(body.substr(starts[n].second + 1, vEnd - starts[n].second - 1));
                switch (at)
                {
                case Control: control[name] = value; if (name == "default_path") defaultPath = value; break;
                case Global: global[name] = value; break;
                case Group: group[name] = value; break;
                case Region: region[name] = value; break;
                default: break;
                }
            }
        }
        closeRegion();
        // a path written with backslashes (Windows-made SFZ) reads with slashes
        for (auto &r : out.regions) std::replace(r.sample.begin(), r.sample.end(), '\\', '/');
        return out;
    }

    std::vector<std::string> sfzSounds(const SfzInstrument &sfz)
    {
        std::vector<std::string> v;
        for (const auto &r : sfz.regions)
            if (std::find(v.begin(), v.end(), r.sample) == v.end()) v.push_back(r.sample);
        return v;
    }
}
