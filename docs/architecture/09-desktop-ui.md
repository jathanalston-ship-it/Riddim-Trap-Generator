# 09 — Desktop UI

**No prompt box anywhere.** Every control is a parameter. Native,
GPU-accelerated (JUCE 8 / Direct2D), modern dark interface — the design
language of Serum's panels crossed with Ableton's arrangement clarity.

## 1. Design system

- **Theme:** near-black surfaces (#0E0F12 base, #16181D panels), one accent
  (electric teal for Riddim, amber for Trap — accent follows selected
  genre), high-contrast type (Inter/JetBrains Mono for values), 4-px grid.
- **Controls:** Serum-style rotary knobs with value readouts, drag +
  double-click-to-type + scroll; sliders for ranges; segmented buttons for
  enums. Every control mod-clicks to its default and shows its effect in a
  one-line hint bar (no tooltips-on-hover mystery).
- **Motion:** 60 fps meters/waveforms; animation only where it carries
  information (progress, playhead, evolution feed).
- **Navigation:** left icon rail — Generate · Library · Projects ·
  Statistics · History · Evolution · Settings. A persistent **Preview
  transport bar** docks at the bottom of every page.

## 2. Generate (home page)

The instrument. Three vertical zones:

1. **Parameter panel (left):**
   - Genre selector (two large cards: RIDDIM / TRAP; accent + BPM range
     update on switch).
   - BPM (knob, genre-clamped), Song Length (mm:ss slider), Drop Count
     (1–4 stepper), Intro Style (segmented enum).
   - Character macro knobs in a Serum-like 2×3 cluster: **Energy,
     Aggression, Darkness, Complexity, Melody, Chaos.**
   - Seed field (monospace, dice button = random, lock icon = pin) —
     the reproducibility contract, front and center.
   - Preset chips (saved parameter snapshots).
2. **Visualization (center):** before generation, a live "predicted
   energy curve" of the arrangement updates as knobs move (the Decision
   Engine's plan preview — the UI's replacement for a prompt: you *see*
   what you asked for). During generation it becomes the staged progress
   view: Plan → Compose → Sounds → Render → Mix → Master, with per-stage
   audio flashes. After: full waveform + section map (Intro/Build/Drop…)
   colored by energy.
3. **Action zone (right):** the one big **GENERATE** button, then result
   actions — Play, **Re-roll section** (click a section in the map, re-roll
   just it), Export (platform variant list from doc 08), Save Project,
   ★ Keep sounds (explicit positive signal to the library).

## 3. Library

The evolving asset browser (docs 05–06).

- **Filter rail:** role (growl/screech/sub/kick…), genre fit, key, duration,
  and metadata range sliders (Brightness, Aggression, Darkness, Transient).
- **Results grid:** cards with mini-waveform, spectral thumbnail, score
  badge, fitness sparkline; click = instant audition (latched to host BPM
  and key); ★ favorite; right-click → Mutate / Breed with… / Find similar
  (vector search) / Show lineage / Archive.
- **Similarity mode:** drop any asset into the "anchor" slot → grid re-ranks
  by embedding distance.
- **Detail drawer:** full metadata table, recipe summary (node-graph
  thumbnail), usage history ("used in 12 tracks").

## 4. Projects

Grid/list of saved `.rtgproj` files: artwork-style waveform tile, title,
genre, BPM, length, creation date, master profile badges. Actions: open
(loads onto Generate page with its exact parameters+seed), duplicate-and-
mutate (same params, new seed), export bundle, delete. A project always
reopens bit-identically (assets pinned by content hash).

## 5. Settings

- **Audio:** device/driver (WASAPI/ASIO), sample rate, buffer size, output
  meter.
- **Performance:** CPU thread budget, GPU inference on/off (DirectML
  status shown), background evolution CPU cap + schedule (e.g. "only when
  idle > 5 min").
- **Library:** disk budget, per-role population caps, admission threshold,
  prune aggressiveness, seed-content reset.
- **Export defaults:** formats, platform profiles, stem export, output
  folders.
- **Appearance:** accent behavior, meter styles, UI scale.
  No account, no login, no cloud switches — nothing to configure online.

## 6. Statistics

The "studio dashboard": totals (tracks generated, listening time, exports),
library health (assets per role, mean fitness trend, diversity index —
embedding-space coverage heat), generation quality metrics over time (mean
generation score, mix conformance pass rates, LUFS/crest distributions),
and compute stats (render speed ×realtime, GPU utilization). All charts are
local data; this page proves the "it gets better over time" promise with
numbers.

## 7. History

Chronological feed of every generation (kept even if not saved as a
project): timestamp, parameter fingerprint (mini knob-state glyph), seed,
waveform strip, quick play. Any entry → **Restore** (exact params+seed back
onto Generate) or **Save as project**. Search/filter by genre, BPM,
parameter ranges. This page is the undo/redo of the whole instrument.

## 8. Sound Evolution

A living view of the background evolution system (doc 06):

- **Activity feed:** "bred growl #4812 × #3977 → #5120 (score 0.87,
  admitted)", "pruned 14 redundant hats", live while idle-evolution runs.
- **Lineage explorer:** select any asset → ancestry tree with per-node
  audition and fitness; visually shows *why* the library sounds the way it
  does.
- **Population view:** 2-D embedding map (UMAP-style projection, computed
  locally) per role — dots sized by fitness, starred favorites highlighted;
  watch clusters form and weak regions get culled.
- **Controls:** pause/resume evolution, "evolve this role now", set
  temperature (mutation size) — the closest thing the app has to gardening.

## 9. Audio Preview (persistent transport)

Docked bottom bar on every page: play/stop, loop section, position scrub on
a mini waveform, section markers, A/B toggle (mix vs master, or last two
generations), LUFS + true-peak meters, volume. Whatever page you're on,
audio behaves like one continuous instrument — page switches never
interrupt playback.

## 10. Why this feels like Serum/Ableton, not ChatGPT

The user's mental model is *configure → generate → audition → refine
(re-roll/branch) → export*. Feedback is always visual and audible —
predicted energy curves, section maps, meters — never conversational. Text
input exists in exactly two places: project names and the seed field.
