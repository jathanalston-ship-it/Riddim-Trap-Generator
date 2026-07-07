// Background library evolution (doc 06 §4-§6). See evolution.h for the design
// and the determinism guarantee. This file owns the cycle logic; it drives the
// library only through its existing public API (pick is bypassed — evolution
// does its own fitness-weighted parent selection so it can breed pairs).
#include "rtg/library/evolution.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "rtg/synth/synth_engine.h"
#include "rtg/utils/audio.h"     // kSampleRate
#include "rtg/utils/rng.h"

namespace rtg {

namespace {

// Roles the generator actually uses (task spec / roleForLane coverage). Evolution
// only invests CPU in palettes that generations draw from — Downlifter and Crash
// are excluded on purpose.
constexpr Role kEvolvableRoles[] = {
    Role::Growl, Role::Screech, Role::Sub, Role::Bass808,
    Role::Kick, Role::Snare, Role::HatClosed, Role::HatOpen, Role::Perc,
    Role::MelodyLead, Role::Pad, Role::Riser, Role::Impact,
};

// Fitness used for parent selection: proven quality + a favorite immortality
// bonus (doc 06 §3, simplified). Kept strictly positive so it can weight a
// softmax-style pick.
double fitnessOf(const RatedSound& s) {
    return double(s.score) + (s.favorite ? 0.15 : 0.0);
}

// Fitness-proportional parent pick over an id-sorted candidate list. `pool`
// holds indices into `sorted`; picks without replacement are handled by the
// caller (it may pass the same pool twice for breeding — duplicates allowed
// only when the pool has a single member).
int pickParent(const std::vector<RatedSound>& sorted, const std::vector<int>& pool,
               Rng& rng) {
    std::vector<double> w(pool.size());
    for (size_t i = 0; i < pool.size(); ++i)
        w[i] = std::exp(fitnessOf(sorted[pool[i]]));   // >0, favors winners
    int k = rng.pickWeighted(w, 0.8);                  // temperature 0.8
    if (k < 0 || k >= (int)pool.size()) k = 0;
    return pool[k];
}

// Parameter-interpolation crossover (doc 06 §4 "Breed"): child parameters are a
// per-parameter lerp of the two parents plus a small Gaussian jitter, so the
// child sits between its parents but is not a pure average of either. Parent A
// supplies the full parameter set (both share a role, hence a shape), keeping
// the child renderable even if B is missing a key.
Recipe breed(const Recipe& a, const Recipe& b, Rng& rng) {
    Recipe child = a;
    child.name = a.name + "_x";
    child.seed = rng.next();
    for (auto& kv : child.p) {
        auto itb = b.p.find(kv.first);
        if (itb == b.p.end()) continue;                // keep A's value
        float u = rng.rangef(0.0f, 1.0f);
        float lerped = a.get(kv.first, kv.second) * (1.0f - u) + itb->second * u;
        float jitter = rng.gaussian() * 0.03f * std::max(std::fabs(lerped), 0.02f);
        kv.second = lerped + jitter;
    }
    return child;
}

} // namespace

EvolutionEngine::CycleReport EvolutionEngine::runCycle(uint64_t cycleSeed) {
    CycleReport report;
    Rng rng(cycleSeed ? cycleSeed : 1);

    // --- [0] Canonical snapshot -------------------------------------------
    // Sort by id so every rng-driven decision is independent of directory
    // iteration order (the determinism guarantee, doc 06 §8).
    std::vector<RatedSound> snap = library_.all();
    std::sort(snap.begin(), snap.end(),
              [](const RatedSound& x, const RatedSound& y) { return x.id < y.id; });

    // --- [1] Pick the weakest-coverage role -------------------------------
    // coverage = fill (count / cap) + mean score. Low coverage == few assets
    // and/or low mean quality. Argmin, ties broken by role order for
    // determinism. As evolution admits sounds to the weakest role its fill
    // rises, so focus rotates across cycles.
    int roleCount[kRoleCount] = {0};
    double roleScoreSum[kRoleCount] = {0.0};
    for (const auto& s : snap) {
        int ri = int(s.recipe.role);
        if (ri >= 0 && ri < kRoleCount) { ++roleCount[ri]; roleScoreSum[ri] += s.score; }
    }
    Role focus = kEvolvableRoles[0];
    double worst = 1e30;
    for (Role r : kEvolvableRoles) {
        int ri = int(r);
        double fill = double(roleCount[ri]) / double(SoundLibrary::kPerRoleCap);
        double mean = roleCount[ri] > 0 ? roleScoreSum[ri] / roleCount[ri] : 0.0;
        double coverage = fill + mean;
        if (coverage < worst) { worst = coverage; focus = r; }
    }
    report.focusedRole = focus;

    // Parents = focus-role assets from the canonical snapshot.
    std::vector<int> parents;
    for (int i = 0; i < (int)snap.size(); ++i)
        if (snap[i].recipe.role == focus) parents.push_back(i);
    const bool haveParent = !parents.empty();
    const bool havePair = parents.size() >= 2;

    // Palette character to seed fresh/immigration recipes: the focus role's own
    // mean feature window (falls back to neutral 0.5 when the role is empty), so
    // immigration explores near the role's established character.
    float palAgg = 0.5f, palDark = 0.5f;
    if (haveParent) {
        double sa = 0.0, sd = 0.0;
        for (int idx : parents) { sa += snap[idx].features.aggression; sd += snap[idx].features.darkness; }
        palAgg = float(sa / parents.size());
        palDark = float(sd / parents.size());
    }

    // --- [2] Spawn a batch of candidates ----------------------------------
    // Per-candidate operator draw: 60% mutate, 25% breed, 15% fresh immigration
    // (doc 06 §5), degrading gracefully to fresh factory recipes when the role
    // lacks the parents an operator needs.
    std::vector<Recipe> batch;
    batch.reserve(kBatchSize);
    for (int k = 0; k < kBatchSize; ++k) {
        Rng cand = rng.stream("cand", k);
        double roll = cand.uniform();
        if (roll < 0.60 && haveParent) {
            int pi = pickParent(snap, parents, cand);
            float amount = cand.rangef(0.15f, 0.40f);
            batch.push_back(synth::mutateRecipe(snap[pi].recipe, amount, cand));
        } else if (roll < 0.85 && havePair) {
            int ai = pickParent(snap, parents, cand);
            int bi = pickParent(snap, parents, cand);
            if (bi == ai) bi = parents[(std::find(parents.begin(), parents.end(), ai)
                                        - parents.begin() + 1) % parents.size()];
            batch.push_back(breed(snap[ai].recipe, snap[bi].recipe, cand));
        } else {
            batch.push_back(synth::makeRecipe(focus, palAgg, palDark, /*novelty*/ 0.5f, cand));
        }
    }

    // --- [3] Render → analyze → rate → admit ------------------------------
    report.candidates = (int)batch.size();
    for (const Recipe& rec : batch) {
        StereoBuffer prev = synth::renderPreview(rec, kRootMidi, kPreviewSeconds, kSampleRate);
        RatedSound rs;
        rs.recipe = rec;
        rs.features = synth::analyze(prev, kSampleRate);
        rs.score = synth::rate(focus, rs.features);
        if (library_.maybeIngest(rs)) ++report.admitted;
    }

    // --- [4] Prune pass ----------------------------------------------------
    // Once the focus role is past the fill fraction, archive its single weakest
    // dead-weight asset: non-favorite, never used, and older than this batch
    // (id was present in the pre-batch snapshot — we never prune what we just
    // admitted). remove() enforces the favorite-refusal invariant as a backstop.
    if (library_.countForRole(focus) >
        int(kPruneFillFraction * SoundLibrary::kPerRoleCap)) {
        int victim = -1;
        for (int i = 0; i < (int)snap.size(); ++i) {
            const RatedSound& s = snap[i];
            if (s.recipe.role != focus) continue;
            if (s.favorite || s.uses != 0) continue;
            if (victim < 0 || s.score < snap[victim].score ||
                (s.score == snap[victim].score && s.id < snap[victim].id))
                victim = i;
        }
        if (victim >= 0 && library_.remove(snap[victim].id)) ++report.pruned;
    }

    return report;
}

} // namespace rtg
