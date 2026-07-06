# Reference Calibration

Riddim Trap Generator ships with hand-tuned targets for loudness and tonal
balance. **Reference calibration** lets you replace those defaults with the
*measured* characteristics of commercial tracks you like: drop a few reference
WAVs in a folder, run one command, and the mix/master engines will target the
loudness, tonal balance and spectral tilt of your references instead of the
built-in numbers.

It is **fully local and analysis-only** — no audio from your references is ever
copied, embedded, or uploaded. Only a handful of aggregate numbers are written
to a small `calibration.json` file.

## 1. Rip your references to WAV

Collect 3–10 commercial riddim (or trap) tracks whose sound you want to match
and export/convert each to a **WAV** file into a single folder, e.g.
`C:\refs`. Supported formats:

- 16-bit, 24-bit or 32-bit PCM, or 32-bit float
- Mono or stereo
- 44.1 kHz or 48 kHz (44.1 kHz is resampled automatically)

More references give a more robust median. Use full tracks (with drops and
breaks) — the analyzer measures the loudest sections, so intros/outros don't
skew the result.

## 2. Run the analyzer

From a command prompt (Windows paths shown):

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

The engines auto-load `calibration.json` from the **library directory** at
startup, so writing it there (as above) is all you need. The GUI's library
directory is `%APPDATA%\RiddimTrapGenerator\library` on Windows.

The analyzer prints a per-file table and the aggregate (median) it stored:

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
| `refCount` | How many references were aggregated | Reported |

Every engine adjustment is **clamped**, so an unusual reference can shift the
sound but can never break the output: loudness stays in a sane window, tilt and
band corrections are bounded to a couple of dB. Output remains fully
deterministic — the same seed with the same `calibration.json` renders an
identical file.

## 4. Reset

To go back to the built-in defaults, **delete `calibration.json`** from the
library directory. With no file present the engines use their hand-tuned
targets again. Re-running `--analyze-refs` overwrites the file (updating just
the analyzed genre and the combined fallback).
