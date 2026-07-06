// Drum-role factories and renderers: Kick, Snare, HatClosed, HatOpen, Perc,
// Crash. Deterministic one-shots; drums ignore Voice.freqHz for pitch except
// where a tuned body applies. All variation from Recipe params + Voice seed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../../sound_design/src/dsp.h"
#include "../../sound_design/src/voices.h"

namespace rtg::synth {
using namespace dsp;

static inline size_t drumLen(const Voice& v) {
    return std::max<size_t>(1, size_t(std::llround(v.lenSec * v.sr)));
}
static std::string dName(const char* role, Rng& rng) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04x", unsigned(rng.next() & 0xffff));
    return std::string(role) + "_" + buf;
}

// ============================================================ KICK
Recipe makeKickRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Kick; r.seed = rng.next();
    r.name = dName("kick", rng);
    auto& p = r.p;
    p["startHz"]   = rng.rangef(110.0f, 135.0f);
    p["endHz"]     = rng.rangef(42.0f, 52.0f);
    p["pitchMs"]   = rng.rangef(0.025f, 0.060f);
    p["bodyDecay"] = rng.rangef(0.12f, 0.45f) + (1.0f - aggr) * 0.15f; // trap boomier
    p["clickAmt"]  = 0.3f + aggr * 0.5f;
    p["clickHz"]   = rng.rangef(1500.0f, 4000.0f);
    p["drive"]     = 1.3f + aggr * 2.2f;
    p["gain"]      = 0.85f;
    return r;
}

StereoBuffer renderKick(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float startHz = rc.get("startHz", 120.0f);
    const float endHz = rc.get("endHz", 46.0f);
    const float pitchMs = rc.get("pitchMs", 0.04f);
    const float bodyDecay = rc.get("bodyDecay", 0.28f);
    const float clickAmt = rc.get("clickAmt", 0.5f);
    const float clickHz = rc.get("clickHz", 2500.0f);
    const float drive = rc.get("drive", 2.0f);
    const float gain = rc.get("gain", 0.85f);

    double ph = 0;
    EnvAD body; body.start(0.001f, bodyDecay, sr);
    EnvAD click; click.start(0.0005f, 0.004f, sr);
    Biquad clickBp; clickBp.setBandpass(clickHz, 1.2, sr);
    WhiteNoise noise(v.seed ^ 0x9911u);
    DCBlock dc;
    for (size_t n = 0; n < N; ++n) {
        double t = double(n) / sr;
        double f = endHz + (startHz - endHz) * std::exp(-t / std::max(0.005f, pitchMs));
        double s = std::sin(kTwoPi * ph);
        ph += f / sr; if (ph >= 1.0) ph -= 1.0;
        float bodyOut = std::tanh(float(s) * drive) * body.tick();
        float clk = clickBp.process(noise.tick()) * click.tick() * clickAmt;
        float mono = dc.tick(bodyOut + clk) * v.velocity * gain;
        out.l[n] = mono; out.r[n] = mono;
    }
    return out;
}

// ============================================================ SNARE
Recipe makeSnareRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Snare; r.seed = rng.next();
    r.name = dName("snare", rng);
    auto& p = r.p;
    p["bodyHz"]    = rng.rangef(170.0f, 240.0f);
    p["bodyDecay"] = rng.rangef(0.06f, 0.12f);
    p["noiseDecay"] = rng.rangef(0.15f, 0.40f);
    p["noiseLo"]   = rng.rangef(1200.0f, 2000.0f);
    p["noiseHi"]   = rng.rangef(4000.0f, 8000.0f) - dark * 2500.0f;
    p["metal"]     = aggr > 0.6f ? rng.rangef(0.2f, 0.5f) : 0.0f;
    p["bodyMix"]   = rng.rangef(0.4f, 0.7f);
    p["gain"]      = 0.7f;
    return r;
}

StereoBuffer renderSnare(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float bodyHz = rc.get("bodyHz", 200.0f);
    const float bodyDecay = rc.get("bodyDecay", 0.09f);
    const float noiseDecay = rc.get("noiseDecay", 0.25f);
    const float noiseLo = rc.get("noiseLo", 1600.0f);
    const float noiseHi = rc.get("noiseHi", 6000.0f);
    const float metal = rc.get("metal", 0.0f);
    const float bodyMix = rc.get("bodyMix", 0.55f);
    const float gain = rc.get("gain", 0.7f);

    double ph1 = 0, ph2 = 0;
    EnvAD body; body.start(0.001f, bodyDecay, sr);
    EnvAD nz; nz.start(0.001f, noiseDecay, sr);
    Biquad hpL, lpL, hpR, lpR;
    hpL.setHighpass(noiseLo, 0.707, sr); lpL.setLowpass(noiseHi, 0.707, sr);
    hpR.setHighpass(noiseLo, 0.707, sr); lpR.setLowpass(noiseHi, 0.707, sr);
    WhiteNoise noiseL(v.seed ^ 0x5533u), noiseR(v.seed ^ 0xA1B2u);
    Comb metalComb; metalComb.fb = 0.7f; metalComb.damp = 0.2f;
    metalComb.setMaxDelay(int(sr / 300.0) + 8);
    DCBlock dcL, dcR;
    for (size_t n = 0; n < N; ++n) {
        double b = std::sin(kTwoPi * ph1) * 0.7 + std::sin(kTwoPi * ph2) * 0.3;
        ph1 += bodyHz / sr; if (ph1 >= 1.0) ph1 -= 1.0;
        ph2 += bodyHz * 1.55 / sr; if (ph2 >= 1.0) ph2 -= 1.0;
        float bodyOut = float(b) * body.tick() * bodyMix;
        float ne = nz.tick();
        float nL = lpL.process(hpL.process(noiseL.tick()));
        float nR = lpR.process(hpR.process(noiseR.tick()));
        float noiseL2 = nL * ne * (1.0f - bodyMix);
        if (metal > 0.0f) noiseL2 += metalComb.process(nL, float(sr / 380.0)) * ne * metal * 0.5f;
        float l = dcL.tick(bodyOut + noiseL2) * v.velocity * gain;
        float r = dcR.tick(bodyOut + nR * ne * (1.0f - bodyMix)) * v.velocity * gain;
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

// ------------------------------------------------------------ metallic hat core
static StereoBuffer renderHat(const Recipe& rc, const Voice& v, float decay) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float hpFreq = rc.get("hpFreq", 7000.0f);
    const float ring = rc.get("ring", 0.5f);
    const int partials = std::clamp(int(rc.get("partials", 5.0f)), 4, 6);
    const float gain = rc.get("gain", 0.5f);
    const float mod = clampf(v.mod, 0.0f, 1.0f);
    // note.mod tilts brightness.
    const float hp = hpFreq * (0.7f + 0.6f * mod);

    Rng prng(v.seed ^ 0x7c7cu);
    double php[6], pf[6];
    // Inharmonic metallic partials spread across 3-10 kHz.
    for (int i = 0; i < partials; ++i) {
        php[i] = prng.uniform();
        pf[i] = (3200.0 + i * 1350.0) * (0.85 + prng.uniform() * 0.3);
    }

    Biquad hpfL, hpfR;
    hpfL.setHighpass(hp, 0.707, sr);
    hpfR.setHighpass(hp, 0.707, sr);
    WhiteNoise nzL(v.seed ^ 0x11u), nzR(v.seed ^ 0x22u);
    EnvAD env; env.start(0.0005f, decay, sr);
    DCBlock dcL, dcR;
    for (size_t n = 0; n < N; ++n) {
        double metalPart = 0.0;
        for (int i = 0; i < partials; ++i) {
            metalPart += (php[i] < 0.5 ? 1.0 : -1.0); // square partials
            php[i] += pf[i] / sr; if (php[i] >= 1.0) php[i] -= 1.0;
        }
        metalPart /= partials;
        float e = env.tick();
        float mL = float(metalPart) * ring + nzL.tick() * (1.0f - ring);
        float mR = float(metalPart) * ring + nzR.tick() * (1.0f - ring);
        float l = dcL.tick(hpfL.process(mL)) * e * v.velocity * gain;
        float r = dcR.tick(hpfR.process(mR)) * e * v.velocity * gain;
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

Recipe makeHatClosedRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::HatClosed; r.seed = rng.next();
    r.name = dName("hatc", rng);
    auto& p = r.p;
    p["hpFreq"] = rng.rangef(6500.0f, 9000.0f) - dark * 2000.0f;
    p["ring"]   = rng.rangef(0.35f, 0.6f);
    p["partials"] = float(rng.intRange(4, 6));
    p["decay"]  = rng.rangef(0.03f, 0.07f);
    p["gain"]   = 0.5f;
    return r;
}
StereoBuffer renderHatClosed(const Recipe& rc, const Voice& v) {
    return renderHat(rc, v, rc.get("decay", 0.05f));
}

Recipe makeHatOpenRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::HatOpen; r.seed = rng.next();
    r.name = dName("hato", rng);
    auto& p = r.p;
    p["hpFreq"] = rng.rangef(6500.0f, 9000.0f) - dark * 2000.0f;
    p["ring"]   = rng.rangef(0.4f, 0.65f);
    p["partials"] = float(rng.intRange(4, 6));
    p["decay"]  = rng.rangef(0.2f, 0.45f);
    p["gain"]   = 0.45f;
    return r;
}
StereoBuffer renderHatOpen(const Recipe& rc, const Voice& v) {
    return renderHat(rc, v, rc.get("decay", 0.3f));
}

// ============================================================ PERC
Recipe makePercRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Perc; r.seed = rng.next();
    r.name = dName("perc", rng);
    auto& p = r.p;
    p["toneHz"]  = rng.rangef(400.0f, 1200.0f);
    p["bpQ"]     = rng.rangef(2.0f, 6.0f);
    p["decay"]   = rng.rangef(0.04f, 0.12f);
    p["noiseMix"] = rng.rangef(0.2f, 0.5f);
    p["gain"]    = 0.55f;
    return r;
}
StereoBuffer renderPerc(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float toneHz = rc.get("toneHz", 700.0f);
    const float bpQ = rc.get("bpQ", 4.0f);
    const float decay = rc.get("decay", 0.08f);
    const float noiseMix = rc.get("noiseMix", 0.35f);
    const float gain = rc.get("gain", 0.55f);
    double ph = 0;
    EnvAD env; env.start(0.0008f, decay, sr);
    Biquad bp; bp.setBandpass(toneHz, bpQ, sr);
    WhiteNoise noise(v.seed ^ 0x3131u);
    DCBlock dc;
    for (size_t n = 0; n < N; ++n) {
        double tri = ph < 0.5 ? (4.0 * ph - 1.0) : (3.0 - 4.0 * ph);
        ph += toneHz / sr; if (ph >= 1.0) ph -= 1.0;
        float src = float(tri) * (1.0f - noiseMix) + noise.tick() * noiseMix;
        float e = env.tick();
        float mono = dc.tick(bp.process(src)) * e * v.velocity * gain;
        out.l[n] = mono; out.r[n] = mono;
    }
    return out;
}

// ============================================================ CRASH
Recipe makeCrashRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Crash; r.seed = rng.next();
    r.name = dName("crash", rng);
    auto& p = r.p;
    p["hpFreq"] = rng.rangef(2800.0f, 3600.0f) - dark * 800.0f;
    p["decay"]  = rng.rangef(1.5f, 3.0f);
    p["width"]  = 0.9f;
    p["gain"]   = 0.5f;
    return r;
}
StereoBuffer renderCrash(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float hpFreq = rc.get("hpFreq", 3200.0f);
    const float decay = rc.get("decay", 2.2f);
    const float width = rc.get("width", 0.9f);
    const float gain = rc.get("gain", 0.5f);
    Biquad hpL, hpR, shL, shR;
    hpL.setHighpass(hpFreq, 0.707, sr); hpR.setHighpass(hpFreq, 0.707, sr);
    shL.setHighShelf(9000.0, 3.0, sr); shR.setHighShelf(9000.0, 3.0, sr);
    WhiteNoise nzL(v.seed ^ 0xC1u), nzR(v.seed ^ 0xC2u);
    // fast attack then long exp decay
    EnvAD env; env.start(0.002f, decay, sr);
    DCBlock dcL, dcR;
    for (size_t n = 0; n < N; ++n) {
        float e = env.tick();
        float l = dcL.tick(shL.process(hpL.process(nzL.tick()))) * e * v.velocity * gain;
        float r = dcR.tick(shR.process(hpR.process(nzR.tick()))) * e * v.velocity * gain;
        widen(l, r, 1.0f + width);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

} // namespace rtg::synth
