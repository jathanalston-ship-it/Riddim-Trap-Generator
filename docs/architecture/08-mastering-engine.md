# 08 — Mastering Engine (Fully Automated)

Turns the −6 dBTP headroom mix (doc 07) into commercial-loudness masters
for **Spotify, Apple Music, YouTube, SoundCloud, and Festival playback**.
Deterministic chain, measurement-conformed, per-platform delivery variants.

## 1. Chain

```
mix → [1] Corrective EQ (reference-curve match)
    → [2] Multiband dynamics
    → [3] Saturation / harmonic glue
    → [4] Soft clipper
    → [5] True-peak limiter
    → [6] Loudness & TP conformance loop
    → [7] Per-platform variant rendering + dither/export
```

### [1] Corrective EQ — reference matching, bounded
Long-term spectrum of the mix is compared against the genre's **reference
tonal curve** (data files derived offline from commercial riddim/trap
releases; shipped as curves, not audio). Corrections are gentle and
clamped: ≤ ±2.5 dB per band, ≤ 6 bands, Q ≤ 1.5, nothing below 30 Hz except
a fixed 20 Hz HP. Darkness/Brightness plan values bias the target curve tilt
±1 dB/octave so masters honor the user's intent, not just the reference.

### [2] Multiband dynamics (4 bands: 0–120, 120–600, 600–3k, 3k–20k)
Slow "glue" downward compression, 1–2 dB GR per band, plus low-band
upward-leveling to stabilize sub energy across drop bars. Band GR limits
are hard-capped so multiband can never repaint the mix balance.

### [3] Saturation
Equal-loudness-matched tape/console stage for density (drive ∝ Aggression,
small range). Adds the harmonics that let the limiter work less.

### [4] Soft clipper
Pre-limiter clipper takes 1–3 dB of transient peaks (drums/bass attacks) —
the modern riddim loudness workhorse. Clip amount is auto-set from measured
crest: output crest target 6–8 dB into the limiter.

### [5] True-peak limiter
Lookahead (1.5–2 ms), 4× oversampled, program-dependent release, ceiling
per platform profile. Style guard: average GR ≤ 3 dB in drops (clipper
should be doing the heavy lifting; if GR exceeds guard, the conformance
loop shifts work to the clipper).

### [6] Conformance loop (≤ 3 passes)
Measure integrated LUFS, short-term max, true peak, PLR; adjust limiter
input gain / clip drive by clamped deltas until the active profile's window
is hit. Deterministic and convergent.

### [7] Export
Per-platform renders + dither (TPDF) at target bit depth; embedded
metadata; optional stems master.

## 2. Platform profiles (data files)

| Profile | Integrated LUFS | True peak ceiling | Notes |
|---|---|---|---|
| **Spotify** | −8.5 (drop ST ≈ −6.5) | −1.0 dBTP | Normalized to −14 on playback, but genre practice masters hot; PLR preserved so it survives turn-down. |
| **Apple Music** | −8.5 | −1.0 dBTP | Sound Check −16 LUFS; same master as Spotify (one "Streaming Hot" render serves both). |
| **YouTube** | −9 | −1.0 dBTP | −14 LUFS normalization; slightly higher PLR favored for codec robustness. |
| **SoundCloud** | −7.5 | −0.7 dBTP | No normalization historically; loudest variant — the "producer flex" render. |
| **Festival / Club** | −8 | −0.5 dBTP (48 kHz WAV) | No normalization; maximized punch: extra low-band headroom check at high SPL (crest in 20–120 Hz kept ≥ 5 dB), 24-bit WAV. |

Rationale: streaming platforms normalize *down*, so chasing −14 LUFS is
unnecessary — what matters is surviving turn-down with intact punch
(healthy PLR, clean true peak ≤ −1 for lossy codec overshoot). Riddim/trap
commercial practice masters at −9…−7 integrated; profiles encode exactly
that, while the crest/PLR guards keep the result dynamic enough to sound
commercial rather than crushed.

## 3. Quality gates (hard, post-render)

- True peak never above ceiling (verified 8× oversampled).
- Inter-sample overs: 0. DC offset: 0.
- Mono fold-down loss < 1.5 dB in drops (protects festival/mono systems).
- Sub band (20–60 Hz) integrated energy within genre window.
- Codec audition: encode AAC/Opus in-process and re-check TP overshoot.

Failures re-enter the conformance loop with tightened bounds; a track can
never export in a non-conforming state.

## 4. "AI" in mastering

Like the mix engine, the chain is DSP + measurement (no ML needed for
conformance). The *decisions* — tilt bias, aggression-mapped drive, profile
selection defaults — come from the Decision Engine plan. The optional ONNX
genre-fit model runs once as an advisory A/B check ("does the master score
at least as genre-typical as the mix?"); it can flag, never modify.
