// Sound Design Engine dispatch: role<->lane mapping, recipe factory dispatch,
// mutation, and the two public render entry points (preview + lane).
// Per-role factories/renderers live in bass_synth.cpp, drum_synth.cpp,
// fx_synth.cpp. Analysis + rating live in analysis.cpp.
#include <algorithm>
#include <cmath>
#include <iterator>
#include "dsp.h"
#include "voices.h"
#include "rtg/synth/synth_engine.h"

namespace rtg::synth {
using namespace dsp;

// ------------------------------------------------------------------ dispatch
Recipe makeRecipeForRole(Role role, float a, float d, float n, Rng& rng) {
    switch (role) {
        case Role::Growl:      return makeGrowlRecipe(a, d, n, rng);
        case Role::Screech:    return makeScreechRecipe(a, d, n, rng);
        case Role::Sub:        return makeSubRecipe(a, d, n, rng);
        case Role::Bass808:    return makeBass808Recipe(a, d, n, rng);
        case Role::Kick:       return makeKickRecipe(a, d, n, rng);
        case Role::Snare:      return makeSnareRecipe(a, d, n, rng);
        case Role::HatClosed:  return makeHatClosedRecipe(a, d, n, rng);
        case Role::HatOpen:    return makeHatOpenRecipe(a, d, n, rng);
        case Role::Perc:       return makePercRecipe(a, d, n, rng);
        case Role::MelodyLead: return makeMelodyLeadRecipe(a, d, n, rng);
        case Role::Pad:        return makePadRecipe(a, d, n, rng);
        case Role::Riser:      return makeRiserRecipe(a, d, n, rng);
        case Role::Downlifter: return makeDownlifterRecipe(a, d, n, rng);
        case Role::Impact:     return makeImpactRecipe(a, d, n, rng);
        case Role::Crash:      return makeCrashRecipe(a, d, n, rng);
        default:               return makeGrowlRecipe(a, d, n, rng);
    }
}

StereoBuffer renderRole(Role role, const Recipe& rc, const Voice& v) {
    switch (role) {
        case Role::Growl:      return renderGrowl(rc, v);
        case Role::Screech:    return renderScreech(rc, v);
        case Role::Sub:        return renderSub(rc, v);
        case Role::Bass808:    return renderBass808(rc, v);
        case Role::Kick:       return renderKick(rc, v);
        case Role::Snare:      return renderSnare(rc, v);
        case Role::HatClosed:  return renderHatClosed(rc, v);
        case Role::HatOpen:    return renderHatOpen(rc, v);
        case Role::Perc:       return renderPerc(rc, v);
        case Role::MelodyLead: return renderMelodyLead(rc, v);
        case Role::Pad:        return renderPad(rc, v);
        case Role::Riser:      return renderRiser(rc, v);
        case Role::Downlifter: return renderDownlifter(rc, v);
        case Role::Impact:     return renderImpact(rc, v);
        case Role::Crash:      return renderCrash(rc, v);
        default:               return renderGrowl(rc, v);
    }
}

bool roleIsSustained(Role role) {
    switch (role) {
        case Role::Growl: case Role::Screech: case Role::Sub:
        case Role::Pad: case Role::MelodyLead:
            return true;
        default:
            return false;
    }
}

double laneLenSec(Role role, double gateSec, const Recipe& rc) {
    switch (role) {
        case Role::Growl:   return gateSec + 0.2;
        case Role::Screech: return gateSec + 0.2;
        case Role::Sub:     return gateSec + 0.15;
        case Role::Pad:     return gateSec + rc.get("rel", 0.5f) + 0.4;
        case Role::MelodyLead:
            return std::max(gateSec, double(rc.get("decay", 0.3f)) * 3.0) + rc.get("rel", 0.1f) + 0.1;
        case Role::Bass808:
            return std::max(gateSec, 0.1) + double(rc.get("decay", 1.0f)) * 3.0;
        case Role::Kick:    return double(rc.get("bodyDecay", 0.28f)) * 4.0 + 0.1;
        case Role::Snare:   return double(rc.get("noiseDecay", 0.25f)) * 4.0 + 0.1;
        case Role::HatClosed:
        case Role::HatOpen:
        case Role::Perc:    return double(rc.get("decay", 0.1f)) * 5.0 + 0.05;
        case Role::Crash:   return double(rc.get("decay", 2.2f)) * 3.0 + 0.2;
        case Role::Impact: {
            double m = std::max({double(rc.get("boomDecay", 0.45f)),
                                 double(rc.get("noiseDecay", 0.25f)),
                                 double(rc.get("rumbleDecay", 0.3f))});
            return m * 4.0 + 0.2;
        }
        case Role::Riser:
        case Role::Downlifter: return gateSec + 0.05;
        default: return gateSec + 0.2;
    }
}

// ------------------------------------------------------------------ public API
Recipe makeRecipe(Role role, float aggression01, float darkness01,
                  float novelty01, Rng& rng) {
    float a = clampf(aggression01, 0.0f, 1.0f);
    float d = clampf(darkness01, 0.0f, 1.0f);
    float n = clampf(novelty01, 0.0f, 1.0f);
    return makeRecipeForRole(role, a, d, n, rng);
}

Recipe mutateRecipe(const Recipe& parent, float amount, Rng& rng) {
    Recipe child = parent;
    child.name = parent.name + "_m";
    child.seed = rng.next();
    amount = clampf(amount, 0.0f, 1.0f);

    for (auto& kv : child.p) {
        float g = rng.gaussian();
        float scale = std::max(std::fabs(kv.second) * 0.5f, 0.02f);
        kv.second += g * amount * scale;
    }
    // Occasionally re-draw a single param from the factory's fresh range.
    if (!child.p.empty() && rng.chance(double(amount) * 0.6)) {
        Rng fr = rng.stream("mutate_redraw");
        Recipe fresh = makeRecipeForRole(parent.role, 0.5f, 0.5f, 0.5f, fr);
        int idx = rng.intRange(0, int(child.p.size()) - 1);
        auto it = child.p.begin(); std::advance(it, idx);
        auto fit = fresh.p.find(it->first);
        if (fit != fresh.p.end()) it->second = fit->second;
    }
    return child;
}

StereoBuffer renderPreview(const Recipe& recipe, int rootMidi, double seconds,
                           double sampleRate) {
    Voice v;
    v.sr = sampleRate;
    v.freqHz = midiToFreq(double(rootMidi));
    v.lenSec = std::max(0.02, seconds);
    v.gateSec = v.lenSec;                 // hold for the whole preview
    v.velocity = 1.0f;
    v.mod = (recipe.role == Role::HatClosed || recipe.role == Role::HatOpen) ? 0.5f : 0.3f;
    v.bendSemis = 0.0f;
    v.seed = recipe.seed ? recipe.seed : 1;
    v.syncHz = 140.0 / 60.0;              // audition the wub at a musical ~140 BPM

    StereoBuffer buf = renderRole(recipe.role, recipe, v);

    // Normalize to ~-3 dBFS so previews sit in a sane audition/rating range,
    // preserving crest factor (pure scaling).
    float pk = buf.peak();
    if (pk > 1.0e-6f) buf.applyGain(0.70794578f / pk); // 0.708 ~= -3 dBFS
    return buf;
}

static uint64_t noteSeed(uint64_t base, const Note& note, int idx) {
    uint64_t h = base + 0x9E3779B97F4A7C15ull;
    h ^= uint64_t(int64_t(std::llround(note.startBeat * 1000.0))) * 0x100000001B3ull;
    h ^= uint64_t(note.midi + 128) * 0x9E3779B1ull;
    h ^= uint64_t(idx + 1) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    return h ? h : 1;
}

StereoBuffer renderLane(Lane /*lane*/, const std::vector<Note>& notes,
                        const Recipe& recipe, const Plan& plan,
                        const Score& score, double sampleRate) {
    const double spb = plan.secondsPerBeat();
    const double totalSec = score.totalBeats * spb + 2.0;
    const size_t total = std::max<size_t>(1, size_t(std::ceil(totalSec * sampleRate)));
    StereoBuffer out(total);
    const Role role = recipe.role;

    int idx = 0;
    for (const Note& note : notes) {
        const int myIdx = idx++;
        if (note.velocity <= 1.0e-4f) continue;
        double startSec = note.startBeat * spb;
        if (startSec < 0) startSec = 0;
        size_t off = size_t(std::llround(startSec * sampleRate));
        if (off >= total) continue;

        Voice v;
        v.sr = sampleRate;
        v.freqHz = midiToFreq(double(note.midi));
        v.gateSec = std::max(0.01, note.lengthBeats * spb);
        v.lenSec = laneLenSec(role, v.gateSec, recipe);
        v.velocity = clampf(note.velocity, 0.0f, 1.0f);
        v.mod = note.mod;
        v.bendSemis = note.bendSemis;
        v.seed = noteSeed(recipe.seed, note, myIdx);
        v.syncHz = plan.bpm / 60.0;       // quarter-note in Hz for the riddim gate

        StereoBuffer tmp = renderRole(role, recipe, v);
        out.addFrom(tmp, off);
    }
    return out;
}

Role roleForLane(Lane lane, Genre genre) {
    const bool trap = (genre == Genre::Trap);
    switch (lane) {
        case Lane::Sub:       return Role::Sub;
        case Lane::BassA:     return trap ? Role::Bass808 : Role::Growl;
        // Riddim BassB is the aggressive HORN/STAB layer (bright Screech voice)
        // that hits on every chug alongside the BassA growl — the two-tonal-bass
        // architecture of reference riddim (e.g. Seleman): growl + horn stab over
        // a constant sub.
        case Lane::BassB:     return Role::Screech;
        case Lane::BassC:     return Role::Screech;
        case Lane::Kick:      return Role::Kick;
        case Lane::Snare:     return Role::Snare;
        case Lane::HatClosed: return Role::HatClosed;
        case Lane::HatOpen:   return Role::HatOpen;
        case Lane::Perc:      return Role::Perc;
        case Lane::Melody:    return Role::MelodyLead;
        case Lane::Pad:       return Role::Pad;
        case Lane::Riser:     return Role::Riser;
        case Lane::Downlifter:return Role::Downlifter;
        case Lane::Impact:    return Role::Impact;
        case Lane::Crash:     return Role::Crash;
        default:              return Role::Growl;
    }
}

} // namespace rtg::synth

// roleForLane is declared in rtg (not rtg::synth) per recipe.h.
namespace rtg {
Role roleForLane(Lane lane, Genre genre) { return rtg::synth::roleForLane(lane, genre); }
}
