#pragma once
// Private interface between the synth dispatch (synth_engine.cpp) and the
// per-role factories/renderers spread across bass_synth.cpp, drum_synth.cpp,
// and fx_synth.cpp. Internal to the sound-design engine only.
#include <cstdint>
#include "rtg/synth/recipe.h"
#include "rtg/utils/audio.h"
#include "rtg/utils/rng.h"

namespace rtg::synth {

// Per-note render context. `gateSec` is the note-on duration (used to gate
// sustained roles); `lenSec` is the full buffer length to render (tail included).
struct Voice {
    double sr = 48000.0;
    double freqHz = 55.0;   // fundamental for pitched roles (drums ignore)
    double gateSec = 0.5;
    double lenSec = 0.5;
    float velocity = 1.0f;  // 0..1
    float mod = 0.0f;       // articulation macro 0..1
    float bendSemis = 0.0f; // glide over the note
    uint64_t seed = 1;      // per-note deterministic seed
};

// --- Factories (draw named params into recipe.p) ---
Recipe makeGrowlRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeScreechRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeSubRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeBass808Recipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeKickRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeSnareRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeHatClosedRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeHatOpenRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makePercRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeMelodyLeadRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makePadRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeRiserRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeDownlifterRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeImpactRecipe(float aggr, float dark, float nov, Rng& rng);
Recipe makeCrashRecipe(float aggr, float dark, float nov, Rng& rng);

// --- Renderers (return a fresh buffer of round(lenSec*sr) samples) ---
StereoBuffer renderGrowl(const Recipe&, const Voice&);
StereoBuffer renderScreech(const Recipe&, const Voice&);
StereoBuffer renderSub(const Recipe&, const Voice&);
StereoBuffer renderBass808(const Recipe&, const Voice&);
StereoBuffer renderKick(const Recipe&, const Voice&);
StereoBuffer renderSnare(const Recipe&, const Voice&);
StereoBuffer renderHatClosed(const Recipe&, const Voice&);
StereoBuffer renderHatOpen(const Recipe&, const Voice&);
StereoBuffer renderPerc(const Recipe&, const Voice&);
StereoBuffer renderMelodyLead(const Recipe&, const Voice&);
StereoBuffer renderPad(const Recipe&, const Voice&);
StereoBuffer renderRiser(const Recipe&, const Voice&);
StereoBuffer renderDownlifter(const Recipe&, const Voice&);
StereoBuffer renderImpact(const Recipe&, const Voice&);
StereoBuffer renderCrash(const Recipe&, const Voice&);

// Dispatch helpers implemented in synth_engine.cpp.
Recipe makeRecipeForRole(Role role, float aggr, float dark, float nov, Rng& rng);
StereoBuffer renderRole(Role role, const Recipe&, const Voice&);
// Buffer length (seconds) a lane note should render given its gate + role tail.
double laneLenSec(Role role, double gateSec, const Recipe&);
// Whether a role is gated by note length (vs. free-running one-shot).
bool roleIsSustained(Role role);

} // namespace rtg::synth
