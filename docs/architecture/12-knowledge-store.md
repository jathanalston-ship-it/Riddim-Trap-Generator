# 12 — Knowledge Store (the system's long-term memory)

**Status: foundation shipped (schema + Python access layer + first producer).**

The product directive is literal and repeated: *"Never throw information away.
Store every measurement. Estimate confidence. Allow future analyzers to consume
previous measurements. Treat every song as engineering data. Never allow
knowledge to disappear."* Before this subsystem, RTG observed brilliantly and
then forgot: every `tools/earview` analysis emitted a one-off JSON/PNG and was
discarded after steering a single render; `calibration.json` held only the
*current* reference and was overwritten; no measurement carried a confidence.

The Knowledge Store is the fix and the compounding asset the master architecture
calls "the moat" (doc 01 §10). It is the shared memory that Observation writes
to, the Producer Cortex reasons over, the Reconstruction Engine scores against,
and the Self-Improvement loop reads to get better every project.

---

## 1. Responsibilities

| Does | Does not |
|---|---|
| Persist every measurement forever (append-only) | Judge whether a sound is "good" (no learned oracle — see EARS_AND_EYES) |
| Attach a confidence to every number | Analyze audio (producers write to it; it stores) |
| Record production hypotheses and refine them | Overwrite or delete anything |
| Log reconstruction attempts — successes AND failures | Live in the audio render path (it is metadata) |
| Let any tool consume any prior measurement | Require numpy/ML/network (stdlib sqlite3 only) |

## 2. Data model

Canonical schema: `database/migrations/001_knowledge_store.sql`. Four tables.

- **subjects** — anything observable: `reference | render | stem | sound | mix |
  master`. Identity is `(kind, name, content_hash)`, so re-observing the same
  audio *accumulates* against one subject instead of forking. Carries `seed` and
  a JSON `meta` (genre, bpm, params, lineage) for reproducibility.

- **observations** — the append-only measurement ledger. Each row: a namespaced
  `metric` (e.g. `loudness.integrated_lufs`), a scalar `value` or a `value_json`
  vector (bark[24], mfcc[13]), `unit`, a `confidence` (0..1), the analysis
  `window` (`drop`, `break`, `stem:growl`, …), full provenance
  (`tool`/`tool_version`/`method`), and `derived_from` — the observation ids this
  number consumed, so any derived value's lineage is walkable.

- **hypotheses** — the Producer Cortex layer: inferred production decisions as
  *claims*, not guesses. Each has an `aspect`
  (`growl.synth_method`), a `statement` (JSON/text), a `confidence`, a `status`
  (`open | supported | refuted | superseded`), `evidence` (observation ids), and
  `refined_from` — a refinement links back to the claim it replaces.

- **reconstructions** — the Reconstruction Engine's log: a `target` subject, the
  `result` render, the `recipe` tried, the perceptual `distance` achieved, and a
  `success` flag. Failures are retained as diagnostic data; the winner is
  `min(distance)` per target.

## 3. Confidence model — `tools/knowledge/confidence.py`

`docs/EARS_AND_EYES.md` documents, in prose, which metrics are robust and which
are degenerate/confounded. This module makes that knowledge *data*: a grounded
prior per metric so a degenerate number can never masquerade as ground truth.

- Robust (K-weighted loudness) → ~0.9; Zwicker sharpness → ~0.75; roughness →
  ~0.6; bark shares → ~0.65; mfcc → ~0.5.
- Documented-weak → low: formant-motion median (collapses to 0) → 0.2; low-band
  crest (kick transient, not sub) → 0.3; full-mix onset/hat counts (mix-bleed
  confound) → 0.35.
- **Stem bonus**: the same confounded metric measured on an *isolated stem*
  (`window` starts `stem:`) is lifted, because the mix-bleed confound is gone.

Callers may override per-observation; the default fires when they don't.

## 4. How each ability uses the store

```
                    ┌───────────────────── KNOWLEDGE STORE ─────────────────────┐
 OBSERVATION  ──►   │  subjects · observations(+confidence,+provenance,+lineage) │
 (earview, engine)  │                                                            │
 CORTEX       ◄──►  │  hypotheses (refined, never overwritten)                   │
 RECONSTRUCTION ◄─► │  reconstructions (successes + failures)                    │
 CREATION     ──►   │  renders become subjects; their metrics become training/   │
 SELF-IMPROVE ◄──   │  diagnostic data read back to steer the next project       │
                    └────────────────────────────────────────────────────────────┘
```

- **Observation** writes measurements. First producer wired:
  `tools/earview/ears.py --record` persists loudness/roughness/sharpness/bark/
  mfcc with derived confidence. (Off by default → ears.py unchanged without the
  flag.) Next producers: `instruments.py`, `structure.py`, and the C++ engine's
  `CalibrationProfile` self-measurement.
- **Cortex** reads a subject's observations and posts hypotheses about how a
  sound was produced, refining them as evidence accrues.
- **Reconstruction** logs each recipe attempt's distance-to-target; querying
  `best_reconstruction(target)` returns the recipe to keep.
- **Creation / Self-Improvement** treats every render as a subject: its metrics
  are training data on success, diagnostic data on failure — read back to steer
  the next generation, so the instrument is genuinely better each project.

## 5. Determinism (CLAUDE.md Rule 7)

The store is metadata that lives **entirely outside the audio render path**. It
uses wall-clock timestamps and autoincrement ids; nothing here feeds a seeded
RNG or a sample buffer. Recording an observation can never change rendered
audio. Databases are gitignored data, never committed.

## 6. Interfaces

- Python: `tools/knowledge/kstore.py` (`KnowledgeStore`), `kcli.py` (shell:
  `stats/subjects/record/history/latest/query/compare/hypotheses/recon/export`).
- Storage: `$RTG_KNOWLEDGE_DB`, else `<repo>/rtg_library/knowledge.db`.
- C++ (future): `database/` reads the same SQLite schema to persist engine-side
  self-measurement and consume prior reference observations as live targets.

## 7. Success criteria (measurable)

1. **Nothing lost**: re-running an analysis appends a row; `history()` shows
   every past reading; no path updates/deletes an observation. *(Verified by
   `selfcheck.py`.)*
2. **Confidence is grounded, not uniform**: robust metrics > 0.8, documented-
   degenerate metrics < 0.3, stems lift confounded metrics. *(Verified.)*
3. **Cross-tool reuse**: a tool can read and diff a prior measurement it did not
   itself produce, without touching audio (`kcli compare`). *(Verified.)*
4. **Durable**: the store survives process restarts (on-disk SQLite).
   *(Verified.)*
5. Coverage grows over time: number of producers writing to the store, and
   distinct metrics per subject, both trend up release over release.

## 8. Roadmap for this subsystem

- Wire remaining Python producers (`instruments.py`, `structure.py`,
  `dissect.py`, `abdiff.py`) behind the same `--record` convention.
- Engine-side writer: persist `CalibrationProfile` self-measurements each render
  so renders become first-class subjects with a metric history.
- Cortex v1: derive hypotheses (synth method, processing chain) from stored
  observations, with confidence that reconstruction refines.
- Reconstruction v1: a synth-parameter search that logs every attempt's
  distance here and converges on `best_reconstruction`.
- Embeddings + vector index (doc 03) keyed to subjects for similarity queries.

## Gate

`python3 tools/knowledge/selfcheck.py` (stdlib only) must pass before any push
that touches this subsystem.
