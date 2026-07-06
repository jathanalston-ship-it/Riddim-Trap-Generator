# 11 — Roadmap: Version 1.0 → 5.0

Each major version has one theme, builds strictly on the previous, and
never violates the pillars: offline-first, parameter-driven, low operating
cost. Cloud appears only as *optional convenience* (backup/marketplace),
never as required compute.

---

## v1.0 — "The Instrument" (foundation)

Everything in docs 01–10, shipped:

- Riddim + Trap generation end-to-end (plan → compose → synthesize →
  render → mix → master) from the 12-parameter Generate page.
- Sound Design Engine with rating pipeline; seed library; inline ingestion.
- Library Evolution v1 (inline scoring + background mutate/breed/prune).
- Full UI (Generate, Library, Projects, Settings, Statistics, History,
  Evolution, Preview). Platform-profile mastering + WAV/FLAC/MP3 export.
- Determinism guarantee + golden test suite. Windows installer.

**Exit criteria:** blind A/B — ≥ 30% of generated drops judged
release-plausible by target-genre producers; cold start < 2 s; 3-minute
track generated in < 60 s on a mid-range 8-core.

## v2.0 — "The Studio" (musical depth + interop)

Theme: from generator to studio centerpiece.

- **MIDI export** — arrangement, bass lines (with pitch-bend articulation),
  drums as standard MIDI; drag-and-drop from the section map straight into
  any DAW.
- **Stem export v2** — bus and per-track stems with/without processing.
- **Feature generation** — generate *into* a constraint: lock a section,
  a bass palette, or a drum pattern and regenerate everything else around
  it ("keep drop 1, redo the rest"); import a MIDI motif or a one-shot as a
  locked seed element. This is the bridge from autonomous to collaborative
  producing — still zero prompts.
- Genre packs framework hardened (riddim/trap rulebooks fully data-driven;
  first expansion styles: tearout, hybrid trap) — proving modularity.
- Evolution v2: embedding-space niching, per-role population dashboards.

## v3.0 — "The Rig" (plugins + pro workflow)

Theme: open the engine.

- **VST3 hosting** — user's plugins slot into defined chain positions
  (per-bus insert slots in mix, master chain slots, "external bass
  processor" in sound design candidate chains). Rendered offline exactly
  like internal DSP; plugin state saved in projects. (JUCE host layer was
  wired for this since v1 — doc 03.)
- **RTG Plugin SDK** — the internal interfaces (`ISoundDesigner`,
  `IMixEngine`, `IComposer`, style rulebooks) exposed over a stable C ABI:
  third parties ship new bass designers, mastering chains, and genre packs
  as `/plugins` modules.
- **Ableton integration (phase 1)** — Ableton Link tempo sync for live
  audition; **ALS project export**: generated track exported as an Ableton
  Live set with stems, MIDI, arrangement markers, and mixer settings
  reconstructed — "finish it in Ableton" in one click.
- Batch generation & A/B lab (generate N seeds overnight, tournament UI).

## v4.0 — "The Voice" (AI vocals + collaboration)

Theme: the last missing commercial element, still local-first.

- **AI vocals** — offline, licensed local voice models (ONNX/DirectML):
  vocal *chops* first (riddim's actual need — short phrases, formant-played
  as instruments, generated as library assets with the same rate-and-ingest
  pipeline), then full toplines for trap (phrase templates + pitch curves
  from the composition engine; no text prompts — style/intensity/lyrical-
  theme parameters). Vocal assets evolve in the library like any sound.
- **Collaboration** — project bundles carry full lineage (params, seed,
  recipes) so a collaborator's RTG regenerates and edits bit-identically;
  LAN/file-based handoff, plus optional end-to-end-encrypted **cloud
  backup & sync** of projects/library (user-keyed, pure storage — no cloud
  compute, subscription covers storage cost only).
- Shared-library merge tools (import a friend's favorites with lineage).

## v5.0 — "The Ecosystem" (marketplace + platform)

Theme: the library economy.

- **Marketplace** — buy/sell/share *recipes, genre packs, palettes, and
  evolved sound lineages* (tiny JSON + FLAC previews — near-zero
  distribution cost, DRM-light signing). Creators publish evolution lines;
  buyers' libraries interbreed purchased genomes with their own. Curated +
  rated; offline app remains fully functional without it.
- **Ableton integration (phase 2)** — RTG as a **VST3 plugin instrument**
  inside any DAW: generate sections in-place, stream stems to tracks
  (Ableton first-class, works everywhere VST3 does).
- Community model updates: optional download of retrained rater/embedder
  models (still local inference); federated *opt-in* anonymous fitness
  statistics to improve shipped rulebook weights.
- Platform maturity: multi-genre framework (drum & bass, dubstep classic,
  phonk packs), macOS port evaluation, accessibility pass.

---

## Cross-version invariants

1. A v1 project file opens and re-renders identically in v5 (versioned
   schemas + migration + pinned recipes).
2. No feature ever requires the network; cloud features degrade to "off".
3. The 12-parameter Generate page never grows a prompt box.
4. Every new engine ships behind the plugin-SDK interfaces — the core
   pipeline (doc 01 §3) is frozen architecture.

## Sequencing rationale

Interop (v2 MIDI/stems) precedes plugins (v3) because export is cheap and
unlocks pro adoption immediately; hosting precedes vocals (v4) because
vocal chains benefit from user plugins; the marketplace comes last (v5)
because it monetizes what only years of library evolution can create —
by then every user's library is unique, and lineage-signed recipes are the
product only this architecture (recipes, not just audio) makes possible.
