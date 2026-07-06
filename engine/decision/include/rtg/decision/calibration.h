#pragma once
// Reference Calibration (analysis-only). The user drops commercial riddim/trap
// reference tracks (WAV) in a folder; the CLI analyzes them (--analyze-refs)
// and writes calibration.json. When present, the engines target these MEASURED
// characteristics instead of the hand-tuned genre defaults.
//
// Everything here is dependency-free (no JSON library, no JUCE): a hand-rolled
// tolerant flat-object parser keeps rtg_core self-contained.
//
// Registry usage contract: Calibration::setActive() must be called BEFORE
// generation starts (SoundLibrary::load() does this from calibration.json).
// active() is then read-only during generation, so it is thread-safe enough for
// the load-before-generate flow without locking.
#include <array>
#include <optional>
#include <string>
#include "rtg/decision/params.h"       // Genre
#include "rtg/utils/audio.h"           // StereoBuffer

namespace rtg {

// Five analysis bands (edges in Hz). Shared by the analyzer and the mix engine
// so the mix corrects toward exactly the shares that were measured.
constexpr int kCalBands = 5;
inline constexpr float kCalBandEdgesHz[kCalBands + 1] = {
    20.0f, 120.0f, 500.0f, 2000.0f, 6000.0f, 20000.0f
};

// One measured profile (a genre, or the combined fallback).
struct CalibrationProfile {
    bool  present = false;                 // false => fall back to combined
    float targetLufs = -8.5f;              // integrated LUFS of the references
    float crestDb = 9.0f;                  // peak/rms over the loudest 25% blocks
    std::array<float, kCalBands> bands = { 0.42f, 0.20f, 0.20f, 0.11f, 0.07f };
    float dropBreakContrastLu = 6.0f;      // loud-25% ST mean minus quiet-40% gated mean
    float spectralTiltDbPerOct = -3.0f;    // linear fit, 60 Hz..10 kHz
    float stereoWidth = 0.30f;             // side/mid RMS ratio
    int   refCount = 0;                    // number of reference files aggregated
};

struct Calibration {
    bool perGenre = false;                 // true when riddim/trap were split out
    CalibrationProfile riddim;
    CalibrationProfile trap;
    CalibrationProfile combined;           // always-available fallback

    // Profile for a genre, falling back to the combined aggregate.
    const CalibrationProfile& forGenre(Genre g) const {
        if (perGenre) {
            const CalibrationProfile& p = (g == Genre::Trap) ? trap : riddim;
            if (p.present) return p;
        }
        return combined;
    }

    // Hand-rolled minimal JSON (flat numeric object). Tolerant: unknown keys
    // ignored, missing keys keep defaults. Returns false only on I/O failure.
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    // Process-wide active calibration. Set before generation; read during.
    static void setActive(std::optional<Calibration> c);
    static const std::optional<Calibration>& active();
};

// ---------------------------------------------------------------------------
// Measurement primitives (operate on a 48 kHz StereoBuffer). Shared by the CLI
// analyzer and the mix engine's corrective pass so both speak the same numbers.
// ---------------------------------------------------------------------------

// Normalized 5-band energy shares (sum == 1) over the loudest 25% of 400 ms
// blocks, using kCalBandEdgesHz.
std::array<float, kCalBands> measureBandShares(const StereoBuffer& audio, double sampleRate);

// Crest (dB) = 20*log10(peak/rms) measured over the loudest 25% of blocks.
float measureCrestDb(const StereoBuffer& audio, double sampleRate);

// Energy contrast proxy (LU): loudest-25% mean short-term loudness minus the
// quietest-40% gated mean (both K-weighted, 400 ms short-term windows).
float measureContrastLu(const StereoBuffer& audio, double sampleRate);

// Spectral tilt (dB per octave) via a linear fit of octave-band log energy from
// 60 Hz to 10 kHz. Negative = darker (energy falling toward the top).
float measureSpectralTiltDbPerOct(const StereoBuffer& audio, double sampleRate);

// Side/mid RMS ratio (0 = mono, larger = wider).
float measureStereoWidth(const StereoBuffer& audio);

} // namespace rtg
