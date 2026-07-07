// Global A/B full-drop test log: compact JSONL I/O + preference analysis.
// Dependency-free (no JSON lib, no JUCE), mirroring calibration.cpp's style.
#include "rtg/library/ab_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>

namespace rtg {
namespace {

namespace fs = std::filesystem;

// Pack an ABFeat into its fixed 13-float serialization order.
void featToArray(const ABFeat& f, float out[13]) {
    out[0] = f.lufs; out[1] = f.crest; out[2] = f.contrast; out[3] = f.tilt;
    out[4] = f.width;
    for (int b = 0; b < 5; ++b) out[5 + b] = f.bands[b];
    out[10] = f.growlOdd; out[11] = f.growlWobbleHz; out[12] = f.growlCentroidHz;
}
void arrayToFeat(const float in[13], ABFeat& f) {
    f.lufs = in[0]; f.crest = in[1]; f.contrast = in[2]; f.tilt = in[3];
    f.width = in[4];
    for (int b = 0; b < 5; ++b) f.bands[b] = in[5 + b];
    f.growlOdd = in[10]; f.growlWobbleHz = in[11]; f.growlCentroidHz = in[12];
}

void writeArray(std::ostream& os, const float* v, int n) {
    os << '[';
    for (int i = 0; i < n; ++i) { if (i) os << ','; os << v[i]; }
    os << ']';
}

// Read `n` comma-separated numbers from `"key":[ ... ]`. Returns count read.
int readArray(const std::string& s, const std::string& key, float* out, int n) {
    size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return 0;
    size_t lb = s.find('[', k);
    if (lb == std::string::npos) return 0;
    size_t rb = s.find(']', lb);
    if (rb == std::string::npos) return 0;
    std::string body = s.substr(lb + 1, rb - lb - 1);
    int got = 0; size_t i = 0;
    while (i < body.size() && got < n) {
        while (i < body.size() && (body[i] == ',' || body[i] == ' ')) ++i;
        size_t vs = i;
        while (i < body.size() && body[i] != ',') ++i;
        if (i > vs) { try { out[got++] = std::stof(body.substr(vs, i - vs)); } catch (...) {} }
    }
    return got;
}

// Read a scalar number after `"key":`.
double readScalar(const std::string& s, const std::string& key, double def) {
    size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return def;
    size_t c = s.find(':', k);
    if (c == std::string::npos) return def;
    ++c;
    while (c < s.size() && (s[c] == ' ' || s[c] == '\t')) ++c;
    size_t vs = c;
    while (c < s.size()) {
        char ch = s[c];
        if ((ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.'
            || ch == 'e' || ch == 'E') ++c;
        else break;
    }
    if (c <= vs) return def;
    try { return std::stod(s.substr(vs, c - vs)); } catch (...) { return def; }
}

} // namespace

std::string abLogPath(const std::string& dataDir) {
    return (fs::path(dataDir) / "ab_drops.jsonl").string();
}

bool abLogAppend(const std::string& path, const ABResult& r) {
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    float fa[13], fb[13];
    featToArray(r.featA, fa);
    featToArray(r.featB, fb);

    std::ostringstream os;
    os << "{\"ts\":" << r.ts << ",\"g\":" << r.genre
       << ",\"sa\":" << r.seedA << ",\"sb\":" << r.seedB
       << ",\"w\":" << r.winner
       << ",\"pa\":"; writeArray(os, r.paramsA, AB_ParamCount);
    os << ",\"pb\":"; writeArray(os, r.paramsB, AB_ParamCount);
    os << ",\"fa\":"; writeArray(os, fa, 13);
    os << ",\"fb\":"; writeArray(os, fb, 13);
    os << "}\n";

    std::string line = os.str();
    FILE* f = std::fopen(path.c_str(), "ab");   // append, create if missing
    if (!f) return false;
    size_t w = std::fwrite(line.data(), 1, line.size(), f);
    std::fclose(f);
    return w == line.size();
}

std::vector<ABResult> abLogLoad(const std::string& path) {
    std::vector<ABResult> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::string all;
    char buf[8192]; size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    std::fclose(f);

    std::istringstream is(all);
    std::string line;
    while (std::getline(is, line)) {
        if (line.find("\"ts\"") == std::string::npos) continue;
        ABResult r;
        r.ts = (long long) llround(readScalar(line, "ts", 0));
        r.genre = (int) llround(readScalar(line, "g", 0));
        r.seedA = (uint64_t) readScalar(line, "sa", 0);
        r.seedB = (uint64_t) readScalar(line, "sb", 0);
        r.winner = (int) llround(readScalar(line, "w", 0));
        readArray(line, "pa", r.paramsA, AB_ParamCount);
        readArray(line, "pb", r.paramsB, AB_ParamCount);
        float fa[13] = {0}, fb[13] = {0};
        readArray(line, "fa", fa, 13);
        readArray(line, "fb", fb, 13);
        arrayToFeat(fa, r.featA);
        arrayToFeat(fb, r.featB);
        out.push_back(r);
    }
    return out;
}

ABSummary abLogSummarize(const std::vector<ABResult>& results, int lastN) {
    ABSummary s;
    size_t total = results.size();
    size_t start = (lastN > 0 && (size_t) lastN < total) ? total - (size_t) lastN : 0;
    float fAcc[13] = {0}; float pAcc[AB_ParamCount] = {0};
    int n = 0;
    for (size_t i = start; i < total; ++i) {
        const ABResult& r = results[i];
        const ABFeat& win  = (r.winner == 0) ? r.featA : r.featB;
        const ABFeat& lose = (r.winner == 0) ? r.featB : r.featA;
        const float* pw = (r.winner == 0) ? r.paramsA : r.paramsB;
        const float* pl = (r.winner == 0) ? r.paramsB : r.paramsA;
        float fw[13], fl[13];
        featToArray(win, fw); featToArray(lose, fl);
        for (int k = 0; k < 13; ++k) fAcc[k] += fw[k] - fl[k];
        for (int k = 0; k < AB_ParamCount; ++k) pAcc[k] += pw[k] - pl[k];
        ++n;
    }
    s.n = n;
    if (n > 0) {
        float fd[13];
        for (int k = 0; k < 13; ++k) fd[k] = fAcc[k] / float(n);
        arrayToFeat(fd, s.featDelta);
        for (int k = 0; k < AB_ParamCount; ++k) s.paramDelta[k] = pAcc[k] / float(n);
    }

    std::ostringstream os;
    os << "A/B drop preference summary over " << n << " test(s):\n";
    if (n == 0) { os << "  (no results yet)\n"; s.text = os.str(); return s; }
    os << "  Preferred drops tend to have (winner - loser, mean):\n";
    auto line = [&](const char* name, float d, const char* unit) {
        os << "    " << name << ": " << (d >= 0 ? "+" : "") << d << unit << "\n";
    };
    line("aggression",  s.paramDelta[AB_Aggression], "");
    line("darkness",    s.paramDelta[AB_Darkness], "");
    line("energy",      s.paramDelta[AB_Energy], "");
    line("complexity",  s.paramDelta[AB_Complexity], "");
    line("melody",      s.paramDelta[AB_Melody], "");
    line("chaos",       s.paramDelta[AB_Chaos], "");
    line("bpm",         s.paramDelta[AB_Bpm], " BPM");
    line("growl square-ness (odd)", s.featDelta.growlOdd, "");
    line("growl wobble",            s.featDelta.growlWobbleHz, " Hz");
    line("growl brightness",        s.featDelta.growlCentroidHz, " Hz");
    line("crest (punch)",           s.featDelta.crest, " dB");
    line("contrast (drop slam)",    s.featDelta.contrast, " LU");
    line("stereo width",            s.featDelta.width, "");
    line("spectral tilt",           s.featDelta.tilt, " dB/oct");
    os << "  Band-share deltas (sub,lowmid,mid,himid,air): ";
    for (int b = 0; b < 5; ++b) { if (b) os << ", "; os << s.featDelta.bands[b]; }
    os << "\n";
    s.text = os.str();
    return s;
}

} // namespace rtg
