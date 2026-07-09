# Eyes & Ears — perception tooling for the RTG sound-quality loop

How the engine "hears" its own output and a reference, what that machinery can
and cannot do, and the honest limits. This is the analysis layer that drives
sound-design decisions (growl, toms, mix balance, arrangement, FX).

## The core method (and its one hard limit)

I **cannot process audio directly** in this environment — the file tools read
images/PDF, not sound — and pretrained perceptual audio models (CLAP, PANNs,
OpenL3, …) are **blocked by egress policy** (403 on every weight host; only
PyPI/npm/crates are reachable). So there is no learned "does this sound good"
oracle available.

The working substitute — and the real capability that's been built — is:

> **Render audio into high-fidelity, perceptually-organized images, then judge
> them with multimodal vision + domain knowledge.** Vision is a genuine
> perceptual channel; on a good log-frequency spectrogram you can *see* harmonic
> density, formant movement, transient sharpness, noise, spectral balance,
> ringing, and rhythmic density. Paired with DSP measurement and knowledge of
> what good riddim looks like, this **diagnoses** sound-quality defects
> reliably.

This is powerful for finding *what's wrong* and *which direction to fix*. It
does **not** deliver the final "yes, this now sounds good" verdict — that still
needs a human ear. Every defect below was found this way.

## Python tools — `tools/earview/` (numpy/scipy/librosa/sklearn; no ML weights)

| Tool | What it does |
|---|---|
| `earview.py` | Core "ears via eyes": spectrograms (incl. Bark), stem/compare/score-overlay renders, `fp` fingerprint. |
| `ears.py` | Psychoacoustic layer: K-weighted loudness (BS.1770), roughness (15–150 Hz AM = perceived gnarl), Zwicker sharpness, Bark spectrum, MFCC timbre; `ears`/`dist`/`describe`/`nearest`. |
| `dissect.py` | HPSS harmonic/percussive separation (isolate a reference's growl/sub from its drums — no stems needed) + bassline transcription (autocorrelation pitch track → notes). |
| `instruments.py` | **Full per-instrument detection + analysis**: NMF-decomposes drums (kick/snare/hat/tom/perc) and tonal (sub/growl/lead/stab/pad); per-voice timbre, pitch/root, decay, 16-step pattern. Emits an engine drum profile (`--drum-profile-out`). |
| `structure.py` | **Whole-song arrangement**: segments intro/build/drop/break/outro over time with energy/sub/drum curves; emits an engine structure profile (`--struct-profile-out`). |
| `build.py` | Build-up drum usage: per-bar onset density, subdivision acceleration, voice mix, riser level, pre-drop gap. |
| `toms.py` | Tuned-tom character: per-hit pitch, decay, tonality, pitch-glide, centroid. |
| `growlscope.py` | Growl perceptual scope: high-res log-freq spectrogram + formant-envelope-over-time panel for direct visual A/B of growl movement/character. |
| `rhythmogram.py` | Modulation rhythmogram: *where* the growl gnarl lives rhythmically (beat-relative modulation rates over time). |
| `abdiff.py` | Bark-scale A/B difference image (mine − reference), ranked spectral deficits/excesses. |
| `riddim_analyze.py` | Tempo/key/grid/section "math" of a drop. |

The engine can also **export per-lane stems** (`rtg_cli --stems <dir>`) so any
instrument can be analysed in isolation — essential when full-mix metrics are
confounded (see below).

## Engine-side "eyes" — C++, in the generation loop

Measured on the loudest drop window, stored in `CalibrationProfile`, and used as
live targets:

- **Loudness/dynamics**: integrated K-LUFS, crest, drop↔break contrast.
- **Spectrum**: 5-band shares, spectral tilt, stereo width.
- **Growl fingerprint**: odd-harmonic fraction, wobble rate, growl-band centroid.
- **Perceptual**: `hiShare` (brightness proxy), `roughness` (AM gnarl),
  `sharpness` (24-band Bark/Zwicker acum).

**Closed feedback loops** (steer synthesis toward a loaded reference):
brightness (master air/presence), dynamics (crest floor + K-LUFS), roughness
(synth candidate selection), band balance (mix EQ toward reference shares),
Zwicker sharpness (brightness tracks a reference), plus `DrumProfile` (kick/
snare/hat character) and `StructureProfile` (section lengths).

## What it can do — concrete wins

Every one of these was a defect **found by the perception tools** and then fixed
in synthesis, verified by re-measuring:

- **Growl was static/lifeless** → `growlscope` showed flat harmonics vs the
  references' moving formants; re-enabled controlled formant sweep; formant
  center matched the reference (914 → 750 Hz).
- **Mix was bright/congested** → band measurement showed inverted balance
  (10% sub / 86% mids vs the reference's 84% / 5%); strengthened the correction
  → sub 0.10 → 0.51.
- **Toms boomed** → `toms.py` measured 82 ms decay vs the reference's 14 ms;
  removed the 0.34 s decay floor → tight tribal hits.
- **Too many hats in the drop** → isolated hat stem measured ~2.6/beat vs
  ~1.9; cut the 16th-roll flurry → the drop breathes.
- **Whole-song replication**: instruments + calibration + structure profiles
  reproduce a reference's kick/sub/growl character, tempo, key, chug rhythm, and
  section form (break/outro within ~2–3 s of the reference).

## Limitations — read before trusting a number

1. **No literal hearing / no learned quality model.** Vision + DSP diagnose;
   they do not certify "sounds good." Final taste calls need the user's ears.
2. **DSP metrics measure similarity, not quality — and several are degenerate.**
   Formant-motion medians collapse to 0; centroid "instability" conflates note
   changes with timbre; low-band crest is dominated by the kick transient, not
   the sub; a metric matching the reference does not guarantee it sounds right.
3. **Measurement confounds are everywhere.** HPSS sends resonant tails to the
   harmonic side (under-reads tom/drum decay); onset detection merges fast hits
   (under-counts rolls); full-mix high-band onset counts catch snare/growl top,
   not just hats (**use `--stems`**); per-image spectrogram normalization
   exaggerates whatever's loudest.
4. **Single-reference bias.** Most targets come from one track (Seleman).
   "Correct" for one reference (e.g. a continuous sub wall) is wrong for another
   (a pumping, gapped sub) — several fixes are now calibration-gated for this
   reason, but the reference set is small.
5. **Cross-tool measurement mismatch.** The Python analyzer and the C++ synth
   measure e.g. decay differently, so absolute numbers don't line up 1:1;
   steering is directional, not a clone.

## Bottom line

The eyes are strong: a full analysis stack that reads a reference or our own
output down to individual instruments, arrangement, and psychoacoustic
character, and drives the engine toward it. The "ears" are **vision + knowledge
standing in for hearing** — excellent at *diagnosis* (it has repeatedly found
real defects that DSP stats and prior sessions missed), bounded at *final
judgement*. The reliable workflow is: measure/scope → diagnose the direction →
fix synthesis → re-measure, then **hand the audio to a human to confirm taste**.
