# CLAUDE.md — Session Rules for Riddim Trap Generator

Multiple Claude sessions work on this repo, sometimes simultaneously.
These rules keep that safe. **Read and follow all of them before touching
any file.**

## What this project is

An autonomous riddim/trap music producer: C++20 engine (`rtg_core`, JUCE-free)
+ JUCE 8 desktop app, fully offline, deterministic per seed. Architecture
docs are authoritative: `docs/architecture/01…11`. Current state: shipped
v1.x via the release pipeline; sound-quality iteration ongoing.

## Rule 1 — One branch, ever

- **All work happens on `claude/music-producer-architecture-bfq8pb`.**
- Never create, switch to, or push any other branch. Never open PRs.
- Never force-push. Never rewrite history.
- Do not push tags (the git proxy silently drops them; releases don't need
  them — see Rule 6).

## Rule 2 — Sync before you touch anything

- `git pull --rebase origin claude/music-producer-architecture-bfq8pb`
  **at session start, before every push, and before claiming work.**
- If a push is rejected: pull --rebase and push again. Never `--force`.
- Merge conflicts in `.claude/coordination/CLAIMS.md`: keep BOTH sides'
  entries (it's an append-style ledger).

## Rule 3 — Claims: reserve your work area before editing

The claims ledger is `.claude/coordination/CLAIMS.md`.

1. Before editing code, add a claim row: session name, date/time (UTC),
   the **paths/subsystems** you will modify, and a one-line goal.
2. **Commit and push the claim immediately** (message: `claim: <area>`),
   so parallel sessions see it.
3. Do not edit paths inside another session's ACTIVE claim. If you must,
   coordinate by scoping your claim to different files, or wait.
4. When done: mark the claim DONE in the same push as your final work.
5. A claim older than **12 hours** with no matching commits may be marked
   STALE and taken over.
6. Claims should be narrow (specific files/dirs), not "the whole engine".

## Rule 4 — Use subagents for implementation

- Substantial implementation goes to **subagents (Agent tool, model
  `opus`)** to conserve usage; the main session designs contracts,
  integrates, verifies. This has worked well: give each agent an explicit
  file-ownership list (disjoint from other concurrent agents), the exact
  compile-check commands, and a verification bar.
- Never let two of your own agents edit the same file concurrently.
- Agents must not run git commands; the main session commits.

## Rule 5 — Quality gates before every push (code changes)

- Engine (`engine/`, `library/`, `utils/`, `tools/`):
  `cmake -B build-core -G Ninja -DRTG_BUILD_APP=OFF && cmake --build build-core -j4`
  then smoke both genres:
  `./build-core/rtg_cli --out /tmp/a.wav --seconds 60 --seed 42 --library /tmp/libA`
  (repeat with `--genre trap`), and the determinism check: same seed +
  fresh library dirs twice → `cmp` identical WAVs.
- UI/app (`ui/`, `app/`): full app must link:
  `cmake --build build --target RiddimTrapGenerator -j4`
  (JUCE lives in `third_party/JUCE` locally; CI fetches it).
- Never push code that breaks `rtg_core` compilation — other sessions
  build on top of HEAD.

## Rule 6 — Releases

- A release = a commit that **changes the `VERSION` file** on this branch
  (the Release workflow builds the Windows exe, tags `vX.Y.Z`, publishes,
  and every installed app self-updates from it). The manual
  "Run workflow" button also works for the user.
- **Only bump `VERSION` when explicitly shipping**, all gates green, and
  no other session mid-release (check CLAIMS.md — a release is a claim on
  `VERSION`).
- Version discipline: sound/feature work → minor bump; fixes → patch.

## Rule 7 — Engine contracts & invariants

- **Determinism is a product guarantee.** All randomness flows from the
  seeded `rtg::Rng` streams. Never use `rand()`, `time()`, unseeded state,
  or iteration order of unordered containers in the audio path.
- Public headers under `*/include/rtg/**` are contracts. Changing one
  means updating every caller in the same push + full app build gate.
- `rtg_core` stays JUCE-free. JUCE code lives only in `app/` and `ui/`.
- 48 kHz everywhere; sound recipes must stay re-renderable + rateable.
- Do not commit `build*/`, `third_party/`, or generated audio.

## Rule 8 — Commit style

- Small, complete units; descriptive messages (what + why), present tense.
- Keep the existing trailer convention (Co-Authored-By + Claude-Session
  link). Never mention internal model IDs in commits or code.
- Push promptly after each completed unit so parallel sessions stay fresh.

## Current subsystem map (for scoping claims)

| Area | Paths |
|---|---|
| Decision/plan | `engine/decision/` |
| Composition | `engine/composition/`, `engine/arrangement/` |
| Synthesis core | `engine/sound_design/` |
| Bass / drums | `engine/bass_designer/`, `engine/drum_generator/` |
| Mix / master | `engine/mix/`, `engine/master/` |
| Pipeline | `engine/generation/` |
| Library/evolution/preferences | `library/` |
| Storage/config/project | `database/`, `config/`, `project/` |
| UI | `ui/` |
| App shell/controller/updater | `app/` |
| CLI + tools | `tools/` |
| CI/Release | `.github/workflows/`, `VERSION` |
