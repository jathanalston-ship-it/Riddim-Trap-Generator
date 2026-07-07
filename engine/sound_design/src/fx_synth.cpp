// FX and melodic-role factories/renderers: Riser, Downlifter, Impact, Pad,
// MelodyLead. Deterministic; variation from Recipe params + Voice seed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "dsp.h"
#include "voices.h"

namespace rtg::synth {
using namespace dsp;

static inline size_t fxLen(const Voice& v) {
    return std::max<size_t>(1, size_t(std::llround(v.lenSec * v.sr)));
}
static std::string fName(const char* role, Rng& rng) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04x", unsigned(rng.next() & 0xffff));
    return std::string(role) + "_" + buf;
}

// ============================================================ RISER
Recipe makeRiserRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Riser; r.seed = rng.next();
    r.name = fName("riser", rng);
    auto& p = r.p;
    p["octaves"]  = rng.rangef(1.0f, 2.0f);
    p["sawVoices"] = float(rng.intRange(3, 5));
    p["detune"]   = rng.rangef(8.0f, 25.0f);
    p["lpStart"]  = rng.rangef(400.0f, 900.0f);
    p["lpEnd"]    = rng.rangef(6000.0f, 12000.0f) - dark * 3000.0f;
    p["noiseMix"] = rng.rangef(0.4f, 0.7f);
    p["widthEnd"] = 0.9f;
    p["gain"]     = 0.5f;
    return r;
}

StereoBuffer renderRiser(const Recipe& rc, const Voice& v) {
    const size_t N = fxLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(40.0, v.freqHz);
    const float octaves = rc.get("octaves", 1.5f);
    const int voices = std::clamp(int(rc.get("sawVoices", 4.0f)), 2, 5);
    const float detune = rc.get("detune", 15.0f);
    const float lpStart = rc.get("lpStart", 600.0f);
    const float lpEnd = rc.get("lpEnd", 9000.0f);
    float noiseMix = rc.get("noiseMix", 0.55f);
    const float widthEnd = rc.get("widthEnd", 0.9f);
    const float gain = rc.get("gain", 0.5f);

    // VARIANT (seed-picked): 0 = noise-only whoosh, 1 = tonal saw sweep,
    // 2 = hybrid (recipe blend). Gives risers distinct character per instance.
    const int variant = int(v.seed % 3u);
    if (variant == 0)      noiseMix = 0.92f;
    else if (variant == 1) noiseMix = 0.12f;

    // REVERSE-SWELL mode for short risers (<= ~2 beats): a fast, front-open
    // filtered swoosh used for boundary/turnaround ear-candy. Detected via the
    // tempo-synced gate length so it is BPM-independent.
    const double beats = (v.syncHz > 0.0) ? v.gateSec * v.syncHz : 99.0;
    const bool reverseSwell = beats <= 2.05;

    Rng prng(v.seed ^ 0x2222u);
    PhaseOsc saw[5];
    double det[5];
    for (int i = 0; i < voices; ++i) {
        double cents = (double(i) - 0.5 * (voices - 1)) * detune;
        det[i] = std::pow(2.0, cents / 1200.0);
        saw[i].reset(prng.uniform());
    }
    Biquad lpL, lpR;
    WhiteNoise nzL(v.seed ^ 0x31u), nzR(v.seed ^ 0x32u);
    DCBlock dcL, dcR;
    const double invN = 1.0 / double(N);
    int coefCtr = 0;
    float curLp = lpStart;
    for (size_t n = 0; n < N; ++n) {
        double prog = double(n) * invN;             // 0..1
        double pmul = std::pow(2.0, octaves * prog); // rising pitch
        double tonal = 0.0;
        for (int i = 0; i < voices; ++i) {
            saw[i].setFreq(freq * det[i] * pmul, sr);
            tonal += saw[i].saw();
        }
        tonal /= voices;
        if ((coefCtr++ & 31) == 0) {
            // Reverse-swell opens the filter faster (linear) for an immediate
            // whoosh; the long riser eases in (prog^2).
            double fcurve = reverseSwell ? prog : (prog * prog);
            curLp = lpStart + (lpEnd - lpStart) * float(fcurve);
            lpL.setLowpass(curLp, 0.9, sr);
            lpR.setLowpass(curLp, 0.9, sr);
        }
        // Amplitude swell: long riser is prog^2 (abrupt end); reverse-swell
        // uses a smoother prog^1.4 so the tail peaks and rolls into the hit.
        float amp = reverseSwell ? float(std::pow(prog, 1.4)) : float(prog * prog);
        float sigL = lpL.process(float(tonal) * (1.0f - noiseMix) + nzL.tick() * noiseMix);
        float sigR = lpR.process(float(tonal) * (1.0f - noiseMix) + nzR.tick() * noiseMix);
        float l = dcL.tick(sigL) * amp * v.velocity * gain;
        float r = dcR.tick(sigR) * amp * v.velocity * gain;
        widen(l, r, 1.0f + widthEnd * float(prog));
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

// ============================================================ DOWNLIFTER
Recipe makeDownlifterRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Downlifter; r.seed = rng.next();
    r.name = fName("downlifter", rng);
    auto& p = r.p;
    p["octaves"]  = rng.rangef(1.0f, 2.0f);
    p["sawVoices"] = float(rng.intRange(3, 5));
    p["detune"]   = rng.rangef(8.0f, 25.0f);
    p["lpStart"]  = rng.rangef(6000.0f, 11000.0f) - dark * 3000.0f;
    p["lpEnd"]    = rng.rangef(300.0f, 700.0f);
    p["noiseMix"] = rng.rangef(0.4f, 0.7f);
    p["gain"]     = 0.5f;
    return r;
}

StereoBuffer renderDownlifter(const Recipe& rc, const Voice& v) {
    const size_t N = fxLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(40.0, v.freqHz) * 2.0;
    const float octaves = rc.get("octaves", 1.5f);
    const int voices = std::clamp(int(rc.get("sawVoices", 4.0f)), 2, 5);
    const float detune = rc.get("detune", 15.0f);
    const float lpStart = rc.get("lpStart", 9000.0f);
    const float lpEnd = rc.get("lpEnd", 500.0f);
    const float noiseMix = rc.get("noiseMix", 0.55f);
    const float gain = rc.get("gain", 0.5f);

    Rng prng(v.seed ^ 0x4444u);
    PhaseOsc saw[5];
    double det[5];
    for (int i = 0; i < voices; ++i) {
        double cents = (double(i) - 0.5 * (voices - 1)) * detune;
        det[i] = std::pow(2.0, cents / 1200.0);
        saw[i].reset(prng.uniform());
    }
    Biquad lpL, lpR;
    WhiteNoise nzL(v.seed ^ 0x51u), nzR(v.seed ^ 0x52u);
    DCBlock dcL, dcR;
    const double invN = 1.0 / double(N);
    int coefCtr = 0; float curLp = lpStart;
    for (size_t n = 0; n < N; ++n) {
        double prog = double(n) * invN;
        double pmul = std::pow(2.0, -octaves * prog);   // falling pitch
        double tonal = 0.0;
        for (int i = 0; i < voices; ++i) {
            saw[i].setFreq(freq * det[i] * pmul, sr);
            tonal += saw[i].saw();
        }
        tonal /= voices;
        if ((coefCtr++ & 31) == 0) {
            curLp = lpStart + (lpEnd - lpStart) * float(prog);
            lpL.setLowpass(curLp, 0.9, sr); lpR.setLowpass(curLp, 0.9, sr);
        }
        float amp = 1.0f - float(prog) * 0.15f;         // gentle fade
        float sigL = lpL.process(float(tonal) * (1.0f - noiseMix) + nzL.tick() * noiseMix);
        float sigR = lpR.process(float(tonal) * (1.0f - noiseMix) + nzR.tick() * noiseMix);
        float l = dcL.tick(sigL) * amp * v.velocity * gain;
        float r = dcR.tick(sigR) * amp * v.velocity * gain;
        widen(l, r, 1.4f);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

// ============================================================ IMPACT
Recipe makeImpactRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Impact; r.seed = rng.next();
    r.name = fName("impact", rng);
    auto& p = r.p;
    p["boomHz"]    = rng.rangef(45.0f, 60.0f);
    p["boomDecay"] = rng.rangef(0.3f, 0.6f);
    p["noiseDecay"] = rng.rangef(0.15f, 0.35f);
    p["rumbleDecay"] = rng.rangef(0.25f, 0.4f);
    p["drive"]     = 1.5f + aggr * 1.5f;
    p["gain"]      = 0.8f;
    return r;
}

StereoBuffer renderImpact(const Recipe& rc, const Voice& v) {
    const size_t N = fxLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float boomHz = rc.get("boomHz", 52.0f);
    const float boomDecay = rc.get("boomDecay", 0.45f);
    const float noiseDecay = rc.get("noiseDecay", 0.25f);
    const float rumbleDecay = rc.get("rumbleDecay", 0.3f);
    const float drive = rc.get("drive", 2.0f);
    const float gain = rc.get("gain", 0.8f);

    // KEYTRACK the sub boom to the plan root when a sane sub pitch is supplied
    // (Impact notes carry rootSub), so the boom is tonally locked to the track;
    // fall back to the recipe's random boomHz otherwise.
    const double boomBase = (v.freqHz >= 28.0 && v.freqHz <= 80.0)
                                ? v.freqHz : double(boomHz);

    double ph = 0;
    EnvAD boom; boom.start(0.002f, boomDecay, sr);
    EnvAD nz; nz.start(0.001f, noiseDecay, sr);
    EnvAD rumble; rumble.start(0.02f, rumbleDecay, sr);
    Biquad nHpL, nHpR, rLpL, rLpR;
    nHpL.setHighpass(600.0, 0.707, sr); nHpR.setHighpass(600.0, 0.707, sr);
    rLpL.setLowpass(200.0, 0.9, sr); rLpR.setLowpass(200.0, 0.9, sr);
    WhiteNoise nzL(v.seed ^ 0x61u), nzR(v.seed ^ 0x62u);
    DCBlock dcL, dcR;
    for (size_t n = 0; n < N; ++n) {
        double t = double(n) / sr;
        double f = boomBase * (1.0 + 1.5 * std::exp(-t / 0.06));  // slight pitch drop
        double bo = std::sin(kTwoPi * ph);
        ph += f / sr; if (ph >= 1.0) ph -= 1.0;
        float boomOut = std::tanh(float(bo) * drive) * boom.tick();
        float ne = nz.tick(), re = rumble.tick();
        float airL = nHpL.process(nzL.tick()) * ne;
        float airR = nHpR.process(nzR.tick()) * ne;
        float rumL = rLpL.process(nzL.tick()) * re * 0.6f;
        float rumR = rLpR.process(nzR.tick()) * re * 0.6f;
        float l = dcL.tick(boomOut + airL + rumL) * v.velocity * gain;
        float r = dcR.tick(boomOut + airR + rumR) * v.velocity * gain;
        widen(l, r, 1.6f);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

// ============================================================ PAD
Recipe makePadRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Pad; r.seed = rng.next();
    r.name = fName("pad", rng);
    auto& p = r.p;
    p["oscCount"] = float(rng.intRange(2, 3));
    p["detune"]   = rng.rangef(6.0f, 16.0f);
    p["lpFreq"]   = rng.rangef(1800.0f, 5000.0f) - dark * 2200.0f;
    p["atk"]      = rng.rangef(0.3f, 1.0f);
    p["rel"]      = rng.rangef(0.3f, 0.8f);
    p["width"]    = rng.rangef(0.6f, 0.95f);
    p["triMix"]   = rng.rangef(0.3f, 0.7f);
    p["gain"]     = 0.4f;
    return r;
}

StereoBuffer renderPad(const Recipe& rc, const Voice& v) {
    const size_t N = fxLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(30.0, v.freqHz);
    const int osc = std::clamp(int(rc.get("oscCount", 3.0f)), 2, 3);
    const float detune = rc.get("detune", 10.0f);
    const float lpFreq = rc.get("lpFreq", 3000.0f);
    const float atk = rc.get("atk", 0.5f);
    const float rel = rc.get("rel", 0.5f);
    const float width = rc.get("width", 0.8f);
    const float triMix = rc.get("triMix", 0.5f);
    const float gain = rc.get("gain", 0.4f);

    Rng prng(v.seed ^ 0x8888u);
    PhaseOsc oscL[3], oscR[3];
    double det[3];
    for (int i = 0; i < osc; ++i) {
        double cents = (double(i) - 0.5 * (osc - 1)) * detune;
        det[i] = std::pow(2.0, cents / 1200.0);
        oscL[i].reset(prng.uniform());
        oscR[i].reset(prng.uniform());
    }
    Biquad lpL, lpR; lpL.setLowpass(lpFreq, 0.707, sr); lpR.setLowpass(lpFreq, 0.707, sr);

    // mod (0..1) stretches the attack into a slow swell — used by texture pads.
    const float atkEff = atk * (1.0f + v.mod * 2.5f);
    EnvADSR amp; amp.start(atkEff, 0.3f, 0.85f, rel, sr);
    DCBlock dcL, dcR;

    // Slow CHORUS movement (per-channel LFOs at slightly different rates give a
    // drifting, alive detune) + gentle FILTER MOTION over the note. More motion
    // for texture pads (higher mod).
    LFO chorusL, chorusR, filtLfo;
    chorusL.setRate(0.18 + 0.10 * prng.uniform(), sr); chorusL.reset(prng.uniform());
    chorusR.setRate(0.15 + 0.10 * prng.uniform(), sr); chorusR.reset(prng.uniform());
    filtLfo.setRate(0.08 + 0.08 * prng.uniform(), sr); filtLfo.reset(prng.uniform());
    const double chorusDepth = 0.010 + 0.012 * v.mod;   // ± ~1-2% detune drift
    const float filtDepth = 0.35f + 0.35f * v.mod;      // fractional LP swing

    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    int coefCtr = 0;
    for (size_t n = 0; n < N; ++n) {
        const bool gate = n < gateN;
        // Update chorus detune + filter cutoff a few hundred times/sec.
        if ((coefCtr++ & 63) == 0) {
            double ml = 1.0 + chorusDepth * double(chorusL.tick());
            double mr = 1.0 + chorusDepth * double(chorusR.tick());
            for (int i = 0; i < osc; ++i) {
                oscL[i].setFreq(freq * det[i] * ml, sr);
                oscR[i].setFreq(freq * det[i] * 1.001 * mr, sr);
            }
            float fm = 0.5f * (filtLfo.tick() + 1.0f);        // 0..1
            float fc = lpFreq * (1.0f - filtDepth * 0.5f + filtDepth * fm);
            fc = clampf(fc, 200.0f, 16000.0f);
            lpL.setLowpass(fc, 0.707, sr); lpR.setLowpass(fc, 0.707, sr);
        }
        double sl = 0, srr = 0;
        for (int i = 0; i < osc; ++i) {
            sl += lerpf(float(oscL[i].saw()), float(oscL[i].triangle()), triMix);
            srr += lerpf(float(oscR[i].saw()), float(oscR[i].triangle()), triMix);
        }
        sl /= osc; srr /= osc;
        float e = amp.tick(gate) * v.velocity * gain;
        float l = dcL.tick(lpL.process(float(sl))) * e;
        float r = dcR.tick(lpR.process(float(srr))) * e;
        widen(l, r, 1.0f + width);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

// ============================================================ MELODYLEAD
Recipe makeMelodyLeadRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::MelodyLead; r.seed = rng.next();
    r.name = fName("lead", rng);
    auto& p = r.p;
    p["bell"]    = rng.chance(0.5) ? 1.0f : 0.0f;
    p["fmRatio"] = float(rng.intRange(2, 7));
    p["fmIndex"] = rng.rangef(1.0f, 4.0f) + aggr * 2.0f;
    p["decay"]   = rng.rangef(0.15f, 0.5f);
    p["rel"]     = rng.rangef(0.05f, 0.15f);
    p["width"]   = rng.rangef(0.3f, 0.6f);
    p["lpFreq"]  = rng.rangef(3000.0f, 8000.0f) - dark * 2500.0f;
    p["gain"]    = 0.5f;
    return r;
}

StereoBuffer renderMelodyLead(const Recipe& rc, const Voice& v) {
    const size_t N = fxLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(40.0, v.freqHz);
    const bool bell = rc.get("bell", 0.0f) > 0.5f;
    const float ratio = rc.get("fmRatio", 3.0f);
    const float index = rc.get("fmIndex", 2.5f);
    const float decay = rc.get("decay", 0.3f);
    const float rel = rc.get("rel", 0.1f);
    const float width = rc.get("width", 0.45f);
    const float lpFreq = rc.get("lpFreq", 5000.0f);
    const float gain = rc.get("gain", 0.5f);

    // mod (0..1) = FORMANT COLOR for vocal-chop character: shifts the FM index
    // (vowel openness) and a resonant peak's centre so repeated chops sing on
    // different "vowels". Neutral at mod≈0.3.
    const float formant = v.mod;
    const float indexF = index * (0.6f + 1.1f * formant);      // brighter/darker vowel
    const double formHz = 700.0 + 1900.0 * double(formant);    // vowel peak sweep

    double carPh = 0, modPh = 0, h4Ph = 0;
    EnvADSR amp; amp.start(0.003f, decay, 0.0f, rel, sr); // pluck: decay to zero-ish
    EnvAD idxEnv; idxEnv.start(0.001f, decay * 0.6f, sr);
    Biquad lpL, lpR; lpL.setLowpass(lpFreq, 0.707, sr); lpR.setLowpass(lpFreq, 0.707, sr);
    Biquad formL, formR; // vowel-ish resonant peak driven by mod
    formL.setPeak(formHz, 1.4, 6.0 + 6.0 * double(formant), sr);
    formR.setPeak(formHz, 1.4, 6.0 + 6.0 * double(formant), sr);
    Allpass1 apR; apR.setCoef(1500.0, sr);
    DCBlock dcL, dcR;
    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    for (size_t n = 0; n < N; ++n) {
        const bool gate = n < gateN;
        double s;
        if (bell) {
            s = std::sin(kTwoPi * carPh) + 0.35 * std::sin(kTwoPi * h4Ph);
            h4Ph += freq * 4.0 / sr; if (h4Ph >= 1.0) h4Ph -= 1.0;
        } else {
            float ie = idxEnv.tick();
            double m = std::sin(kTwoPi * modPh) * indexF * (0.3 + 0.7 * ie);
            s = std::sin(kTwoPi * carPh + m);
        }
        carPh += freq / sr; if (carPh >= 1.0) carPh -= 1.0;
        modPh += freq * ratio / sr; if (modPh >= 1.0) modPh -= 1.0;
        float e = amp.tick(gate) * v.velocity * gain;
        float mono = float(s) * e;
        // Formant peak adds the vowel colour (only meaningfully when mod != 0).
        float lIn = formL.process(mono);
        float rIn = formR.process(mono);
        float l = dcL.tick(lpL.process(lIn));
        float rC = apR.process(rIn);
        float r = dcR.tick(lpR.process(lerpf(rIn, rC, width)));
        widen(l, r, 1.0f + width);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

} // namespace rtg::synth
