# Work Claims Ledger

Reserve your work area BEFORE editing (see CLAUDE.md Rule 3). Append a row,
commit + push immediately (`claim: <area>`), mark DONE when finished.
Conflict resolution when merging this file: **keep both sides' rows.**

Format:
`| session | claimed (UTC) | status | paths | goal |`

Status: ACTIVE → DONE (or STALE if >12h without commits; may be taken over).

| session | claimed (UTC) | status | paths | goal |
|---|---|---|---|---|
| fable-main-1 | 2026-07-06 04:20 | DONE | engine/decision/{include/rtg/decision/calibration.h,src/calibration.cpp}, tools/rtg_cli/main.cpp, app/src/GenerationController.{h,cpp}, ui/src/pages/SettingsPage.{h,cpp}, docs/CALIBRATION.md, VERSION | In-app calibration flow (Settings panel), shipped as v1.2.0 |
| fable-main-1 | 2026-07-06 06:05 | DONE | engine/drum_generator/** (drum_synth.cpp + new drum_profile module), tools/drum_profiler/ (new), CMakeLists.txt (add rtg_drumprof target only), engine/composition/src/composer.cpp (hat pattern density ONLY) | Drum reference-matching: analyze user reference track's drums, DrumProfile targets steer kick/snare/hat synthesis. NOT touching calibration.*, bass_designer, analysis.cpp, rtg_cli, VERSION (growl-fingerprint session owns those + this round's release) |
| fable-main-1 | 2026-07-06 08:55 | DONE | engine/decision/src/decision_engine.cpp, engine/composition/src/composer.cpp, library/src/sound_library.cpp (pick only), engine/generation/src/pipeline.cpp (palette selection only), ui/src/pages/GeneratePage.{h,cpp} (seed UX only) | Variation overhaul: seed auto-regen+lock, structural variety, drop archetypes, palette rotation. Still NOT touching bass_designer/sound_design/calibration.*/rtg_cli/VERSION |
| fable-main-1 | 2026-07-06 09:40 | DONE | VERSION | Release v1.3.0 — SUPERSEDED: tag was pre-minted by a manual workflow run at an old commit; actual content ships as v1.4.0 |
| fable-main-1 | 2026-07-06 09:55 | ACTIVE | VERSION | Release v1.4.0 (real drum reference-matching + variation overhaul + mix polish content) |
| fable-growl-1 | 2026-07-06 06:05 | DONE | engine/bass_designer/**, engine/sound_design/src/{dsp.h,voices.h,synth_engine.cpp}, engine/decision/{include/rtg/decision/calibration.h,src/calibration.cpp}, engine/mix/src/{mix_engine.cpp (depth section only),mix_dsp.h}, app/src/GenerationController.cpp, tools/rtg_cli/main.cpp, docs/CALIBRATION.md, engine/composition/src/composer.cpp (bass rhythm + ear-candy; merged with fable-main-1's hat/variation), VERSION | Square-wave riddim bass + wub gate, riddim 1/16 rhythm + ear-candy, mix depth, growl-fingerprint reference matching, calibration/stepped-LFO crash fixes. Ships as v1.5.0. |
