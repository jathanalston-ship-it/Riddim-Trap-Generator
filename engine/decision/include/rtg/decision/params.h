#pragma once
// The complete user-facing parameter surface (doc 01 §2). Nothing else is
// user-controllable; everything downstream derives from this + seed.
#include <cstdint>
#include <string>

namespace rtg {

enum class Genre : int { Riddim = 0, Trap = 1 };

enum class IntroStyle : int {
    Atmospheric = 0, Minimal = 1, VocalChop = 2, Impact = 3, Fakeout = 4
};

struct Params {
    Genre genre = Genre::Riddim;
    double bpm = 145.0;          // Riddim 140–150, Trap 130–170 (UI clamps)
    double lengthSec = 180.0;    // target duration, snapped to bars by planner
    // Character macros, all 0–100:
    int energy = 65;
    int aggression = 70;
    int darkness = 55;
    int complexity = 50;
    int melody = 30;
    int chaos = 25;
    int dropCount = 2;           // 1–4
    IntroStyle introStyle = IntroStyle::Atmospheric;
    uint64_t seed = 1;           // 0 means "auto" (caller replaces before generate)
};

inline const char* genreName(Genre g) { return g == Genre::Riddim ? "Riddim" : "Trap"; }
inline const char* introStyleName(IntroStyle s) {
    switch (s) {
        case IntroStyle::Atmospheric: return "Atmospheric";
        case IntroStyle::Minimal:     return "Minimal";
        case IntroStyle::VocalChop:   return "VocalChop";
        case IntroStyle::Impact:      return "Impact";
        case IntroStyle::Fakeout:     return "Fakeout";
    }
    return "Atmospheric";
}

} // namespace rtg
