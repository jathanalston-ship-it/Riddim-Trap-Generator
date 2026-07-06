# 10 — AI Decision Engine

The software contains **no conversational AI**. In its place: a Decision
Engine — the "producer brain" that converts 12 user parameters + a seed into
every automatic choice a professional producer would make. It is a layered
system of **rulebooks, weighted stochastic sampling, and constraint
satisfaction**, emulating producer judgment explicitly rather than
statistically.

## 1. Position in the pipeline

```
User params + seed ─► DECISION ENGINE ─► GenerationPlan ─► (composition,
                                                sound design, mix, master
                                                all *execute* the plan)
```

Downstream engines make no taste decisions; they realize the plan. This
keeps "why does it sound like this?" answerable in one place, and makes the
plan itself inspectable (History page) and re-rollable in parts.

## 2. Anatomy of a GenerationPlan

```
GenerationPlan {
  global:      genre, bpm, key/root, length, seed tree
  energyCurve: E(t) target per section
  structure:   section intents (count, types, drop/fakeout/switch slots)
  palette:     per-role sound constraints + anchors + novelty quota
  drums:       density/complexity envelope, fill budget, humanize profile
  bass:        voice count, call/response scheme, articulation intensity
  melody:      budget + placement (from Melody Amount)
  fx:          transition budget per boundary type
  automation:  macro intents per section (talk amount, filter moves…)
  mix/master:  aggression/darkness biases, contrast targets, profiles
}
```

## 3. Layer 1 — Parameter interpretation (deterministic mapping)

Each user knob maps to many internal quantities via calibrated curves
(data-defined, per genre):

| User param | Drives (examples) |
|---|---|
| Energy | energy-curve ceiling/valleys, element counts, drum bus parallel comp blend, FX rate |
| Aggression | distortion/OTT depth, sound-selection aggression range, snare character, clip drive bias |
| Darkness | key/scale bias (aeolian→phrygian), register, EQ tilt bias, reverb damping |
| Complexity | pattern ornament probability, variation operator rate, section grammar richness |
| Melody Amount | melodic budget system (doc 04 §5), break richness |
| Chaos | sampling temperature everywhere, fakeout probability, novelty quota, mutation size |
| Drop Count / Length / Intro Style | structure grammar constraints |
| BPM / Genre | timebase, rulebook selection, palette priors |

Interactions are resolved by explicit rules, not accident: e.g.
`high Darkness + low Aggression → "deep/minimal" profile` (dark palette but
clean dynamics), `high Chaos + low Complexity → wild sounds, simple rhythms`.

## 4. Layer 2 — Producer heuristics (the rulebook)

Encoded professional judgment, as declarative rules with weights
(`decision/data/riddim.json`, `trap.json`). Samples:

- **Drop placement:** first drop at 25–40% of runtime; never two identical
  drops — Drop B must switch palette or rhythm skeleton; with 3+ drops, one
  must be a "variation drop" (thinner, weirder) to avoid fatigue.
- **Bass choice:** each drop gets a palette of 2–4 mid-bass voices +1 sub;
  voices must be mutually spectrally complementary (embedding + band-energy
  check) and within the plan's aggression window; palette anchored on one
  high-fitness library asset, others selected by "similar but not
  redundant" (vector distance band), novelty quota filled by fresh
  synthesis when Chaos demands.
- **Drum density:** base density from Energy, modulated per section type
  (build ↑ rolls, break ↓ halftime), bounded by genre rules (riddim drops
  stay minimal — density spent on bass rhythm instead).
- **Transition timing:** every boundary gets a transition whose type is
  keyed by (fromEnergy → toEnergy) and Chaos (doc 04 §7); budget caps total
  FX events so tracks don't smear.
- **Automation:** macro intents per section — e.g. drops get "talk"
  articulation on bass voice A with intensity ∝ Aggression; builds always
  automate filter + riser pitch; breaks get width + reverb swell.
- **Variation & randomization:** a global "no dead repetition" rule — any
  8 consecutive bars must differ in at least one lane; variation operators
  are scheduled to satisfy it (rate ∝ Complexity).

## 5. Layer 3 — Weighted stochastic sampling

Where multiple options survive the rules, the engine samples from weighted
distributions using its named PRNG stream — weights from the rulebook,
**temperature from Chaos** (Chaos 0 ⇒ argmax "safe producer"; Chaos 100 ⇒
near-uniform "mad scientist"). This is why two seeds with identical knobs
produce sibling tracks, not clones — and why identical seeds reproduce
exactly.

## 6. Layer 4 — Constraint solver & self-check

Sampled plans are validated against hard constraints (length tolerance,
drop count, energy-curve monotonicity into drops, palette spectral
coverage: sub+mid+high all occupied, melodic budget ≤ cap). Violations
trigger bounded local repair (re-sample the offending slot, ≤ N attempts,
then fall back to the highest-weight legal option). The final plan is
guaranteed legal before composition starts.

## 7. Feedback (how the "producer" improves)

The engine's *rules* are static per release, but its *choices* improve
because choice inputs improve: library fitness/popularity (doc 06) re-rank
sound selection; per-user statistics (kept tracks' parameter regions,
re-rolled sections) nudge default weights within a bounded band (±15%) —
personalization without ML training, fully local, resettable in Settings.

## 8. Why not an LLM

Every decision above is a choice among enumerable options under explicit
constraints — the natural home of rulebooks + weighted sampling. This gives
millisecond planning cost, exact reproducibility, inspectable "why"
(every decision records its rule and weights into the plan), and offline
operation. An LLM would add latency, nondeterminism, and inference cost
while being *worse* at honoring hard musical constraints. If future
versions want richer taste, the upgrade path is learned *weights*
(tiny ONNX preference model re-ranking candidate plans), never generative
text-in-the-loop.
