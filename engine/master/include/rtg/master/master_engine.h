#pragma once
// Automated Master Engine (doc 08, v1 chain): gentle tilt EQ → saturation →
// soft clipper → lookahead true-peak limiter → loudness conformance loop to
// plan.masterTargetLufs with -1.0 dBTP ceiling. Implemented in engine/master/src/.
#include "rtg/decision/plan.h"
#include "rtg/utils/audio.h"

namespace rtg {

struct MasterStats {
    float integratedLufs = -70.0f;
    float truePeakDb = -70.0f;
    float crestDb = 0.0f;
};

/// Consumes the premaster mix, returns the finished master.
/// stats (optional) receives final measurements.
StereoBuffer masterize(const StereoBuffer& premaster, const Plan& plan,
                       double sampleRate, MasterStats* stats);

/// K-weighted-ish integrated loudness estimate (shared by mix/master/tests).
float measureLufs(const StereoBuffer& audio, double sampleRate);
/// 4x-oversampled true peak in dBTP.
float measureTruePeakDb(const StereoBuffer& audio);

} // namespace rtg
