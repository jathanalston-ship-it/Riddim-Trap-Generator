// Autonomous Mix Engine (doc 07): gain staging, spectral-slotting EQ, all-drum
// sidechain ducking of the bass chain, bass-bus saturation + OTT-lite, stereo policy (mono <120 Hz),
// section automation (buildFilter/breakSoften/fxSend), and -6 dBTP headroom.
#include "rtg/mix/mix_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "rtg/master/master_engine.h"   // measureTruePeakDb (defined in master_engine.cpp)
#include "rtg/decision/calibration.h"    // reference-calibration band correction
#include "mix_dsp.h"

namespace rtg {
using namespace mixdsp;

namespace {

// Per-lane static gain staging table (dB), indexed by Lane.
// Balance target (doc 07): low band (20-120 Hz) carries ~40-50% of drop
// energy, mid-bass character audibly on top — sub supports, growls lead.
constexpr float kLaneGainDb[kLaneCount] = {
    /*Sub*/ -17.0f, /*BassA*/ -2.5f, /*BassB*/ -3.0f, /*BassC*/ -7.0f,
    /*Kick*/ -9.0f, /*Snare*/ -6.5f, /*HatClosed*/ -12.5f, /*HatOpen*/ -13.5f,
    /*Perc*/ -13.5f, /*Melody*/ -12.0f, /*Pad*/ -15.0f, /*Riser*/ -12.5f,
    /*Downlifter*/ -12.5f, /*Impact*/ -8.0f, /*Crash*/ -11.0f,
};

// --- Depth send/tilt tables (parallel to kLaneGainDb) ----------------------
// Per-lane send levels (linear 0..1) into the two shared depth reverb buses.
// NEAR = short/bright room (glue for drums/perc); FAR = long/dark room (pushes
// music/atmos to the back). Sub/Bass/Kick get ZERO send — time effects never
// touch sub/low-mid energy (the returns are also band-limited on the way back).
constexpr float kSendNear[kLaneCount] = {
    /*Sub*/ 0.f, /*BassA*/ 0.f, /*BassB*/ 0.f, /*BassC*/ 0.f,
    /*Kick*/ 0.f, /*Snare*/ 0.18f, /*HatClosed*/ 0.05f, /*HatOpen*/ 0.08f,
    /*Perc*/ 0.16f, /*Melody*/ 0.10f, /*Pad*/ 0.05f, /*Riser*/ 0.05f,
    /*Downlifter*/ 0.04f, /*Impact*/ 0.02f, /*Crash*/ 0.10f,
};
constexpr float kSendFar[kLaneCount] = {
    /*Sub*/ 0.f, /*BassA*/ 0.f, /*BassB*/ 0.f, /*BassC*/ 0.f,
    /*Kick*/ 0.f, /*Snare*/ 0.05f, /*HatClosed*/ 0.f, /*HatOpen*/ 0.03f,
    /*Perc*/ 0.05f, /*Melody*/ 0.22f, /*Pad*/ 0.30f, /*Riser*/ 0.22f,
    /*Downlifter*/ 0.14f, /*Impact*/ 0.f, /*Crash*/ 0.20f,
};
// Front/back spectral tilt: a subtle per-lane high-shelf (dB @ ~7 kHz) applied
// to the dry lane. Negative = darker/recedes (pads, risers, downlifters);
// slightly positive = present/forward (kick, snare).
constexpr float kDepthTiltDb[kLaneCount] = {
    /*Sub*/ 0.f, /*BassA*/ 0.f, /*BassB*/ 0.f, /*BassC*/ 0.f,
    /*Kick*/ 0.5f, /*Snare*/ 0.6f, /*HatClosed*/ 0.f, /*HatOpen*/ 0.f,
    /*Perc*/ 0.f, /*Melody*/ 0.f, /*Pad*/ -2.5f, /*Riser*/ -2.0f,
    /*Downlifter*/ -2.5f, /*Impact*/ 0.f, /*Crash*/ -1.0f,
};

// Deterministic ducking envelope from note onsets (gain multiplier, 1 = open).
std::vector<float> buildDuckEnv(const std::vector<Note>& notes, size_t N, double spb,
                                double sr, double atkMs, double holdMs, double relSec,
                                float minGain) {
    std::vector<float> env(N, 1.0f);
    if (minGain >= 1.0f || N == 0) return env;
    const int atk = std::max(1, int(atkMs * sr / 1000.0));
    const int hold = std::max(0, int(holdMs * sr / 1000.0));
    const int rel = std::max(1, int(relSec * sr));
    for (const Note& nt : notes) {
        long s0 = long(nt.startBeat * spb * sr);
        if (s0 < 0) s0 = 0;
        for (int i = 0; i < atk; ++i) {
            long idx = s0 + i; if (idx >= (long)N) break;
            float v = 1.0f + (minGain - 1.0f) * (float(i) / atk);
            if (v < env[idx]) env[idx] = v;
        }
        long hs = s0 + atk;
        for (int i = 0; i < hold; ++i) {
            long idx = hs + i; if (idx >= (long)N) break;
            if (minGain < env[idx]) env[idx] = minGain;
        }
        long rs = hs + hold;
        for (int i = 0; i < rel; ++i) {
            long idx = rs + i; if (idx >= (long)N) break;
            float v = minGain + (1.0f - minGain) * (float(i) / rel);
            if (v < env[idx]) env[idx] = v;
        }
    }
    return env;
}

void applyEnv(StereoBuffer& b, const std::vector<float>& env) {
    const size_t n = std::min(b.size(), env.size());
    for (size_t i = 0; i < n; ++i) { b.l[i] *= env[i]; b.r[i] *= env[i]; }
}

void forceMono(StereoBuffer& b) {
    for (size_t i = 0; i < b.size(); ++i) { float m = 0.5f * (b.l[i] + b.r[i]); b.l[i] = m; b.r[i] = m; }
}

// RBJ peaking EQ (used only by the reference-calibration band correction).
Biquad makePeaking(double fs, double f0, double gainDb, double Q = 1.0) {
    Biquad bq;
    f0 = std::min(std::max(f0, 10.0), fs * 0.49);
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0);
    double alpha = s / (2 * Q);
    bq.setCoeffs(1 + alpha * A, -2 * c, 1 - alpha * A,
                 1 + alpha / A, -2 * c, 1 - alpha / A);
    return bq;
}

// One deterministic corrective pass toward the reference band shares. Compares
// the summed mix's own loudest-25% band shares against the calibration and
// applies clamped (+/-2.5 dB) shelf/peak trims so the mix matches the tonal
// balance of the reference material. No iteration — a single gentle nudge.
void applyCalibrationBandCorrection(StereoBuffer& mix, double sr, Genre genre) {
    const auto& cal = Calibration::active();
    if (!cal) return;
    const CalibrationProfile& prof = cal->forGenre(genre);
    if (!prof.present) return;

    // Two passes with re-measurement: reference comparison showed one gentle
    // pass leaves large spectral gaps (gen mid-band 2.8% vs ref 15.4%) —
    // self-limiting because each pass corrects toward the measured target,
    // so source-side improvements automatically shrink the applied EQ.
    const double fc[kCalBands] = { 120.0, 250.0, 1000.0, 3500.0, 6000.0 };
    for (int pass = 0; pass < 2; ++pass) {
        std::array<float, kCalBands> mine = measureBandShares(mix, sr);
        bool touched = false;
        for (int b = 0; b < kCalBands; ++b) {
            float target = prof.bands[b];
            float have = std::max(mine[b], 1e-6f);
            // Energy ratio in dB at half strength, clamped to +/-4.5 dB/pass.
            float trimDb = std::clamp(5.0f * std::log10(std::max(target, 1e-6f) / have),
                                      -4.5f, 4.5f);
            if (std::fabs(trimDb) < 0.25f) continue;
            touched = true;
            Biquad bq = (b == 0)               ? makeLowShelf(sr, fc[0], trimDb)
                      : (b == kCalBands - 1)   ? makeHighShelf(sr, fc[kCalBands - 1], trimDb)
                                               : makePeaking(sr, fc[b], trimDb, 1.0);
            applyStereo(bq, mix);
        }
        if (!touched) break;
    }
}

// Time-varying HP/LP applied in blocks; biquad state persists across blocks
// (only coefficients are refreshed) so there are no per-block discontinuities.
template <class CutoffFn>
void sweepFilter(StereoBuffer& buf, double sr, double spb, bool highpass, CutoffFn fc) {
    const size_t N = buf.size();
    const size_t B = 128;
    Biquad l{}, r{};
    for (size_t s = 0; s < N; s += B) {
        double beat = (double(s) / sr) / spb;
        double cut = fc(beat);
        Biquad nc = highpass ? makeHighpass(sr, cut) : makeLowpass(sr, cut);
        l.b0 = nc.b0; l.b1 = nc.b1; l.b2 = nc.b2; l.a1 = nc.a1; l.a2 = nc.a2;
        r.b0 = nc.b0; r.b1 = nc.b1; r.b2 = nc.b2; r.a1 = nc.a1; r.a2 = nc.a2;
        size_t end = std::min(N, s + B);
        for (size_t i = s; i < end; ++i) { buf.l[i] = l.process(buf.l[i]); buf.r[i] = r.process(buf.r[i]); }
    }
}

} // namespace

StereoBuffer mixDown(const std::array<StereoBuffer, kLaneCount>& laneAudio,
                     const Score& score, const Plan& plan, double sampleRate) {
    const double sr = sampleRate;
    const double spb = 60.0 / std::max(1.0, plan.bpm);
    const bool trap = plan.params.genre == Genre::Trap;

    size_t N = 0;
    for (const auto& b : laneAudio) N = std::max(N, b.size());
    N = std::max(N, size_t(score.totalBeats * spb * sr));
    if (N == 0) return StereoBuffer();

    auto has = [&](Lane l) { return !laneAudio[int(l)].empty(); };
    auto laneBuf = [&](Lane l) {
        StereoBuffer b(N);
        const auto& src = laneAudio[int(l)];
        size_t m = std::min(N, src.size());
        for (size_t i = 0; i < m; ++i) { b.l[i] = src.l[i]; b.r[i] = src.r[i]; }
        b.applyGain(dbToGain(kLaneGainDb[int(l)]));
        const float tilt = kDepthTiltDb[int(l)];      // front/back spectral tilt
        if (std::fabs(tilt) > 0.01f) applyStereo(makeHighShelf(sr, 7000.0, tilt), b);
        return b;
    };

    // --- Sidechain envelopes (deterministic, from note lists) --------------
    const double relSec = 60.0 / std::max(1.0, plan.bpm) * 0.35;
    const float kickMin = dbToGain(-7.0f * plan.sidechainDepth);
    std::vector<float> kickDuck = buildDuckEnv(score.notes(Lane::Kick), N, spb, sr, 2.0, 40.0, relSec, kickMin);
    std::vector<float> snareDuck = buildDuckEnv(score.notes(Lane::Snare), N, spb, sr, 2.0, 40.0, relSec, dbToGain(-2.0f));

    // --- Bass-chain sidechain: duck the WHOLE bass chain (sub + bass buses)
    // to ALL drum onsets so each hit pierces cleanly and the chug "breathes"
    // with the groove. Fast attack (~2 ms), short hold, fast release (~0.15
    // beat) so the bass springs straight back between hits. Kick ducks deepest,
    // snare a touch less, hats/perc are LIGHT (they're quiet — clarity, not a
    // pump). Depths scale with plan.sidechainDepth. Deterministic: purely a
    // function of the note lists, combined by per-sample minimum (deepest wins).
    const double scRelSec = spb * 0.15;                          // ~0.15 beat, punchy
    // Riddim tearout is built on a DEEP low-end pump: the sub/bass duck to
    // near-silence on the kick then swell back in the gap (that "breathing" is
    // the defining low end). The shared depth (0.45 for riddim) only dips the
    // bass chain to ~-3 dB — a flat wall on the sub-envelope compare. Force a
    // deep effective depth + a hard kick duck for the bass chain here; the
    // gentler pad/reverb kickMin above is unchanged.
    const bool riddim = plan.params.genre == Genre::Riddim;
    const float pumpDepth = riddim ? std::max(plan.sidechainDepth, 0.9f) : plan.sidechainDepth;
    const float bassKickMin = dbToGain((riddim ? -20.0f : -7.0f) * pumpDepth);
    const float snareMin = dbToGain((riddim ? -12.0f : -4.5f) * pumpDepth);
    const float hatMin   = dbToGain(-1.5f * plan.sidechainDepth);
    const float percMin  = dbToGain(-2.0f * plan.sidechainDepth);
    // The KICK pump gets a longer release than the drum-clarity ducks: the sub
    // stays ducked well into the gap then swells back, so the low end reads as a
    // deep pumping arch (ref dipRatio 0.9) rather than a quick notch. Riddim
    // kicks are half-time (~2 beats apart) so there is room for a long swell.
    const double kickPumpRel = riddim ? spb * 0.42 : scRelSec;
    std::vector<float> bassChainDuck =
        buildDuckEnv(score.notes(Lane::Kick), N, spb, sr, 2.0, 10.0, kickPumpRel, bassKickMin);
    auto mergeDuck = [&](const std::vector<float>& d) {
        for (size_t i = 0; i < N; ++i) if (d[i] < bassChainDuck[i]) bassChainDuck[i] = d[i];
    };
    mergeDuck(buildDuckEnv(score.notes(Lane::Snare),     N, spb, sr, 2.0, 8.0, scRelSec,        snareMin));
    mergeDuck(buildDuckEnv(score.notes(Lane::HatClosed), N, spb, sr, 2.0, 3.0, scRelSec * 0.7f, hatMin));
    mergeDuck(buildDuckEnv(score.notes(Lane::HatOpen),   N, spb, sr, 2.0, 3.0, scRelSec * 0.7f, hatMin));
    mergeDuck(buildDuckEnv(score.notes(Lane::Perc),      N, spb, sr, 2.0, 5.0, scRelSec,        percMin));

    StereoBuffer mix(N);

    // --- Depth send accumulators -------------------------------------------
    // Per-lane audio is tapped into these two reverb-send buses (and a small
    // ping-pong "throw" bus) as each lane is processed, so the shared depth
    // buses are rendered ONCE later. A baseline space is always present and
    // swells in breaks/intros via the per-beat fxSend curve (spaceMod).
    StereoBuffer nearSend(N), farSend(N), throwSend(N);
    std::vector<float> spaceMod(N);
    for (size_t i = 0; i < N; ++i) {
        double beat = (double(i) / sr) / spb;
        float s = std::clamp(score.fxSend.sample(beat), 0.0f, 1.0f);
        spaceMod[i] = 0.7f + 0.6f * s;                // 0.7 baseline .. 1.3 in breaks
    }
    auto addSend = [&](const StereoBuffer& b, Lane l) {
        const float sn = kSendNear[int(l)], sf = kSendFar[int(l)];
        if (sn <= 0.f && sf <= 0.f) return;
        for (size_t i = 0; i < N; ++i) {
            const float m = spaceMod[i];
            nearSend.l[i] += b.l[i] * sn * m; nearSend.r[i] += b.r[i] * sn * m;
            farSend.l[i]  += b.l[i] * sf * m; farSend.r[i]  += b.r[i] * sf * m;
        }
    };
    auto addThrow = [&](const StereoBuffer& b, float w) {
        for (size_t i = 0; i < N; ++i) {
            const float m = spaceMod[i];
            throwSend.l[i] += b.l[i] * w * m; throwSend.r[i] += b.r[i] * w * m;
        }
    };

    // --- SUB bus -----------------------------------------------------------
    StereoBuffer subBus(N);
    if (has(Lane::Sub)) {
        subBus = laneBuf(Lane::Sub);
        forceMono(subBus);                        // sub forced mono
        applyStereo(makeLowpass(sr, 120.0), subBus);  // sub OWNS <120 Hz (clean crossover)
        applyStereo(makeHighpass(sr, 35.0), subBus);  // kill sub-35 rumble that bloats the 20-60 band
        applyEnv(subBus, bassChainDuck);          // whole bass chain ducks to ALL drums
    }

    // --- BASS bus (sum voices -> saturation -> OTT-lite -> width) ----------
    StereoBuffer bassBus(N);
    bool anyBass = false;
    for (Lane bl : {Lane::BassA, Lane::BassB, Lane::BassC}) {
        if (!has(bl)) continue;
        anyBass = true;
        StereoBuffer b = laneBuf(bl);
        applyStereo(makeHighpass(sr, 120.0), b);  // bass OWNS 120 Hz-2 kHz; clean crossover, sub keeps <120
        applyEnv(b, bassChainDuck);               // whole bass chain ducks to ALL drums
        for (size_t i = 0; i < N; ++i) { bassBus.l[i] += b.l[i]; bassBus.r[i] += b.r[i]; }
    }
    if (anyBass) {
        // Low-mid mud dip (deep + wide): scoop the growl's ~150-450 Hz body hard
        // so the 120-500 band stops masking the sub and mids (target share ~0.11;
        // was reading ~0.33 — muddy). Two scoops: a wide body cut + a lower dip.
        applyStereo(makePeaking(sr, 260.0, -8.0, 0.6), bassBus);
        applyStereo(makePeaking(sr, 160.0, -4.0, 0.9), bassBus);
        // Mid presence: a broad lift through the 600-2500 Hz "scream" band plus a
        // narrower bite so the growl carries the mid/presence lane the aggressive
        // references sit in (500-2k share ~0.06).
        applyStereo(makePeaking(sr, 1000.0, 5.0, 0.55), bassBus);
        applyStereo(makePeaking(sr, 1800.0, 3.0, 1.1), bassBus);
        // Multiband distortion: keep <100 Hz CLEAN (sub tight) and tanh-drive the
        // mid/high band only, so harmonics scream where the presence lift sits.
        driveAboveClean(bassBus, sr, 240.0, 1.4 + 2.2 * plan.mixAggression);
        // HEAVY OTT (the riddim/dubstep density trick): the growl is a very peaky
        // stab (raw crest ~28, RMS ~24 dB below the sub) so its mid/scream is
        // inaudible in the sum. Strong upward multiband compression lifts the
        // growl's sustained harmonics into a dense, present WALL so the mid/
        // presence lane the references sit in (5-14%) actually reads.
        ottLite(bassBus, sr, 120.0, 2500.0, 0.45 + 0.30 * plan.mixAggression);
        applyWidth(bassBus, 0.18f);   // tearout is centered/focused, not wide
        sweepFilter(bassBus, sr, spb, true, [&](double beat) {
            return std::max(20.0, double(score.buildFilter.sample(beat)) * 400.0);
        });
        // Break dip: pull the bass down in breaks so the drop slams with more
        // contrast (bass is the dominant tonal element when the drop hits).
        {
            double g = 1.0;
            const double smooth = std::exp(-1.0 / (0.005 * sr));  // 5 ms glide
            for (size_t i = 0; i < N; ++i) {
                double beat = (double(i) / sr) / spb;
                float bs = score.breakSoften.sample(beat);
                double tgt = (bs > 0.0f) ? dbToGain(-5.0f * bs) : 1.0;
                g = smooth * g + (1.0 - smooth) * tgt;
                bassBus.l[i] *= float(g);
                bassBus.r[i] *= float(g);
            }
        }
    }

    // --- DRUM bus ----------------------------------------------------------
    StereoBuffer drumBus(N);
    StereoBuffer snareBuf(N);
    auto addToDrums = [&](const StereoBuffer& b) {
        for (size_t i = 0; i < N; ++i) { drumBus.l[i] += b.l[i]; drumBus.r[i] += b.r[i]; }
    };
    if (has(Lane::Kick)) {
        StereoBuffer b = laneBuf(Lane::Kick);
        applyStereo(makeHighpass(sr, trap ? 45.0 : 30.0), b); // 808 owns lows in trap
        forceMono(b);
        addToDrums(b);
    }
    if (has(Lane::Snare)) {
        snareBuf = laneBuf(Lane::Snare);
        addToDrums(snareBuf);
        addSend(snareBuf, Lane::Snare);
        addThrow(snareBuf, 0.05f);                    // section-tail throws (via spaceMod)
    }
    for (Lane hl : {Lane::HatClosed, Lane::HatOpen}) {
        if (!has(hl)) continue;
        StereoBuffer b = laneBuf(hl);
        applyStereo(makeHighpass(sr, 300.0), b);
        applyWidth(b, 0.5f);
        addToDrums(b);
        addSend(b, hl);
    }
    if (has(Lane::Perc)) {
        StereoBuffer b = laneBuf(Lane::Perc);
        applyStereo(makeHighpass(sr, 300.0), b);
        addToDrums(b);
        addSend(b, Lane::Perc);
    }

    // --- MUSIC bus ---------------------------------------------------------
    StereoBuffer musicBus(N);
    const float musicScale = 0.9f + 0.2f * plan.energy01;   // slight energy scaling
    if (has(Lane::Melody)) {
        StereoBuffer b = laneBuf(Lane::Melody);
        b.applyGain(musicScale);
        applyStereo(makeHighpass(sr, 200.0), b);
        applyEnv(b, snareDuck);                              // snare ducks melody lightly
        for (size_t i = 0; i < N; ++i) { musicBus.l[i] += b.l[i]; musicBus.r[i] += b.r[i]; }
        addSend(b, Lane::Melody);
        addThrow(b, 0.11f);                                  // melody ping-pong throws
    }
    StereoBuffer padBuf(N);
    if (has(Lane::Pad)) {
        padBuf = laneBuf(Lane::Pad);
        padBuf.applyGain(musicScale);
        applyStereo(makeHighpass(sr, 180.0), padBuf);
        applyStereo(makeLowpass(sr, 9000.0), padBuf);
        applyEnv(padBuf, kickDuck);                          // kick ducks pad
        applyEnv(padBuf, snareDuck);                         // snare ducks pad lightly
        applyWidth(padBuf, 0.72f);                           // narrower pad (focused field)
        for (size_t i = 0; i < N; ++i) { musicBus.l[i] += padBuf.l[i]; musicBus.r[i] += padBuf.r[i]; }
        addSend(padBuf, Lane::Pad);
    }
    if (has(Lane::Melody) || has(Lane::Pad)) {
        sweepFilter(musicBus, sr, spb, true, [&](double beat) {
            return std::max(20.0, double(score.buildFilter.sample(beat)) * 400.0);
        });
    }

    // --- FX bus ------------------------------------------------------------
    StereoBuffer fxBus(N);
    auto addToFx = [&](const StereoBuffer& b) {
        for (size_t i = 0; i < N; ++i) { fxBus.l[i] += b.l[i]; fxBus.r[i] += b.r[i]; }
    };
    if (has(Lane::Riser)) {
        StereoBuffer b = laneBuf(Lane::Riser);
        applyStereo(makeHighpass(sr, 150.0), b);
        applyWidth(b, 0.6f);
        addToFx(b);
        addSend(b, Lane::Riser);                             // riser tails pushed back
    }
    if (has(Lane::Downlifter)) {
        StereoBuffer b = laneBuf(Lane::Downlifter);
        addToFx(b);
        addSend(b, Lane::Downlifter);
    }
    if (has(Lane::Impact)) {
        StereoBuffer b = laneBuf(Lane::Impact);
        addToFx(b);
        addSend(b, Lane::Impact);
    }
    if (has(Lane::Crash)) {
        StereoBuffer b = laneBuf(Lane::Crash);
        applyWidth(b, 0.55f);
        addToFx(b);
        addSend(b, Lane::Crash);
    }

    // --- Drum-bus section automation (build last-bar dip + break softening) -
    {
        // Identify last bar of each Build section.
        auto inLastBuildBar = [&](double beat) {
            int bar = int(beat / std::max(1.0, plan.beatsPerBar));
            for (const Section& s : plan.sections)
                if (s.type == SectionType::Build && bar == s.startBar + s.bars - 1) return true;
            return false;
        };
        double g = 1.0;
        const double smooth = std::exp(-1.0 / (0.005 * sr)); // 5 ms glide
        for (size_t i = 0; i < N; ++i) {
            double beat = (double(i) / sr) / spb;
            float bf = score.buildFilter.sample(beat);
            float bs = score.breakSoften.sample(beat);
            double tgt = 1.0;
            if (inLastBuildBar(beat) && bf > 0.85f) tgt *= dbToGain(-4.0f);
            if (bs > 0.0f) tgt *= dbToGain(-4.0f * bs);   // deeper break dip -> more drop contrast
            g = smooth * g + (1.0 - smooth) * tgt;
            drumBus.l[i] *= float(g);
            drumBus.r[i] *= float(g);
        }
    }

    // --- Drum-bus low-mid tame (reference band-matching) -------------------
    // Isolation analysis showed the drum bus carries the bulk of the generated
    // 120-500 Hz lump (loM ~22% vs reference ~10%) which masks the mid/presence
    // lane. A gentle wide peaking cut at ~300 Hz pulls the drum body out of the
    // growl's vocal register without touching the kick's sub punch (<120 Hz).
    applyStereo(makePeaking(sr, 320.0, -6.0, 0.8), drumBus);
    applyStereo(makePeaking(sr, 180.0, -3.0, 1.1), drumBus);

    // --- Drum-bus clipper --------------------------------------------------
    // Odd-harmonic soft clip: tightens the kick's low-end sustain and caps the
    // snare transient so the bus reads punchy (tighter kick + controlled snare)
    // instead of splatty. Raises loudness/punch without a brickwall limiter.
    clipDrive(drumBus, 1.2 + 1.0 * plan.mixAggression, 0.98f);

    // --- Sum buses ---------------------------------------------------------
    for (size_t i = 0; i < N; ++i) {
        mix.l[i] = subBus.l[i] + bassBus.l[i] + drumBus.l[i] + musicBus.l[i] + fxBus.l[i];
        mix.r[i] = subBus.r[i] + bassBus.r[i] + drumBus.r[i] + musicBus.r[i] + fxBus.r[i];
    }

    // --- Depth: dual reverb buses (near/far) + ping-pong throws ------------
    // Two shared, band-limited reverb buses build front-to-back space. NEAR is
    // a short/bright room that glues drums & percussion; FAR is a long/dark
    // room that pushes music, risers and crash tails to the back. Both returns
    // are HP'd (>=200 Hz) + LP'd and kick-ducked so tails never smear the
    // groove or fight the sub. Each bus is processed ONCE (sends summed in).
    {
        // Slower-release kick duck for the wet tails (swell up in the gaps).
        const double verbRelSec = spb * 0.45;
        std::vector<float> verbDuck = buildDuckEnv(score.notes(Lane::Kick), N, spb, sr,
                                                   4.0, 30.0, verbRelSec, dbToGain(-4.5f));

        // NEAR bus: short, bright, small room.
        StereoBuffer nearWet(N);
        {
            RoomVerb rv; rv.init(sr, /*preMs*/ 6.0, /*fb*/ 0.70f, /*damp*/ 0.45f, /*size*/ 0.72);
            for (size_t i = 0; i < N; ++i) {
                float x = 0.5f * (nearSend.l[i] + nearSend.r[i]);
                float wl, wr; rv.process(x, wl, wr);
                nearWet.l[i] = wl; nearWet.r[i] = wr;
            }
            applyStereo(makeHighpass(sr, 200.0), nearWet);   // keep lows out of the verb
            applyStereo(makeLowpass(sr, 9000.0), nearWet);   // bright but not harsh
            applyEnv(nearWet, verbDuck);
            applyWidth(nearWet, 0.95f);                       // narrower wet field
        }

        // FAR bus: long, dark, big room.
        StereoBuffer farWet(N);
        {
            RoomVerb rv; rv.init(sr, /*preMs*/ 26.0, /*fb*/ 0.80f, /*damp*/ 0.62f, /*size*/ 1.25);
            for (size_t i = 0; i < N; ++i) {
                float x = 0.5f * (farSend.l[i] + farSend.r[i]);
                float wl, wr; rv.process(x, wl, wr);
                farWet.l[i] = wl; farWet.r[i] = wr;
            }
            applyStereo(makeHighpass(sr, 220.0), farWet);    // protect low-mids
            applyStereo(makeLowpass(sr, 7000.0), farWet);    // darker = further back
            applyEnv(farWet, verbDuck);
            applyWidth(farWet, 1.05f);                        // pulled in from 1.30 (focused field)
        }

        // Ping-pong throws: small, band-limited rhythmic depth from melody and
        // section-tail snare (already weighted into throwSend via spaceMod).
        StereoBuffer ppWet(N);
        {
            PingPong pp; pp.init(sr, /*msL*/ 190.0, /*msR*/ 285.0, /*fb*/ 0.33f);
            for (size_t i = 0; i < N; ++i) {
                float x = 0.5f * (throwSend.l[i] + throwSend.r[i]);
                float wl, wr; pp.process(x, wl, wr);
                ppWet.l[i] = wl; ppWet.r[i] = wr;
            }
            applyStereo(makeHighpass(sr, 220.0), ppWet);
            applyStereo(makeLowpass(sr, 6000.0), ppWet);
            applyEnv(ppWet, verbDuck);
        }

        // Sum ducked, band-limited wet into the mix. Lower wet levels than before:
        // the reverb wash was filling the gaps (raising the RMS floor -> low crest)
        // and widening the field. Trimmed for a drier, punchier, more centered mix.
        const float nearGain = 0.72f, farGain = 0.52f, ppGain = 0.30f;
        for (size_t i = 0; i < N; ++i) {
            mix.l[i] += nearWet.l[i] * nearGain + farWet.l[i] * farGain + ppWet.l[i] * ppGain;
            mix.r[i] += nearWet.r[i] * nearGain + farWet.r[i] * farGain + ppWet.r[i] * ppGain;
        }
    }

    // --- Global break softening LP (20 kHz -> 4 kHz) -----------------------
    sweepFilter(mix, sr, spb, false, [&](double beat) {
        return 20000.0 - double(score.breakSoften.sample(beat)) * 16000.0;
    });

    // --- Master edge: gentle pre-safety soft clip --------------------------
    // Shave the hardest peaks with odd harmonics so the final true-peak trim
    // doesn't have to pull the whole track down (more loudness + tearout edge).
    // Light drive, high ceiling; runs BEFORE the untouched monoBelow ->
    // calibration -> headroom safety chain.
    clipDrive(mix, 1.1, 0.99f);

    // --- Stereo policy: mono below 120 Hz ----------------------------------
    monoBelow(mix, sr, 120.0);

    // --- Reference-calibration band correction (one clamped pass) ----------
    applyCalibrationBandCorrection(mix, sr, plan.params.genre);

    // --- Headroom safety: trim true peak to <= -6 dBTP ---------------------
    float tp = measureTruePeakDb(mix);
    if (tp > -6.0f) mix.applyGain(dbToGain(-6.0f - tp));

    return mix;
}

} // namespace rtg
