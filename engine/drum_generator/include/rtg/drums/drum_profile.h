#pragma once
// Drum Reference Profile (analysis + steering). Mirrors rtg::Calibration in
// spirit: a small, dependency-free bundle of MEASURED drum characteristics that
// the drum synthesis + hat composition target instead of hand-tuned defaults.
//
// Where Calibration captures long-term full-mix spectral/loudness numbers, a
// DrumProfile captures per-instrument transient character (kick body/decay/
// click, snare body/crack/tonality, hat density/decay/brightness) extracted
// from commercial riddim references. The engine ships with a baked-in profile
// (builtinReference) so it renders toward the references OUT OF THE BOX; a
// freshly extracted profile can be made active to override it.
//
// Everything here is JUCE-free and JSON-library-free: a hand-rolled tolerant
// flat-object parser keeps rtg_core self-contained (same approach as
// calibration.cpp). All measured fields are floats; refCount is the number of
// reference files folded into this profile.
//
// Registry contract: setActive() before generation; active() read-only during.
// active() ALWAYS returns a usable profile (defaults to builtinReference()).
#include <optional>
#include <string>
#include "rtg/utils/audio.h"   // StereoBuffer

namespace rtg {

struct DrumProfile {
    // ---- KICK ----------------------------------------------------------------
    float kickBodyHz     = 55.0f;   // fundamental the body settles to (47..63)
    float kickDecayMs    = 90.0f;   // low-band body decay time, biased short (40..180)
    float kickClickShare = 0.055f;  // 2..8 kHz energy fraction of the whole hit (0.04..0.07)
    float kickSubShare   = 0.71f;   // 30..120 Hz energy fraction of the whole hit (0.65..0.77)
    // ---- SNARE ---------------------------------------------------------------
    float snareBodyHz      = 145.0f; // tuned body fundamental (140..150)
    float snareCrackShare  = 0.05f;  // 2..5 kHz crack energy fraction (0.03..0.07)
    float snareCrackDecayMs = 110.0f;// 2..5 kHz band decay time (56..193)
    float snareTonality    = 0.25f;  // spectral flatness of 1..8 kHz (0.19..0.32; lower = tonal)
    // ---- HATS ----------------------------------------------------------------
    float hatDensityPerBeat = 2.8f;  // hat events per beat in drops (2.4..3.2)
    float hatDecayMs        = 78.0f; // hat-band decay time (37..120)
    float hatCentroidHz     = 10800.0f;// hat-band spectral centroid (crisp top)
    // ---- RHYTHMIC ACCENT MAPS ("how they're used") ---------------------------
    // 16-step (one bar) relative accent maps extracted from a reference. The
    // composer biases its chug/hat hit probabilities toward these so generations
    // follow the reference's actual rhythm. Sentinel [0] < 0 => UNSET (the engine
    // uses its own probabilistic pattern, and the default render is unchanged).
    float growlPattern[16] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    float hatPattern[16]   = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};

    int refCount = 0;                // number of reference files aggregated

    // Baked medians measured from the two commercial riddim references. This is
    // the DEFAULT active profile, so the stock engine renders toward the refs.
    static const DrumProfile& builtinReference();

    // Process-wide active profile. Set before generation, read during. active()
    // never returns null: falls back to builtinReference() when nothing is set.
    static void setActive(std::optional<DrumProfile> p);
    static const DrumProfile& active();

    // Hand-rolled minimal flat-JSON I/O. Tolerant: unknown keys ignored, missing
    // keys keep defaults. Returns false only on I/O failure.
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;
};

// Extract a DrumProfile from a rendered/commercial track. Analyzes the loudest
// ~60 s window. Onset detection uses band energy envelopes (low 45..100 Hz,
// crack 2..5 kHz, hat 6.5..14 kHz) plus a broadband 2..8 kHz transient band.
//
// KICK detection is transient-gated (the extraction lesson): a low-band onset
// counts as a kick ONLY when a coincident 2..8 kHz transient (broadband flux
// spike) lands within ~10 ms — naive low-band onset detection otherwise tracks
// the sustained sub-bass, yielding ~1 detection per beat instead of ~1 per bar.
//
// Side-channel diagnostics (not part of the stored profile): event counts and
// tempo for the analyzed window. The kick count proving the transient gate
// works should land near winBeats/4 (≈ one kick per bar), NOT winBeats (one per
// beat, which is what naive low-band onset detection yields).
struct DrumProfileStats {
    int    kickCount  = 0;
    int    snareCount = 0;
    int    hatCount   = 0;
    double bpm        = 0.0;
    double windowSec  = 0.0;
    double windowBeats = 0.0;
};

// Deterministic; bpmHint (>0) skips the onset-autocorrelation tempo estimate.
// The returned profile has refCount = 1. Pass a non-null `stats` to receive the
// side-channel diagnostics above.
DrumProfile extractDrumProfile(const StereoBuffer& audio, double sr,
                               double bpmHint = 0.0, DrumProfileStats* stats = nullptr);

} // namespace rtg
