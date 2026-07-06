// Autonomous Mix Engine (doc 07): gain staging, spectral-slotting EQ, kick->bass
// sidechain ducking, bass-bus saturation + OTT-lite, stereo policy (mono <120 Hz),
// section automation (buildFilter/breakSoften/fxSend), and -6 dBTP headroom.
#include "rtg/mix/mix_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "rtg/master/master_engine.h"   // measureTruePeakDb (defined in master_engine.cpp)
#include "mix_dsp.h"

namespace rtg {
using namespace mixdsp;

namespace {

// Per-lane static gain staging table (dB), indexed by Lane.
constexpr float kLaneGainDb[kLaneCount] = {
    /*Sub*/ -8.0f, /*BassA*/ -10.0f, /*BassB*/ -11.0f, /*BassC*/ -13.0f,
    /*Kick*/ -7.0f, /*Snare*/ -8.5f, /*HatClosed*/ -19.0f, /*HatOpen*/ -20.0f,
    /*Perc*/ -18.0f, /*Melody*/ -14.0f, /*Pad*/ -17.0f, /*Riser*/ -14.0f,
    /*Downlifter*/ -14.0f, /*Impact*/ -7.0f, /*Crash*/ -13.0f,
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
        return b;
    };

    // --- Sidechain envelopes (deterministic, from note lists) --------------
    const double relSec = 60.0 / std::max(1.0, plan.bpm) * 0.35;
    const float kickMin = dbToGain(-7.0f * plan.sidechainDepth);
    std::vector<float> kickDuck = buildDuckEnv(score.notes(Lane::Kick), N, spb, sr, 2.0, 40.0, relSec, kickMin);
    std::vector<float> snareDuck = buildDuckEnv(score.notes(Lane::Snare), N, spb, sr, 2.0, 40.0, relSec, dbToGain(-2.0f));

    StereoBuffer mix(N);

    // --- SUB bus -----------------------------------------------------------
    StereoBuffer subBus(N);
    if (has(Lane::Sub)) {
        subBus = laneBuf(Lane::Sub);
        forceMono(subBus);                        // sub forced mono
        applyStereo(makeLowpass(sr, 110.0), subBus);
        applyStereo(makeHighpass(sr, 25.0), subBus);
        applyEnv(subBus, kickDuck);
    }

    // --- BASS bus (sum voices -> saturation -> OTT-lite -> width) ----------
    StereoBuffer bassBus(N);
    bool anyBass = false;
    for (Lane bl : {Lane::BassA, Lane::BassB, Lane::BassC}) {
        if (!has(bl)) continue;
        anyBass = true;
        StereoBuffer b = laneBuf(bl);
        applyStereo(makeHighpass(sr, 95.0), b);   // never fight the sub
        applyEnv(b, kickDuck);
        for (size_t i = 0; i < N; ++i) { bassBus.l[i] += b.l[i]; bassBus.r[i] += b.r[i]; }
    }
    if (anyBass) {
        tanhSaturate(bassBus, 1.0 + 2.0 * plan.mixAggression);
        ottLite(bassBus, sr, 120.0, 2500.0, 0.25 + 0.35 * plan.mixAggression);
        applyWidth(bassBus, 0.25f);
        sweepFilter(bassBus, sr, spb, true, [&](double beat) {
            return std::max(20.0, double(score.buildFilter.sample(beat)) * 400.0);
        });
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
    }
    for (Lane hl : {Lane::HatClosed, Lane::HatOpen}) {
        if (!has(hl)) continue;
        StereoBuffer b = laneBuf(hl);
        applyStereo(makeHighpass(sr, 300.0), b);
        applyWidth(b, 0.7f);
        addToDrums(b);
    }
    if (has(Lane::Perc)) {
        StereoBuffer b = laneBuf(Lane::Perc);
        applyStereo(makeHighpass(sr, 300.0), b);
        addToDrums(b);
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
    }
    StereoBuffer padBuf(N);
    if (has(Lane::Pad)) {
        padBuf = laneBuf(Lane::Pad);
        padBuf.applyGain(musicScale);
        applyStereo(makeHighpass(sr, 180.0), padBuf);
        applyStereo(makeLowpass(sr, 9000.0), padBuf);
        applyEnv(padBuf, kickDuck);                          // kick ducks pad
        applyEnv(padBuf, snareDuck);                         // snare ducks pad lightly
        applyWidth(padBuf, 0.85f);
        for (size_t i = 0; i < N; ++i) { musicBus.l[i] += padBuf.l[i]; musicBus.r[i] += padBuf.r[i]; }
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
        applyWidth(b, 0.85f);
        addToFx(b);
    }
    if (has(Lane::Downlifter)) addToFx(laneBuf(Lane::Downlifter));
    if (has(Lane::Impact)) addToFx(laneBuf(Lane::Impact));
    if (has(Lane::Crash)) {
        StereoBuffer b = laneBuf(Lane::Crash);
        applyWidth(b, 0.7f);
        addToFx(b);
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
            if (inLastBuildBar(beat) && bf > 0.85f) tgt *= dbToGain(-3.0f);
            if (bs > 0.0f) tgt *= dbToGain(-1.5f * bs);
            g = smooth * g + (1.0 - smooth) * tgt;
            drumBus.l[i] *= float(g);
            drumBus.r[i] *= float(g);
        }
    }

    // --- Sum buses ---------------------------------------------------------
    for (size_t i = 0; i < N; ++i) {
        mix.l[i] = subBus.l[i] + bassBus.l[i] + drumBus.l[i] + musicBus.l[i] + fxBus.l[i];
        mix.r[i] = subBus.r[i] + bassBus.r[i] + drumBus.r[i] + musicBus.r[i] + fxBus.r[i];
    }

    // --- fxSend diffusion (Melody/Pad/Snare/FX -> cheap reverb, subtle) -----
    {
        Diffuser diff; diff.init(sr);
        for (size_t i = 0; i < N; ++i) {
            double beat = (double(i) / sr) / spb;
            float send = score.fxSend.sample(beat) * 0.18f;
            if (send <= 1e-5f) { diff.process(0.0f); continue; }
            float srcMono = 0.5f * (musicBus.l[i] + musicBus.r[i])
                          + 0.5f * (fxBus.l[i] + fxBus.r[i])
                          + 0.5f * (snareBuf.l[i] + snareBuf.r[i]);
            float wet = diff.process(srcMono * send);
            mix.l[i] += wet;
            mix.r[i] += wet;
        }
    }

    // --- Global break softening LP (20 kHz -> 4 kHz) -----------------------
    sweepFilter(mix, sr, spb, false, [&](double beat) {
        return 20000.0 - double(score.breakSoften.sample(beat)) * 16000.0;
    });

    // --- Stereo policy: mono below 120 Hz ----------------------------------
    monoBelow(mix, sr, 120.0);

    // --- Headroom safety: trim true peak to <= -6 dBTP ---------------------
    float tp = measureTruePeakDb(mix);
    if (tp > -6.0f) mix.applyGain(dbToGain(-6.0f - tp));

    return mix;
}

} // namespace rtg
