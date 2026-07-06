#pragma once
// Sound Design Engine (doc 05): recipe factories, rendering, analysis, rating.
// Pure C++/float DSP, deterministic per (recipe, note data). Implemented in
// engine/sound_design/src/ (+ bass_designer/ and drum_generator/ sources).
#include <vector>
#include "rtg/synth/recipe.h"
#include "rtg/utils/audio.h"
#include "rtg/utils/rng.h"

namespace rtg::synth {

/// Factory: create a fresh recipe for a role, honoring the palette character.
/// novelty01 widens the parameter space (Chaos). Deterministic per rng state.
Recipe makeRecipe(Role role, float aggression01, float darkness01,
                  float novelty01, Rng& rng);

/// Mutate an existing recipe (library evolution, doc 06 §4). amount 0..1.
Recipe mutateRecipe(const Recipe& parent, float amount, Rng& rng);

/// Render a standalone one-shot preview of a recipe (for rating & audition).
/// rootMidi is the pitch for pitched roles; drums ignore it.
StereoBuffer renderPreview(const Recipe& recipe, int rootMidi, double seconds,
                           double sampleRate);

/// Render a full-length lane: every note of `notes` synthesized with `recipe`
/// into a buffer covering the whole score (score.totalBeats at plan.bpm).
/// Applies note velocity, mod (articulation macro), and bendSemis.
StereoBuffer renderLane(Lane lane, const std::vector<Note>& notes,
                        const Recipe& recipe, const Plan& plan,
                        const Score& score, double sampleRate);

/// Feature extraction (shared language of rating, metadata, selection).
Features analyze(const StereoBuffer& audio, double sampleRate);

/// Role-aware heuristic quality score 0..1 (doc 05 §4): hard gates
/// (silence, clipping, DC) return 0; otherwise fitness per role targets.
float rate(Role role, const Features& f);

} // namespace rtg::synth
