# Drum Synthesis Techniques (dubstep / riddim / trap)

Research distilled for the drum overhaul. Goal: **punchy, clean, modern** drums.
Power comes from **layering + envelope/transient shaping + EQ**, NOT from drive.
Distortion is the enemy that made the old kit sound "cheesy / bogus".

## Universal principles
- Every percussive sound = **transient (click)** + **body (tone)** + **tail (decay)**.
  Design and envelope each layer separately, then balance.
- Loudness/"punch" is a **crest-factor** phenomenon: a hot, short transient over a
  controlled body. Transient shaping and EQ carving beat saturation for punch.
- Saturation, if used at all, is **glue** — output-compensated, gentle
  (tanh drive ~1.0-1.6), and **low-passed** so it never adds >6-8 kHz "fizz".
  Heavy clipping of a sine is what produces the cheap buzz.

## Kick (dubstep/trap)
- **Body**: pure **sine** with an **exponential pitch drop**, start 120-160 Hz →
  40-55 Hz within ~15-45 ms. Amplitude env: fast attack, 150-350 ms decay
  (trap = longer/boomier, riddim = tighter). The sub fundamental is what you feel.
- **Punch**: real kicks are harmonically rich *only during the attack*. Add a few
  low-order harmonics (2f-5f, ≤ ~250 Hz) on a **short** envelope, and an optional
  **mid "knock"** tone/burst (~120-250 Hz) — this moves early energy into the
  low-mids for body without any high-frequency fizz. Then **low-pass the body
  ~3-4 kHz** to guarantee no buzz.
- **Click layer**: separate, 1-3 ms, band-limited (2-4 kHz filtered-noise burst or
  damped sine ping) for beater attack. Keep it band-limited/low-passed so it
  sharpens the onset without adding hiss above ~7 kHz.
- **No heavy clip.** Optional gentle glue only.

## Snare (three-layer, transient-forward; riddim = tuned/metallic but still punchy)
- **Tuned body**: one or two sines/tri around **170-240 Hz** (a detuned partner
  ~1.5x adds thickness), 60-120 ms — the "thump/tone".
- **Crack transient**: **band-pass noise 2-5 kHz**, very short (5-15 ms), **hot**.
  This is the snap that cuts through a mix; velocity mainly rides its level.
- **Tail**: **high-passed noise** (120-350 ms) with a **falling low-pass** so the
  tail darkens as it decays; velocity also rides tail length.
- **Metal/comb layer** for riddim character: only at high aggression (>0.75) and
  **quieter than before** — a subtle comb-resonated noise, not a ring-out.
- Reverb/density belongs on the tail/body, never on the transient.

## Hats (metallic, velocity-sensitive)
- Classic 808 recipe: **~6 detuned square oscillators** summed → a detuned,
  inharmonic metallic smear, then **high-passed 6-9 kHz**. FM / **ring-modulation
  with noise** adds shimmer and grit cheaply.
- Sharp env, no sustain: **closed 25-60 ms**, **open 180-400 ms**.
- Articulation (`note.mod`, varied by the composer) should tilt **both brightness
  (HP cutoff) and decay** — real hats open/close and brighten with playing.
- Per-note level jitter (velocity) already provides human variation.

## Perc / Crash
- Perc: band-passed tone + a little noise, tight decay; keep it clean.
- Crash: **less white-noise wash, more shimmer** — high-pass ~5 kHz, add a slight
  **comb** for metallic partials, high-shelf sparkle, **1.2-2.5 s** decay.

## Sources
- SoundBridge — *A Quick Guide to Designing Dubstep Drums* (transient vs tone / click vs body): https://www.soundbridge.io/designing-dubstep-drums
- Sound On Sound — *Dubstep Drums* (kick sub 50-60 Hz, pitch-envelope timing): https://www.soundonsound.com/techniques/dubstep-drums
- MusicRadar — *Multi-layered D&B snare in Serum* (body sine thump + noise crack + tail): https://www.musicradar.com/how-to/how-to-design-a-multi-layered-drum-and-bass-snare-in-serum
- Sweetwater InSync — *Synthesizing Your Own Percussion Samples* (body/pitch-sweep/transient split, 40-60 Hz sine kick, tight envelopes): https://www.sweetwater.com/insync/synthesizing-and-creating-your-own-percussion-samples/
- Noise Engineering — *Patching hihats from scratch with FM* & the 808's 6 detuned squares + ring-mod for metallic hats: https://noiseengineering.us/blogs/loquelic-literitas-the-blog/creating-fm-hats/
- MasteringBox — *Using Transient Shapers for Punchy Drums* (punch = transient shaping, not distortion): https://www.masteringbox.com/learn/transient-shaper-techniques
</content>
</invoke>
