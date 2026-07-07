#pragma once
// Background library evolution (doc 06 §4-§6): idle-time cycles that mutate,
// breed, and prune the sound palette so it improves in quality even when the
// user isn't generating. One EvolutionEngine::runCycle() is a single bounded,
// synchronous cycle (~2-4 s of CPU): it picks the weakest-covered role, spawns
// a small batch (mutations of fitness-weighted parents, parameter-interpolation
// breeds, and fresh-factory immigration), renders → analyzes → rates → admits
// each candidate through the standard library gate, then runs one prune pass.
//
// Determinism (product guarantee, doc 06 §8): a cycle is a pure function of the
// library contents + the cycle seed. All randomness flows from a single seeded
// rtg::Rng; parent selection and prune targeting operate on an id-sorted
// snapshot so directory iteration order never leaks into decisions.
#include <cstdint>
#include "rtg/library/sound_library.h"
#include "rtg/synth/recipe.h"

namespace rtg {

class EvolutionEngine {
public:
    explicit EvolutionEngine(SoundLibrary& library) : library_(library) {}

    struct CycleReport {
        int candidates = 0;              // sounds spawned & rated this cycle
        int admitted = 0;                // passed the library admission gate
        int pruned = 0;                  // archived by the prune pass
        Role focusedRole = Role::Growl;  // weakest-coverage role this cycle
    };

    /// Run ONE bounded evolution cycle synchronously. Deterministic per
    /// (library contents, cycleSeed).
    CycleReport runCycle(uint64_t cycleSeed);

    // Batch size per cycle (doc 06 §5: ~6 candidates, bounded CPU budget).
    static constexpr int kBatchSize = 6;
    // Prune trigger: only prune a role once it exceeds this fraction of its cap.
    static constexpr float kPruneFillFraction = 0.80f;
    // Preview length rendered for rating each candidate (seconds).
    static constexpr double kPreviewSeconds = 2.0;
    // Fixed audition pitch for pitched roles (drums ignore it); a constant keeps
    // rating — and therefore admission — deterministic across cycles.
    static constexpr int kRootMidi = 41;

private:
    SoundLibrary& library_;
};

} // namespace rtg
