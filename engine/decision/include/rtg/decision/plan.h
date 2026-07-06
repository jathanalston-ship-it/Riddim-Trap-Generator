#pragma once
// GenerationPlan: everything the Decision Engine chose (doc 10 §2).
// Downstream engines execute this plan and make no taste decisions.
#include <vector>
#include "rtg/decision/params.h"

namespace rtg {

enum class SectionType : int { Intro = 0, Build, Drop, Break, Outro };

inline const char* sectionName(SectionType t) {
    switch (t) {
        case SectionType::Intro: return "Intro";
        case SectionType::Build: return "Build";
        case SectionType::Drop:  return "Drop";
        case SectionType::Break: return "Break";
        case SectionType::Outro: return "Outro";
    }
    return "?";
}

struct Section {
    SectionType type = SectionType::Intro;
    int startBar = 0;
    int bars = 8;
    float energy = 0.5f;      // target energy 0..1 for this section
    int paletteIndex = 0;     // which drop palette (bass voice set) is active
    bool switchAt8 = false;   // Drop only: mid-drop palette/pattern switch
    bool fakeout = false;     // Build only: resolves to a fakeout bar
};

// Per-drop sound palette constraints (selection window for the library /
// synthesis factories).
struct PaletteSpec {
    float aggression01 = 0.7f;
    float darkness01 = 0.5f;
    float novelty01 = 0.25f;  // fraction of voices that must be freshly synthesized
    int bassVoices = 2;       // 2..3 mid-bass voices (BassA/B/C lanes)
};

struct Plan {
    Params params;
    // Global musical choices:
    int rootMidi = 29;            // e.g. 29 = F1 (sub fundamental ~43.65 Hz)
    int scale[7] = {0, 2, 3, 5, 7, 8, 10}; // semitone offsets (natural minor default)
    double bpm = 145.0;
    int totalBars = 0;
    double beatsPerBar = 4.0;
    // Structure:
    std::vector<Section> sections;
    std::vector<PaletteSpec> palettes;    // indexed by Section::paletteIndex
    // Normalized character (post-interpretation, doc 10 §3):
    float energy01 = 0.65f, aggression01 = 0.7f, darkness01 = 0.55f;
    float complexity01 = 0.5f, melody01 = 0.3f, chaos01 = 0.25f;
    // Mix/master intents:
    float masterTargetLufs = -8.5f;   // integrated
    float sidechainDepth = 0.6f;      // 0..1 kick→bass duck depth
    float mixAggression = 0.7f;       // drives OTT/saturation amounts

    double secondsPerBeat() const { return 60.0 / bpm; }
    double totalBeats() const { return totalBars * beatsPerBar; }
    double totalSeconds() const { return totalBeats() * secondsPerBeat(); }
};

/// Decision Engine entry point (doc 10). Deterministic for a given Params
/// (including Params::seed). Implemented in engine/decision/src/.
Plan makePlan(const Params& params);

} // namespace rtg
