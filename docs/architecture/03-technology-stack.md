# 03 — Technology Stack

Every choice is justified against the hard requirements: **Windows desktop,
modern UI, GPU acceleration when available, fast startup, audio rendering,
plugin hosting, database, vector search, local AI, fully offline.** No
cloud-first anything.

## Summary table

| Layer | Choice | Why (one line) |
|---|---|---|
| Core language | **C++20** | Realtime + offline DSP performance, JUCE/VST3/ONNX are C++-native |
| App/audio framework | **JUCE 8** | Audio engine, offline render, VST3 hosting, and GPU-accelerated native UI in one framework |
| UI rendering | **JUCE 8 + Direct2D backend** | Hardware-accelerated native dark UI, instant startup, no browser runtime |
| DSP | **JUCE DSP + custom SIMD (xsimd), pffft** | Full control over synthesis/mix/master quality |
| Local AI runtime | **ONNX Runtime + DirectML EP (CPU fallback)** | Vendor-neutral Windows GPU accel (NVIDIA/AMD/Intel), tiny footprint, offline |
| Metadata DB | **SQLite (+ FTS5)** | Zero-admin single file, transactional, embeds in-process |
| Vector search | **hnswlib (in-process ANN) over SQLite-stored embeddings** | Millisecond similarity search, no server |
| Loudness/metering | **libebur128 + custom true-peak** | Standards-correct LUFS/TP for mix & master targets |
| Audio codecs | **libFLAC, dr_wav, LAME** | Lossless asset store, WAV/MP3 export, all offline |
| Plugin hosting | **VST3 SDK via JUCE host classes** | Industry standard on Windows; roadmap item wired in from day one |
| Build | **CMake + vcpkg (manifest mode)** | Reproducible pinned deps, standard C++ ecosystem |
| Tests | **Catch2 + golden-render harness** | Fast unit tests + audio determinism gates |
| Packaging | **Inno Setup (v1), MSIX later** | Simple signed Windows installer |

## Choice-by-choice justification

### C++20 (core language)
The system is dominated by DSP: synthesis graphs, offline rendering, mix and
master chains. C++ gives deterministic performance, SIMD access, and
first-party compatibility with every key dependency (JUCE, VST3 SDK, ONNX
Runtime, SQLite, FLAC). Rust was considered — excellent for safety, but VST3
hosting, JUCE-class UI, and ONNX integration are all friction points, and the
audio-plugin talent pool is C++. C# /.NET was rejected for the engine because
GC pauses and marshaling complicate realtime preview and tight DSP loops.

### JUCE 8 (application + audio framework)
One framework covers four hard requirements at once:

- **Audio rendering:** `AudioProcessorGraph` + offline `AudioBuffer`
  processing gives faster-than-realtime block rendering and realtime preview
  from the same processor code.
- **Plugin hosting:** mature VST3 (and CLAP-adjacent) hosting classes —
  the roadmap's "VST hosting" needs no re-architecture.
- **Modern UI:** JUCE 8's Direct2D renderer is GPU-accelerated on Windows;
  custom look-and-feel is exactly how Serum-class UIs are built. The UI is
  native code → **fast startup** (target < 2 s cold), tiny memory, no
  Electron/WebView runtime.
- **Device I/O:** WASAPI/ASIO out of the box for low-latency preview.

Alternative considered: Tauri/WebView2 (React) UI + C++ engine over IPC.
Rejected for v1: two runtimes, IPC for meter/waveform streaming at 60 fps is
avoidable complexity, slower cold start, and pro-audio feel favors native.
The UI layer is isolated (views only), so a future swap remains possible.

### ONNX Runtime + DirectML (local AI)
The app uses **small discriminative models** (sound-quality rater, audio
embedder, genre-fit classifier — a few MB each), not generative audio models.
ONNX Runtime is the neutral local-inference standard; the **DirectML
execution provider accelerates on any Windows GPU** (NVIDIA, AMD, Intel,
integrated) with automatic **CPU fallback**, satisfying "GPU when available"
without CUDA lock-in. Fully offline; models ship as versioned files in
`/models/packaged`. PyTorch/libtorch rejected: 100s of MB runtime for
inference we can serve in <10 MB.

### SQLite + FTS5 (database)
Asset metadata, projects, history, statistics, evolution lineage — all
relational, all local, all small (thousands→millions of rows). SQLite is
in-process (no service to install/run → fast startup, low cost), ACID,
famously reliable, and FTS5 covers text search over tags/names. Postgres or
any server DB contradicts "offline, low operating cost, fast startup."

### hnswlib (vector search)
Similarity queries ("more sounds like this growl", palette anchoring, dedup
of near-identical assets) run over ~512-dim audio embeddings. hnswlib is a
header-light, in-process HNSW ANN index: sub-millisecond queries at library
scale, serializes to a single file loaded at startup, no server. Embeddings
themselves are stored in SQLite (source of truth); the index is a rebuildable
cache. Faiss rejected (heavier, GPU-oriented for our tiny scale);
sqlite-vec noted as a viable simpler fallback if we want one storage engine.

### DSP libraries
- **pffft** for FFTs (fast, permissively licensed) powering analysis,
  spectral processing, and convolution.
- **libebur128** for EBU R128 / LUFS metering — the mix and master engines
  are measurement-driven, so standards-correct loudness is non-negotiable.
- **xsimd / hand-tuned kernels** for hot synthesis loops.
- **libsamplerate (or custom windowed-sinc)** for high-quality resampling
  (recipe re-pitching, export sample rates).

### Codecs
- **FLAC** for the internal asset store: lossless (sounds are re-analyzed
  and re-processed; generational lossy re-encoding is unacceptable) at
  ~50–60% of WAV size — matters for an ever-growing library.
- **WAV (dr_wav)** for stems/masters; **LAME MP3** for quick shares.

### Build, test, package
- **CMake + vcpkg manifest**: every dependency pinned in `vcpkg.json`;
  one-command reproducible builds on any Windows dev box and CI.
- **Catch2** unit tests, plus a **golden-render harness**: fixed
  (params, seed, library snapshot) triples must produce bit-identical (or
  tolerance-bounded) audio — this is the determinism contract under test.
- **Inno Setup** for a signed installer in v1; MSIX when we want store
  distribution and delta updates.

## GPU strategy

GPU is an accelerator, never a requirement:

1. **UI:** Direct2D hardware acceleration (automatic).
2. **AI inference:** DirectML when a GPU exists; CPU otherwise (models are
   small enough that CPU-only remains fully usable).
3. **DSP stays on CPU.** Block-based offline rendering with SIMD is already
   faster than realtime; GPU DSP adds complexity for little gain at our
   track counts. Re-evaluate only for massive evolution batch jobs (v3+).

## Startup budget (fast startup requirement)

| Step | Budget |
|---|---|
| Exe load + JUCE init | < 400 ms |
| Config + SQLite open | < 100 ms |
| HNSW index mmap/load | < 300 ms |
| ONNX session create (lazy, background) | off critical path |
| First interactive frame | **< 1.5 s cold, < 500 ms warm** |

Models and the audio device open lazily; the Generate page renders
immediately and the first generation waits (milliseconds) on whatever isn't
warm yet.
