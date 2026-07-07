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
#include <utility>
#include <vector>

namespace rtg {
namespace {

constexpr double BPB = 4.0; // beats per bar

// ---------------------------------------------------------------------------
struct Archetype { const char* pat; double weight; };

// Riddim bass — the signature bounce on a 1-bar 1/16 grid (16 steps, step i ==
// beat i*0.25). Rests are first-class: step 0 (the kick's downbeat) and step 8
// (the beat-3 halftime snare) are kept clear so the bass ANSWERS around the
// kick and snare, leaning into space. Off-beat / "e-&-a" stabs dominate;
// clustered bursts then rests, ~40-55% density. These are the CALL patterns;
// the response bar mutates them.
const Archetype kRiddim16Bank[] = {
    { "..X..X.X..X..X.X", 1.4 }, // classic answer-the-kick bounce
    { "..X.X..X..X.X..X", 1.3 }, // driving off-beats, gap at beat 3
    { "...X..X...X..X.X", 1.0 }, // sparse, late answers
    { "..XX...X..XX...X", 1.0 }, // paired-stab bursts
    { "..X.X.X...X.X.X.", 0.8 }, // busier gallop
    { "..X..XX...X..XX.", 0.8 }, // double-stab clusters
};
constexpr int kRiddim16BankN = int(sizeof(kRiddim16Bank) / sizeof(kRiddim16Bank[0]));

// Riddim triplet-feel option — a 1-bar 12-step grid (step i == beat i/3) for a
// shuffle/triplet bounce. Gaps at beats 1 & 3 (steps 0 and 6).
const Archetype kRiddim12Bank[] = {
    { ".XX..X.XX..X", 1.2 },
    { ".X..XX..X.X.", 1.0 },
    { ".X.X.X..X.X.", 0.9 },
};
constexpr int kRiddim12BankN = int(sizeof(kRiddim12Bank) / sizeof(kRiddim12Bank[0]));

// HARD TEAROUT riddim bank — dense, on-grid straight-16th patterns anchored on
// the DOWNBEAT (step 0 always 'X') and emphasizing beats 1/2/3/4 (steps 0/4/8/
// 12). No negative space around the kick: the bass STOMPS locked with kick+sub
// instead of answering around them. Used for riddim DROPS, blended toward with
// aggression. No swing/triplet — these read as machine-gun square-wave stomps.
const Archetype kTearout16Bank[] = {
    // Sparse, HARD stomps on the strong beats — each sustained stab is chopped
    // into the eighth-note "wob" by the growl's gate, so the pattern itself
    // stays spaced (hard hits), not a busy machine-gun of onsets.
    { "X...X...X...X...", 1.5 }, // pure quarter stomp (hardest, most spaced)
    { "X...X...X...X..X", 1.3 }, // quarter stomp + lead-in pickup
    { "X..XX...X..XX...", 1.0 }, // stomp pairs on 1 & 3
    { "X...X..XX...X..X", 0.9 }, // stomp with a double tail
    { "X.XXX.XXX.XXX.XX", 0.4 }, // occasional machine-gun burst (rare variation)
};
constexpr int kTearout16BankN = int(sizeof(kTearout16Bank) / sizeof(kTearout16Bank[0]));

// Trap 808 skeletons (1/8 grid). Sparse-long families plus rolling and
// glide-chain shapes; behavior (note length / gliding) is chosen per drop.
const Archetype kEightOhEightBank[] = {
    { "X.......", 1.2 },
    { "X....X..", 1.0 },
    { "X...X...", 1.0 },
    { "X..X....", 0.8 },
    { "X.....X.", 0.7 },
    { "X..X..X.", 0.7 },  // rolling-lean
    { "X.X...X.", 0.6 },  // rolling-lean
    { "X...X.X.", 0.6 },  // glide-chain friendly
    { "X.X.X...", 0.5 },  // rolling
};
constexpr int kEightOhEightBankN = int(sizeof(kEightOhEightBank) / sizeof(kEightOhEightBank[0]));

// ---------------------------------------------------------------------------
struct Comp {
    const Plan& plan;
    Score& score;
    bool riddim;
    int rootMid;   // mid-bass root (one octave above the sub)
    int rootSub;   // sub fundamental

    // ---- Per-track "feel" (seeded once, so two seeds swing differently). ----
    float humScale = 1.0f;   // velocity-jitter scale (tight ~0.6, loose ~1.5)
    float humTime = 0.0f;    // timing-jitter amount in beats (loose grooves drift)
    float humTimeMul = 1.0f; // temporary timing-drift scale (riddim drops tighten it)
    int   melodyReg = 0;     // extra register offset for motif placement
    int   breakProg = 0;     // which break/pad chord progression (0..3)
    bool  introRampExp = false; // intro energy ramp shape (linear vs exponential)

    explicit Comp(const Plan& p, Score& s)
        : plan(p), score(s),
          riddim(p.params.genre == Genre::Riddim),
          rootMid(p.rootMidi + 12), rootSub(p.rootMidi) {
        Rng f = Rng(p.params.seed).stream("composition").stream("feel");
        const bool loose = f.chance(0.5);
        humScale     = loose ? f.rangef(1.1f, 1.55f) : f.rangef(0.55f, 0.9f);
        humTime      = loose ? f.rangef(0.02f, 0.055f) : f.rangef(0.0f, 0.018f);
        melodyReg    = (f.chance(0.5) ? 0 : 12) + (f.chance(0.3) ? -12 : 0);
        breakProg    = f.intRange(0, 3);
        introRampExp = f.chance(0.5);
    }

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

    // Humanized velocity around base (jitter scaled by the track's feel).
    float hvel(Rng& r, float base, float jitter = 0.08f) {
        return std::max(0.05f, std::min(1.0f, base + r.gaussian() * jitter * humScale));
    }

    // Timing humanization: nudge an off-grid event by the track's feel amount.
    double mt(Rng& r, double t) {
        const float ht = humTime * humTimeMul;
        if (ht <= 0.0f) return t;
        return std::max(0.0, t + double(r.gaussian()) * double(ht));
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
    void addRiddimHatBar(double bb, Rng& r, float energy, int archetype = 0);
    void addFill(int bar, Rng& r, float nextEnergy);
    void addSubFollow(const Section& s, int skipFirstBar);
    void addMotif(int barStart, int bars, Rng& r, bool echo);
    void addPad(const Section& s, Rng& r);

    // Musicality helpers (breaks/intros/outros/transitions).
    void addBreakPad(const Section& s, Rng& r, float intensity);
    void addTexture(const Section& s, Rng& r, int count);
    void addTonalPerc(const Section& s, Rng& r, float density);
    void dropTurnaround(const Section& s, int idx, Rng& r);
    void addTransitions();
    bool hasFxNear(double beat, double halfWin) const;
    void addSwell(double startBeat, double lenBeats, float vel, bool rising);

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
        // ---- Trap: BassA is the 808 line; BassB sparse stabs. Per-drop 808
        // archetype: 0 = sparse-long, 1 = rolling-8ths, 2 = glide-chains. ----
        int skIdx = r.pickWeighted(
            [] { std::vector<double> w; for (auto& a : kEightOhEightBank) w.push_back(a.weight); return w; }(),
            0.4 + plan.chaos01);
        const int t808mode = r.pickWeighted({1.1, 0.9, 0.8}, 1.0); // sparse / rolling / glide
        for (int b = 0; b < bars; ++b) {
            if (b < skipFirstBar) continue;
            if (s.switchAt8 && b == 16)
                skIdx = (skIdx + 1) % kEightOhEightBankN;
            std::string pat = kEightOhEightBank[skIdx].pat;
            if (t808mode == 1) {
                // rolling-8ths: fill in extra onsets so the 808 drives eighths.
                for (int slot = 0; slot < 8; ++slot)
                    if (pat[slot] != 'X' && r.chance(0.35 + 0.25 * plan.complexity01))
                        pat[slot] = 'X';
            }
            int prevOff = 0;
            for (int slot = 0; slot < 8; ++slot) {
                if (pat[slot] != 'X') continue;
                double t = mt(r, base + b * BPB + slot * 0.5);
                // root / b7 / b6 moves.
                int off = 0;
                double roll = r.uniform();
                if (roll < 0.18 * plan.complexity01) off = -2;      // b7 below
                else if (roll < 0.30 * plan.complexity01) off = -4; // b6 below
                int midi = rootSub + off;
                // Note length by mode: sparse-long vs rolling (short) vs glide.
                double len = (t808mode == 1) ? r.range(0.4, 0.8) : r.range(0.75, 2.0);
                float bend = 0.0f;
                // glide-chains: bend into most notes; sparse: occasional slide.
                double bendChance = (t808mode == 2) ? 0.75 : 0.5;
                if (off != prevOff && r.chance(bendChance))
                    bend = float(r.range(-2.0, 2.0)); // tuned slide into note
                else if (t808mode == 2 && r.chance(0.4))
                    bend = float(r.range(-1.5, 1.5)); // glide even at pitch
                add(Lane::BassA, t, len, midi, hvel(r, 0.95f),
                    0.4f + 0.4f * pal.aggression01, bend);
                prevOff = off;
            }
            // BassB: sparse stab (beat 2.5-ish) occasionally.
            if (r.chance(0.25 + 0.3 * plan.complexity01)) {
                double t = mt(r, base + b * BPB + (r.chance(0.5) ? 2.5 : 3.5));
                add(Lane::BassB, t, 0.35, rootMid, hvel(r, 0.7f), 0.5f);
            }
        }
        return;
    }

    // ---- Riddim: syncopated 1/16 (or triplet) 2-bar CALL/RESPONSE. ----
    // BassA = call voice (even bars), BassB = response voice (odd bars, an
    // answered/mutated variant). Staccato stabs, growl "talk" via `mod`,
    // per-pattern swing for the bounce, negative space around beats 1 & 3.
    // HARD TEAROUT mode: on riddim DROPS we kill the reggaeton bounce and stomp
    // ON the grid, scaling intensity with aggression. Other riddim sections
    // (e.g. the Impact-intro teaser) keep the classic swung/answering feel, so
    // all their RNG draws below stay in their original order (identical output).
    const bool tearout = (s.type == SectionType::Drop);
    const double agg = double(plan.aggression01);

    // ---- CHUG (riddim DROP): the signature interlock. BassA plays short
    // square-wave STAB "chugs" on a STRAIGHT-EIGHTH grid, LOCKED to the beat/
    // half-beat with the kick / snare / whack-kicks + the Sub (which mirrors
    // these exact stabs in addSubFollow). The four beats (slots 0/2/4/6) ALWAYS
    // chug; the off-beat eighths (1/3/5/7) fill in as aggression/complexity rise
    // → busier but always tight and on-grid. Mostly ROOT (tonal chug); rare
    // octave / dark scale move for movement. No swing, no triplet — a
    // machine-square "chug chug chug" that coincides with every drum hit.
    // Handled entirely here (dedicated path) so the classic call/response riddim
    // below — used by non-drop riddim sections — keeps its exact RNG draw order.
    if (tearout) {
        // The chug EVOLVES across the drop: each 4-bar phrase picks a chug MODE
        // so the drop "switches up" (the signature riddim move) instead of 16-32
        // bars of one pattern. Mode 0 = straight 8ths (establish), 1 = DOUBLE-
        // TIME 16ths (the hard switch), 2 = SYNCOPATED off-beat stabs. The growl
        // also "talks": the per-note mod sweeps across each bar (formant/filter
        // movement) rather than sitting static. All deterministic per seed.
        for (int b = 0; b < bars; ++b) {
            if (b < skipFirstBar) continue;
            const int phrase = (b - skipFirstBar) / 4;
            int mode = 0;
            if (phrase > 0) {   // first phrase establishes; later phrases can switch
                Rng pr = Rng(plan.params.seed).stream("chugmode", s.startBar * 97 + phrase);
                std::vector<double> mw = {1.5, 1.0 + 1.2 * agg, 0.7};
                mode = pr.pickWeighted(mw, 1.0);
            }
            const int steps    = (mode == 1) ? 16 : 8;
            const double stepB = (mode == 1) ? 0.25 : 0.5;

            bool hit[16] = {false};
            for (int e = 0; e < steps; ++e) {
                if (mode == 2) {                          // syncopated: lean off-beats
                    if (e % 2 == 1) { hit[e] = true; continue; }
                    hit[e] = r.chance(0.5 + 0.3 * agg);
                    continue;
                }
                const bool onBeat = (mode == 1) ? (e % 4 == 0) : (e % 2 == 0);
                if (onBeat) { hit[e] = true; continue; }
                double p = 0.62 + 0.33 * agg + 0.10 * plan.complexity01;
                if (mode == 1) p = (e % 2 == 0) ? 0.9 : (0.45 + 0.4 * agg); // DT: 8ths solid, 16ths fill
                hit[e] = r.chance(std::min(0.98, p));
            }

            std::vector<std::pair<int, double>> stabs;
            for (int e = 0; e < steps; ++e)
                if (hit[e]) stabs.emplace_back(e, base + b * BPB + e * stepB);

            int distinctCap = (plan.melody01 < 0.3f) ? 1 : 2;
            int distinctUsed = 0, lastOff = 0;

            for (size_t si = 0; si < stabs.size(); ++si) {
                const int e = stabs[si].first;
                const double t = stabs[si].second;
                const double nextT = (si + 1 < stabs.size())
                                         ? stabs[si + 1].second
                                         : base + (b + 1) * BPB;
                const double maxLen = std::max(0.05, (nextT - t) * 0.9);
                const double len = std::min((mode == 1 ? 0.16 : 0.30) - 0.06 * agg, maxLen);

                int off = 0;
                if (si != 0 && distinctUsed < distinctCap
                    && r.chance(0.06 + 0.30 * plan.melody01 * plan.complexity01)) {
                    static const int devs[]  = {12, -12, 10, 6, 3}; // 8va,-8va,b7,b5,b3
                    static const double dw[]  = {1.7, 0.6, 0.8, 0.5, 0.6};
                    std::vector<double> dvw(dw, dw + 5);
                    off = devs[r.pickWeighted(dvw, 1.0)];
                    if (off != lastOff) ++distinctUsed;
                }
                lastOff = off;

                // TALK: the growl mod SWEEPS across the bar (slow raised-cosine over
                // the 4 beats) + a per-phrase bias, so the formant/filter MOVES like
                // the reference instead of a static chug — still a stab per hit, not
                // a wobble. Deterministic (function of bar position + phrase).
                const double barPos = (t - (base + b * BPB)) / BPB;      // 0..1 across bar
                const float sweep = 0.5f * (1.0f - std::cos(float(barPos) * 6.2831853f));
                const float phraseBias = 0.12f * float(phrase % 3);
                float artic = std::clamp(0.20f + 0.55f * sweep + phraseBias, 0.0f, 1.0f);
                if (r.chance(0.10)) artic = 0.95f; // occasional full-open sweep accent
                artic = std::min(1.0f, artic * (0.75f + 0.5f * pal.aggression01));

                const bool eOnBeat = (mode == 1) ? (e % 4 == 0) : (e % 2 == 0);
                float velBase = (e == 0) ? 0.98f : (eOnBeat ? 0.9f : 0.82f);
                velBase = std::min(1.0f, velBase + 0.05f * float(agg));
                add(Lane::BassA, t, len, bassPitch(off), hvel(r, velBase), artic);
            }

            // BassB = HORN/STAB layer, locked to the same hits on the same grid
            // (the two-tonal-bass architecture: growl + horn over the sub).
            for (int e = 0; e < steps; ++e) {
                if (!hit[e]) continue;
                const double t = base + b * BPB + e * stepB;
                const double len = std::min(mode == 1 ? 0.14 : 0.22, stepB * 0.85);
                int hoff = (r.chance(0.14 + 0.20 * plan.melody01)) ? 12 : 0;
                float hartic = std::min(1.0f, 0.55f + 0.40f * pal.aggression01);
                const bool eOnBeat = (mode == 1) ? (e % 4 == 0) : (e % 2 == 0);
                float hvelBase = eOnBeat ? 0.9f : 0.78f;
                add(Lane::BassB, t, len, bassPitch(hoff), hvel(r, hvelBase), hartic);
            }

            // BassC: rare octave accent on an off-beat when a 3rd voice exists.
            if (pal.bassVoices >= 3 && r.chance(0.22)) {
                const int slot = r.chance(0.5) ? 3 : 5;
                add(Lane::BassC, base + b * BPB + slot * 0.5, 0.20,
                    bassPitch(12), hvel(r, 0.6f), 0.5f);
            }
        }
        return;
    }

    // TRIPLET feel: kept for classic riddim, but forced OFF on tearout unless
    // aggression is genuinely low AND chaos is high (a rare shuffled tearout).
    const bool triplet0 = r.chance(0.22 + 0.18 * plan.chaos01);
    const bool triplet = tearout ? (triplet0 && agg < 0.5 && plan.chaos01 > 0.5f)
                                 : triplet0;

    // Bank: triplet -> 12-step; else blend toward the dense downbeat-anchored
    // TEAROUT bank as aggression rises (only draws in tearout, so non-drop
    // riddim keeps its exact original draw order). Otherwise the classic bank.
    const Archetype* bank; int bankN;
    if (triplet) { bank = kRiddim12Bank; bankN = kRiddim12BankN; }
    else if (tearout && r.chance(0.35 + 0.65 * agg)) { bank = kTearout16Bank; bankN = kTearout16BankN; }
    else { bank = kRiddim16Bank; bankN = kRiddim16BankN; }
    const int steps       = triplet ? 12 : 16;
    const double stepBeats = BPB / double(steps); // 0.25 (1/16) or 1/3 (triplet)

    // Per-pattern SWING (~55-68%): delay the off (odd) steps by a fraction of a
    // step to get the shuffle/triplet "bounce". Small, never overlaps the grid.
    // On tearout we force it STRAIGHT: lerp(0.53, 0.50, aggression) — dead-flat
    // at high aggression, barely-there at low (the stomp must land on the grid).
    double swing = r.range(0.55, 0.68);
    if (tearout) swing = 0.53 + (0.50 - 0.53) * agg;
    const double swingDelay = stepBeats * (2.0 * swing - 1.0);

    std::vector<double> w;
    for (int i = 0; i < bankN; ++i) w.push_back(bank[i].weight);
    int skIdx = r.pickWeighted(w, 0.4 + plan.chaos01);

    std::string callPat;   // the current 2-bar phrase's CALL pattern
    int stabCounter = 0;   // running stab index → alternating "talk" mod

    for (int b = 0; b < bars; ++b) {
        if (b < skipFirstBar) continue;
        // Hard skeleton swap at the mid-drop switch (kept from before).
        if (s.switchAt8 && b == 16)
            skIdx = (skIdx + 3) % bankN;

        const bool callBar = (b % 2 == 0);
        // Refresh the CALL pattern at the start of each 2-bar phrase.
        if (callBar || callPat.empty()) {
            callPat = bank[skIdx].pat;
            // Evolve the groove every 4 bars (mutate a few steps).
            if ((b % 4) == 0 && b > 0)
                for (int i = 0; i < steps; ++i)
                    if (r.chance(0.18)) callPat[i] = (callPat[i] == 'X') ? '.' : 'X';
        }

        // This bar's pattern: call = base; response = mutated answer with a
        // resolving pickup on the last step into the next phrase.
        std::string pat = callPat;
        Lane lane = callBar ? Lane::BassA : Lane::BassB;
        if (!callBar) {
            for (int i = steps / 2; i < steps; ++i)
                if (r.chance(0.30)) pat[i] = (pat[i] == 'X') ? '.' : 'X';
            pat[steps - 1] = 'X'; // lead-in pickup
        }
        // Downbeat: classic riddim clears it so the bass answers AROUND the
        // kick; tearout STOMPS on beat 1 locked with the kick+sub (the stomp).
        pat[0] = tearout ? 'X' : '.';

        // Precompute stab (step, start-time) pairs so lengths can be clamped to
        // the actual next onset — guarantees staccato, no overlapping starts.
        std::vector<std::pair<int, double>> stabs;
        for (int i = 0; i < steps; ++i) {
            if (pat[i] != 'X') continue;
            double t = base + b * BPB + i * stepBeats + ((i % 2) ? swingDelay : 0.0);
            stabs.emplace_back(i, t);
        }

        int distinctCap = (plan.melody01 < 0.3f) ? 1 : 2; // melodic restraint
        int distinctUsed = 0;
        int lastOff = 0;

        for (size_t si = 0; si < stabs.size(); ++si) {
            double t   = stabs[si].second;
            double nextT = (si + 1 < stabs.size()) ? stabs[si + 1].second
                                                   : base + (b + 1) * BPB;
            // Staccato: never overrun the next stab within this voice.
            double maxLen = std::max(0.06, (nextT - t) * 0.9);
            double len = std::min(r.range(0.12, 0.40), maxLen);

            // PITCH: mostly root; occasional octave jumps / dark scale moves.
            int off = 0;
            if (si != 0 && distinctUsed < distinctCap
                && r.chance(0.10 + 0.45 * plan.complexity01 * plan.melody01
                                 + 0.15 * plan.melody01)) {
                static const int devs[]  = {12, -12, 3, 6, 10}; // 8va, -8va, b3, b5, b7
                static const double dw[] = {1.6, 0.7, 0.9, 0.6, 0.7};
                std::vector<double> dvw(dw, dw + 5);
                off = devs[r.pickWeighted(dvw, 1.0)];
                if (off != lastOff) ++distinctUsed;
            }
            lastOff = off;
            int midi = bassPitch(off);

            // ARTICULATION ("talk"): alternate low/high per stab so consecutive
            // stabs sound different through the growl; occasional sweep accent.
            float artic = (stabCounter % 2 == 0) ? 0.22f : 0.60f;
            if (r.chance(0.14)) artic = 0.95f; // sweep accent
            artic = std::min(1.0f, artic * (0.65f + 0.6f * pal.aggression01));
            ++stabCounter;

            float velBase = callBar ? 0.90f : 0.82f;
            if (tearout) velBase = std::min(1.0f, velBase + 0.08f * float(agg));
            float vel = hvel(r, velBase);
            add(lane, t, len, midi, vel, artic);
        }

        // BassC: rare off-beat octave accent (only if a third voice exists).
        if (pal.bassVoices >= 3 && r.chance(0.28)) {
            int slot = triplet ? (r.chance(0.5) ? 4 : 10)
                               : (r.chance(0.5) ? 6 : 13);
            double t = base + b * BPB + slot * stepBeats
                       + ((slot % 2) ? swingDelay : 0.0);
            add(Lane::BassC, t, 0.22, bassPitch(12), hvel(r, 0.6f), 0.5f);
        }
    }
}

// SUB lane: riddim only. On the DROP the Sub LAYERS THE CHUG — it hits the exact
// same short stabs as BassA (the booming low octave under the mid square), so
// drums and chug coincide. Elsewhere: root on the downbeat, held into the pocket.
void Comp::addSubFollow(const Section& s, int skipFirstBar) {
    if (!riddim) return; // trap: 808 covers the sub, leave Sub empty
    const double base = s.startBar * BPB;
    const double agg = double(plan.aggression01);

    if (s.type == SectionType::Drop) {
        // BOOMING SUSTAINED sub: re-hit on each BEAT (half-beat at high
        // aggression) with LONG ringing notes so it's a booming TONE that rings
        // under the chug — NOT a percussive thump on every 16th (that read as a
        // "kick on every beat"). The square MID chug carries the fast rhythm;
        // the sub + kick own the low pulse, and the mix ducks the sub to the
        // kick so the low end PUMPS. Deterministic (no RNG).
        const int subDiv = (agg > 0.6) ? 2 : 1;      // per-beat, or per-half-beat when hard
        const double stepB = 1.0 / double(subDiv);
        // Leave a real GAP between sub hits so the low end PUMPS (swells then
        // fades to a gap), like the reference — not a continuous wall. Soft
        // attack keeps each hit a boom, not a kick thump.
        const double slen = stepB * 0.58;
        for (int b = 0; b < s.bars; ++b) {
            if (b < skipFirstBar) continue;
            const int hits = int(BPB) * subDiv;      // 4 or 8 per bar
            for (int k = 0; k < hits; ++k) {
                const double t = base + b * BPB + k * stepB;
                const double inBeat = k * stepB - std::floor(k * stepB);
                const bool onBeat = inBeat < 1e-6;
                const bool downbeat = (k == 0);
                const float v = downbeat ? 0.98f : (onBeat ? 0.9f : 0.8f);
                add(Lane::Sub, t, slen, rootSub, v, 0.12f);
            }
        }
        return;
    }

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
            // CHUG interlock: on riddim drops the drums lock UNDER the chug so
            // every beat/half-beat has a drum + a stab. Quieter "whack" kicks on
            // beats 2 & 4 (offsets 1.0/3.0), a medium reinforcing kick under the
            // beat-3 snare ("kick on 1 & 3"), and a ghost perc for groove.
            // Busier than before — near-constant whacks at high aggression.
            if (s.type == SectionType::Drop) {
                const double agg = double(plan.aggression01);
                // Kick anchors beat 1; the snare owns beat 3 (half-time). Only a
                // SUBTLE "whack" ghost on 2 & 4 (felt, not heard as a kick) so the
                // low end does NOT read as "kick on every beat".
                if (r.chance(0.4 + 0.25 * agg)) {
                    add(Lane::Kick, bb + 1.0, 0.18, rootSub, hvel(r, 0.30f, 0.03f));
                    add(Lane::Kick, bb + 3.0, 0.18, rootSub, hvel(r, 0.28f, 0.03f));
                }
                if (r.chance(0.35 + 0.30 * double(density)))
                    add(Lane::Perc, bb + 2.5, 0.16, 37, hvel(r, 0.42f, 0.05f), 0.5f);
            }
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
    // Per-section hat archetype: 0 busy-16ths, 1 offbeat-opens, 2 roll-accents,
    // 3 half-gap. Chosen once so the section keeps a consistent hat identity.
    const int hatArch = r.pickWeighted({1.1, 0.9, 0.9, 0.7}, 1.0);
    for (int i = 0; i < barCount; ++i) {
        int bar = startBar + i;
        double bb = bar * BPB;

        if (riddim) {
            // Busy 16th-grid riddim hats, density steered by the reference
            // profile and scaled by section energy (drops full, intros sparse).
            addRiddimHatBar(bb, r, s.energy, hatArch);
        } else {
            // Trap: 1/8 base plus roll bursts, shaped by the section archetype.
            const bool offbeatOpens = (hatArch == 1);
            const bool rollAccents  = (hatArch == 2);
            const bool halfGap      = (hatArch == 3);
            const double rollP = (0.10 + 0.35 * density) * (rollAccents ? 1.8 : 1.0);
            for (int e = 0; e < 8; ++e) {
                double t = mt(r, bb + e * 0.5);
                // half-gap: drop the closed hats on beats 1 & 3 (skeletal feel).
                const bool inGap = halfGap && (e == 0 || e == 4);
                if (!inGap)
                    add(Lane::HatClosed, t, 0.25, 42, hvel(r, 0.6f, 0.06f), 0.3f);
                // Roll burst.
                if (r.chance(rollP)) {
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
            // Open hats: offbeat-opens archetype scatters them across the offbeats.
            if (offbeatOpens) {
                for (double ob : {0.5, 1.5, 2.5, 3.5})
                    if (r.chance(0.55))
                        add(Lane::HatOpen, mt(r, bb + ob), 0.4, 46, hvel(r, 0.55f, 0.05f), 0.55f);
            } else if (r.chance(0.25)) {
                add(Lane::HatOpen, bb + 2.5, 0.4, 46, hvel(r, 0.55f, 0.05f), 0.5f);
            }
        }

        // Perc / tops when the section is energetic and offbeat.
        if (s.energy > 0.6f && r.chance(0.5)) {
            double t = mt(r, bb + (r.chance(0.5) ? 1.5 : 3.5));
            add(Lane::Perc, t, 0.2, 37, hvel(r, 0.5f, 0.06f), 0.5f);
        }
    }
}

// One bar of busy, 16th-based riddim closed hats with velocity variation and
// occasional gaps/rolls. Target events/beat comes from the active DrumProfile
// (~2.4-3.2 in drops), scaled down by section energy so builds/intros thin out.
// Deterministic: draws only from the passed section stream `r`.
void Comp::addRiddimHatBar(double bb, Rng& r, float energy, int archetype) {
    const float e = std::max(0.0f, std::min(1.0f, energy));
    const float target = DrumProfile::active().hatDensityPerBeat;   // ~2.8
    // Archetype reshapes density and roll/open behavior:
    //  0 busy-16ths, 1 offbeat-opens (thinner closed, richer opens),
    //  2 roll-accents (frequent rolls), 3 half-gap (skeletal, beats breathe).
    float densMul = 1.0f, rollBias = 0.0f, gapBias = 0.0f, openBias = 0.0f;
    switch (archetype) {
        case 1: densMul = 0.8f;  openBias = 0.35f; break;
        case 2: densMul = 1.05f; rollBias = 0.22f; break;
        case 3: densMul = 0.7f;  gapBias = 0.18f;  break;
        default: break;
    }
    const float perBeat = target * densMul * (0.30f + 0.82f * e);
    const float pbase = std::max(0.0f, std::min(0.98f, perBeat / 4.0f));

    for (int beat = 0; beat < 4; ++beat) {
        // Occasional whole-beat gap for breathing room (rarer at high energy).
        if (r.chance((0.10f + gapBias) * (1.2f - e))) continue;
        // half-gap archetype: leave beats 1 & 3 sparse for a skeletal pocket.
        if (archetype == 3 && (beat == 0 || beat == 2) && r.chance(0.5)) continue;
        // Occasional 16th roll fill on this beat (busier as energy rises).
        const bool roll = r.chance(0.10 + rollBias + 0.18 * e);
        for (int sub = 0; sub < 4; ++sub) {
            const int slot = beat * 4 + sub;             // 0..15
            const double t = mt(r, bb + beat + 0.25 * sub);
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
    // Open-hat offbeat accent (richer for the offbeat-opens archetype).
    if (r.chance(0.25 + openBias + 0.25 * e))
        add(Lane::HatOpen, mt(r, bb + 2.5), 0.4, 46, hvel(r, 0.55f, 0.05f), 0.6f);
    if (openBias > 0.0f && r.chance(0.35 + 0.25 * e))
        add(Lane::HatOpen, mt(r, bb + 0.5), 0.35, 46, hvel(r, 0.5f, 0.05f), 0.55f);
}

// FILL at end of an 8-bar phrase: last 2 beats of `bar`. Energy-matched to the
// section that follows so the fill ramps INTO the next section's intensity.
void Comp::addFill(int bar, Rng& r, float nextEnergy) {
    double bb = bar * BPB;
    double fillStart = bb + 2.0; // last 2 beats
    double roll = r.uniform();

    // Reverse-swell ear-candy: a short riser across the fill bar for an
    // uplifting sweep into whatever comes next (more likely for big energy).
    if (r.chance((riddim ? 0.10 : 0.22) + (riddim ? 0.15 : 0.30) * nextEnergy))
        add(Lane::Riser, bb, BPB, plan.rootMidi + 34,
            (riddim ? 0.35f : 0.55f) + (riddim ? 0.20f : 0.30f) * nextEnergy, 0.85f);

    if (roll < 0.22 * (0.5 + plan.chaos01)) {
        // Cut fill: remove all drum notes in the LAST beat (silence gap).
        double cutFrom = bb + 3.0, cutTo = bb + 4.0;
        for (Lane l : {Lane::Kick, Lane::Snare, Lane::HatClosed, Lane::HatOpen, Lane::Perc}) {
            auto& v = score.notes(l);
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Note& n) {
                return n.startBeat >= cutFrom - 1e-6 && n.startBeat < cutTo - 1e-6;
            }), v.end());
        }
    } else if (roll < 0.55) {
        // Accelerating snare roll: hit count + ramp matched to next energy, and
        // steps bunch toward the end (accelerando) for a rising push.
        int hits = 4 + int(std::lround(nextEnergy * 4.0)); // 4..8
        hits = std::min(9, std::max(4, hits));
        for (int h = 0; h < hits; ++h) {
            double frac = double(h) / double(hits);
            double t = fillStart + 2.0 * (frac * frac);
            float v = 0.55f + 0.40f * frac;
            add(Lane::Snare, t, 0.18, 38, hvel(r, v, 0.04f));
        }
    } else if (roll < 0.72) {
        // Tom/perc flourish descending across the last 2 beats.
        int n = 5 + int(std::lround(nextEnergy * 2.0));
        n = std::min(7, std::max(5, n));
        for (int h = 0; h < n; ++h)
            add(Lane::Perc, fillStart + (2.0 * h) / n, 0.18, 37,
                hvel(r, 0.5f + 0.3f * (float(h) / float(n)), 0.06f), 0.5f);
    } else if (roll < 0.86) {
        // Snare + off-beat hat combo build.
        for (int h = 0; h < 4; ++h) {
            add(Lane::Snare, fillStart + h * 0.5, 0.2, 38,
                hvel(r, 0.6f + 0.06f * h, 0.05f));
            add(Lane::HatClosed, fillStart + h * 0.5 + 0.25, 0.18, 42,
                hvel(r, 0.5f, 0.05f), 0.3f);
        }
    } else {
        // Reverse-swell / riser lift into the next section (subtler on riddim).
        add(Lane::Riser, fillStart, 2.0, plan.rootMidi + 36,
            hvel(r, riddim ? 0.5f : 0.7f, 0.04f), 0.8f);
        add(Lane::Perc, bb + 3.5, 0.2, 37, hvel(r, 0.5f, 0.05f), 0.5f);
    }

    // Impact landing on the downbeat right after the fill, for energetic
    // transitions (skipped for calm sections to avoid clutter).
    if (nextEnergy > 0.6f && r.chance(0.5))
        add(Lane::Impact, bb + BPB, 0.6, rootSub, 0.85f, 1.0f);
}

// ---------------------------------------------------------------------------
// MELODY & PAD
void Comp::addMotif(int barStart, int bars, Rng& r, bool echo) {
    if (plan.melody01 < 0.2f) return;
    // Register varies per track (melodyReg) plus a per-call octave lift.
    const int reg = plan.rootMidi + 24 + melodyReg + (r.chance(0.5) ? 12 : 0);
    // Build a 1-bar motif: constrained random walk, step bias, rests. Note
    // count scales with Melody Amount so busier melodies fill more of the bar.
    int nNotes = 2 + r.intRange(0, 1 + int(std::lround(plan.melody01 * 3.0f))); // 2..6
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
    // One of four progressions, chosen per track (breakProg). Degrees in
    // semitones from the minor tonic.
    static const int progs[4][4] = {
        {0, 8, 10, 5},   // i - VI - VII - iv
        {0, 5, 8, 10},   // i - iv - VI - VII
        {0, 10, 8, 3},   // i - VII - VI - III
        {0, 3, 5, 8},    // i - III - iv - VI
    };
    const int* prog = progs[breakProg & 3];
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

// Break/atmosphere chord pad with real VOICE-LEADING: chords share common
// tones (sustained, not re-struck) and only changed voices move. `intensity`
// (0..1, from melody01) adds extension tones and a 7th/9th color as it rises.
void Comp::addBreakPad(const Section& s, Rng& r, float intensity) {
    static const int progs[4][4] = {
        {0, 8, 10, 5},   // i - VI - VII - iv
        {0, 5, 8, 10},   // i - iv - VI - VII
        {0, 10, 8, 3},   // i - VII - VI - III
        {0, 3, 5, 8},    // i - III - iv - VI
    };
    const int* prog = progs[breakProg & 3];
    const int reg = plan.rootMidi + 12;
    const double clen = 2.0 * BPB; // a chord spans two bars

    // Build each chord as a small voicing (root, 3rd, 5th [, 7th]).
    struct Held { int midi; double startBeat; double endBeat; float vel; };
    std::vector<Held> held; // currently sounding voices (previous chord)

    const bool addSeventh = intensity > 0.38f;
    for (int b = 0; b < s.bars; b += 2) {
        int chordRoot = reg + prog[(b / 2) % 4];
        double bb = (s.startBar + b) * BPB;
        double endB = bb + clen;

        // Chord tones (minor triad + optional b7); voices as absolute midi.
        std::vector<int> voices = {chordRoot, chordRoot + 3, chordRoot + 7};
        if (addSeventh && r.chance(0.6)) voices.push_back(chordRoot + 10);
        if (intensity > 0.75f && r.chance(0.4)) voices.push_back(chordRoot + 14); // 9th color

        std::vector<Held> next;
        for (int vm : voices) {
            // Voice-leading: if a held voice already sounds this pitch, EXTEND it
            // (sustain the common tone) rather than re-striking.
            bool common = false;
            for (auto& h : held) {
                if (h.endBeat >= bb - 1e-6 && h.midi == vm) {
                    h.endBeat = endB;                 // sustain through this chord
                    next.push_back({vm, h.startBeat, endB, h.vel});
                    common = true;
                    break;
                }
            }
            if (!common) {
                float vel = hvel(r, (vm == chordRoot ? 0.5f : 0.42f), 0.03f);
                add(Lane::Pad, bb, clen, vm, vel, 0.3f);
                next.push_back({vm, bb, endB, vel});
            }
        }
        held = next;
    }
}

// TEXTURE layer: 1-2 slow-attack pad notes an octave up over the section
// (mod=high → the Pad renderer stretches its attack into a swell).
void Comp::addTexture(const Section& s, Rng& r, int count) {
    const int reg = plan.rootMidi + 24;
    const double span = s.bars * BPB;
    for (int i = 0; i < count; ++i) {
        // Spread across the section; scale-tone color (root / 5th / octave).
        static const int cols[] = {0, 7, 12, 3};
        int deg = cols[r.intRange(0, 3)];
        double start = (s.startBar) * BPB + (span * double(i)) / double(std::max(1, count));
        double len = std::min(span, 3.0 * BPB) + r.range(0.0, BPB);
        add(Lane::Pad, start, len, reg + deg, hvel(r, 0.32f, 0.03f),
            0.85f); // high mod = slow-attack texture swell
    }
}

// Sparse TONAL percussion (rim/blip on scale tones) for break motion.
void Comp::addTonalPerc(const Section& s, Rng& r, float density) {
    const int reg = plan.rootMidi + 24;
    for (int b = 0; b < s.bars; ++b) {
        if (!r.chance(double(density))) continue;
        double bb = (s.startBar + b) * BPB;
        double off = r.chance(0.5) ? 1.5 : 3.5;
        int deg = r.chance(0.5) ? 0 : (r.chance(0.5) ? 4 : 2);
        add(Lane::Perc, mt(r, bb + off), 0.2, scalePitch(reg, deg),
            hvel(r, 0.4f, 0.05f), 0.5f);
    }
}

// DROP TURNAROUND: last bar of a drop breathes — the melodic bass rests (Sub
// stays for continuity), a drum fill drives, and a micro-riser lifts into the
// next section. Applied as a post-pass so the rest of the drop is untouched.
void Comp::dropTurnaround(const Section& s, int idx, Rng& r) {
    if (s.bars < 2) return;
    const int lastBar = s.startBar + s.bars - 1;
    const double lb = lastBar * BPB;
    // Rest the melodic bass voices over the last two beats (the turnaround
    // window) so the fill + micro-riser breathe; keep the first half of the bar
    // and Sub intact for low-end glue and drop character.
    for (Lane l : {Lane::BassA, Lane::BassB, Lane::BassC}) {
        auto& v = score.notes(l);
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Note& n) {
            return n.startBeat >= lb + 2.0 - 1e-6 && n.startBeat < lb + BPB - 1e-6;
        }), v.end());
    }
    // Micro-riser (2-beat reverse swell) into the next section.
    float nextE = (idx + 1 < int(plan.sections.size()))
                      ? plan.sections[idx + 1].energy : 0.4f;
    addSwell(lb + 2.0, 2.0, 0.6f + 0.3f * nextE, true);
    // A tumbling perc/snare fill across the last two beats.
    int n = 4 + int(std::lround(nextE * 3.0));
    for (int h = 0; h < n; ++h) {
        double t = lb + 2.0 + (2.0 * h) / n;
        add(Lane::Perc, t, 0.18, 37,
            hvel(r, 0.45f + 0.35f * (float(h) / float(n)), 0.05f), 0.5f);
    }
}

// Is there any transition-FX note within ±halfWin beats of `beat`?
bool Comp::hasFxNear(double beat, double halfWin) const {
    for (Lane l : {Lane::Riser, Lane::Downlifter, Lane::Impact, Lane::Crash}) {
        for (const Note& n : score.notes(l))
            if (std::abs(n.startBeat - beat) <= halfWin) return true;
    }
    return false;
}

// A short reverse-riser swell (2-beat by default) — routed to the Riser lane
// with a short length so the synth renders its reverse-swell mode.
void Comp::addSwell(double startBeat, double lenBeats, float vel, bool rising) {
    // rising → riser (bright up-sweep); falling → downlifter dive.
    if (rising)
        add(Lane::Riser, startBeat, lenBeats, plan.rootMidi + 32, vel, 0.9f);
    else
        add(Lane::Downlifter, startBeat, lenBeats, plan.rootMidi + 26, vel, 0.8f);
}

// TRANSITIONS pass: guarantee ear-candy at every section boundary. Draws from a
// dedicated stream so it never perturbs per-section (drop) determinism.
void Comp::addTransitions() {
    Rng r = Rng(plan.params.seed).stream("composition").stream("transitions");
    for (size_t i = 1; i < plan.sections.size(); ++i) {
        const Section& prev = plan.sections[i - 1];
        const Section& cur = plan.sections[i];
        const double bnd = cur.startBar * BPB;
        const float dE = cur.energy - prev.energy;

        // Skip if a strong marker already sits on the boundary (drop entry,
        // build riser start, break downlifter, etc.) — just complement it.
        if (hasFxNear(bnd, 1.0)) {
            // Add a subtle 1-beat pre-boundary swell as extra glue occasionally
            // (sparser + subtler on riddim to cut cheese).
            if (r.chance(riddim ? 0.22 : 0.35))
                addSwell(bnd - 2.0, 2.0, riddim ? 0.4f : 0.5f, dE >= 0.0f);
            continue;
        }

        if (dE > 0.1f) {
            // Rising: reverse-riser swell + a soft impact landing (impact kept).
            addSwell(bnd - 2.0, 2.0,
                     (riddim ? 0.45f : 0.6f) + (riddim ? 0.15f : 0.25f) * cur.energy, true);
            if (r.chance(0.6))
                add(Lane::Impact, bnd, 0.8, rootSub, 0.8f, 1.0f);
        } else if (dE < -0.1f) {
            // Falling: downlifter dive + crash wash on the downbeat (subtler/
            // sparser on riddim).
            add(Lane::Downlifter, bnd, 2.0, plan.rootMidi + 26, riddim ? 0.55f : 0.75f, 0.8f);
            if (r.chance(riddim ? 0.25 : 0.5))
                add(Lane::Crash, bnd, 1.8, 49, riddim ? 0.45f : 0.7f, 0.5f);
        } else {
            // Flat: a light crash or swell to mark the seam (subtler on riddim).
            if (r.chance(0.5))
                add(Lane::Crash, bnd, 1.5, 49, riddim ? 0.42f : 0.6f, 0.5f);
            else
                addSwell(bnd - 2.0, 2.0, riddim ? 0.4f : 0.5f, true);
        }
    }
}

// ---------------------------------------------------------------------------
// SECTION COMPOSERS
void Comp::composeIntro(const Section& s, int idx, Rng& r) {
    (void)idx;
    const int half = std::max(1, s.bars / 2);
    const double bb0 = s.startBar * BPB;

    // RIDDIM: high-intensity TRIBAL / ORCHESTRAL tom buildup (overrides the
    // generic intro styles). Tuned resonant toms (Kick lane, pitched >80 Hz →
    // tom mode = timpani/bongo) drive a war-drum phrase that builds in density
    // and pitch, a deep timpani + kick anchoring the downbeat, light hats +
    // shaker, aggressive synth-horn pops in the back half, and an accelerating
    // tom roll slamming into the drop. No sustained sub/growl until the drop —
    // the low end SLAMS in only when the drop hits (ref: Seleman's intro).
    if (riddim) {
        const int bars = std::max(1, s.bars);
        static const int popDeg[] = {0, 0, 3, 0, 5, 3, 0, -2};   // horn-pop phrase
        static const int tomDeg[] = {0, 4, 2, 4, 0, 5, 4, 2};    // tuned-tom phrase
        auto tom = [&](int deg) { return scalePitch(rootMid + 12, deg); }; // ~150-260 Hz bongo/tom
        for (int b = 0; b < bars; ++b) {
            const double bb = (s.startBar + b) * BPB;
            const bool last = (b == bars - 1);
            const float prog = bars > 1 ? float(b) / float(bars - 1) : 1.0f; // 0..1 build

            if (!last) {
                // Deep timpani (low tom) + a kick for weight on the downbeat.
                add(Lane::Kick, bb + 0.0, 0.6, tom(0) - 12, hvel(r, 0.95f, 0.04f)); // low timpani
                add(Lane::Kick, bb + 0.0, 0.28, rootSub, hvel(r, 0.82f, 0.05f));    // sub weight
                if (r.chance(0.5 + 0.4f * prog))
                    add(Lane::Kick, bb + 2.0, 0.5, tom(2) - 12, hvel(r, 0.78f, 0.05f));

                // Driving tribal TOMS on the 8ths (tuned phrase), building fills.
                for (int e = 0; e < 8; ++e) {
                    const bool onBeat = (e % 2) == 0;
                    if (!onBeat && !r.chance(0.32 + 0.55f * prog)) continue;
                    add(Lane::Kick, mt(r, bb + e * 0.5), 0.42, tom(tomDeg[e & 7]),
                        hvel(r, (onBeat ? 0.82f : 0.55f) * (0.72f + 0.28f * prog), 0.05f));
                    // 16th roll fill after the beat as it builds.
                    if (onBeat && prog > 0.45f && r.chance(0.5f * prog))
                        add(Lane::Kick, bb + e * 0.5 + 0.25, 0.3,
                            tom(tomDeg[(e + 1) & 7]), hvel(r, 0.5f, 0.05f));
                }

                // Light hats + soft shaker (thinner than the drop's).
                addRiddimHatBar(bb, r, 0.4f + 0.4f * prog, 3);
                for (int sub = 1; sub < 16; sub += 2)
                    if (r.chance(0.30 + 0.30f * prog))
                        add(Lane::Perc, mt(r, bb + sub * 0.25), 0.11, 37,
                            hvel(r, 0.28f, 0.05f), 0.4f);          // shaker

                // Aggressive synth-horn POPS in the back half, building.
                if (prog > 0.30f) {
                    for (int e = 0; e < 8; ++e) {
                        if (!r.chance(0.18 + 0.55f * prog)) continue;
                        add(Lane::BassB, mt(r, bb + e * 0.5), 0.18,
                            scalePitch(rootMid, popDeg[e & 7]),
                            hvel(r, 0.55f + 0.35f * prog, 0.06f),
                            std::min(1.0f, 0.62f + 0.40f * prog));
                    }
                }
            } else {
                // LAST BAR: accelerating TOM roll (rising pitch + velocity) slam.
                for (int k = 0; k < 16; ++k) {
                    const float f = float(k) / 15.0f;
                    add(Lane::Kick, bb + k * 0.25, 0.28,
                        tom(tomDeg[k & 7]) + (k > 11 ? 5 : 0),
                        hvel(r, 0.45f + 0.50f * f, 0.04f));
                }
                addRiddimHatBar(bb, r, 1.0f, 2);
                add(Lane::Kick, bb + 0.0, 0.6, tom(0) - 12, hvel(r, 0.95f, 0.04f));
            }
        }
        // Rising riser across the whole buildup into the drop.
        add(Lane::Riser, bb0, double(bars) * BPB, plan.rootMidi + 34, 0.55f, 0.85f);
        add(Lane::Crash, double(s.startBar + bars) * BPB - 0.02, 1.6, 49, 0.6f, 0.5f);
        return;
    }

    switch (plan.params.introStyle) {
        case IntroStyle::Atmospheric: {
            // Evolving pad bed + texture; a long riser lifts into the first
            // build; sparse perc; drums only enter at the halfway point.
            addBreakPad(s, r, std::max(0.35f, plan.melody01));
            addTexture(s, r, 1 + (s.bars >= 12 ? 1 : 0));
            // Rising riser over the back half → energy into the first build.
            add(Lane::Riser, bb0 + double(half) * BPB, double(s.bars - half) * BPB,
                plan.rootMidi + 34, 0.6f, 0.85f);
            for (int b = 0; b < s.bars; ++b)
                if (r.chance(0.35))
                    add(Lane::Perc, mt(r, (s.startBar + b) * BPB + 2.5), 0.2, 37,
                        hvel(r, 0.4f, 0.05f), 0.5f);
            addDrums(s, r, 0.4f, true, true, s.startBar + half, s.bars - half);
            break;
        }
        case IntroStyle::Minimal: {
            // Dry drum groove (no snare glue) + a filtered one-bar bass teaser
            // per phrase: BassA root notes, low velocity, mod low = filtered.
            addDrums(s, r, 0.5f, false, true, s.startBar, s.bars);
            for (int b = 0; b < s.bars; ++b) {
                // One teaser bar every 4 bars: a couple of soft, dark stabs.
                if ((b % 4) != 3) continue;
                double bb = (s.startBar + b) * BPB;
                for (double off : {1.5, 2.5, 3.5})
                    if (r.chance(0.55))
                        add(Lane::BassA, mt(r, bb + off), 0.3, rootMid,
                            hvel(r, 0.45f, 0.04f), 0.2f); // filtered teaser
            }
            break;
        }
        case IntroStyle::VocalChop: {
            // Rhythmic MelodyLead chop pattern — short notes on a 1/8 grid with
            // per-hit mod variation (formant color). Our closest thing to vocal
            // chops: lean in. Drums enter halfway.
            const int reg = plan.rootMidi + 24 + melodyReg;
            static const int chopDegs[] = {0, 0, 2, 0, 4, 2, 0, -3};
            for (int b = 0; b < s.bars; ++b) {
                double bb = (s.startBar + b) * BPB;
                // Rotate the chop phrase each bar for call/response feel.
                int rot = (b % 2) ? 2 : 0;
                for (int e = 0; e < 8; ++e) {
                    if (!r.chance(0.55 + 0.2 * plan.melody01)) continue;
                    int deg = chopDegs[(e + rot) & 7];
                    double len = r.chance(0.5) ? 0.25 : 0.5;
                    float col = 0.2f + 0.7f * float(r.uniform()); // formant color
                    add(Lane::Melody, mt(r, bb + e * 0.5), len, scalePitch(reg, deg),
                        hvel(r, 0.6f, 0.06f), col);
                }
            }
            addDrums(s, r, 0.45f, true, true, s.startBar + half, s.bars - half);
            break;
        }
        case IntroStyle::Impact: {
            // Cold open: a single Impact then an immediate half-groove (drums +
            // bass but thinned) — no long ramp, straight into motion.
            add(Lane::Impact, bb0, 1.0, rootSub, 1.0f, 1.0f);
            add(Lane::Crash, bb0, 1.8, 49, 0.75f, 0.5f);
            addDrums(s, r, 0.7f, true, true, s.startBar, s.bars); // half-groove w/ snare
            addBass(s, r, 0);
            addSubFollow(s, 0);
            break;
        }
        case IntroStyle::Fakeout: {
            // Build-like tension from bar 1: full-length riser + snare-roll
            // thickening toward the end + thinning kick.
            add(Lane::Riser, bb0, s.bars * BPB, plan.rootMidi + 36, 0.8f, 0.85f);
            const int rollStart = std::max(0, s.bars - 3);
            for (int b = 0; b < s.bars; ++b) {
                double bb = (s.startBar + b) * BPB;
                if (b < s.bars - 2 || r.chance(0.5))
                    add(Lane::Kick, bb, 0.5, rootSub, hvel(r, 0.9f, 0.05f));
                add(Lane::Snare, bb + 2.0, 0.5, 38, hvel(r, 0.85f));
                if (b >= rollStart) {
                    int stage = b - rollStart;
                    double step = 1.0 / double(1 << stage);
                    int n = int(std::lround(BPB / step));
                    for (int k = 0; k < n; ++k)
                        add(Lane::Snare, bb + k * step, step * 0.9, 38,
                            hvel(r, 0.5f + 0.4f * (float(k) / float(std::max(1, n))), 0.04f));
                } else if (riddim) {
                    addRiddimHatBar(bb, r, s.energy);
                } else {
                    for (int beat = 0; beat < 4; ++beat)
                        if (r.chance(0.5))
                            add(Lane::HatClosed, bb + beat + 0.5, 0.25, 42,
                                hvel(r, 0.55f), 0.3f);
                }
            }
            break;
        }
    }
}

void Comp::composeBuild(const Section& s, int idx, Rng& r) {
    (void)idx;
    const int bars = s.bars;
    const double bb0 = s.startBar * BPB;
    // Riser spanning the whole build.
    add(Lane::Riser, bb0, bars * BPB, plan.rootMidi + 36, 0.85f, 0.9f);
    // Extra short, accelerating riser over the final 1-2 bars for a stronger
    // pre-drop lift (higher pitch + max mod = a faster, brighter sweep).
    if (bars >= 2) {
        int lb = std::max(0, bars - (r.chance(0.5) ? 2 : 1));
        add(Lane::Riser, (s.startBar + lb) * BPB, (bars - lb) * BPB,
            plan.rootMidi + 40, 0.95f, 1.0f);
    }

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
        // 1 bar of tension before the real drop, style varied per drop:
        // 0 = pure silence gap, 1 = vocal-ish stab, 2 = downlifter dive.
        const double fb = s.startBar * BPB;
        const int fakeStyle = r.pickWeighted({0.9, 1.0, 0.9}, 1.0);
        if (fakeStyle == 1) {
            add(Lane::Melody, fb, 1.0, plan.rootMidi + 24, 0.75f, 0.6f);
            add(Lane::Impact, fb, 0.5, rootSub, 0.9f, 1.0f);
        } else if (fakeStyle == 2) {
            add(Lane::Downlifter, fb, 1.0 * BPB, plan.rootMidi + 24, 0.85f, 0.8f);
            add(Lane::Impact, fb, 0.5, rootSub, 0.85f, 1.0f);
        } else {
            // silence: just a soft impact tail, then the drop hits.
            add(Lane::Impact, fb, 0.5, rootSub, 0.7f, 0.9f);
        }
    }

    // FX: impact + crash at the (real) drop entry.
    double entry = (s.startBar + skip) * BPB;

    // PRE-DROP SILENCE: cut the tail of the preceding build for a punchy gap so
    // the drop lands harder (skipped for the fakeout, which has its own gap).
    if (!fakeout && idx > 0 && plan.sections[idx - 1].type == SectionType::Build
        && r.chance(0.7)) {
        double gap = r.chance(0.5) ? 1.0 : 0.5;
        double cutFrom = entry - gap, cutTo = entry;
        for (Lane l : {Lane::Kick, Lane::Snare, Lane::HatClosed, Lane::HatOpen,
                       Lane::Perc, Lane::Sub, Lane::BassA, Lane::BassB, Lane::BassC}) {
            auto& v = score.notes(l);
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Note& n) {
                return n.startBeat >= cutFrom - 1e-6 && n.startBeat < cutTo - 1e-6;
            }), v.end());
        }
    }

    add(Lane::Impact, entry, 1.0, rootSub, 1.0f, 1.0f);
    // Keep the impact punchy; the crash is kept SUBTLE on riddim (lower velocity)
    // so the drop entry isn't cheesy — the chug + drums carry it.
    add(Lane::Crash, entry, 2.0, 49, riddim ? 0.5f : 0.9f, 0.5f);
    // TRANSITION sweep: a short falling downlifter tail smoothing into the drop.
    // Sparser + subtler on riddim.
    if (r.chance(riddim ? 0.22 : 0.5))
        add(Lane::Downlifter, entry, 1.5, plan.rootMidi + 30, riddim ? 0.45f : 0.7f, 0.8f);

    // Full bass + drums. TEAROUT: tighten timing drift toward 0 as aggression
    // rises so the stomp stays LOCKED (bass stabs are already grid-exact; this
    // also tightens the drop's hats). Restored right after so the melody/motif
    // and every other section keep their normal humanized feel. Riddim only —
    // trap drops are untouched.
    const float savedHumMul = humTimeMul;
    if (riddim) humTimeMul = std::max(0.0f, 1.0f - 0.9f * plan.aggression01);
    addBass(s, r, skip);
    addSubFollow(s, skip);
    addDrums(s, r, 0.9f, true, true, s.startBar + skip, s.bars - skip);
    humTimeMul = savedHumMul;

    // Melody stabs shadowing the drop when the budget allows.
    if (plan.melody01 > 0.5f)
        addMotif(s.startBar + skip, s.bars - skip, r, false);

    // Impact + crash again at the mid-drop switch, with a lead-in riser sweep.
    if (s.switchAt8 && s.bars > 16) {
        double sw = (s.startBar + 16) * BPB;
        add(Lane::Crash, sw, 2.0, 49, riddim ? 0.5f : 0.85f, 0.5f);
        add(Lane::Impact, sw, 0.8, rootSub, 0.9f, 1.0f);
        if (r.chance(riddim ? 0.22 : 0.5))
            add(Lane::Riser, sw - BPB, BPB, plan.rootMidi + 38, riddim ? 0.5f : 0.8f, 0.9f);
    }

    // Fills at the end of every 8-bar phrase, plus a crash marking the new
    // phrase that follows a big fill.
    for (int b = 7; b < s.bars; b += 8) {
        float nextE = (idx + 1 < int(plan.sections.size()))
                          ? plan.sections[idx + 1].energy : 0.5f;
        addFill(s.startBar + b, r, nextE);
        if (b + 1 < s.bars && r.chance(riddim ? 0.3 : 0.6))
            add(Lane::Crash, (s.startBar + b + 1) * BPB, 1.5, 49, riddim ? 0.5f : 0.8f, 0.5f);
    }

    // --- Post-pass ear-candy (drawn AFTER all drop content so the drop pattern
    //     itself is bit-identical to before). ---
    // Pre-drop "breath": a 1-beat all-lanes rest right before a non-fakeout
    // drop, so it lands harder (prob 0.35). The fakeout has its own gap.
    if (!fakeout && r.chance(0.35)) {
        double cutFrom = entry - 1.0, cutTo = entry;
        for (Lane l : {Lane::Kick, Lane::Snare, Lane::HatClosed, Lane::HatOpen,
                       Lane::Perc, Lane::Sub, Lane::BassA, Lane::BassB, Lane::BassC}) {
            auto& v = score.notes(l);
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Note& n) {
                return n.startBeat >= cutFrom - 1e-6 && n.startBeat < cutTo - 1e-6;
            }), v.end());
        }
    }
    // Last-bar turnaround into the next section.
    dropTurnaround(s, idx, r);
}

void Comp::composeBreak(const Section& s, int idx, Rng& r) {
    // No sub/bass — this is the song's breathing room. The material scales with
    // Melody Amount from an atmospheric drone up to a full topline + pad.
    const float m = plan.melody01;

    if (m < 0.2f) {
        // ATMOSPHERIC (but never empty): a slow moving pad drone + texture and a
        // whisper of tonal perc. Still has motion via the pad progression.
        addBreakPad(s, r, 0.35f);
        addTexture(s, r, 1 + (s.bars >= 12 ? 1 : 0));
        addTonalPerc(s, r, 0.35f);
    } else {
        // Voice-led chord progression that actually moves + a call/response
        // motif (quieter echo) + texture + sparse tonal percussion.
        addBreakPad(s, r, m);
        addTexture(s, r, 1 + (m > 0.5f ? 1 : 0));
        addMotif(s.startBar, s.bars, r, true);
        addTonalPerc(s, r, 0.3f + 0.4f * m);
    }

    // Sparse halftime drums (snare kept on 2) — a little busier as energy rises.
    for (int b = 0; b < s.bars; ++b) {
        int bar = s.startBar + b;
        double bb = bar * BPB;
        if (r.chance(0.55 + 0.2 * s.energy)) add(Lane::Kick, bb, 0.5, rootSub, hvel(r, 0.8f));
        add(Lane::Snare, bb + 2.0, 0.5, 38, hvel(r, 0.7f));
        if (r.chance(0.4))
            add(Lane::HatClosed, mt(r, bb + 2.5), 0.25, 42, hvel(r, 0.5f), 0.4f);
    }

    // Downlifter into the break (after the preceding drop).
    if (idx > 0 && plan.sections[idx - 1].type == SectionType::Drop)
        add(Lane::Downlifter, s.startBar * BPB, 2.0 * BPB, plan.rootMidi + 24,
            0.8f, 0.7f);
}

void Comp::composeOutro(const Section& s, int idx, Rng& r) {
    // Element-by-element removal every 4 bars, resolving to a held root chord +
    // sub note that fade with the tail.
    addBreakPad(s, r, std::max(0.35f, plan.melody01 * 0.8f));
    addTexture(s, r, 1);
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
            add(Lane::HatClosed, mt(r, bb + 2.5), 0.25, 42, hvel(r, 0.45f), 0.4f);
    }

    // FINAL TONAL RESOLUTION: a root minor pad chord + sub fundamental over the
    // last four bars, ringing out into the tail.
    const int resBars = std::min(s.bars, 4);
    const double rb = (s.startBar + s.bars - resBars) * BPB;
    const double rlen = double(resBars) * BPB;
    const int reg = plan.rootMidi + 12;
    add(Lane::Pad, rb, rlen, reg, hvel(r, 0.5f, 0.02f), 0.6f);      // root
    add(Lane::Pad, rb, rlen, reg + 3, hvel(r, 0.44f, 0.02f), 0.6f); // b3
    add(Lane::Pad, rb, rlen, reg + 7, hvel(r, 0.42f, 0.02f), 0.6f); // 5th
    // Gentle sub that fades with the tail (kept low so it never competes with
    // the drops for loudness — it's a resolution, not a new low-end event).
    add(Lane::Sub, rb, 2.0, rootSub, 0.4f, 0.2f);                   // fading sub
    // Downlifter into the resolution when the outro follows a drop.
    if (idx > 0 && plan.sections[idx - 1].type == SectionType::Drop)
        add(Lane::Downlifter, s.startBar * BPB, 2.0 * BPB, plan.rootMidi + 24,
            0.75f, 0.7f);
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
                case SectionType::Intro: {
                    if (plan.params.introStyle == IntroStyle::Atmospheric)
                        bs[b] = 0.65f;
                    // Ramp shape seeded per track: linear vs exponential swell.
                    const double shaped = introRampExp ? (frac * frac) : frac;
                    en[b] = float(0.25f + 0.2f * shaped); // ramp 0.25 -> 0.45
                    break;
                }
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
    comp.addTransitions();   // ear-candy at every section boundary
    comp.buildCurves();
    return score;
}

} // namespace rtg
