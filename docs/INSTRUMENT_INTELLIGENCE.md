# Instrument Intelligence — detect, analyze, and reproduce a reference's instruments

The engine can take a commercial reference track, **identify every instrument
in it, characterize how each one sounds and is used, and then steer generation
toward that instrument set.** This doc is the map of that end-to-end flow.

## 1. Understand — `tools/earview/instruments.py`

Classical MIR (librosa + sklearn + soundfile; no ML weights — those hosts are
network-blocked, and for a genre with a known instrument palette this is
enough). Analyzes the loudest ~40 s drop.

```
python3 tools/earview/instruments.py <ref.wav> [--bpm N] \
    [--json out.json] [--png out.png] [--drum-profile-out drums.json]
```

Pipeline:
- **HPSS** splits percussive (drums) from harmonic (tonal).
- **Drums** — NMF-decomposes the percussive spectrogram into voices and
  classifies each by its spectral template into `kick / snare / hat / tom /
  perc`, with per-voice **decay** (activation 1/e falloff), **centroid**, and a
  16-step **grid pattern**. A dedicated 35–160 Hz onset detector recovers the
  **kick** (HPSS sends the kick's sustained body to the harmonic side, so NMF on
  the percussive stem alone misses it) with decay, pitch, and pitch-drop.
- **Tonal** — NMF-decomposes the harmonic spectrogram and classifies components
  by **timbre** (odd/even harmonic ratio, roughness = 15–150 Hz AM, centroid,
  sustain) into `sub / growl / bass / lead / stab / pad`; the fundamental comes
  from a harmonic-product-spectrum. A dedicated 25–130 Hz detector reads the
  **sub** root + gate.

Output: a structured per-instrument profile (JSON) + a plain-language summary +
a per-instrument pattern image. Example (Seleman): kick 43 Hz / 22 ms punchy,
snare 3.2 kHz / 43 ms, hat 7.3 kHz, sub root **F1**, growl odd-0.55 rough mid,
high lead.

Related "eyes" that dissect a reference without stems:
- `dissect.py hpss|bass` — harmonic/percussive separation + bassline transcription.
- `rhythmogram.py` — where the growl gnarl lives rhythmically (beat-relative).
- `abdiff.py` — Bark A/B spectral difference vs a reference.

## 2. Apply — feed the analysis into generation

Two engine steering systems consume the analysis; together they cover the full
instrument set.

### Drums — `--drum-profile`
`instruments.py --drum-profile-out drums.json` emits a JSON whose keys match
`rtg::DrumProfile` (kick body/decay, snare crack decay, hat centroid/decay/
density), clamped to the profiler's sane ranges. The engine loads it and the
kick/snare/hat synthesis targets the **detected** drums instead of the baked-in
reference:

```
rtg_cli --out track.wav --drum-profile drums.json ...
```

(Before this, `DrumProfile` could be extracted but was never activated in
generation — the engine always used its builtin reference.)

**Rhythm ("how they're used").** The drum profile also carries 16-step accent
maps for the growl chug (`growlPattern0..15`) and hats (`hatPattern0..15`),
phase-aligned to the downbeat (rotated so the strongest kick step is index 0).
The composer biases its off-beat chug/hat hit *probabilities* toward these, so
the generated rhythm follows the reference's — without changing the RNG draw
order, so the no-profile render stays byte-identical and every render is
deterministic. On-beats stay forced (the genre's on-grid stomp), so this
refines the groove rather than overhauling it; the engine's default chug is
already riddim-shaped (high phase-invariant correlation with references), so a
new riddim reference nudges the specific accents rather than transforming them.

### Growl / spectral / loudness — `--analyze-refs` → `--calibration`
The C++ analyzer measures the growl fingerprint (odd-harmonic fraction, centroid,
wobble, roughness), Zwicker sharpness, band balance, crest, and loudness, and
writes `calibration.json`. The bass/mix/master synthesis targets those:

```
rtg_cli --analyze-refs <folder> --genre riddim --calib-out calibration.json
rtg_cli --out track.wav --calibration calibration.json --drum-profile drums.json ...
```

## 3. End-to-end recipe

```
# 1. understand every instrument in the reference
python3 tools/earview/instruments.py ref.wav --bpm 140 \
        --json ref_instruments.json --drum-profile-out ref_drums.json
# 2. measure growl/spectral/loudness targets
rtg_cli --analyze-refs refs/ --genre riddim --calib-out ref_calib.json
# 3. generate toward that instrument set
rtg_cli --out track.wav --genre riddim --seconds 90 \
        --drum-profile ref_drums.json --calibration ref_calib.json
```

## Honesty notes
- Steering is **directional**, not a 1:1 clone: the Python analyzer and the C++
  synth measure e.g. decay differently, so absolute numbers won't match exactly,
  and clamps keep a noisy detection from pushing the synth somewhere ugly.
- The builtin drum reference is already riddim-tuned, so a new *riddim*
  reference refines rather than swings the drums; a very different reference
  moves them more.
- Determinism is preserved: same seed + same profile → byte-identical WAV; with
  no `--drum-profile`/`--calibration` the render is byte-identical to stock.
