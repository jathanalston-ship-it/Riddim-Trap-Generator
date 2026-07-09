#!/usr/bin/env python3
"""confidence — how much to trust each measurement.

The product directive says "Estimate confidence." docs/EARS_AND_EYES.md already
documents, in prose, which of our metrics are robust and which are degenerate or
confounded. This module turns that prose into data: a default confidence prior
per metric, so every observation lands in the Knowledge Store already carrying a
trust weight. Later analyzers weight by it; a degenerate metric can no longer
quietly masquerade as ground truth.

Priors are deliberately conservative and grounded in the limitations doc:
  * K-weighted loudness (BS.1770)        -> high   (well-defined, robust)
  * Zwicker sharpness (24-band Bark)     -> good   (proper filterbank now)
  * roughness (15-150 Hz AM proxy)       -> medium (directional, not absolute)
  * bark band shares                     -> medium (per-image norm confound)
  * mfcc timbre                          -> medium-low
  * formant-motion median                -> LOW    (documented: collapses to 0)
  * low-band crest                       -> LOW    (dominated by kick transient)
  * full-mix hat/onset rate              -> LOW    (confounded; use stems)
Anything not listed defaults to NEUTRAL (0.5).

A measurement taken on an ISOLATED STEM ('window' starts with 'stem:') is more
trustworthy than the same measurement on the full mix, because the biggest
confounds in the doc are mix-bleed ones — so stems get a bonus.
"""

NEUTRAL = 0.5

# metric prefix -> prior confidence. Longest matching prefix wins, so
# 'onset.hat_rate' can be low while 'onset' generally is neutral.
_PRIORS = {
    "loudness.integrated_lufs": 0.90,
    "loudness":                 0.85,
    "sharpness.zwicker":        0.75,
    "sharpness":                0.70,
    "spectrum.band_share":      0.70,
    "bark":                     0.65,
    "spectrum.tilt":            0.65,
    "roughness":                0.60,
    "growl.odd_harmonic_frac":  0.55,
    "growl.wobble_rate":        0.55,
    "mfcc":                     0.50,
    "growl.centroid":           0.45,
    "onset":                    0.35,   # full-mix onset counts are confounded
    "crest.low_band":           0.30,   # kick transient, not the sub
    "formant.motion":           0.20,   # documented degenerate (median -> 0)
}

# Metrics that are only trustworthy on an isolated stem, and get penalised on a
# full mix even beyond their base prior.
_STEM_SENSITIVE = ("onset", "crest.low_band", "formant.motion", "growl.centroid")


def _base_prior(metric: str) -> float:
    # A prior keyed on 'formant.motion' should match both 'formant.motion.median'
    # and 'formant.motion_median' — a metric leaf may be joined with '.' or '_'.
    best = None
    for prefix, conf in _PRIORS.items():
        if metric == prefix or metric.startswith(prefix + ".") or metric.startswith(prefix + "_"):
            if best is None or len(prefix) > best[0]:
                best = (len(prefix), conf)
    return NEUTRAL if best is None else best[1]


def confidence_for(metric: str, window: str | None = None) -> float:
    """Return a 0..1 confidence prior for `metric`, adjusted for `window`.

    Isolated-stem windows ('stem:growl', 'stem:sub', ...) lift confidence for
    stem-sensitive metrics (the mix-bleed confound is gone); full-mix windows
    leave those metrics at their low base prior.
    """
    conf = _base_prior(metric)
    on_stem = bool(window) and window.startswith("stem:")
    if any(metric.startswith(s) for s in _STEM_SENSITIVE):
        if on_stem:
            conf = min(1.0, conf + 0.35)   # confound removed -> trust it more
    return round(max(0.0, min(1.0, conf)), 3)


if __name__ == "__main__":
    # Quick human-readable dump of the confidence model.
    samples = [
        ("loudness.integrated_lufs", None),
        ("sharpness.zwicker_acum", None),
        ("roughness.am_gnarl", None),
        ("onset.hat_rate", "full"),
        ("onset.hat_rate", "stem:hat"),
        ("crest.low_band", "full"),
        ("crest.low_band", "stem:sub"),
        ("formant.motion_median", "stem:growl"),
        ("some.unknown.metric", None),
    ]
    for m, w in samples:
        print(f"  {confidence_for(m, w):.2f}  {m}  [{w or 'full'}]")
