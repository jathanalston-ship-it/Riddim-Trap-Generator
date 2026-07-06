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
