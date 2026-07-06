# Riddim / Dubstep Growl-Bass Technique Notes

Distilled from production tutorials (Rocket Powered Sound "5 Ways To Make Growl
Bass In Serum", ADSR "Skrillex Growl in Serum", Bassgorilla "Super Growl",
Unison/Soundbridge formant-filter guides, CCRMA source-filter formant tables,
EDMTemplates/MusicRadar neuro-bass resampling, EDMProd "How to Make Riddim").
Each bullet = the real-world technique → how we replicate it in our fixed-point
C++ DSP (`dsp.h` primitives + `bass_synth.cpp` renderers).

## 1. Formant / vowel filtering = the "talking" secret
- **Technique**: chain/parallel a small bank of narrow resonant band-passes tuned
  to the first 2-3 vowel formants (F1/F2/F3). Morphing the formant frequencies
  (A↔E↔I↔O↔U) makes the synth pronounce vowels — the "wah/talk". High resonance
  (Q ≈ 4-12) makes the vocal peaks pronounced.
- **Formant table used** (sung-vowel F1/F2/F3 in Hz):
  A 730/1090/2440 · E 530/1840/2480 · I 390/1990/2550 · O 570/840/2410 · U 440/1020/2240.
- **Our DSP**: `dsp::Vowel` table + `dsp::FormantBank` (3 parallel RBJ band-passes,
  per-formant gains). A morph position `0..1` walks a per-recipe *vowel path*
  (2-3 chosen vowels) and linearly interpolates F1/F2/F3. The morph is driven by
  `note.mod` (bias) **plus** a synced shape/step LFO (the rhythmic talk). Formant
  path is blended with the direct path via `formantMix`.

## 2. Rich, dense source (harmonics 100 Hz–4 kHz before filtering)
- **Technique**: growls start from harmonically dense wavetables; producers morph
  wavetable position and stack saw/square/PWM + FM feedback so the formant filter
  has energy to carve. Resampling adds notches → "watery" movement.
- **Our DSP**: per-voice source = FM (carrier+modulator with light feedback)
  **blended** with band-limited saw + square + variable-width pulse (`PhaseOsc::pulse`).
  A `srcMorph` param crossfades saw→square→pulse for wavetable-like variety;
  unison detune thickens. Dense harmonics guaranteed before the formant stage.

## 3. Rhythmic (stepped / ramp) modulation, not a plain sine wobble
- **Technique**: modulate with tempo-synced stepped S&H, rising/falling ramps,
  triplet rates — rhythmic movement reads as "talking", a slow sine reads as a
  boring wobble ("fart").
- **Our DSP**: `dsp::ShapeLFO` adds shapes 0-5 = sine/tri/ramp-up/ramp-down/
  square/stepped-S&H (deterministic per-voice pattern of N steps). A per-recipe
  `lfoShape` + `lfoSteps` pick the pattern; rate is baked into the recipe in Hz
  (musical rates ≈ 1.5-9 Hz, i.e. 1/8–1/2 note at 140-ish BPM). Drives the vowel
  morph + a secondary filter wiggle.

## 4. Distortion staging with mud control
- **Technique**: pre-drive → cut low-mid mud (~300 Hz, 2-6 dB) between stages →
  filter/formant → post-drive (mix of soft-clip + wavefold) → presence lift
  (700 Hz–2.5 kHz) → high-pass ~90-110 Hz (sub owns the lows).
- **Our DSP**: `tanh(x*drivePre)` → peak dip at ~300 Hz (`mudDb`) → formant/SVF
  section → `lerp(tanh, shFold, wsMix)` post-drive → 700–2500 Hz presence peak
  (`midGainDb`) → `Biquad` high-pass at `hpFreq` (90-110). Optional `OTTLite`
  3-band upward/downward compressor to glue and push the formant mids forward.

## 5. Movement polish
- **Technique**: slow phaser / all-pass sweep adds rumble movement; light chorus
  on the highs; small, mono-compatible stereo width.
- **Our DSP**: subtle 3-stage `Allpass1` phaser at a low rate (independent of the
  talk LFO), `width` kept small with `widen()` (mono-safe mid/side).

## 6. Screech = same formant thinking, higher + hard-sync scream
- **Technique**: push formants up an octave, add hard-sync / fold for the scream,
  less static ring more vocal.
- **Our DSP**: `renderScreech` reuses `FormantBank` with an upward octave bias on
  the vowel path, hard-sync flavour via `shFold` + comb, phaser sweep retained but
  the morph (mod + LFO) now animates the formant peaks so it screams rather than
  drones.

## Rater alignment (analysis.cpp)
- Reward **movement** = normalized variance of the 300–2500 Hz band across frames
  (vocal peaks that MOVE). Target movement ≥ 0.35.
- Reward **presence** = 600–2500 Hz share of total (proxied by the mid-high
  `aggression` feature already computed), target band ~0.15–0.45.
- Reward vocal-range **centroid** (~350–1900 Hz for growl, shifted up for screech).
- **Penalize** sub-heavy spectra (subRatio > 0.5) and static spectra (low movement).
</content>
</invoke>
