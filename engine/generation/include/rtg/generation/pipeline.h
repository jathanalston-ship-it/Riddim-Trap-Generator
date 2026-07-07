#pragma once
// Generation Orchestrator (doc 01 §4.2): runs the full staged pipeline
// Plan → Compose → Sound selection/synthesis → Render → Mix → Master →
// Library update, with progress reporting and cooperative cancellation.
// Implemented in engine/generation/src/.
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "rtg/composition/score.h"
#include "rtg/decision/plan.h"
#include "rtg/library/sound_library.h"
#include "rtg/master/master_engine.h"

namespace rtg {

struct SectionInfo {
    SectionType type;
    double startSec, lengthSec;
    float energy;
};

struct UsedSound {
    Lane lane;
    std::string libraryId;   // empty if freshly synthesized this run
    std::string name;
    bool freshlySynthesized = false;
    bool ingested = false;   // fresh sound admitted to the library
};

struct GenerationResult {
    StereoBuffer master;
    double sampleRate = kSampleRate;
    Plan plan;
    Score score;
    std::vector<SectionInfo> sections;
    std::vector<UsedSound> usedSounds;
    MasterStats stats;
    int newSoundsIngested = 0;
    double renderSeconds = 0.0;   // wall-clock cost (Statistics page)
};

/// progress01 in [0,1]; stage is a short human label ("Composing", "Mixing"…).
using ProgressFn = std::function<void(float progress01, const std::string& stage)>;

/// Runs the whole pipeline. Deterministic per (params incl. seed, library
/// contents). Returns nullopt only if cancelled. Synthesized sounds that
/// rate above the admission threshold are ingested into `library` as a side
/// effect (doc 05 §4). Safe to call from a worker thread.
/// `ingest` (default true): admit synthesized sounds into `library` and update
/// usage counts. Pass false for read-only renders (e.g. A/B previews) so the
/// library snapshot is unchanged and the render is exactly reproducible.
std::optional<GenerationResult> generateTrack(const Params& params,
                                              SoundLibrary& library,
                                              const std::atomic<bool>& cancelFlag,
                                              const ProgressFn& progress,
                                              bool ingest = true);

} // namespace rtg
