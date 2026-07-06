# 01 — Master Architecture

**Riddim Trap Generator (RTG): an autonomous electronic music producer.**

Windows desktop. Fully offline. Local inference only. No conversational AI.
The user configures parameters and presses Generate; the software delivers a
complete, commercial-ready Riddim or Trap instrumental and grows its own
sound library while doing so.

---

## 1. What the system is (and is not)

| Is | Is not |
|----|--------|
| A parameter-driven production instrument (like Serum/Ableton) | A chatbot or prompt-to-song service |
| Specialized: Riddim Dubstep + Trap only | A general-purpose music generator (Suno) |
| Algorithmic composer + procedural synthesizer + rule-based mix/master | An end-to-end neural audio model |
| Self-improving via an evolving local asset library | Dependent on cloud compute or sample packs |

The narrow genre focus is the core strategic advantage: two genres with
well-understood grammar (song structure, sound palette, mix targets) can be
encoded as explicit algorithms and heuristics, which is cheap to run,
deterministic, debuggable, and offline — where a general model would require
massive cloud inference.

## 2. User-facing contract

**Inputs (the only user controls):**

- Genre: `Riddim | Trap`
- BPM (genre-clamped: Riddim ~140–150, Trap ~130–170 half-time feel)
- Song Length (target duration)
- Energy, Aggression, Darkness, Complexity, Melody Amount, Chaos — each 0–100
- Drop Count (1–4)
- Intro Style (enum: `Atmospheric | Minimal | Vocal-chop | Impact | Fakeout`)
- Random Seed (64-bit; blank = auto)

**Output:** a rendered, mastered WAV/FLAC/MP3 + an editable project file +
stems (optional) + any newly minted library assets.

**Determinism guarantee:** identical (parameters, seed, library snapshot)
⇒ identical audio. Every stochastic choice in every subsystem draws from one
seeded PRNG hierarchy (see §6).

## 3. System overview

```
┌────────────────────────────────────────────────────────────────────────┐
│  UI LAYER (native, dark, parameter-driven — no prompt box)             │
│  Generate · Library · Projects · Statistics · History · Evolution ·    │
│  Settings · Audio Preview                                              │
└──────────────┬─────────────────────────────────────────▲───────────────┘
               │ GenerationRequest (params + seed)       │ progress, audio,
               ▼                                         │ project, stats
┌────────────────────────────────────────────────────────┴───────────────┐
│  GENERATION ORCHESTRATOR  (pipeline controller, cancellable, staged)   │
│                                                                        │
│   1. DECISION ENGINE ──► GenerationPlan (the "producer brain")         │
│   2. COMPOSITION ENGINE ──► Arrangement + patterns (symbolic score)    │
│   3. SOUND SELECTION ──► query Sound Library (vector + metadata)       │
│   4. SOUND DESIGN ENGINE ──► synthesize missing/novel sounds           │
│   5. AUDIO RENDERER ──► per-track audio (offline, faster-than-RT)      │
│   6. MIX ENGINE ──► balanced, sidechained, bus-processed mix           │
│   7. MASTER ENGINE ──► loudness/true-peak targeted master              │
│   8. LIBRARY EVOLUTION ──► score sounds, ingest winners, update stats  │
└──────┬───────────────┬──────────────────┬──────────────────────────────┘
       │               │                  │
       ▼               ▼                  ▼
┌────────────┐  ┌──────────────┐  ┌──────────────────────────────┐
│ SOUND      │  │ MODEL RUNTIME│  │ STORAGE                      │
│ LIBRARY    │  │ ONNX Runtime │  │ SQLite (metadata, projects,  │
│ (assets +  │  │ (DirectML GPU│  │ history, stats) + vector     │
│ recipes +  │  │ /CPU): sound │  │ index + FLAC asset store on  │
│ evolution) │  │ raters,      │  │ disk                         │
│            │  │ embedders    │  │                              │
└────────────┘  └──────────────┘  └──────────────────────────────┘
```

## 4. Subsystems

### 4.1 UI Layer
Native GPU-accelerated dark UI (see doc 09). Owns no logic: it serializes a
`GenerationRequest`, streams progress events, and previews audio through the
shared audio device service. Runs on the main thread; all engines run on a
worker pool.

### 4.2 Generation Orchestrator
A staged, cancellable pipeline controller. Each stage is a pure-ish module:
`input → artifact`, with artifacts persisted so a generation can be resumed,
partially re-rolled ("re-roll drop 2 only"), or inspected. Emits progress
(stage, %, preview snippets) to the UI. Runs stages 3–5 with parallelism
across tracks/sections.

### 4.3 Decision Engine (doc 10)
Translates the 12 user parameters + seed into a complete `GenerationPlan`:
energy curve, section map, drop character, sound palette constraints, drum
density, transition/FX budget, mix intent. Rule-based + weighted stochastic
sampling. This is the only place "taste" lives; everything downstream
executes the plan.

### 4.4 Composition Engine (doc 04)
Deterministic algorithmic composer. Produces a symbolic score: arrangement
grid, bass rhythm patterns (the riddim "call and response"), drum patterns
with variation/fills, melodic/atmospheric material (restrained by Melody
Amount), automation curves, and transition events. No LLM composes anything.

### 4.5 Sound Library + Sample Database (docs 05, 06)
The asset system. Every sound is stored as **recipe (synth genome) +
rendered audio + metadata + embedding**. Selection is a hybrid query:
metadata filters (genre, root note, duration, aggression range) + vector
similarity ("sounds like the palette anchor") + popularity/score ranking.
The library ships with a seed corpus and grows autonomously.

### 4.6 Sound Design Engine (doc 05)
Procedural synthesis graph engine (FM, wavetable, granular, noise, comb,
distortion, resampling, multiband). Invoked when the plan wants novelty
(Chaos high), when the library lacks a required sound, and as a background
"evolution" job. Every render is analyzed and scored; winners are ingested.

### 4.7 Audio Renderer
Offline (non-realtime) block renderer: takes symbolic score + resolved
sounds + automation and produces per-track 32-bit float audio at 48 kHz,
faster than realtime on CPU with SIMD. Also drives realtime preview.

### 4.8 Mix Engine (doc 07)
Autonomous mixing engineer: gain staging, per-track EQ/compression/OTT/
distortion/imaging, sidechain & ducking matrix, bus architecture, headroom
management. Measurement-driven: it renders, measures (LUFS, crest, spectral
balance, masking), and iterates a bounded number of passes.

### 4.9 Master Engine (doc 08)
EQ → multiband dynamics → clipper → limiter chain targeting per-platform
loudness/true-peak profiles (Spotify, Apple Music, YouTube, SoundCloud,
Festival). Reference-curve matching against genre tonal targets.

### 4.10 Model Runtime
Thin wrapper over ONNX Runtime with DirectML (any Windows GPU) and CPU
fallback. Hosts only **small discriminative models**: sound-quality raters,
audio embedders, genre-fit classifiers. Models are versioned files in
`/models`; the app runs fully without GPU.

### 4.11 Storage
- **SQLite** — single-file, zero-admin: asset metadata, projects, generation
  history, statistics, evolution lineage.
- **Vector index** — ANN index over audio embeddings for similarity search.
- **Asset store** — content-addressed FLAC files + recipe JSON on disk.
- **Project files** — self-contained `.rtgproj` (JSON + referenced asset ids).

## 5. Data flow of one generation

1. UI submits `GenerationRequest{params, seed}`.
2. Decision Engine emits `GenerationPlan` (persisted).
3. Composition Engine emits `Score` (sections, patterns, automation).
4. Sound Selection resolves every score role (sub, growl A/B, screech,
   kick, snare, hats, FX, atmos…) to library assets; unresolved or
   novelty-flagged roles go to the Sound Design Engine, which synthesizes,
   rates, and returns candidates (side effect: good ones are ingested).
5. Renderer produces track stems → Mix Engine → Master Engine.
6. Evolution pass: usage counters, generation scores, and any new assets
   are written; Statistics updated; History records the full (params, seed,
   plan) tuple so any track is reproducible.

## 6. Determinism & randomness architecture

A single master seed spawns a **named PRNG tree** (PCG64):
`seed → {decision, composition.arrangement, composition.drums.section[i],
sounddesign.growl[j], mix.microvariation, …}`. Each subsystem receives only
its named stream, so changing one stage's logic or re-rolling one section
never perturbs the others. Chaos maps to distribution temperature, never to
un-seeded randomness.

## 7. Process & threading model

Single process. Main thread = UI. A job system (thread pool sized to cores)
runs generation stages; synthesis candidate rendering and per-track
rendering parallelize naturally. The realtime audio callback thread is
isolated and lock-free (ring buffers) for preview playback. ONNX inference
runs on the job pool (GPU-serialized queue when DirectML is active).
Background evolution jobs run at idle priority and pause during generation.

## 8. Low operating cost

- No cloud: all compute local; models are ≤ tens of MB, inference is
  milliseconds per sound.
- DSP-first: 95% of the system is classic DSP and algorithms; ML is used
  only where heuristics are weak (perceptual quality rating, similarity).
- Offline rendering is CPU-friendly; GPU strictly optional.

## 9. Modularity & expansion

Every engine sits behind a versioned C++ interface (`IComposer`,
`ISoundDesigner`, `IMixEngine`, …) registered in a module registry. Future
plugin system (doc 11) exposes these same interfaces plus VST3 hosting, so
third-party bass designers or mastering chains can be dropped in without
touching the core. New genres = new data packs (style rules, mix targets,
palette priors) + optional new modules — the pipeline itself is
genre-agnostic.

## 10. Self-improvement loop (the moat)

```
Generate → Rate (DSP metrics + ONNX rater + implicit user signals)
        → Ingest winners (recipe + audio + metadata + embedding)
        → Evolve (mutate / crossover / prune — doc 06)
        → Better palette next generation
```

The library is the compounding asset. Recipes (not just audio) are stored,
so sounds can be re-rendered at any key/length and bred parametrically.
Users who generate more get a genuinely better instrument over time — with
zero marginal cloud cost.
