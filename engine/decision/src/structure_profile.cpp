// Structure (song-FORM) Reference Profile: tolerant flat-JSON I/O and the
// process-wide active registry. Dependency-free (no JSON library, no JUCE),
// mirroring DrumProfile exactly. When a StructureProfile is active, the
// arrangement builder uses its measured section bar-lengths + drop count
// verbatim instead of its own style/energy menus and hill-climb.
#include "rtg/decision/structure_profile.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace rtg {
namespace {

// Hand-rolled minimal flat-JSON scanner (copied from drum_profile.cpp). Tolerant:
// unknown keys ignored, missing keys keep defaults. Numeric values only.
std::map<std::string, double> scanFlat(const std::string& text) {
    std::map<std::string, double> out;
    size_t i = 0, n = text.size();
    while (i < n) {
        if (text[i] != '"') { ++i; continue; }
        size_t ks = ++i;
        while (i < n && text[i] != '"') ++i;
        if (i >= n) break;
        std::string key = text.substr(ks, i - ks);
        ++i;
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        if (i >= n || text[i] != ':') continue;
        ++i;
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        size_t vs = i;
        while (i < n) {
            char c = text[i];
            if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') ++i;
            else break;
        }
        if (i > vs) { try { out[key] = std::stod(text.substr(vs, i - vs)); } catch (...) {} }
    }
    return out;
}

// Process-wide active profile slot. Set before generation, read during.
std::optional<StructureProfile>& activeSlot() {
    static std::optional<StructureProfile> g;
    return g;
}

} // namespace

void StructureProfile::setActive(std::optional<StructureProfile> p) { activeSlot() = std::move(p); }

const StructureProfile& StructureProfile::active() {
    static const StructureProfile builtin;   // present == false default
    const auto& s = activeSlot();
    return s ? *s : builtin;
}

bool StructureProfile::loadFromFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text; char buf[4096]; size_t r;
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, r);
    std::fclose(f);
    std::map<std::string, double> m = scanFlat(text);
    auto get = [&](const char* k, double def) -> double {
        auto it = m.find(k); return it != m.end() ? it->second : def;
    };
    introBars = int(std::lround(get("introBars", double(introBars))));
    buildBars = int(std::lround(get("buildBars", double(buildBars))));
    dropBars  = int(std::lround(get("dropBars",  double(dropBars))));
    breakBars = int(std::lround(get("breakBars", double(breakBars))));
    outroBars = int(std::lround(get("outroBars", double(outroBars))));
    dropCount = int(std::lround(get("dropCount", double(dropCount))));
    present   = true;   // a successfully loaded file makes the profile present
    return true;
}

} // namespace rtg
