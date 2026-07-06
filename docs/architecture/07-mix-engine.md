# 07 — Mix Engine (Autonomous Mixing Engineer)

A deterministic, measurement-driven mixing system targeting modern riddim
(and trap) release standards. **No machine learning in the mix path** — the
domain is well-specified by meters and genre conventions; classic DSP plus a
feedback loop outperforms a learned black box here and stays debuggable.
(ML appears only upstream, in sound rating.)

## 1. Bus architecture

```
tracks → SUB bus ──────────────┐
         BASS bus (mid growls) ├─► DRUM/BASS group ─┐
         DRUMS bus (kick/snare/hats/percs)──────────┤
         MUSIC bus (melody/chords/atmos) ───────────┼─► MIX BUS → (Master, doc 08)
         FX bus (risers/impacts/transitions) ───────┤
         SENDS: reverb A/B, delay, width ───────────┘
```

Fixed topology, genre-templated processing per bus. Templates live in
`engine/mix/data/` and encode target levels, EQ intents, and dynamics
settings per genre; the engine adapts them per track via measurement.

## 2. Stage order (per generation)

### 2.1 Gain staging
Every stem normalized to a per-role target (RMS/short-term LUFS): e.g.
kick −10 LUFS-S, sub −12, growls −13, hats −20, atmos −26. Mix bus input
targets **−6 dB peak headroom** pre-master. All later stages assume staged
levels, which makes their thresholds meaningful and deterministic.

### 2.2 Spectral slotting (EQ)
Rule-based static EQ per role: sub HP 25 Hz / LP 120 Hz, mono; growls HP at
100–120 Hz (they must never fight the sub — riddim's cardinal rule); kick
gets its 50–60 Hz thump only if the sub pattern leaves room, else tightened
to a 100 Hz knock; hats HP 300+; atmos wide but HP 200 with 2–5 kHz shelf
cut to clear screech space.
Then a **masking resolver**: pairwise spectral overlap is measured on real
stems (short-time band energy correlation); where two roles collide beyond
threshold, complementary dynamic cuts are applied to the lower-priority
role (priority order: sub > kick > snare > growl > screech > melody >
atmos). This is deterministic "EQ carving," not ML.

### 2.3 Compression & OTT
- Per-track compressor settings from role templates (drums: fast attack for
  control or slow for punch, chosen by measured transient strength).
- **OTT-style multiband up/down compression** on mid-bass and screech buses
  — depth scaled by Aggression (30–60% typical riddim). Applied on the
  *bus*, post-carve, so it doesn't reinflate masked regions.
- Parallel ("New York") compression on the drum bus, blend ∝ Energy.

### 2.4 Distortion & color
Bus saturation stages (tape/soft-clip) add density: drive amounts mapped
from Aggression/Darkness, always gain-compensated (equal-loudness matched
via quick LUFS null test so "drive" never masquerades as "louder").

### 2.5 Stereo imaging
Mono below 120 Hz enforced globally (elliptical filter on the mix bus).
Width by role: sub/kick mono; growls narrow (haas-free, ~20%); screeches
and FX wide (60–90%); atmos widest. Correlation meter must stay > 0 on the
mix bus — width is automatically reduced if mono-compatibility fails.

### 2.6 Sidechain & ducking matrix
Data-driven matrix (who ducks whom, how hard):

| Trigger → Target | Depth | Shape |
|---|---|---|
| Kick → Sub | 3–6 dB | fast, 60–90 ms release (tempo-scaled) |
| Kick → Bass bus | 2–4 dB | slightly slower |
| Snare → Screech/tops | 1–3 dB | keeps snare crack audible |
| Riser → full mix (pre-drop) | up to 2 dB swell-duck | musical pump |
| Voice/lead (if present) → Music bus | 2 dB | dynamic slot |

Implemented as envelope-follower ducking (not pumping-for-effect unless the
plan's automation requests it in builds).

### 2.7 Clipping & headroom
Per-track soft clippers shave only inaudible overs on drums/808 attacks
(≤1–2 dB), raising crest efficiency before the master. Mix bus true peak
kept ≤ −6 dBTP; **crest factor target on the mix bus: 8–11 dB** (riddim
drop sections) — measured and enforced by adjusting clip/comp amounts.

### 2.8 Automation reconciliation
The composer's symbolic automation (doc 04 §7 — build filter sweeps, break
high-cuts, FX sends, sidechain intensity per section) is compiled onto the
final mix graph, section-aware: e.g. break sections get +2 dB reverb send
and mix-bus high-shelf easing; drops snap everything tight.

## 3. The measurement feedback loop

The engine is a closed loop, bounded to ≤ 3 iterations:

```
render mix pass → measure:
  short-term LUFS per section  (drop vs break contrast target: 4–7 LU)
  spectral balance vs genre reference curve (±2 dB per octave band)
  masking residuals, correlation, crest factor, true peak
→ compute bounded corrections (gain trims, carve depths, width, clip drive)
→ re-render (only affected buses)
```

Convergence is guaranteed by monotone, clamped corrections; final metrics
are stored with the project (visible in Statistics, doc 09).

## 4. Genre targets (data, not code)

| Metric (mix bus, pre-master) | Riddim | Trap |
|---|---|---|
| Drop short-term LUFS | −10 ±1 | −11 ±1 |
| Drop↔break contrast | 5–7 LU | 4–6 LU |
| Crest factor (drops) | 8–10 dB | 9–12 dB |
| Low band share (20–120 Hz) | 35–45 % energy | 40–50 % |
| Correlation | > +0.2 | > +0.2 |
| True peak | ≤ −6 dBTP | ≤ −6 dBTP |

## 5. Why no ML

Every quantity the mix cares about is directly measurable (LUFS, crest,
band energy, correlation, masking overlap), and genre practice supplies
explicit targets. A rules+feedback system hits those targets
deterministically, renders identical output for identical seeds, fails
loudly and explainably, and costs near-zero compute. ML would add value
only for *taste* judgments — which live upstream in sound selection/rating
where we already deploy the small ONNX rater.
