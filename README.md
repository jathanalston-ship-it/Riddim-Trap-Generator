# Riddim Trap Generator (RTG)

**An autonomous electronic music producer for Windows desktop.**

RTG is not a chatbot and not a prompt-to-song service. It is a self-contained,
offline music production application that specializes in exactly two genres —
modern **Riddim Dubstep** and **Trap** — and generates complete,
commercial-ready instrumentals from a parameter panel, not a text box.

The user configures a small set of musical controls (BPM, energy, aggression,
darkness, complexity, drop count, seed, …), presses **Generate**, and the
software composes, sound-designs, arranges, mixes, and masters a finished
track. Every sound it synthesizes is rated; the best sounds are kept as
permanent assets, so the internal sound library grows and improves with every
generation.

The application should feel like **Serum or Ableton** — a professional audio
tool — never like ChatGPT.

## Design pillars

1. **No prompts.** All control is parametric. Determinism via random seed.
2. **Local everything.** No cloud compute, no network dependency, low
   operating cost. AI inference (small ONNX models only) runs on the local
   CPU/GPU.
3. **Algorithmic composition.** No LLM composes music. Composition, mixing,
   and mastering are deterministic, rule-and-heuristic driven systems that
   emulate a professional producer.
4. **Evolving sound library.** Sounds are synthesized from procedural
   recipes, scored, stored with rich metadata, and bred/mutated/pruned over
   time. The software never depends entirely on pre-existing samples.
5. **Modularity.** Every subsystem is an isolated module behind a narrow
   interface, so engines can be replaced, upgraded, or shipped as plugins.

## Architecture documentation

| # | Document | Covers |
|---|----------|--------|
| 01 | [Master Architecture](docs/architecture/01-master-architecture.md) | System overview, subsystems, data flow, process model |
| 02 | [Folder Structure](docs/architecture/02-folder-structure.md) | Production repository layout (mirrored by this repo's scaffold) |
| 03 | [Technology Stack](docs/architecture/03-technology-stack.md) | Every technology choice, with justification |
| 04 | [Composition Engine](docs/architecture/04-composition-engine.md) | Music theory, structures, drops, rhythm, energy curves |
| 05 | [Sound Design Engine](docs/architecture/05-sound-design-engine.md) | Procedural synthesis: growls, screeches, subs, drums, FX |
| 06 | [Library Evolution](docs/architecture/06-library-evolution.md) | Asset metadata, scoring, breeding, mutation, pruning |
| 07 | [Mix Engine](docs/architecture/07-mix-engine.md) | Autonomous mixing: gain staging → sidechain → bus processing |
| 08 | [Mastering Engine](docs/architecture/08-mastering-engine.md) | Automated mastering and per-platform delivery targets |
| 09 | [Desktop UI](docs/architecture/09-desktop-ui.md) | Every screen of the parameter-driven dark UI |
| 10 | [Decision Engine](docs/architecture/10-decision-engine.md) | The "producer brain": how every automatic choice is made |
| 11 | [Roadmap](docs/architecture/11-roadmap.md) | Version 1.0 → 5.0 evolution plan |

## Repository layout

The source tree scaffold in this repository mirrors the production layout
defined in [doc 02](docs/architecture/02-folder-structure.md). Each subsystem
folder contains a README describing its responsibility and public interface.

## Status

Architecture phase. The documents above are the authoritative design; the
folder scaffold is in place and ready for implementation to begin per the
[roadmap](docs/architecture/11-roadmap.md).
