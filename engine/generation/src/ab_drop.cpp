// A/B full-drop rendering: pipeline -> slice the drop -> measure. See ab_drop.h.
#include "rtg/generation/ab_drop.h"

#include <algorithm>
#include <atomic>

#include "rtg/decision/calibration.h"    // measure* + GrowlFingerprint
#include "rtg/master/master_engine.h"    // measureLufs

namespace rtg {

ABFeat measureABFeat(const StereoBuffer& audio, double sr) {
    ABFeat f;
    if (audio.empty() || sr <= 0.0) return f;
    f.lufs     = measureLufs(audio, sr);
    f.crest    = measureCrestDb(audio, sr);
    f.contrast = measureContrastLu(audio, sr);
    f.tilt     = measureSpectralTiltDbPerOct(audio, sr);
    f.width    = measureStereoWidth(audio);
    std::array<float, kCalBands> bands = measureBandShares(audio, sr);
    for (int b = 0; b < 5 && b < kCalBands; ++b) f.bands[b] = bands[b];
    GrowlFingerprint gf = measureGrowlFingerprint(audio, sr);
    f.growlOdd = gf.odd; f.growlWobbleHz = gf.wobbleHz; f.growlCentroidHz = gf.centroidHz;
    return f;
}

namespace {
void fillParams(float p[AB_ParamCount], const Params& pr, double bpm) {
    p[AB_Energy]     = float(pr.energy)     / 100.0f;
    p[AB_Aggression] = float(pr.aggression) / 100.0f;
    p[AB_Darkness]   = float(pr.darkness)   / 100.0f;
    p[AB_Complexity] = float(pr.complexity) / 100.0f;
    p[AB_Melody]     = float(pr.melody)     / 100.0f;
    p[AB_Chaos]      = float(pr.chaos)      / 100.0f;
    p[AB_Bpm]        = float(bpm);
}
} // namespace

DropClip renderDropClip(const Params& base, SoundLibrary& lib, double seconds, uint64_t seed) {
    DropClip clip;
    clip.seed  = seed ? seed : 1;
    clip.genre = (base.genre == Genre::Trap) ? 1 : 0;
    seconds = std::max(2.0, std::min(seconds, 40.0));

    Params p = base;
    p.seed = clip.seed;
    if (p.lengthSec < 45.0) p.lengthSec = 45.0;   // ensure the structure has a drop

    std::atomic<bool> cancel{false};
    // Read-only render: don't ingest/pollute the library, so A and B are drawn
    // from the same snapshot and each clip is exactly reproducible from its seed.
    std::optional<GenerationResult> res = generateTrack(p, lib, cancel, nullptr, /*ingest*/ false);
    if (!res || res->master.empty()) return clip;

    const double sr = res->sampleRate;
    clip.sampleRate = sr;
    fillParams(clip.params, p, res->plan.bpm);

    // Slice the first Drop section (fallback: ~40% into the track).
    double startSec = double(res->master.size()) / sr * 0.4;
    double lenSec   = seconds;
    for (const SectionInfo& s : res->sections)
        if (s.type == SectionType::Drop) { startSec = s.startSec; lenSec = std::min(seconds, s.lengthSec); break; }

    size_t s0 = size_t(std::max(0.0, startSec) * sr);
    if (s0 >= res->master.size()) s0 = 0;
    size_t n = size_t(std::max(0.5, std::min(lenSec, seconds)) * sr);
    n = std::min(n, res->master.size() - s0);

    StereoBuffer buf(n);
    for (size_t i = 0; i < n; ++i) { buf.l[i] = res->master.l[s0 + i]; buf.r[i] = res->master.r[s0 + i]; }
    clip.feat  = measureABFeat(buf, sr);
    clip.audio = std::move(buf);
    clip.ok    = true;
    return clip;
}

ABResult makeABResult(const DropClip& a, const DropClip& b, int winner, long long ts) {
    ABResult r;
    r.ts = ts;
    r.genre = a.genre;
    r.seedA = a.seed; r.seedB = b.seed;
    for (int k = 0; k < AB_ParamCount; ++k) { r.paramsA[k] = a.params[k]; r.paramsB[k] = b.params[k]; }
    r.featA = a.feat; r.featB = b.feat;
    r.winner = (winner == 1) ? 1 : 0;
    return r;
}

} // namespace rtg
