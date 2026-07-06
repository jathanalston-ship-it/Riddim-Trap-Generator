# 06 — Library Evolution (Evolving Audio Asset Database)

The sound library is a **living population**: every asset carries metadata,
a quality score, and a lineage; the system automatically combines, mutates,
breeds, prunes, and favorites assets so the palette improves with use.

## 1. What an asset is

```
Asset {
  id            (content hash)
  recipe        (synth genome JSON — doc 05; null only for imported samples)
  audio         (FLAC, content-addressed blob store)
  embedding     (512-dim vector from local ONNX embedder)
  metadata      (below)
  lineage       (parent ids + operator that produced it)
  state         (candidate | active | favorite | archived)
}
```

## 2. Metadata schema (per asset)

| Field | Source | Example |
|---|---|---|
| Role | factory/tag | growl, screech, sub, kick, riser… |
| Brightness | spectral centroid (norm.) | 0.62 |
| Aggression | distortion density + crest + high-mid energy | 0.81 |
| Darkness | inverse tilt + low-mid weight | 0.44 |
| Spectral centroid | Hz | 1,840 |
| Transient strength | attack slope / onset energy | 0.9 (drums), 0.2 (pads) |
| Duration | seconds | 1.6 |
| Root note | pitch tracker (+ recipe seedNote) | F1 |
| Genre compatibility | ONNX genre-fit per genre | {riddim: 0.92, trap: 0.55} |
| Movement | mod-spectrum energy 0.5–8 Hz | 0.7 (growls) |
| Sub ratio / mono purity | band energy + correlation | 0.98 |
| Generation score | rating pipeline (doc 05 §4) | 0.84 |
| Popularity | user signal (kept in final render, favorited, previewed) | 37 |
| Re-use frequency | times selected by generations | 12 |
| Age / last used | timestamps | — |

All numeric descriptors come from the shared `synth/analysis` extractor, so
search, rating, and evolution speak one feature language. Metadata lives in
SQLite; embeddings are additionally indexed in HNSW for similarity queries.

## 3. Fitness

Evolution needs one number. Fitness is a moving blend that shifts from
*predicted* to *proven* quality as evidence accumulates:

```
fitness = w_g·generation_score            (rating at birth)
        + w_u·usage_value                 (re-use freq, decayed by age)
        + w_p·popularity                  (explicit user signals ×3 weight)
        + w_n·niche_bonus                 (rare region of embedding space)
        − w_r·redundancy_penalty          (too close to a stronger sibling)
```

User signals dominate once present: a favorited sound is effectively
immortal; a sound users always re-roll away from sinks fast.

## 4. Evolution operators

All operators act on **recipes** (genomes), then render → gate → score
through the standard pipeline (doc 05 §4), so no unrated audio ever enters
the active pool.

- **Mutate** — perturb parameters (Gaussian, size ∝ temperature), swap a
  node for a same-category sibling, re-tune a comb, change an FM ratio,
  re-draw one macro map. Small mutations refine winners; large mutations
  explore.
- **Breed (crossover)** — two parents of the same role: subgraph exchange
  (e.g. parent A's source chain + parent B's distortion/comb chain), macro
  map blending, parameter interpolation. Parents selected by
  fitness-proportional tournament, with an embedding-distance floor so we
  don't inbreed near-duplicates.
- **Combine** — build a new *layered* asset from complementary assets
  (spectral-slot check: sub-pure + mid-mover + high-air) as a graph whose
  sampler nodes reference the parents; used heavily for drums and impacts.
- **Delete (prune)** — archive when fitness < threshold AND unused for N
  generations AND a stronger near-neighbor exists (redundancy). Archived ≠
  destroyed: recipe + metadata are kept (tiny), audio blob is dropped;
  history/projects can always re-render. Favorites and any asset referenced
  by a saved project are prune-exempt.
- **Favorite** — automatic (fitness > p95 for its role, sustained re-use)
  or manual (user tap in Library/Preview). Favorites seed future palettes
  and get priority as breeding parents.

## 5. The evolution loop

Two triggers:

**Inline (every generation):** winning candidates ingested; usage counters
and generation scores updated for every asset the track used; losers'
near-duplicates get redundancy pressure.

**Background (idle-priority scheduler):**
```
each cycle (bounded CPU budget, pauses during generation):
  1. Select role with weakest coverage (few assets / low mean fitness /
     empty embedding regions)
  2. Spawn batch: 60% mutations of top performers, 25% crossovers,
     15% fresh factory recipes (immigration — prevents stagnation)
  3. Render → gate → score → admit above threshold
  4. Prune pass (archival rules above)
  5. Update statistics + evolution feed (UI doc 09 shows this live)
```

Population control per role: soft cap (e.g. 300 active growls) with
admission displacing the weakest non-favorite — the library grows in
*quality* indefinitely but in *size* sub-linearly, keeping disk and query
cost low.

## 6. Diversity maintenance

Pure fitness selection collapses palettes into one "best growl". Countered
by: niche bonus (fitness reward for sparse embedding regions), per-role
embedding-space quotas (k-means cells each keep their local champion),
immigration (fresh random recipes every cycle), and novelty-weighted
selection whenever the Decision Engine requests high Chaos.

## 7. Lineage & explainability

Every asset records `(parents, operator, params, seed)` — a full family
tree. This powers the **Sound Evolution** UI page (ancestry graphs,
generation-over-generation fitness curves), debugging ("why did growls get
worse?"), and reproducibility (any asset can be rebuilt from the root of
its lineage).

## 8. Guarantees

- Nothing unrated enters the active pool; nothing referenced is ever lost.
- All evolution is seeded → a library snapshot + seed replays identically.
- The system runs forever without user attention, but every user signal
  (favorite, keep, re-roll) immediately steers it.
