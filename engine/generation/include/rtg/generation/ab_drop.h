#pragma once
// A/B full-drop rendering for user-guided design feedback. Renders two
// standalone ~20 s DROP clips (all lanes together, in context) that the app
// auditions and the user votes on; each vote is measured and appended to the
// global A/B log (library/ab_log.h) for later "overhaul from the last N tests"
// analysis. Deterministic per (params, seed, library snapshot). JUCE-free.
#include <cstdint>
#include "rtg/generation/pipeline.h"
#include "rtg/library/ab_log.h"
#include "rtg/utils/audio.h"

namespace rtg {

// One rendered drop clip plus its measured character and design vector.
struct DropClip {
    StereoBuffer audio;                  // the ~N-second drop clip (48 kHz)
    double sampleRate = kSampleRate;
    uint64_t seed = 0;
    int genre = 0;                       // 0 riddim, 1 trap
    float params[AB_ParamCount] = {0};   // energy..bpm design vector
    ABFeat feat;                         // measured character
    bool ok = false;                     // false => render failed / empty
};

// Measure a rendered clip into the compact ABFeat (calibration measures +
// growl fingerprint), so A/B records speak the same language as calibration.
ABFeat measureABFeat(const StereoBuffer& audio, double sampleRate);

// Render a standalone ~`seconds`-second DROP for A/B: run the pipeline for
// `params` at `seed`, slice the first Drop section, measure it. Deterministic
// per (params, seed, library snapshot). Ingests synthesized sounds into
// `library` exactly like a normal generation.
DropClip renderDropClip(const Params& params, SoundLibrary& library,
                        double seconds, uint64_t seed);

// Assemble an ABResult from two clips + winner (0=A, 1=B), stamped with `ts`
// (unix seconds; caller supplies it so this stays deterministic/testable).
ABResult makeABResult(const DropClip& a, const DropClip& b, int winner, long long ts);

} // namespace rtg
