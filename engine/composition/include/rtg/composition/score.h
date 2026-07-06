#pragma once
// Symbolic score: the Composition Engine's output (doc 04). All timing in
// beats (4 beats per bar). The renderer/mix consume this + resolved sounds.
#include <array>
#include <vector>
#include "rtg/decision/plan.h"

namespace rtg {

enum class Lane : int {
    Sub = 0, BassA, BassB, BassC,
    Kick, Snare, HatClosed, HatOpen, Perc,
    Melody, Pad,
    Riser, Downlifter, Impact, Crash,
    Count
};
constexpr int kLaneCount = int(Lane::Count);

inline const char* laneName(Lane l) {
    static const char* names[] = { "Sub", "BassA", "BassB", "BassC", "Kick", "Snare",
        "HatClosed", "HatOpen", "Perc", "Melody", "Pad", "Riser", "Downlifter",
        "Impact", "Crash" };
    return names[int(l)];
}

struct Note {
    double startBeat = 0.0;
    double lengthBeats = 0.25;
    int midi = 29;            // pitch (ignored by unpitched drum lanes)
    float velocity = 1.0f;    // 0..1
    float mod = 0.0f;         // articulation macro 0..1 (growl "talk", hat tone…)
    float bendSemis = 0.0f;   // pitch glide over the note (808 slides, dives)
};

/// Per-beat automation curve; linear interpolation between beat samples.
struct Curve {
    std::vector<float> perBeat;   // one value per beat, size == totalBeats+1
    float sample(double beat) const {
        if (perBeat.empty()) return 0.0f;
        if (beat <= 0) return perBeat.front();
        const double last = double(perBeat.size() - 1);
        if (beat >= last) return perBeat.back();
        const int i = int(beat);
        const float f = float(beat - i);
        return perBeat[i] + (perBeat[i + 1] - perBeat[i]) * f;
    }
};

struct Score {
    std::array<std::vector<Note>, kLaneCount> lanes;
    double totalBeats = 0.0;

    // Global automation (doc 04 §7), all 0..1:
    Curve buildFilter;   // riser/filter-sweep intensity (peaks at drop entries)
    Curve breakSoften;   // "underwater" high-cut amount in breaks/intro/outro
    Curve fxSend;        // reverb/space send amount per beat
    Curve energy;        // realized energy curve (UI display + mix scaling)

    std::vector<Note>&       notes(Lane l)       { return lanes[int(l)]; }
    const std::vector<Note>& notes(Lane l) const { return lanes[int(l)]; }
};

/// Composition Engine entry point (doc 04). Deterministic for a given Plan.
/// Implemented in engine/composition/src/.
Score compose(const Plan& plan);

} // namespace rtg
