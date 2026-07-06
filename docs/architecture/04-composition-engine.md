# 04 — Composition Engine (Music Theory & Structure)

The autonomous composer for modern Riddim and Trap. **Everything here is
algorithmic and deterministic** — grammars, rulebooks, weighted choices from
seeded PRNG streams. **No LLM composes anything.** Genre knowledge lives in
data files (`engine/decision/data/*.json`, `engine/composition/…`) so it can
be tuned without code changes.

Input: `GenerationPlan` (from the Decision Engine, doc 10).
Output: `Score` — a symbolic, fully-specified song: section map, per-track
patterns, automation curves, transition events.

---

## 1. Musical timebase

- Grid: bars of 4/4 subdivided to 1/32 + triplet lanes (trap hat rolls).
- Riddim: 140–150 BPM, minor keys (E, F, F#, G favored for sub range),
  halftime feel — snare on beat 3.
- Trap: 130–170 BPM notated (or 65–85 halftime), 808-centric, snare on 3.
- Key selection: root chosen so the sub fundamental sits ~35–55 Hz.

## 2. Arrangement: section grammar

Songs are derived from a **stochastic context-free grammar** whose
productions are weighted by plan parameters (length, drop count, intro
style, energy curve).

```
SONG      → INTRO BLOCK+ OUTRO
BLOCK     → BUILD DROPSET BREAK?
DROPSET   → DROP (SWITCH DROP)*
DROP      → drop16 | drop8 drop8 | drop8 FAKEOUT drop8
```

Canonical realized forms (bars):

- **Riddim, 2 drops:** Intro 16 → Build 8 → Drop A 32 (16+switch+16) →
  Break 16 → Build 8 → Drop B 32 → Outro 8–16.
- **Trap, 2 drops:** Intro 8 → Verse/Build 16 → Drop (hook) 16 → Break 16 →
  Build 8 → Drop 2 (variation) 16–32 → Outro 8.

Grammar expansion is constrained to hit the requested song length within
±one section, then section lengths are snapped to 4/8/16-bar units.

### Structural devices (each an explicit, parameterized event type)

| Device | Rule |
|---|---|
| **Drop** | Highest-energy section; first drop lands 25–40% into the track. |
| **Fakeout** | Build resolves to 1 bar of silence/vocal stab instead of the drop, then the real drop hits 1–2 bars later. Probability rises with Chaos; max one per song at Chaos < 70. |
| **Switch** | Mid-drop palette/pattern change at bar 16 (or 8): new bass set, altered rhythm, same energy. Drop B must switch relative to Drop A. |
| **Build** | 8 (or 16) bars: riser + snare-roll density doubling (1/4→1/8→1/16→1/32 over last 4 bars) + low-cut sweep + drum thinning before impact. |
| **Break** | Energy valley: melodic/atmospheric material, no sub, halftime or no drums; where Melody Amount is mostly spent. |
| **Fills** | Last 1–2 beats of every 4/8-bar phrase: drum fill, vocal chop, reverse crash, or silence-gap ("cut fill" — riddim's favorite). |
| **Intro pacing** | From Intro Style enum: Atmospheric (pads+FX, drums enter at 50%), Minimal (dry drums immediately), Vocal-chop, Impact (cold-open near-drop), Fakeout intro. |
| **Outro** | Mirrors intro palette; element-by-element removal every 4 bars; ends on filtered loop or FX tail. DJ-friendly 8–16 bars when length allows. |

## 3. Energy curve

The Decision Engine supplies a target curve `E(t) ∈ [0,1]` (piecewise over
sections: intro ramp, build ascents, drop plateaus at ~0.9–1.0, break
valleys at ~0.3). The composer treats it as a constraint: each section's
**element count, drum density, bass activity, and FX rate** are monotone
functions of `E`. Verification pass: predicted energy (weighted element sum)
must track the target within tolerance, else elements are added/removed.

## 4. Rhythm generation

### 4.1 Bass rhythm (the heart of riddim)
Riddim bass is **call-and-response over a halftime grid**:

1. Choose a 1-bar rhythmic skeleton from a weighted pattern bank
   (e.g. `X.X. ..X. X.X. ....` on 1/8s) — banks are genre data files.
2. Assign slots alternately to **Voice A (call)** and **Voice B (response)**
   — two different bass sounds trading phrases (2-beat or 1-bar alternation).
3. Pitch: mostly root; deviations (b3, b5, octave) with probability ∝
   Complexity; riddim melodic restraint keeps ≤2 distinct pitches per bar at
   low Melody Amount.
4. Variation schedule: bars 1–4 literal, bar 8 adds a pickup, bar 16 mutates
   the response slots (see §6). Switches replace the skeleton entirely.
5. Articulation lane: per-note pitch-bend dives, formant/wah automation
   (rendered by sound design macros), and rest-gaps — silence is treated as
   a first-class rhythmic value.

Trap 808s instead use: long glide notes on a sparse grid, root–b7–b6 moves,
occasional 1/16 double-hits, tuned slides into downbeats.

### 4.2 Drum generation & variation
Per-lane pattern synthesis over the section grid:

- **Kick/snare:** genre archetype (riddim: kick on 1, snare on 3; trap:
  syncopated kick clusters avoiding the snare) + Complexity-weighted
  ornaments (kick pickups, ghost snares).
- **Hats:** base 1/8 or 1/16 lane; trap adds roll bursts (1/32, triplet)
  with velocity ramps; riddim keeps hats sparse and offbeat-open.
- **Percs/tops:** offbeat shaker/rim lanes activated by Energy.
- **Variation operators** applied on phrase boundaries: rotate, thin,
  densify, swap-voice, ghost-add, drop-a-beat. Each drum lane mutates every
  4–8 bars (probability ∝ Complexity) so no 8 bars are identical.
- **Fills:** rule-scheduled (§2) from fill archetypes, intensity-matched to
  the next section's energy.

Humanization: velocity jitter and micro-timing (±3–8 ms, seeded) scaled by
genre (tight riddim, looser trap hats). Swing parameter for trap.

## 5. Melody, harmony, atmosphere (restraint model)

Riddim/trap are riff music, not chord music — **Melody Amount** gates a
budget system:

- 0–20: no melodic content; atmosphere = single drone/noise bed.
- 20–50: 2–4 note motif in intro/break only (pluck, bell, vocal chop),
  pentatonic minor / phrygian, 1-bar motif with call-and-response echo.
- 50–80: motif also shadows drops (sparse top-line stabs); breaks get a
  chord pad (i–VI–VII or i–iv loop).
- 80–100: full break topline + countermelody, arps in builds.

Motif generation: constrained random walk over the scale (step bias,
leap penalty, rest probability from restraint budget), rhythm drawn from the
genre's motif-rhythm bank, then motif development by transposition,
inversion, and truncation — never free composition. Darkness biases scale
(natural minor → phrygian) and register downward.

## 6. Call-and-response engine (general)

A reusable primitive used by bass, drums, melody, and FX:
`respond(phrase, mode)` where mode ∈ {echo, invert, complement (fill the
rests), truncate+answer, timbre-swap}. The composer guarantees at least one
call/response relationship is active in every drop (bass A↔B) and every
break with melody (motif↔echo).

## 7. Automation & transitions

The composer emits **symbolic automation curves** consumed by sound design
and mix: filter sweeps in builds (log ramps), riser pitch curves, drop
low-cut releases, break high-cut "underwater" moves, sidechain intensity per
section, FX send swells at section tails. Transition events (impact, crash,
downlifter, sub-drop, silence-gap) are scheduled at every boundary with
type chosen by (from-energy → to-energy) rules — e.g. high→low uses
downlifter+tail, low→high uses riser+snare roll+impact.

## 8. Determinism & testability

Every choice draws from named PRNG streams
(`composition.arrangement`, `composition.drums.section[i]`, …). Unit tests
assert grammar-legal structures for 10k seeds; golden tests pin exact
Scores for reference seeds; property tests verify constraints (snare always
on 3 in halftime, drops match Drop Count, length within tolerance, melodic
budget respected).
