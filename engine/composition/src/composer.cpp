// Composition Engine — deterministic symbolic composer (doc 04).
// Implements rtg::compose. Pure function of Plan. ALL randomness draws from
// Rng(plan.params.seed).stream("composition") and named per-section children,
// so sections are independently stable. Same Plan => identical Score.
#include "rtg/composition/score.h"
#include "rtg/utils/rng.h"
#include "rtg/drums/drum_profile.h"   // DrumProfile::active().hatDensityPerBeat

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace rtg {
namespace {

constexpr double BPB = 4.0; // beats per bar

// ---------------------------------------------------------------------------
// Bass rhythm archetypes on a 1-bar 1/8 grid (8 slots). Rests are first-class:
// halftime feel, weighted toward space. Slot i == beat i*0.5.
struct Archetype { const char* pat; double weight; };
const Archetype kBassBank[] = {
    { "X.X...X.", 1.4 },
    { "X...X.X.", 1.2 },
    { "X.X.X...", 1.0 },
    { "X..X..X.", 1.0 },
    { "XX..X.X.", 0.8 },
    { "X.XX..X.", 0.7 },
    { "X...XX..", 0.7 },
    { "X.X.X.X.", 0.5 },
};
constexpr int kBassBankN = int(sizeof(kBassBank) / sizeof(kBassBank[0]));

// Trap 808 sparse skeletons (long notes), 1/8 grid.
const Archetype kEightOhEightBank[] = {
    { "X.......", 1.2 },
    { "X....X..", 1.0 },
    { "X...X...", 1.0 },
    { "X..X....", 0.8 },
    { "X.....X.", 0.7 },
};
constexpr int kEightOhEightBankN = int(sizeof(kEightOhEightBank) / sizeof(kEightOhEightBank[0]));

// ---------------------------------------------------------------------------
struct Comp {
    const Plan& plan;
    Score& score;
    bool riddim;
    int rootMid;   // mid-bass root (one octave above the sub)
    int rootSub;   // sub fundamental

    explicit Comp(const Plan& p, Score& s)
        : plan(p), score(s),
          riddim(p.params.genre == Genre::Riddim),
          rootMid(p.rootMidi + 12), rootSub(p.rootMidi) {}

    void add(Lane l, double startBeat, double len, int midi,
             float vel, float mod = 0.0f, float bend = 0.0f) {
        Note n;
        n.startBeat = startBeat;
        n.lengthBeats = len;
        n.midi = midi;
        n.velocity = std::max(0.0f, std::min(1.0f, vel));
        n.mod = std::max(0.0f, std::min(1.0f, mod));
        n.bendSemis = bend;
        score.notes(l).push_back(n);
    }

    // Humanized velocity around base.
    float hvel(Rng& r, float base, float jitter = 0.08f) {
        return std::max(0.05f, std::min(1.0f, base + r.gaussian() * jitter));
    }

    // Bass pitch helpers (semitone offsets from mid-bass root).
    int bassPitch(int off) const { return rootMid + off; }

    // Scale degree -> midi at a given base register.
    int scalePitch(int base, int degree) const {
        int oct = 0;
        while (degree < 0) { degree += 7; --oct; }
        oct += degree / 7;
        int d = degree % 7;
        return base + plan.scale[d] + 12 * oct;
    }

    // Was the section before `idx` a fakeout Build?
    bool precededByFakeout(int idx) const {
        if (idx <= 0) return false;
        const Section& p = plan.sections[idx - 1];
        return p.type == SectionType::Build && p.fakeout;
    }

    void composeSection(int idx);
    void composeIntro(const Section& s, int idx, Rng& r);
    void composeBuild(const Section& s, int idx, Rng& r);
    void composeDrop(const Section& s, int idx, Rng& r);
    void composeBreak(const Section& s, int idx, Rng& r);
    void composeOutro(const Section& s, int idx, Rng& r);

    void addBass(const Section& s, Rng& r, int skipFirstBar);
    void addDrums(const Section& s, Rng& r, float density, bool withSnare,
                  bool halftime, int startBar, int barCount);
    void addHats(const Section& s, Rng& r, float density, int startBar, int barCount);
    void addRiddimHatBar(double bb, Rng& r, float energy);
    void addFill(int bar, Rng& r, float nextEnergy);
    void addSubFollow(const Section& s, int skipFirstBar);
    void addMotif(int barStart, int bars, Rng& r, bool echo);
    void addPad(const Section& s, Rng& r);

    void buildCurves();
};

// ---------------------------------------------------------------------------
// BASS — the heart. Riddim call & response A/B (+sparse C); trap 808 line.
void Comp::addBass(const Section& s, Rng& r, int skipFirstBar) {
    const PaletteSpec& pal = plan.palettes[std::min<int>(s.paletteIndex,
                                          int(plan.palettes.size()) - 1)];
    const double base = s.startBar * BPB;
    const int bars = s.bars;

    if (!riddim) {
        // ---- Trap: BassA is the 808 line; BassB sparse stabs. ----
        int skIdx = r.pickWeighted(
            [] { std::vector<double> w; for (auto& a : kEightOhEightBank) w.push_back(a.weight); return w; }(),
            0.4 + plan.chaos01);
        for (int b = 0; b < bars; ++b) {
            if (b < skipFirstBar) continue;
            if (s.switchAt8 && b == 16)
                skIdx = (skIdx + 1) % kEightOhEightBankN;
            const char* pat = kEightOhEightBank[skIdx].pat;
            int prevOff = 0;
            for (int slot = 0; slot < 8; ++slot) {
                if (pat[slot] != 'X') continue;
                double t = base + b * BPB + slot * 0.5;
                // root / b7 / b6 moves.
                int off = 0;
                double roll = r.uniform();
                if (roll < 0.18 * plan.complexity01) off = -2;      // b7 below
                else if (roll < 0.30 * plan.complexity01) off = -4; // b6 below
                int midi = rootSub + off;
                // Long 808 notes; sparse.
                double len = r.range(0.75, 2.0);
                float bend = 0.0f;
                if (off != prevOff && r.chance(0.5))
                    bend = float(r.range(-2.0, 2.0)); // tuned slide into note
                add(Lane::BassA, t, len, midi, hvel(r, 0.95f),
                    0.4f + 0.4f * pal.aggression01, bend);
                prevOff = off;
            }
            // BassB: sparse stab (beat 2.5-ish) occasionally.
            if (r.chance(0.25 + 0.3 * plan.complexity01)) {
                double t = base + b * BPB + (r.chance(0.5) ? 2.5 : 3.5);
                add(Lane::BassB, t, 0.35, rootMid, hvel(r, 0.7f), 0.5f);
            }
        }
        return;
    }

    // ---- Riddim: A (call) / B (response) staccato, growl "talk" mod. ----
    std::vector<double> w;
    for (auto& a : kBassBank) w.push_back(a.weight);
    int skIdx = r.pickWeighted(w, 0.4 + plan.chaos01);
    // Response mutation state, refreshed every 4 bars.
    for (int b = 0; b < bars; ++b) {
        if (b < skipFirstBar) continue;
        if (s.switchAt8 && b == 16)
            skIdx = (skIdx + 3) % kBassBankN; // hard skeleton swap at the switch
        std::string pat = kBassBank[skIdx].pat;

        // Variation: every 4 bars mutate the response half (slots 4..7).
        if ((b % 4) == 3) {
            for (int slot = 4; slot < 8; ++slot)
                if (r.chance(0.35)) pat[slot] = (pat[slot] == 'X') ? '.' : 'X';
        }
        // Bar-8 pickup: add a lead-in eighth.
        if ((b % 8) == 7) pat[7] = 'X';

        // How many distinct pitches allowed this bar (melodic restraint).
        int distinctCap = (plan.melody01 < 0.3f) ? 1 : 2;
        int distinctUsed = 0;
        int lastOff = 0;

        for (int slot = 0; slot < 8; ++slot) {
            if (pat[slot] != 'X') continue;
            double t = base + b * BPB + slot * 0.5;
            // First half = call (BassA), second half = response (BassB).
            Lane lane = (slot < 4) ? Lane::BassA : Lane::BassB;

            int off = 0; // root by default
            if (slot != 0 && r.chance(0.15 + 0.55 * plan.complexity01)
                && distinctUsed < distinctCap) {
                static const int devs[] = {3, 6, 12}; // b3, b5, octave
                off = devs[r.intRange(0, 2)];
                if (off != lastOff) ++distinctUsed;
            }
            lastOff = off;
            int midi = bassPitch(off);

            // Staccato gate.
            double len = r.range(0.2, 0.45);
            // Articulation ("talk"): rhythmic alternation + occasional sweep.
            float artic = (slot % 2 == 0) ? 0.25f : 0.7f;
            if (r.chance(0.15)) artic = 0.95f; // sweep accent
            artic = std::min(1.0f, artic * (0.6f + 0.6f * pal.aggression01));

            float vel = hvel(r, (lane == Lane::BassA) ? 0.9f : 0.82f);
            add(lane, t, len, midi, vel, artic);
        }

        // BassC: sparse accents (only if palette has a third voice).
        if (pal.bassVoices >= 3 && r.chance(0.35)) {
            int slot = 2 + r.intRange(0, 4);
            double t = base + b * BPB + slot * 0.5;
            add(Lane::BassC, t, 0.25, bassPitch(12), hvel(r, 0.6f), 0.5f);
        }
    }
}

// SUB lane: riddim only, follows bass downbeats simplified (root, long gate).
void Comp::addSubFollow(const Section& s, int skipFirstBar) {
    if (!riddim) return; // trap: 808 covers the sub, leave Sub empty
    const double base = s.startBar * BPB;
    for (int b = 0; b < s.bars; ++b) {
        if (b < skipFirstBar) continue;
        // Root on the bar downbeat, held into the halftime pocket.
        add(Lane::Sub, base + b * BPB, 1.5, rootSub, 0.95f, 0.2f);
        // Occasional mid-bar sub reinforcement.
        add(Lane::Sub, base + b * BPB + 2.0, 1.0, rootSub, 0.85f, 0.2f);
    }
}

// ---------------------------------------------------------------------------
// DRUMS
void Comp::addDrums(const Section& s, Rng& r, float density, bool withSnare,
                    bool halftime, int startBar, int barCount) {
    const double base = 0.0; // absolute beats computed per bar below
    (void)base;
    for (int i = 0; i < barCount; ++i) {
        int bar = startBar + i;
        double bb = bar * BPB;

        // Kick.
        if (riddim) {
            add(Lane::Kick, bb + 0.0, 0.5, rootSub, hvel(r, 1.0f, 0.05f));
            // Complexity-scaled pickup before the next bar.
            if (r.chance(0.25 * plan.complexity01 * density))
                add(Lane::Kick, bb + 3.5, 0.25, rootSub, hvel(r, 0.7f, 0.05f));
        } else {
            add(Lane::Kick, bb + 0.0, 0.5, rootSub, hvel(r, 1.0f, 0.05f));
            // Syncopated clusters (1..3 per 2 bars), never on beat 2.
            if ((bar % 2) == 0) {
                int extra = 1 + int(std::lround(density * 2.0));
                extra = std::min(3, extra);
                for (int e = 0; e < extra; ++e) {
                    static const double slots[] = {0.75, 1.5, 2.75, 3.25, 3.75};
                    double off = slots[r.intRange(0, 4)];
                    if (std::abs(off - 2.0) < 0.01) continue; // never on beat 2
                    add(Lane::Kick, bb + off, 0.25, rootSub, hvel(r, 0.8f, 0.05f));
                }
            }
        }

        // Snare — ALWAYS on beat 2 (halftime), in drops/builds/breaks-w/drums.
        if (withSnare)
            add(Lane::Snare, bb + 2.0, 0.5, 38, hvel(r, 0.95f, 0.05f));
    }
    // Hats layered separately.
    addHats(s, r, density, startBar, barCount);
}

void Comp::addHats(const Section& s, Rng& r, float density, int startBar, int barCount) {
    for (int i = 0; i < barCount; ++i) {
        int bar = startBar + i;
        double bb = bar * BPB;

        if (riddim) {
            // Busy 16th-grid riddim hats, density steered by the reference
            // profile and scaled by section energy (drops full, intros sparse).
            addRiddimHatBar(bb, r, s.energy);
        } else {
            // Trap: 1/8 base plus roll bursts (1/16, 1/32, occasional triplet).
            for (int e = 0; e < 8; ++e) {
                double t = bb + e * 0.5;
                add(Lane::HatClosed, t, 0.25, 42, hvel(r, 0.6f, 0.06f), 0.3f);
                // Roll burst.
                if (r.chance(0.10 + 0.35 * density)) {
                    double roll = r.uniform();
                    double step = (roll < 0.5) ? 0.25 : (roll < 0.8 ? 0.125 : (1.0 / 3.0));
                    int n = int(0.5 / step);
                    for (int k = 1; k < n; ++k) {
                        float ramp = 0.4f + 0.5f * (float(k) / float(std::max(1, n)));
                        add(Lane::HatClosed, t + k * step, step * 0.9, 42,
                            hvel(r, ramp, 0.05f), 0.3f);
                    }
                }
            }
            if (r.chance(0.25))
                add(Lane::HatOpen, bb + 2.5, 0.4, 46, hvel(r, 0.55f, 0.05f), 0.5f);
        }

        // Perc / tops when the section is energetic and offbeat.
        if (s.energy > 0.6f && r.chance(0.5)) {
            double t = bb + (r.chance(0.5) ? 1.5 : 3.5);
            add(Lane::Perc, t, 0.2, 37, hvel(r, 0.5f, 0.06f), 0.5f);
        }
    }
}

// One bar of busy, 16th-based riddim closed hats with velocity variation and
// occasional gaps/rolls. Target events/beat comes from the active DrumProfile
// (~2.4-3.2 in drops), scaled down by section energy so builds/intros thin out.
// Deterministic: draws only from the passed section stream `r`.
void Comp::addRiddimHatBar(double bb, Rng& r, float energy) {
    const float e = std::max(0.0f, std::min(1.0f, energy));
    const float target = DrumProfile::active().hatDensityPerBeat;   // ~2.8
    const float perBeat = target * (0.30f + 0.82f * e);             // drop~2.9, intro~1.6
    const float pbase = std::max(0.0f, std::min(0.98f, perBeat / 4.0f));

    for (int beat = 0; beat < 4; ++beat) {
        // Occasional whole-beat gap for breathing room (rarer at high energy).
        if (r.chance(0.10 * (1.2f - e))) continue;
        // Occasional 16th roll fill on this beat (busier as energy rises).
        const bool roll = r.chance(0.10 + 0.18 * e);
        for (int sub = 0; sub < 4; ++sub) {
            const int slot = beat * 4 + sub;             // 0..15
            const double t = bb + beat + 0.25 * sub;
            const bool isEighth = (sub % 2) == 0;
            const bool isDown = (sub == 0);
            float p = roll ? 1.0f : (isEighth ? pbase * 1.25f : pbase * 0.75f);
            if (!r.chance(p)) continue;
            // Accent downbeats and the offbeat "and"; ghost the in-betweens.
            float base = isDown ? 0.72f : (sub == 2 ? 0.66f : 0.5f);
            if (roll) base = 0.42f + 0.42f * (float(sub) / 3.0f);   // ramp up
            add(Lane::HatClosed, t, 0.24, 42, hvel(r, base, 0.07f),
                0.4f + 0.15f * float(slot & 1));
        }
    }
    // Open-hat offbeat accent (kept from the classic riddim feel).
    if (r.chance(0.25 + 0.25 * e))
        add(Lane::HatOpen, bb + 2.5, 0.4, 46, hvel(r, 0.55f, 0.05f), 0.6f);
}

// FILL at end of an 8-bar phrase: last 2 beats of `bar`.
void Comp::addFill(int bar, Rng& r, float nextEnergy) {
    double bb = bar * BPB;
    double fillStart = bb + 2.0; // last 2 beats
    double roll = r.uniform();

    if (roll < 0.25 * plan.chaos01) {
        // Cut fill: remove all drum notes in the LAST beat (silence gap).
        double cutFrom = bb + 3.0, cutTo = bb + 4.0;
        for (Lane l : {Lane::Kick, Lane::Snare, Lane::HatClosed, Lane::HatOpen, Lane::Perc}) {
            auto& v = score.notes(l);
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Note& n) {
                return n.startBeat >= cutFrom - 1e-6 && n.startBeat < cutTo - 1e-6;
            }), v.end());
        }
    } else if (roll < 0.7) {
        // Snare fill: 3..5 hits densifying, velocity ramp matched to next energy.
        int hits = 3 + int(std::lround(nextEnergy * 2.0));
        hits = std::min(5, std::max(3, hits));
        for (int h = 0; h < hits; ++h) {
            double t = fillStart + (2.0 * h) / hits;
            float v = 0.6f + 0.35f * (float(h) / float(hits));
            add(Lane::Snare, t, 0.2, 38, hvel(r, v, 0.05f));
        }
    } else {
        // Extra perc flourish.
        for (int h = 0; h < 4; ++h)
            add(Lane::Perc, fillStart + h * 0.5, 0.2, 37, hvel(r, 0.55f, 0.06f), 0.5f);
    }
}

// ---------------------------------------------------------------------------
// MELODY & PAD
void Comp::addMotif(int barStart, int bars, Rng& r, bool echo) {
    if (plan.melody01 < 0.2f) return;
    const int reg = plan.rootMidi + 24 + (r.chance(0.5) ? 12 : 0); // rootMidi+24..+36
    // Build a 1-bar motif: constrained random walk, step bias, rests.
    int nNotes = 2 + r.intRange(0, 2); // 2..4
    struct MN { double beat; int deg; double len; };
    std::vector<MN> motif;
    int deg = 0;
    double cursor = 0.0;
    for (int i = 0; i < nNotes && cursor < BPB; ++i) {
        if (i > 0 && r.chance(0.25)) { cursor += 0.5; continue; } // rest
        int step = r.chance(0.75) ? (r.chance(0.5) ? 1 : -1)
                                   : (r.chance(0.5) ? 2 : -2);   // step bias, small leap
        deg += step;
        deg = std::clamp(deg, -2, 6);
        double len = r.chance(0.5) ? 0.5 : 1.0;
        motif.push_back({cursor, deg, len});
        cursor += len + (r.chance(0.3) ? 0.5 : 0.0);
    }
    if (motif.empty()) return;

    auto place = [&](int bar, float velBase, int transpose) {
        double bb = bar * BPB;
        for (auto& m : motif) {
            int midi = scalePitch(reg, m.deg) + transpose;
            add(Lane::Melody, bb + m.beat, m.len, midi, hvel(r, velBase, 0.06f), 0.5f);
        }
    };

    // 1-bar motif + echo response (call & response) across the region.
    for (int b = 0; b + 1 < bars; b += 2) {
        place(barStart + b, 0.8f, 0);
        if (echo) place(barStart + b + 1, 0.62f, r.chance(0.4) ? 5 : 0); // echoed answer
        else place(barStart + b + 1, 0.72f, 0);
    }
}

void Comp::addPad(const Section& s, Rng& r) {
    if (plan.melody01 < 0.5f && s.type == SectionType::Drop) return;
    // i - VI - VII - iv progression, whole/half-note chords.
    static const int prog[4] = {0, 8, 10, 5};
    const int reg = plan.rootMidi + 12;
    for (int b = 0; b < s.bars; b += 2) {
        int chordRoot = reg + prog[(b / 2) % 4];
        double bb = (s.startBar + b) * BPB;
        double len = 2.0 * BPB; // two-bar (whole-ish) pad
        // 2..3 simultaneous notes.
        add(Lane::Pad, bb, len, chordRoot, hvel(r, 0.5f, 0.03f), 0.3f);
        add(Lane::Pad, bb, len, chordRoot + 3, hvel(r, 0.45f, 0.03f), 0.3f);
        if (r.chance(0.6))
            add(Lane::Pad, bb, len, chordRoot + 7, hvel(r, 0.42f, 0.03f), 0.3f);
    }
}

// ---------------------------------------------------------------------------
// SECTION COMPOSERS
void Comp::composeIntro(const Section& s, int idx, Rng& r) {
    (void)idx;
    const int half = std::max(1, s.bars / 2);
    switch (plan.params.introStyle) {
        case IntroStyle::Atmospheric:
            addPad(s, r);
            // Sparse perc throughout; drums enter halfway.
            for (int b = 0; b < s.bars; ++b)
                if (r.chance(0.4))
                    add(Lane::Perc, (s.startBar + b) * BPB + 2.5, 0.2, 37,
                        hvel(r, 0.4f, 0.05f), 0.5f);
            addDrums(s, r, 0.4f, true, true, s.startBar + half, s.bars - half);
            break;
        case IntroStyle::Minimal:
            addDrums(s, r, 0.5f, false, true, s.startBar, s.bars);
            break;
        case IntroStyle::VocalChop:
            addMotif(s.startBar, s.bars, r, true); // Melody-lane chops
            addDrums(s, r, 0.4f, true, true, s.startBar + half, s.bars - half);
            break;
        case IntroStyle::Impact:
            add(Lane::Impact, s.startBar * BPB, 1.0, rootSub, 1.0f, 1.0f);
            addDrums(s, r, 0.8f, true, false, s.startBar, s.bars); // near-full groove
            addBass(s, r, 0);
            addSubFollow(s, 0);
            break;
        case IntroStyle::Fakeout:
            // Build-like intro.
            add(Lane::Riser, s.startBar * BPB, s.bars * BPB, plan.rootMidi + 36,
                0.8f, 0.8f);
            addDrums(s, r, 0.6f, true, false, s.startBar, s.bars);
            break;
    }
}

void Comp::composeBuild(const Section& s, int idx, Rng& r) {
    (void)idx;
    const int bars = s.bars;
    const double bb0 = s.startBar * BPB;
    // Riser spanning the whole build.
    add(Lane::Riser, bb0, bars * BPB, plan.rootMidi + 36, 0.85f, 0.9f);

    // Bass mostly silent; a touch early, silent last 2 bars.
    if (plan.melody01 > 0.8f) addMotif(s.startBar, bars - 2, r, false); // build arps

    const int rollStart = std::max(0, bars - 4);
    for (int b = 0; b < bars; ++b) {
        int bar = s.startBar + b;
        double bb = bar * BPB;
        bool lastTwo = (b >= bars - 2);

        // Kick thins in the last 2 bars.
        if (!lastTwo || r.chance(0.4))
            add(Lane::Kick, bb, 0.5, rootSub, hvel(r, lastTwo ? 0.7f : 0.95f, 0.05f));

        if (b < rollStart) {
            // Early bars: halftime snare + hats. Riddim builds run the busy
            // 16th hat pattern (energy-scaled); trap keeps its offbeat feel.
            add(Lane::Snare, bb + 2.0, 0.5, 38, hvel(r, 0.9f));
            if (riddim) {
                addRiddimHatBar(bb, r, s.energy);
            } else {
                for (int beat = 0; beat < 4; ++beat)
                    if (r.chance(0.6))
                        add(Lane::HatClosed, bb + beat + 0.5, 0.25, 42, hvel(r, 0.55f), 0.3f);
            }
        } else {
            // Snare roll doubling: 1/4 -> 1/8 -> 1/16 -> 1/32 over last 4 bars.
            int stage = b - rollStart; // 0..3
            double step = 1.0 / double(1 << stage); // 1, .5, .25, .125
            int n = int(std::lround(BPB / step));
            for (int k = 0; k < n; ++k) {
                float ramp = 0.5f + 0.45f * (float(k) / float(std::max(1, n)));
                add(Lane::Snare, bb + k * step, step * 0.9, 38, hvel(r, ramp, 0.04f));
            }
        }
    }
}

void Comp::composeDrop(const Section& s, int idx, Rng& r) {
    const bool fakeout = precededByFakeout(idx);
    const int skip = fakeout ? 1 : 0;

    if (fakeout) {
        // 1 bar of near-silence: a single vocal-ish stab / impact; drop enters bar 2.
        add(Lane::Melody, s.startBar * BPB, 1.0, plan.rootMidi + 24,
            0.75f, 0.6f);
        add(Lane::Impact, s.startBar * BPB, 0.5, rootSub, 0.9f, 1.0f);
    }

    // FX: impact + crash at the (real) drop entry.
    double entry = (s.startBar + skip) * BPB;
    add(Lane::Impact, entry, 1.0, rootSub, 1.0f, 1.0f);
    add(Lane::Crash, entry, 2.0, 49, 0.9f, 0.5f);

    // Full bass + drums.
    addBass(s, r, skip);
    addSubFollow(s, skip);
    addDrums(s, r, 0.9f, true, true, s.startBar + skip, s.bars - skip);

    // Melody stabs shadowing the drop when the budget allows.
    if (plan.melody01 > 0.5f)
        addMotif(s.startBar + skip, s.bars - skip, r, false);

    // Crash again just after a mid-drop switch.
    if (s.switchAt8 && s.bars > 16)
        add(Lane::Crash, (s.startBar + 16) * BPB, 2.0, 49, 0.85f, 0.5f);

    // Fills at the end of every 8-bar phrase.
    for (int b = 7; b < s.bars; b += 8) {
        float nextE = (idx + 1 < int(plan.sections.size()))
                          ? plan.sections[idx + 1].energy : 0.5f;
        addFill(s.startBar + b, r, nextE);
    }
}

void Comp::composeBreak(const Section& s, int idx, Rng& r) {
    // No sub/bass. Pad chords + melody if budget. Halftime sparse drums.
    addPad(s, r);
    if (plan.melody01 >= 0.2f)
        addMotif(s.startBar, s.bars, r, true);
    // Sparse halftime drums (snare kept on 2).
    for (int b = 0; b < s.bars; ++b) {
        int bar = s.startBar + b;
        double bb = bar * BPB;
        if (r.chance(0.6)) add(Lane::Kick, bb, 0.5, rootSub, hvel(r, 0.8f));
        add(Lane::Snare, bb + 2.0, 0.5, 38, hvel(r, 0.7f));
        if (r.chance(0.4))
            add(Lane::HatClosed, bb + 2.5, 0.25, 42, hvel(r, 0.5f), 0.4f);
    }
    // Downlifter into the break (after the preceding drop).
    if (idx > 0 && plan.sections[idx - 1].type == SectionType::Drop)
        add(Lane::Downlifter, s.startBar * BPB, 2.0 * BPB, plan.rootMidi + 24,
            0.8f, 0.7f);
}

void Comp::composeOutro(const Section& s, int idx, Rng& r) {
    (void)idx;
    // Pad tail + element-by-element removal every 4 bars.
    addPad(s, r);
    const int phases = std::max(1, s.bars / 4);
    for (int b = 0; b < s.bars; ++b) {
        int bar = s.startBar + b;
        double bb = bar * BPB;
        int phase = (b / 4);
        // Kick fades out across phases.
        if (phase < phases - 1 && r.chance(1.0 - float(phase) / float(phases)))
            add(Lane::Kick, bb, 0.5, rootSub, hvel(r, 0.75f - 0.15f * phase));
        // Snare only in the first phase.
        if (phase == 0) add(Lane::Snare, bb + 2.0, 0.5, 38, hvel(r, 0.6f));
        if (phase < phases && r.chance(0.4 - 0.1f * phase))
            add(Lane::HatClosed, bb + 2.5, 0.25, 42, hvel(r, 0.45f), 0.4f);
    }
}

void Comp::composeSection(int idx) {
    const Section& s = plan.sections[idx];
    Rng r = Rng(plan.params.seed).stream("composition").stream("section", idx);
    switch (s.type) {
        case SectionType::Intro: composeIntro(s, idx, r); break;
        case SectionType::Build: composeBuild(s, idx, r); break;
        case SectionType::Drop:  composeDrop(s, idx, r);  break;
        case SectionType::Break: composeBreak(s, idx, r); break;
        case SectionType::Outro: composeOutro(s, idx, r); break;
    }
}

// ---------------------------------------------------------------------------
// CURVES
void Comp::buildCurves() {
    const int N = int(std::lround(score.totalBeats));
    auto& bf = score.buildFilter.perBeat;
    auto& bs = score.breakSoften.perBeat;
    auto& fx = score.fxSend.perBeat;
    auto& en = score.energy.perBeat;
    bf.assign(N + 1, 0.0f);
    bs.assign(N + 1, 0.0f);
    fx.assign(N + 1, 0.2f); // baseline
    en.assign(N + 1, 0.3f);

    for (size_t i = 0; i < plan.sections.size(); ++i) {
        const Section& s = plan.sections[i];
        int b0 = s.startBar * int(BPB);
        int b1 = std::min(N, b0 + s.bars * int(BPB));
        float prevE = (i > 0) ? plan.sections[i - 1].energy : 0.2f;

        for (int b = b0; b <= b1 && b <= N; ++b) {
            double frac = (b1 > b0) ? double(b - b0) / double(b1 - b0) : 0.0;

            switch (s.type) {
                case SectionType::Build: {
                    // Ease-in filter sweep 0 -> 1 across the build.
                    bf[b] = float(frac * frac);
                    // Energy ramps from previous toward the drop.
                    en[b] = float(prevE + (0.88f - prevE) * frac);
                    break;
                }
                case SectionType::Drop:
                    bf[b] = 0.0f; // snaps to 0 at the drop
                    en[b] = s.energy;
                    break;
                case SectionType::Break:
                    bs[b] = 0.7f;
                    en[b] = s.energy;
                    break;
                case SectionType::Intro:
                    if (plan.params.introStyle == IntroStyle::Atmospheric)
                        bs[b] = 0.65f;
                    en[b] = float(0.25f + 0.2f * frac); // ramp 0.25 -> 0.45
                    break;
                case SectionType::Outro:
                    bs[b] = 0.7f;
                    en[b] = float(0.3f * (1.0 - 0.5 * frac)); // falling
                    break;
            }

            // fxSend: raised in breaks/intro, brief tail spike everywhere.
            if (s.type == SectionType::Break || s.type == SectionType::Intro)
                fx[b] = std::max(fx[b], 0.5f);
        }
        // Section-tail fx spike (last 2 beats).
        for (int b = std::max(b0, b1 - 2); b <= b1 && b <= N; ++b)
            fx[b] = std::max(fx[b], 0.7f);
    }

    // Smooth breakSoften & energy edges (1-beat transitions) with a light pass.
    auto smooth = [&](std::vector<float>& v) {
        std::vector<float> o = v;
        for (int b = 1; b < N; ++b)
            v[b] = 0.25f * o[b - 1] + 0.5f * o[b] + 0.25f * o[b + 1];
    };
    smooth(bs);
    smooth(en);
}

} // namespace

Score compose(const Plan& plan) {
    Score score;
    score.totalBeats = plan.totalBars * plan.beatsPerBar;

    Comp comp(plan, score);
    for (size_t i = 0; i < plan.sections.size(); ++i)
        comp.composeSection(int(i));
    comp.buildCurves();
    return score;
}

} // namespace rtg
