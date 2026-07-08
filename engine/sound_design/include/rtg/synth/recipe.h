#pragma once
// Sound recipes: the procedural genome of every sound (doc 05 §1). v1 uses a
// flat named-parameter map per role (full node graphs arrive in v2); recipes
// are still deterministic, re-renderable, mutable, and rateable.
#include <cstdint>
#include <map>
#include <string>
#include "rtg/composition/score.h"

namespace rtg {

enum class Role : int {
    Growl = 0, Screech, Sub, Bass808,
    Kick, Snare, HatClosed, HatOpen, Perc,
    MelodyLead, Pad,
    Riser, Downlifter, Impact, Crash,
    Count
};
constexpr int kRoleCount = int(Role::Count);

inline const char* roleName(Role r) {
    static const char* names[] = { "Growl", "Screech", "Sub", "Bass808", "Kick",
        "Snare", "HatClosed", "HatOpen", "Perc", "MelodyLead", "Pad", "Riser",
        "Downlifter", "Impact", "Crash" };
    return names[int(r)];
}

/// Which recipe role renders a given score lane (genre-dependent for bass).
Role roleForLane(Lane lane, Genre genre);

/// Analysis features (doc 06 §2) extracted from rendered audio.
struct Features {
    float centroidHz = 0.0f;     // spectral centroid
    float brightness = 0.0f;     // normalized centroid 0..1
    float aggression = 0.0f;     // distortion density + high-mid energy 0..1
    float darkness = 0.0f;       // inverse tilt 0..1
    float movement = 0.0f;       // 0.5–8 Hz modulation energy 0..1 (growl talk)
    float roughness = 0.0f;      // 15–150 Hz amplitude-modulation / carrier (perceived gnarl/aggression)
    float subRatio = 0.0f;       // energy share below 120 Hz 0..1
    float transient = 0.0f;      // onset strength 0..1
    float crestDb = 0.0f;        // peak/rms in dB
    float durationSec = 0.0f;
};

struct Recipe {
    Role role = Role::Growl;
    std::string name;                 // human-ish generated name ("growl_ravager_7f3a")
    uint64_t seed = 1;                // seed used by the factory that made it
    std::map<std::string, float> p;   // role-specific named parameters

    float get(const std::string& key, float fallback) const {
        auto it = p.find(key);
        return it == p.end() ? fallback : it->second;
    }
};

/// A rated, library-ready sound (recipe + measured features + fitness).
struct RatedSound {
    Recipe recipe;
    Features features;
    float score = 0.0f;       // rating pipeline output 0..1 (doc 05 §4)
    bool favorite = false;
    int uses = 0;
    std::string id;           // stable unique id (assigned by the library)
};

} // namespace rtg
