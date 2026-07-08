// Generation Orchestrator (doc 01 4.2): Plan -> Compose -> sound selection /
// synthesis -> Render -> Mix -> Master -> Library update, with progress
// reporting and cooperative cancellation. Deterministic per (params, library).
#include "rtg/generation/pipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include "rtg/library/preference_model.h"
#include "rtg/mix/mix_engine.h"
#include "rtg/synth/synth_engine.h"
#include "rtg/utils/rng.h"

namespace rtg {

namespace {

PaletteSpec paletteFor(const Plan& plan) {
    if (!plan.palettes.empty()) return plan.palettes[0];
    PaletteSpec p;
    p.aggression01 = plan.aggression01;
    p.darkness01 = plan.darkness01;
    p.novelty01 = 0.25f;
    return p;
}

// Synthesize N candidates, rate them, return the best as a RatedSound.
// When a trained preference model is supplied, blend its taste score into the
// selection metric: sel = 0.65*rate + 0.35*prefModel.score(features). The
// stored RatedSound::score stays the raw synth rating (library admission uses
// it); only the winner-selection metric is reweighted. Deterministic.
RatedSound synthesizeBest(Role role, const PaletteSpec& pal, const Plan& plan,
                          const RatedSound* seedParent, Rng& rng, double sr,
                          const PreferenceModel* prefs) {
    RatedSound best; best.score = -1.0f;
    float bestSel = -1.0f;
    // Perceptual ROUGHNESS loop (closed at synth time, where roughness is created):
    // for the aggressive bass roles, bias candidate selection toward a target
    // gnarl that scales with aggression. Because the candidates vary their
    // detune/distortion, selecting by roughness-nearness effectively tunes those
    // params to the target — the right place to close this loop (the mix can't).
    const bool roughRole = (role == Role::Growl || role == Role::Screech || role == Role::Sub);
    const float roughTarget = 0.28f + 0.30f * pal.aggression01;
    const bool dbg = std::getenv("RTG_SYNTH_DEBUG") != nullptr;
    for (int k = 0; k < 6; ++k) {
        Recipe rec;
        if (k < 2 && seedParent)
            rec = synth::mutateRecipe(seedParent->recipe, 0.3f, rng);
        else
            rec = synth::makeRecipe(role, pal.aggression01, pal.darkness01, pal.novelty01, rng);
        StereoBuffer prev = synth::renderPreview(rec, plan.rootMidi, 2.0, sr);
        Features f = synth::analyze(prev, sr);
        float rated = synth::rate(role, f);
        float sel = prefs ? (0.65f * rated + 0.35f * prefs->score(f)) : rated;
        if (roughRole && f.roughness > 0.0f)
            sel -= 0.7f * std::fabs(f.roughness - roughTarget);   // prefer target gnarl
        if (dbg) std::fprintf(stderr, "[synth] role=%d k=%d roughness=%.3f (target %.2f) sel=%.3f\n",
                              int(role), k, f.roughness, roughTarget, sel);
        if (sel > bestSel) {
            bestSel = sel;
            best = RatedSound{}; best.recipe = rec; best.features = f; best.score = rated;
        }
    }
    return best;
}

} // namespace

std::optional<GenerationResult> generateTrack(const Params& params,
                                              SoundLibrary& library,
                                              const std::atomic<bool>& cancelFlag,
                                              const ProgressFn& progress,
                                              bool ingest,
                                              std::array<StereoBuffer, kLaneCount>* stemsOut) {
    const auto t0 = std::chrono::steady_clock::now();
    const double sr = kSampleRate;
    auto report = [&](float f, const std::string& s) { if (progress) progress(f, s); };
    auto cancelled = [&] { return cancelFlag.load(); };

    // --- [1] Plan ----------------------------------------------------------
    report(0.02f, "Planning");
    if (cancelled()) return std::nullopt;
    Params p = params;
    if (p.seed == 0) p.seed = 1;
    Plan plan = makePlan(p);
    const Genre genre = plan.params.genre;

    // --- [2] Compose -------------------------------------------------------
    report(0.06f, "Composing");
    if (cancelled()) return std::nullopt;
    Score score = compose(plan);

    // --- [3] Choose sounds -------------------------------------------------
    report(0.10f, "Choosing sounds");
    const PaletteSpec pal = paletteFor(plan);

    // Optional A/B preference model. Convention: the trained weights live at
    // <library dir>/../training/preference_weights.json (the app stores the
    // library under <dataDir>/library and votes/weights under <dataDir>/training).
    // Missing or invalid weights ⇒ identical behavior to before (no blend).
    PreferenceModel prefModel;
    bool hasPrefs = false;
    {
        const std::string prefsPath =
            (std::filesystem::path(library.directory()) / ".." / "training" /
             "preference_weights.json").string();
        hasPrefs = prefModel.load(prefsPath);
    }
    const PreferenceModel* prefsPtr = hasPrefs ? &prefModel : nullptr;

    std::array<Recipe, kLaneCount> laneRecipe;
    std::array<bool, kLaneCount> laneActive{};
    std::vector<UsedSound> usedSounds;
    int newIngested = 0;
    std::vector<std::string> usedLibIds;         // library assets already chosen this track

    for (int li = 0; li < kLaneCount; ++li) {
        if (cancelled()) return std::nullopt;
        Lane lane = Lane(li);
        if (score.notes(lane).empty()) continue;

        Role role = roleForLane(lane, genre);
        Rng soundRng = Rng(p.seed).stream("sounds", li);
        std::vector<RatedSound> picks = library.pick(role, pal.aggression01, pal.darkness01, 3, soundRng);

        // Cross-lane dedup: no lane reuses a library asset already chosen this
        // track. If this empties picks, the goFresh path forces fresh synthesis.
        picks.erase(std::remove_if(picks.begin(), picks.end(),
                                   [&](const RatedSound& r) {
                                       return std::find(usedLibIds.begin(), usedLibIds.end(),
                                                        r.id) != usedLibIds.end();
                                   }),
                    picks.end());

        // Novelty floor: even at chaos 0, keep >=25% fresh-synthesis probability.
        const float effNovelty = std::max(0.25f, pal.novelty01);
        bool goFresh = picks.empty() || soundRng.chance(effNovelty);

        if (goFresh) {
            const RatedSound* parent = picks.empty() ? nullptr : &picks.front();
            RatedSound best = synthesizeBest(role, pal, plan, parent, soundRng, sr, prefsPtr);
            bool ingested = ingest ? library.maybeIngest(best) : false;
            if (ingested) ++newIngested;
            laneRecipe[li] = best.recipe;
            laneActive[li] = true;
            UsedSound us; us.lane = lane; us.name = best.recipe.name;
            us.freshlySynthesized = true; us.ingested = ingested;
            usedSounds.push_back(us);
        } else {
            const RatedSound& chosen = picks.front();
            laneRecipe[li] = chosen.recipe;
            laneActive[li] = true;
            if (ingest) library.noteUsed(chosen.id);
            usedLibIds.push_back(chosen.id);
            UsedSound us; us.lane = lane; us.libraryId = chosen.id; us.name = chosen.recipe.name;
            usedSounds.push_back(us);
        }
    }

    // --- [3b] Per-drop bass timbre --------------------------------------------
    // The tonal bass lanes (growl / horn / 3rd voice) get a DISTINCT synthesized
    // recipe per palette, so each drop sounds like its own patch instead of the
    // whole track sharing one growl. Deterministic: a fresh child rng per
    // (lane, palette). Other lanes keep their single recipe. Variants are not
    // ingested (per-track ephemeral); palette 0 reuses the already-chosen recipe.
    const int nPal = int(plan.palettes.size());
    auto isVariableLane = [](int li) {
        return li == int(Lane::BassA) || li == int(Lane::BassB) || li == int(Lane::BassC);
    };
    std::array<std::vector<Recipe>, kLaneCount> paletteRecipes;
    if (nPal > 1) {
        for (int li = 0; li < kLaneCount; ++li) {
            if (!laneActive[li] || !isVariableLane(li)) continue;
            Role role = roleForLane(Lane(li), genre);
            paletteRecipes[li].resize(nPal);
            for (int pi = 0; pi < nPal; ++pi) {
                if (pi == 0) { paletteRecipes[li][0] = laneRecipe[li]; continue; }
                Rng rng2 = Rng(p.seed).stream("sounds", li).stream("palvar", pi);
                RatedSound v = synthesizeBest(role, plan.palettes[pi], plan, nullptr,
                                              rng2, sr, prefsPtr);
                paletteRecipes[li][pi] = v.recipe;
            }
        }
    }
    // paletteIndex for an absolute beat (which section owns it).
    const double bpbBeats = plan.beatsPerBar;
    auto paletteForBeat = [&](double beat) -> int {
        for (const Section& s : plan.sections) {
            const double a = s.startBar * bpbBeats, b = (s.startBar + s.bars) * bpbBeats;
            if (beat >= a - 1e-6 && beat < b - 1e-6)
                return std::clamp(s.paletteIndex, 0, std::max(0, nPal - 1));
        }
        return 0;
    };

    // --- [4] Render (parallel over lanes) ----------------------------------
    if (cancelled()) return std::nullopt;
    std::array<StereoBuffer, kLaneCount> laneAudio;
    std::vector<int> renderLanes;
    for (int li = 0; li < kLaneCount; ++li) if (laneActive[li]) renderLanes.push_back(li);

    std::array<std::future<StereoBuffer>, kLaneCount> futures;
    for (int li : renderLanes) {
        const bool perDrop = (nPal > 1) && isVariableLane(li) && !paletteRecipes[li].empty();
        futures[li] = std::async(std::launch::async, [&, li, perDrop] {
            if (!perDrop) {
                return synth::renderLane(Lane(li), score.notes(Lane(li)), laneRecipe[li],
                                         plan, score, sr);
            }
            // Render each palette's note-subset with its own recipe; sum. Each
            // renderLane call returns a full-length buffer with notes placed at
            // their absolute positions, so summing reconstructs the whole lane.
            const auto& allNotes = score.notes(Lane(li));
            StereoBuffer acc;
            for (int pi = 0; pi < nPal; ++pi) {
                std::vector<Note> sub;
                for (const Note& nt : allNotes)
                    if (paletteForBeat(nt.startBeat) == pi) sub.push_back(nt);
                if (sub.empty()) continue;
                StereoBuffer part = synth::renderLane(Lane(li), sub, paletteRecipes[li][pi],
                                                      plan, score, sr);
                if (acc.empty()) { acc = std::move(part); continue; }
                const size_t m = std::min(acc.size(), part.size());
                for (size_t i = 0; i < m; ++i) { acc.l[i] += part.l[i]; acc.r[i] += part.r[i]; }
            }
            if (acc.empty())  // safety: no notes mapped -> fall back to one recipe
                acc = synth::renderLane(Lane(li), allNotes, laneRecipe[li], plan, score, sr);
            return acc;
        });
    }
    // Collect in lane order; progress reported only from this orchestrating thread.
    int done = 0;
    for (int li : renderLanes) {
        laneAudio[li] = futures[li].get();
        ++done;
        float frac = 0.15f + 0.55f * (float(done) / float(std::max<size_t>(1, renderLanes.size())));
        report(frac, std::string("Rendering ") + laneName(Lane(li)));
        if (cancelled()) return std::nullopt;
    }

    // Optional per-lane stems (raw, pre-mix) for analysis/inspection.
    if (stemsOut) *stemsOut = laneAudio;

    // --- [5] Mix -----------------------------------------------------------
    report(0.78f, "Mixing");
    if (cancelled()) return std::nullopt;
    StereoBuffer premaster = mixDown(laneAudio, score, plan, sr);

    // --- [6] Master --------------------------------------------------------
    report(0.90f, "Mastering");
    if (cancelled()) return std::nullopt;
    MasterStats stats;
    StereoBuffer master = masterize(premaster, plan, sr, &stats);

    // --- [7] Finalize ------------------------------------------------------
    report(0.97f, "Finalizing");
    if (cancelled()) return std::nullopt;

    const double spb = plan.secondsPerBeat();
    const double beatsPerBar = plan.beatsPerBar;

    GenerationResult result;
    result.sampleRate = sr;
    result.plan = plan;
    result.score = score;
    result.usedSounds = std::move(usedSounds);
    result.stats = stats;
    result.newSoundsIngested = newIngested;

    int endBar = 0;
    for (const Section& s : plan.sections) {
        SectionInfo si;
        si.type = s.type;
        si.startSec = s.startBar * beatsPerBar * spb;
        si.lengthSec = s.bars * beatsPerBar * spb;
        si.energy = s.energy;
        result.sections.push_back(si);
        endBar = std::max(endBar, s.startBar + s.bars);
    }

    // Trim / pad to exact section end + 1.5 s tail.
    double endSec = (endBar > 0 ? endBar : plan.totalBars) * beatsPerBar * spb + 1.5;
    size_t target = size_t(endSec * sr);
    if (target == 0) target = master.size();
    StereoBuffer fitted(target);
    size_t m = std::min(target, master.size());
    for (size_t i = 0; i < m; ++i) { fitted.l[i] = master.l[i]; fitted.r[i] = master.r[i]; }
    result.master = std::move(fitted);

    const auto t1 = std::chrono::steady_clock::now();
    result.renderSeconds = std::chrono::duration<double>(t1 - t0).count();

    report(1.0f, "Done");
    return result;
}

} // namespace rtg
