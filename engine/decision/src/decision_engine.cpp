// Decision Engine — deterministic "producer brain".
// Implements rtg::makePlan (doc 10). Pure function of Params (incl. seed).
// All randomness flows from Rng(params.seed).stream("decision"); no globals,
// no time(), no rand().
#include "rtg/decision/plan.h"
#include "rtg/decision/calibration.h"
#include "rtg/utils/rng.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rtg {
namespace {

inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
inline float norm01(int knob) { return clamp01(float(knob) / 100.0f); }

// Number of whole bars whose duration best matches a target seconds value.
int barsForSeconds(double lengthSec, double bpm) {
    const double secPerBar = (60.0 / bpm) * 4.0;
    int bars = int(std::llround(lengthSec / secPerBar));
    return std::max(bars, 16);
}

} // namespace

Plan makePlan(const Params& params) {
    Plan plan;
    plan.params = params;

    Rng rng = Rng(params.seed).stream("decision");

    // ---- Layer 1: normalize the 0..100 character knobs into *01 floats ----
    plan.energy01     = norm01(params.energy);
    plan.aggression01 = norm01(params.aggression);
    plan.darkness01   = norm01(params.darkness);
    plan.complexity01 = norm01(params.complexity);
    plan.melody01     = norm01(params.melody);
    plan.chaos01      = norm01(params.chaos);

    const bool riddim = (params.genre == Genre::Riddim);

    // ---- BPM clamp per genre ----
    double bpm = params.bpm;
    bpm = riddim ? std::clamp(bpm, 140.0, 150.0)
                 : std::clamp(bpm, 130.0, 170.0);
    plan.bpm = bpm;
    plan.beatsPerBar = 4.0;

    // ---- Root note (D1..A1 = 26..33), weighted toward F1 (29). Seed-driven so
    // two tracks rarely share a key; darkness nudges the center down. ----
    {
        static const int roots[] = {26, 27, 28, 29, 30, 31, 32, 33};
        const float center = 29.0f - plan.darkness01 * 2.0f;   // F1, pulled down when dark
        std::vector<double> rw;
        rw.reserve(8);
        for (int m : roots) {
            const float z = (float(m) - center) / 1.9f;
            rw.push_back(std::exp(-0.5 * double(z * z)));       // triangular-ish bell
        }
        plan.rootMidi = roots[rng.pickWeighted(rw, 1.0)];
    }

    // ---- Scale: natural minor 55% / phrygian 30% / harmonic minor 15%,
    // darkness biases toward phrygian's flat-2 color. Seed-driven. ----
    {
        double pMinor = 0.55, pPhry = 0.30, pHarm = 0.15;
        const double shift = 0.30 * double(plan.darkness01 - 0.5f); // ±0.15
        pPhry  = std::max(0.05, pPhry + shift);
        pMinor = std::max(0.05, pMinor - shift);
        const int sc = rng.pickWeighted({pMinor, pPhry, pHarm}, 1.0);
        static const int minor[7]    = {0, 2, 3, 5, 7, 8, 10};
        static const int phrygian[7] = {0, 1, 3, 5, 7, 8, 10};
        static const int harmonic[7] = {0, 2, 3, 5, 7, 8, 11};
        const int* chosen = (sc == 0) ? minor : (sc == 1) ? phrygian : harmonic;
        for (int i = 0; i < 7; ++i) plan.scale[i] = chosen[i];
    }

    // ---- Structure planning -------------------------------------------------
    // Section lengths are drawn from style/energy-weighted menus (seed-driven),
    // then fit toward the target length by a randomized hill-climb so different
    // seeds distribute the "filler" bars across different sections — genuinely
    // different structures per seed rather than one canonical layout.
    int dropCount = std::clamp(params.dropCount, 1, 4);
    const int targetBars = barsForSeconds(params.lengthSec, bpm);
    // Reconcile drop count with the requested length: each dropset needs at
    // least build(4) + drop(16) (+break(8) between), plus intro/outro(4+4).
    // A 45 s request with 4 drops would otherwise silently render ~3 minutes.
    while (dropCount > 1) {
        const int floorBars = 8 + dropCount * 20 + (dropCount - 1) * 8;
        if (targetBars >= int(floorBars * 0.85f)) break;
        --dropCount;
    }

    auto pickMenu = [&](std::initializer_list<int> opts,
                        std::initializer_list<double> w) -> int {
        std::vector<int> o(opts);
        std::vector<double> ww(w);
        return o[rng.pickWeighted(ww, 1.0)];
    };

    // Intro length menu, weighted by intro style.
    int introBars;
    switch (params.introStyle) {
        case IntroStyle::Impact:      introBars = pickMenu({4,8,12,16},   {5.0,3.0,1.0,0.5}); break;
        case IntroStyle::Minimal:     introBars = pickMenu({4,8,12,16},   {2.0,4.0,2.0,1.0}); break;
        case IntroStyle::VocalChop:   introBars = pickMenu({4,8,12,16},   {1.0,4.0,3.0,1.0}); break;
        case IntroStyle::Fakeout:     introBars = pickMenu({4,8,12,16},   {0.5,2.0,3.0,3.0}); break;
        default: /* Atmospheric */    introBars = pickMenu({4,8,12,16},   {0.5,2.0,3.0,4.0}); break;
    }

    // One build length for the track (short builds when energy is high).
    int buildBars = pickMenu({4,8}, {1.0 + 1.4 * double(plan.energy01), 2.0});
    // Outro length menu.
    int outroBars = pickMenu({4,8,12,16}, {2.0, 3.0, 2.0, 1.5});

    // Per-drop base length: 16/24/32, energy + later-position favor the longer,
    // "extended switch" 24 gets a mid-drop switch (see switchAt8 below).
    std::vector<int> dropBars(dropCount);
    for (int d = 0; d < dropCount; ++d) {
        const double pos = (dropCount > 1) ? double(d) / double(dropCount - 1) : 1.0;
        const double e   = double(plan.energy01);
        dropBars[d] = pickMenu({16,24,32},
            {1.4 - 0.5 * e,               // 16
             1.0 + 0.3 * pos,             // 24
             0.8 + 0.8 * e + 0.5 * pos}); // 32
    }
    // Per-break base length: 8/12/16.
    std::vector<int> breakBars(std::max(0, dropCount - 1));
    for (int& b : breakBars) b = pickMenu({8,12,16}, {2.0, 1.4, 1.0});

    // ---- Structure variants (categorical, seed-driven) ---------------------
    // (1) Extended outro-drop: the final drop gets +8 bars ~25% of the time.
    const bool extOutroDrop = rng.chance(0.25);
    if (extOutroDrop && !dropBars.empty()) dropBars.back() += 8;

    // (2) Reset between dropsets ~30%: insert a second Break OR a mini-intro
    //     "reset" at one of the mid-song break boundaries.
    enum class ResetKind { None, ExtraBreak, MiniIntro };
    ResetKind resetKind = ResetKind::None;
    int resetBars = 0, resetAtBreak = -1;
    if (dropCount >= 2 && rng.chance(0.30)) {
        resetAtBreak = rng.intRange(0, dropCount - 2);
        resetKind    = rng.chance(0.5) ? ResetKind::ExtraBreak : ResetKind::MiniIntro;
        resetBars    = rng.chance(0.5) ? 4 : 8;
    }

    // Total-bars accounting (includes the optional reset insert).
    auto totalBars = [&]() {
        int t = introBars + outroBars + resetBars;
        for (int b : dropBars)  t += buildBars + b;
        for (int b : breakBars) t += b;
        return t;
    };

    // ---- Randomized hill-climb fit toward target ---------------------------
    // Each pass gathers legal ±step moves (respecting per-menu caps/floors),
    // visits them in a seed-shuffled order, and keeps the first move that
    // strictly reduces |target - total|. Different seeds therefore park filler
    // in different sections while all still land near the target length.
    {
        struct Move { int* field; int delta; };
        auto climb = [&]() {
            for (int pass = 0; pass < 128; ++pass) {
                std::vector<Move> moves;
                auto addGrow = [&](int& f, int step, int cap) { if (f + step <= cap) moves.push_back({&f, step}); };
                auto addShrink = [&](int& f, int step, int floor) { if (f - step >= floor) moves.push_back({&f, -step}); };
                for (int& d : dropBars)  { addGrow(d, 8, 40); addShrink(d, 8, 16); }
                for (int& b : breakBars) { addGrow(b, 4, 16); addShrink(b, 4, 8); }
                addGrow(introBars, 4, 16); addShrink(introBars, 4, 4);
                addGrow(outroBars, 4, 16); addShrink(outroBars, 4, 4);
                addGrow(buildBars, 4, 8);  addShrink(buildBars, 4, 4);
                // Fisher-Yates shuffle (seeded) for diverse improvement paths.
                for (int i = int(moves.size()) - 1; i > 0; --i)
                    std::swap(moves[i], moves[rng.intRange(0, i)]);
                bool improved = false;
                for (const Move& mv : moves) {
                    const int before = std::abs(targetBars - totalBars());
                    *mv.field += mv.delta;
                    const int after = std::abs(targetBars - totalBars());
                    if (after < before) { improved = true; break; }
                    *mv.field -= mv.delta;   // revert
                }
                if (!improved) break;
            }
        };
        climb();
    }

    // ---- Palettes: one per drop (widened character spread) ------------------
    plan.palettes.clear();
    for (int d = 0; d < dropCount; ++d) {
        PaletteSpec ps;
        ps.aggression01 = clamp01(plan.aggression01 + rng.rangef(-0.18f, 0.18f));
        ps.darkness01   = clamp01(plan.darkness01 + rng.rangef(-0.18f, 0.18f));
        ps.novelty01    = clamp01(0.15f + 0.6f * plan.chaos01 + rng.rangef(-0.08f, 0.08f));
        ps.bassVoices   = (plan.complexity01 + rng.rangef(-0.1f, 0.1f) > 0.6f) ? 3 : 2;
        plan.palettes.push_back(ps);
    }

    // ---- Emit contiguous sections ------------------------------------------
    plan.sections.clear();
    int bar = 0;
    auto push = [&](SectionType t, int bars, int pal, float energy) {
        Section s;
        s.type = t;
        s.startBar = bar;
        s.bars = bars;
        s.paletteIndex = pal;
        s.energy = energy;
        bar += bars;
        plan.sections.push_back(s);
    };

    const float denom = float(std::max(1, dropCount - 1));

    push(SectionType::Intro, introBars, 0, 0.35f);
    for (int d = 0; d < dropCount; ++d) {
        // Build energy ramps with position; drop plateau jittered per drop.
        float buildE = clamp01(0.58f + 0.22f * (float(d) / denom) + rng.rangef(-0.05f, 0.05f));
        float dropE  = clamp01(0.85f + 0.15f * rng.rangef(0.0f, 1.0f) + 0.04f * (float(d) / denom));
        dropE = std::min(1.0f, dropE);
        push(SectionType::Build, buildBars, d, buildE);
        push(SectionType::Drop, dropBars[d], d, dropE);
        if (d < dropCount - 1) {
            float breakE = rng.rangef(0.25f, 0.50f);
            push(SectionType::Break, breakBars[d], d, breakE);
            // Optional reset insert at this break boundary.
            if (d == resetAtBreak && resetKind != ResetKind::None) {
                if (resetKind == ResetKind::ExtraBreak)
                    push(SectionType::Break, resetBars, d, rng.rangef(0.25f, 0.45f));
                else
                    push(SectionType::Intro, resetBars, d, rng.rangef(0.28f, 0.42f));
            }
        }
    }
    push(SectionType::Outro, outroBars, dropCount - 1, rng.rangef(0.26f, 0.34f));

    // switchAt8: mid-drop switch at bar 16 for 24-bar "extended switch" drops
    // and every 32+-bar drop.
    for (Section& s : plan.sections)
        if (s.type == SectionType::Drop && s.bars >= 24)
            s.switchAt8 = true;

    // Fakeout: at most one Build, probability chaos01*0.6.
    for (Section& s : plan.sections) {
        if (s.type != SectionType::Build) continue;
        if (rng.chance(plan.chaos01 * 0.6)) { s.fakeout = true; break; }
    }

    plan.totalBars = bar;

    // ---- Mix / master intents ----------------------------------------------
    plan.masterTargetLufs = riddim ? -8.5f : -9.0f;
    if (plan.energy01 < 0.4f) plan.masterTargetLufs -= 1.0f;  // low energy → slightly quieter master
    plan.sidechainDepth = riddim ? 0.45f : 0.7f;
    plan.mixAggression  = plan.aggression01;

    // ---- Reference calibration override (analysis-only) --------------------
    // If the user calibrated against commercial references, target the measured
    // integrated loudness instead of the hand-tuned default. Clamped so a weird
    // reference can never push the master into an unusable range.
    if (const auto& cal = Calibration::active()) {
        const CalibrationProfile& prof = cal->forGenre(params.genre);
        if (prof.present)
            // Refs measure ~-5.8 but pushing past -6.5 through the sub clipper
            // manufactures low-mid intermod mud (verified: loM 18.6%→29.7% and
            // crest 2.4→1.8 dB at -6.0). Revisit once growls carry real mid
            // energy at the source.
            plan.masterTargetLufs = std::clamp(prof.targetLufs, -14.0f, -6.5f);
    }

    return plan;
}

} // namespace rtg
