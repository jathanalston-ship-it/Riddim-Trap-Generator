# 05 — Sound Design Engine

The heart of the software: a **procedural synthesis engine specialized for
modern riddim** (and trap), generating growls, screeches, metallic basses,
sub basses, FX (impacts, uplifters, downlifters, risers), and drums —
entirely offline, entirely local, with every render **rated**, and the best
renders promoted to permanent library assets.

## 1. Core abstraction: the Recipe Graph (genome)

Every sound is a **directed node graph** ("recipe") plus a parameter set —
serialized as JSON, stored alongside rendered audio. Recipes are:

- **Re-renderable** at any root note, length, or sample rate.
- **Mutable/breedable** — the evolution system (doc 06) operates on graphs.
- **Deterministic** — a recipe + seed renders bit-identically.

```
Recipe = { nodes[], connections[], macros[], meta{role, seedNote, tags} }
```

Node categories (all implemented in `engine/sound_design/src/nodes/`):

| Category | Nodes |
|---|---|
| Sources | FM operator stack, wavetable osc (+morph), granular engine, noise (white/pink/crackle), sampler (library audio as source — resampling!) |
| Filters | SVF (LP/HP/BP/notch), comb (+/−), formant bank, phaser/allpass |
| Nonlinear | waveshaper (tanh/fold/hard), bitcrush, saturator, rectifier, ring mod |
| Dynamics/spectral | OTT-style multiband up/down comp, band-split (process bands independently, e.g. distort mids only), spectral freeze |
| Modulators | LFO (sync-able, morphable shape), envelopes, step sequencer, sample-and-hold, macro inputs |
| Utility | mix/crossfade, gain, pan, unison/detune, pitch/formant shifter, delay, convolution (IR), stereo width |

**Macros** are named high-level controls (`talk`, `bite`, `wobble-rate`,
`darkness`) mapped to many node params at once. The composer's articulation
automation (doc 04 §7) drives macros — this is how one growl "talks"
differently on every note without new recipes.

## 2. Synthesis techniques and how each is used

- **FM:** 2–4 operator stacks with feedback for growl fundamentals and
  metallic inharmonic tones (ratio ≠ integer → bell/metal). Envelope-driven
  FM index = the "yoi" attack bite.
- **Wavetable:** morphing tables (shipped factory set + tables minted from
  the library's own audio) for screeches and modern top-bass; LFO/notes
  drive table position.
- **Granular:** texture beds, riser shimmer, vocal-ish formant clouds from
  resampled library material.
- **Noise synthesis:** snare/hat bodies, riser beds, impact air, crackle.
- **Comb filtering:** the riddim signature — tuned comb (keytracked) on
  distorted material produces the hollow "robotic talking" resonance; two
  moving combs create formant vowels.
- **Distortion:** staged (pre-filter soft → post-filter hard) waveshaping;
  distortion *between* filter stages is the classic growl topology.
- **Resampling:** first-class node — render an intermediate, pitch/stretch/
  reverse/re-slice it, feed it back in. Multi-generation resampling chains
  (render → distort → repitch → comb → render) are encoded in the graph, so
  even "resampled" sounds stay reproducible.
- **Multiband processing:** split at ~100/500/3k Hz; sub band kept clean
  (mono, pure sine/triangle), mid band carries distortion+comb character,
  high band gets exciter/width. Guarantees club-ready low end by
  construction.

## 3. Role generators (recipe factories)

Each role has a factory that assembles a stochastic-but-constrained graph
from that role's topology space (seeded; Chaos widens the space):

- **Growl:** FM/wavetable source → pre-drive → SVF(LP, keytracked) →
  comb ×1–2 → post-drive → OTT → band-split (clean sub + dirty mid).
  Macros: talk (comb/formant sweep), bite (FM index), wobble (LFO rate).
- **Screech:** high-register wavetable + hard sync or high-ratio FM →
  fold distortion → HP → phaser/comb → ping-pong micro-delay.
- **Metallic bass:** inharmonic FM ratios or ring-mod → comb tuned off-key
  → heavy multiband distortion → transient exciter.
- **Sub:** sine/triangle + soft clip + <2nd/3rd harmonic bump; mono below
  120 Hz; tuned decay; trap variant = 808 (pitch-glide envelope + longer
  tail + clip drive).
- **FX:** risers/uplifters (noise+detuned saw cluster, pitch/filter ramps,
  length = build length), downlifters (inverted), impacts (kick layer +
  noise burst + sub drop + IR reverb tail), sweeps, textures.
- **Drums (drum_generator):** kick = sine pitch-drop + click layer +
  saturation; snare = tuned body (190–250 Hz) + noise burst + transient
  shaper (riddim snares get comb/metal layer); hats/cymbals = filtered
  noise + metallic FM partials; percs = resampled hits from the library.
  Layering engine combines 2–3 synthesized layers with spectral-slot EQ.

## 4. Candidate → Rating → Asset pipeline

For each requested sound (role + constraints + novelty flag):

```
1. GENERATE   N candidates (N = 8–32; more when Chaos/novelty high):
              library-similar mutations + fresh factory recipes
2. RENDER     each at working key/length (parallel, faster than realtime)
3. ANALYZE    DSP features: spectral centroid/flux/rolloff, harmonicity,
              transient strength, crest factor, sub energy ratio, mid
              "movement" (mod spectrum 0.5–8 Hz), stereo width, noisiness,
              true peak, mono-compatibility
4. GATE       hard rejects: DC/denormals, silent/clipped, sub phase issues,
              aliasing markers, > -0.1 dBTP after normalization
5. SCORE      composite quality score (below)
6. SELECT     top-K for the track; ingest any candidate above the
              library admission threshold (recipe + audio + metadata +
              embedding + lineage) — doc 06
```

**Scoring** = weighted blend:
- *Heuristic fitness* (per role): e.g. growl wants strong 0.5–8 Hz spectral
  movement, harmonic density in 200–2k Hz, clean sub ratio, controlled
  crest; sub wants near-zero inharmonicity and mono purity.
- *ONNX rater* (small local model): perceptual quality/genre-fit score
  trained offline on labeled genre material — used where heuristics are
  weak ("does this growl sound *modern*?").
- *Novelty* (distance to nearest library embedding): rewarded when the plan
  asks for novelty, penalized when it asks for palette cohesion.

## 5. Subsystem map

| Subsystem | Responsibility |
|---|---|
| `synth/graph` | Recipe schema, graph compiler → ordered DSP program, block renderer, macro/modulation matrix |
| `synth/nodes` | All node DSP (SIMD kernels) |
| `bass_designer` | Growl/screech/metallic/sub factories + articulation macro maps |
| `drum_generator` | Drum factories + layering/transient tools |
| `synth/analysis` | Feature extraction (shared with library metadata, doc 06) |
| `synth/rating` | Gates, heuristic scorers per role, ONNX rater bridge, novelty scorer |
| `library` (doc 06) | Admission, storage, evolution of winning recipes |

## 6. Performance & determinism

Candidate renders are short (0.5–8 s), block-processed with SIMD, and
embarrassingly parallel across the job pool — a 24-candidate growl batch
targets < 2 s on a 8-core CPU. Every factory/mutation draws from named PRNG
streams (`sounddesign.growl[j]`…), so the same seed regenerates the same
candidates; the golden test suite pins reference recipes to reference audio
checksums.

The engine never *needs* pre-existing samples: factory recipes synthesize
from raw oscillators/noise. Shipped seed content only warm-starts quality;
from day one, generation → rating → ingestion continually replaces it with
the app's own evolved assets.
