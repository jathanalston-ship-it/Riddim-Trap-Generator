# 02 — Folder Structure

Production-quality repository layout. Every subsystem owns a folder with a
narrow public interface (`include/`) and private implementation (`src/`).
This repository's scaffold mirrors this structure; each folder carries a
README stating its responsibility.

```
riddim-trap-generator/
├── CMakeLists.txt                  # Root build (CMake + vcpkg manifest)
├── vcpkg.json                      # Pinned third-party dependencies
├── README.md
│
├── app/                            # Application shell
│   ├── src/                        #   entry point, window, app lifecycle,
│   │                               #   module registry, job system bootstrap
│   └── installer/                  #   MSIX/Inno Setup packaging
│
├── ui/                             # UI (doc 09) — views only, no logic
│   ├── include/rtg/ui/
│   ├── src/
│   │   ├── pages/                  #   generate, library, projects, settings,
│   │   │                           #   statistics, history, evolution, preview
│   │   ├── components/             #   knobs, sliders, waveform, spectrogram,
│   │   │                           #   arrangement timeline, energy curve editor
│   │   └── theme/                  #   dark theme tokens, typography, icons
│   └── resources/                  #   fonts, SVG icons, shaders
│
├── engine/
│   ├── generation/                 # Generation Engine (orchestrator)
│   │   ├── include/rtg/generation/ #   IPipeline, GenerationRequest/Plan,
│   │   └── src/                    #   stage runner, progress, cancellation,
│   │                               #   artifact persistence, re-roll support
│   │
│   ├── decision/                   # AI Decision Engine (doc 10)
│   │   ├── include/rtg/decision/
│   │   ├── src/                    #   parameter mapping, energy planner,
│   │   │                           #   palette selector, producer heuristics
│   │   └── data/                   #   style rulebooks (riddim.json, trap.json)
│   │
│   ├── arrangement/                # Arrangement (doc 04 §2)
│   │   ├── include/rtg/arrangement/
│   │   └── src/                    #   section grammar, drop/fakeout/switch
│   │                               #   placement, transitions, song forms
│   │
│   ├── composition/                # Music theory & pattern generation (doc 04)
│   │   ├── include/rtg/composition/
│   │   └── src/                    #   scales/keys, bass rhythm (call/response),
│   │                               #   melody, chords, automation curves
│   │
│   ├── bass_designer/              # Bass Designer (doc 05 §3)
│   │   ├── include/rtg/bass/
│   │   └── src/                    #   growl/screech/metallic/sub recipes,
│   │                               #   FM+wavetable+comb+resample chains
│   │
│   ├── drum_generator/             # Drum Generator (doc 05 §5)
│   │   ├── include/rtg/drums/
│   │   └── src/                    #   kick/snare/hat/perc synthesis,
│   │                               #   layering, transient shaping
│   │
│   ├── sound_design/               # Shared synthesis core (doc 05)
│   │   ├── include/rtg/synth/
│   │   └── src/
│   │       ├── graph/              #   node-graph runtime (recipe = genome)
│   │       ├── nodes/              #   fm, wavetable, granular, noise, comb,
│   │       │                       #   distortion, filters, multiband, resampler
│   │       ├── analysis/           #   feature extraction (centroid, transients…)
│   │       └── rating/             #   DSP heuristics + ONNX rater bridge
│   │
│   ├── mix/                        # Mix Engine (doc 07)
│   │   ├── include/rtg/mix/
│   │   ├── src/                    #   gain staging, EQ/comp/OTT, sidechain
│   │   │                           #   matrix, bus tree, measurement loop
│   │   └── data/                   #   genre mix templates & targets
│   │
│   ├── master/                     # Master Engine (doc 08)
│   │   ├── include/rtg/master/
│   │   ├── src/                    #   eq-match, multiband, clipper, limiter,
│   │   │                           #   loudness/true-peak conformance
│   │   └── data/                   #   platform profiles, reference curves
│   │
│   └── render/                     # Audio Renderer
│       ├── include/rtg/render/
│       └── src/                    #   offline block renderer, realtime preview
│                                   #   graph, resampling, dithered export
│                                   #   (WAV/FLAC/MP3), stem export
│
├── library/                        # Sound Library service (docs 05–06)
│   ├── include/rtg/library/
│   └── src/                        #   asset ingestion, hybrid query API,
│                                   #   evolution scheduler (mutate/breed/prune),
│                                   #   lineage tracking, favorites
│
├── database/                       # Sample Database (storage layer)
│   ├── include/rtg/db/
│   ├── src/                        #   SQLite access, migrations, vector index
│   │                               #   (embeddings ANN), content-addressed
│   │                               #   FLAC blob store
│   └── migrations/                 #   versioned schema files
│
├── project/                        # Project Files
│   ├── include/rtg/project/
│   └── src/                        #   .rtgproj schema (versioned JSON),
│                                   #   save/load, asset pinning, export bundle
│
├── config/                         # Configuration
│   ├── include/rtg/config/
│   ├── src/                        #   typed settings, hot reload, per-user
│   │                               #   overrides, GPU/audio device prefs
│   └── defaults/                   #   shipped default configs
│
├── models/                         # Local AI models (ONNX)
│   ├── runtime/                    #   ONNX Runtime + DirectML wrapper
│   ├── packaged/                   #   shipped .onnx files (rater, embedder,
│   │                               #   genre-fit) + model cards & versions
│   └── training/                   #   offline training scripts (not shipped)
│
├── utils/                          # Utilities
│   ├── include/rtg/utils/
│   └── src/                        #   PRNG tree (PCG64), job system, SIMD
│                                   #   helpers, logging, profiling, file I/O,
│                                   #   units (dB, LUFS, semitones), ids/hashing
│
├── plugins/                        # Future Plugins (doc 11)
│   ├── sdk/                        #   stable C ABI + C++ wrapper for engine
│   │                               #   interfaces (IComposer, ISoundDesigner…)
│   └── hosting/                    #   VST3 host integration (v3.0+)
│
├── assets/                         # Shipped seed content
│   ├── seed_library/               #   starter recipes + rendered previews
│   ├── impulse_responses/          #   reverb/cab IRs
│   └── wavetables/                 #   factory wavetable set
│
├── tests/                          # Tests
│   ├── unit/                       #   per-module (mirrors engine layout)
│   ├── integration/                #   full pipeline: params+seed → audio
│   ├── golden/                     #   determinism: seed → checksum renders
│   ├── audio_quality/              #   LUFS/true-peak/spectral conformance
│   └── benchmarks/                 #   render speed, query latency
│
├── tools/                          # Developer tooling
│   ├── recipe_lab/                 #   CLI to render/inspect a single recipe
│   ├── library_inspector/          #   dump/repair asset DB
│   └── batch_generate/             #   soak-test generation runs
│
└── docs/
    └── architecture/               #   these documents
```

## Rules that keep it production quality

1. **Dependency direction:** `ui → engine → library/database/utils`. Engines
   never include UI headers. `utils` depends on nothing internal.
2. **Public vs private:** other modules may include only `include/rtg/<mod>/`.
   Enforced by CMake target visibility.
3. **Data-driven style:** genre knowledge lives in `data/` JSON rulebooks,
   not in code, so adding a genre pack touches no engine source.
4. **Tests mirror source:** every `engine/x` has `tests/unit/x`; the golden
   determinism suite gates every merge.
5. **One artifact per stage:** generation stages exchange serializable
   artifacts (Plan, Score, Stems…), which is what makes re-roll, resume,
   and the History page possible.
