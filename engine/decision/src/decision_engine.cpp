// Decision Engine — deterministic "producer brain".
// Implements rtg::makePlan (doc 10). Pure function of Params (incl. seed).
// All randomness flows from Rng(params.seed).stream("decision"); no globals,
// no time(), no rand().
#include "rtg/decision/plan.h"
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

    // ---- Root note (E1..G1 = 28..31); darker => lower fundamental ----
    int root = 31 - int(std::lround(plan.darkness01 * 3.0f));
    plan.rootMidi = std::clamp(root, 28, 31);

    // ---- Scale: natural minor default; very dark => phrygian ----
    if (plan.darkness01 > 0.65f) {
        const int phrygian[7] = {0, 1, 3, 5, 7, 8, 10};
        for (int i = 0; i < 7; ++i) plan.scale[i] = phrygian[i];
    } else {
        const int minor[7] = {0, 2, 3, 5, 7, 8, 10};
        for (int i = 0; i < 7; ++i) plan.scale[i] = minor[i];
    }

    // ---- Structure planning -------------------------------------------------
    const int dropCount = std::clamp(params.dropCount, 1, 4);
    const int targetBars = barsForSeconds(params.lengthSec, bpm);

    // Intro length by style.
    int introBars;
    switch (params.introStyle) {
        case IntroStyle::Impact:      introBars = 4;  break;
        case IntroStyle::Atmospheric: introBars = 16; break;
        case IntroStyle::Fakeout:     introBars = 16; break;
        default:                      introBars = 8;  break; // Minimal, VocalChop
    }

    int buildBars = 8;
    int outroBars = 8;
    std::vector<int> dropBars(dropCount, 16);
    std::vector<int> breakBars(std::max(0, dropCount - 1), 8);

    auto totalBars = [&]() {
        int t = introBars + outroBars;
        for (int b : dropBars) t += buildBars + b;
        for (int b : breakBars) t += b;
        return t;
    };

    // Greedy: apply each length upgrade only if it moves us closer to target.
    auto tryUpgrade = [&](int& field, int delta, int cap) {
        if (field >= cap) return;
        int before = std::abs(targetBars - totalBars());
        field += delta;
        int after = std::abs(targetBars - totalBars());
        if (after >= before) field -= delta;
    };
    for (int& d : dropBars) tryUpgrade(d, 16, 32); // biggest lever first
    for (int& b : breakBars) tryUpgrade(b, 8, 16);
    tryUpgrade(outroBars, 8, 16);

    // Short targets: shrink toward the floor as well (drops stay >= 16 bars —
    // an 8-bar drop isn't a drop). Same closer-to-target greedy rule.
    auto tryDowngrade = [&](int& field, int delta, int floor) {
        if (field - delta < floor) return;
        int before = std::abs(targetBars - totalBars());
        field -= delta;
        int after = std::abs(targetBars - totalBars());
        if (after >= before) field += delta;
    };
    tryDowngrade(introBars, 8, 8);    // 16-bar intros halve first
    tryDowngrade(outroBars, 4, 4);
    tryDowngrade(buildBars, 4, 4);
    tryDowngrade(introBars, 4, 4);

    // ---- Palettes: one per drop --------------------------------------------
    plan.palettes.clear();
    for (int d = 0; d < dropCount; ++d) {
        PaletteSpec ps;
        ps.aggression01 = clamp01(plan.aggression01 + rng.rangef(-0.1f, 0.1f));
        ps.darkness01   = clamp01(plan.darkness01 + rng.rangef(-0.1f, 0.1f));
        ps.novelty01    = clamp01(0.15f + 0.6f * plan.chaos01);
        ps.bassVoices   = (plan.complexity01 > 0.6f) ? 3 : 2;
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
        float buildE = 0.6f + 0.2f * (float(d) / denom);
        float dropE  = std::min(1.0f, 0.9f + 0.1f * (float(d) / denom));
        push(SectionType::Build, buildBars, d, buildE);
        push(SectionType::Drop, dropBars[d], d, dropE);
        if (d < dropCount - 1)
            push(SectionType::Break, breakBars[d], d, 0.35f);
    }
    push(SectionType::Outro, outroBars, dropCount - 1, 0.30f);

    // switchAt8 on every 32-bar drop (mid-drop switch at bar 16).
    for (Section& s : plan.sections)
        if (s.type == SectionType::Drop && s.bars >= 32)
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

    return plan;
}

} // namespace rtg
