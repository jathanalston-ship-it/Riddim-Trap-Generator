# Reference Calibration

Riddim Trap Generator ships with hand-tuned targets for loudness and tonal
balance. **Reference calibration** lets you replace those defaults with the
*measured* characteristics of commercial tracks you like: point the app at a few
reference songs and the mix/master engines will target the loudness, tonal
balance and spectral tilt of your references instead of the built-in numbers.

It is **fully local and analysis-only** — no audio from your references is ever
copied, embedded, or uploaded. Only a handful of aggregate numbers are written
to a small `calibration.json` file.

## 1. Calibrate in the app (recommended)

Everything happens on the **Settings** page under **Reference Calibration** — no
terminal, no file conversion.

1. Pick the profile the analysis should update: **Riddim**, **Trap**, or
   **Both** (a single combined profile used for all genres).
2. Click **Add reference tracks…** and multi-select 3–5 commercial songs you
   love. Supported formats: **MP3, WAV, FLAC, AIFF, Ogg** (MP3/AAC decoding is
   available on Windows via Media Foundation).
3. The app analyzes each file on a background thread — you'll see a progress bar
   and the current filename. When it finishes, calibration is **active
   immediately**: the very next generation targets your references.

The status line shows what's active, e.g.
`Active: riddim 4 refs · trap 3 refs · target -8.9 LUFS`, and the result line
reports any files that could not be decoded (e.g. `2 files could not be
decoded.`). Use **Clear calibration** to return to the built-in defaults.

Tips:

- More references give a more robust median. Use full tracks (with drops and
  breaks) — the analyzer measures the loudest sections, so intros/outros don't
  skew the result.
- Any sample rate is fine; files are resampled to 48 kHz internally. Very long
  files are analyzed over their middle 4 minutes to bound memory.
- Re-running for a genre **replaces** that genre's profile and preserves the
  other genre already stored.

The app writes `calibration.json` into its library directory
(`%APPDATA%\RiddimTrapGenerator\library` on Windows) and auto-loads it on every
launch, so the calibration persists across restarts.

## 2. Headless / batch alternative (CLI)

For scripting, CI, or headless machines you can still run the analyzer from the
command line. It shares the exact same measurement and aggregation code as the
in-app panel, so results are identical. References must be **WAV** files in a
folder (16/24/32-bit PCM or float, mono or stereo, 44.1 or 48 kHz):

```
rtg_cli --analyze-refs C:\refs --genre riddim --calib-out "%APPDATA%\RiddimTrapGenerator\library\calibration.json"
```

- `--analyze-refs <folder>` — folder of reference WAVs to analyze.
- `--genre riddim|trap` — *(optional)* store the result as a per-genre profile.
  Run once per genre against genre-specific folders to build both; each run
  preserves the other genre already in the file. Omit `--genre` to store a
  single **combined** profile used for all genres.
- `--calib-out <path>` — where to write `calibration.json`. If omitted, it is
  written next to `--library` (if given), otherwise to `./calibration.json`.

Writing to the library directory (as above) is all you need — the engines
auto-load `calibration.json` from there at startup. The analyzer prints a
per-file table and the aggregate (median) it stored:

```
file                            LUFS  crest    sub   loM   mid   hiM    hi   contr   tilt  width
track_a.wav                     -8.6    9.1   0.44  0.21  0.19  0.10  0.06     6.1  -3.10   0.28
track_b.wav                     -9.1    8.7   0.41  0.22  0.20  0.11  0.06     5.7  -2.95   0.31
[rtg] aggregate (median of 2): LUFS=-8.9 crest=8.9 bands=[0.42 0.21 0.19 0.11 0.06] ...
```

## 3. What gets calibrated

`calibration.json` stores per genre (with a combined fallback):

| Field | Meaning | Where it's used |
|---|---|---|
| `targetLufs` | Integrated loudness of the references | Master target loudness (clamped to −14…−6.5 LUFS) |
| `spectralTiltDbPerOct` | Spectral slope, 60 Hz–10 kHz | Master tilt-EQ bias (clamped to ±2 dB of shelf) |
| `band0…band4` | Energy shares in 20–120, 120–500, 500–2k, 2k–6k, 6k–20k Hz over the loudest 25% | Mix band-balance correction (one clamped ±2.5 dB pass) |
| `crestDb` | Peak-to-RMS over the loudest sections | Reported (informational) |
| `dropBreakContrastLu` | Loud vs. quiet energy contrast | Reported (informational) |
| `stereoWidth` | Side/mid RMS ratio | Reported (informational) |
| `growlOdd` | Odd-harmonic fraction of the loud bass (0–1; a square wave is odd-dominant, ~>0.7) | Biases the riddim growl toward a square source (`srcMorph`/`carMix`) |
| `growlWobbleHz` | Dominant wobble/gate rate of the growl band (Hz) | Biases the growl's tempo-synced gate division (`gateDiv`/`gateDepth`) |
| `growlCentroidHz` | Spectral centroid of the growl band (Hz, brightness) | Biases the growl's body filter + high shelf (`lpMul`/`highShelfDb`) |
| `refCount` | How many references were aggregated | Reported |

### Growl matching (reading a reference's bass)

Beyond the overall mix balance, the analyzer measures a small **growl
fingerprint** from the loudest bass sections of each reference: how *square*
(odd-harmonic) the bass is, how fast it *wobbles*, and how *bright* it is. When a
riddim calibration is active, the growl synth nudges its freshly-drawn recipe
toward those numbers — so calibrating to a hard, square, 16th-note-wobbling
reference makes newly-generated (and A/B-trainer) riddim growls lean the same
way. The nudge is applied **after** the recipe's random draws, so it never breaks
determinism: the same seed with the same `calibration.json` still renders an
identical file. Like every other calibration adjustment it is **clamped** to a
musical range, so an extreme reference colors the growl without ever destabilizing
it.

Every engine adjustment is **clamped**, so an unusual reference can shift the
sound but can never break the output: loudness stays in a sane window, tilt and
band corrections are bounded to a couple of dB. Output remains fully
deterministic — the same seed with the same `calibration.json` renders an
identical file.

## 4. Reset

To go back to the built-in defaults, click **Clear calibration** on the Settings
page (or **delete `calibration.json`** from the library directory manually).
With no file present the engines use their hand-tuned targets again. Re-running
the analyzer — in the app or via `--analyze-refs` — overwrites the file
(updating just the analyzed genre and the combined fallback).
