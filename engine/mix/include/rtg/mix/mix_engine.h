#pragma once
// Autonomous Mix Engine (doc 07): gain staging, spectral slotting EQ,
// kick→bass sidechain ducking, bus saturation/OTT-lite, stereo policy
// (mono <120 Hz), section automation (buildFilter/breakSoften/fxSend),
// headroom management. Deterministic DSP; implemented in engine/mix/src/.
#include <array>
#include "rtg/composition/score.h"
#include "rtg/utils/audio.h"

namespace rtg {

/// laneAudio: one full-length buffer per lane (empty buffers = silent lane).
/// Returns the premaster stereo mix with ~-6 dB true-peak headroom.
StereoBuffer mixDown(const std::array<StereoBuffer, kLaneCount>& laneAudio,
                     const Score& score, const Plan& plan, double sampleRate);

} // namespace rtg
