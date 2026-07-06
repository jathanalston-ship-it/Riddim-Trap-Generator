// Bass-role factories and renderers: Growl (riddim signature), Screech, Sub,
// Bass808. Deterministic; all variation from Recipe params + Voice seed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../../sound_design/src/dsp.h"
#include "../../sound_design/src/voices.h"

namespace rtg::synth {
using namespace dsp;

// ------------------------------------------------------------------ helpers
static inline size_t lenSamples(const Voice& v) {
    return std::max<size_t>(1, size_t(std::llround(v.lenSec * v.sr)));
}
static std::string hexName(const char* role, Rng& rng) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04x", unsigned(rng.next() & 0xffff));
    return std::string(role) + "_" + buf;
}

// ============================================================ GROWL
Recipe makeGrowlRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Growl; r.seed = rng.next();
    r.name = hexName("growl", rng);
    auto& p = r.p;
    static const float ratios[] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
    p["carrierRatio"] = ratios[rng.intRange(0, 4)];
    p["carrierSaw"]   = rng.chance(0.6) ? 1.0f : 0.0f;
    p["fmIndex"]      = 2.0f + aggr * 9.0f + rng.rangef(-1.0f, 1.0f) * (1.0f + nov * 3.0f);
    p["fmDecay"]      = rng.rangef(0.04f, 0.14f);
    p["drivePre"]     = 1.5f + aggr * 2.5f + rng.rangef(-0.3f, 0.5f);
    p["lpMul"]        = rng.rangef(2.5f, 6.0f) - dark * 1.5f;
    p["lpQ"]          = rng.rangef(0.7f, 1.8f);
    p["lfoRate"]      = rng.rangef(0.5f, 8.0f);
    p["lfoDepth"]     = rng.rangef(0.25f, 0.7f);
    p["combTuneMul"]  = rng.rangef(1.0f, 3.5f);
    p["combFb"]       = 0.9f - dark * 0.30f - rng.rangef(0.0f, 0.05f);
    p["combDamp"]     = 0.15f + dark * 0.5f;
    p["wsMix"]        = clampf(aggr * 0.8f + rng.rangef(-0.1f, 0.15f), 0.0f, 1.0f);
    p["drivePost"]    = 1.0f + aggr * 2.0f;
    p["hpFreq"]       = rng.rangef(95.0f, 140.0f);
    p["midGainDb"]    = rng.rangef(2.0f, 6.0f);
    p["midFreq"]      = rng.rangef(600.0f, 1400.0f);
    p["highShelfDb"]  = 3.0f - dark * 9.0f;   // dark -> cut highs
    p["unison"]       = (aggr > 0.6f) ? float(rng.intRange(2, 3)) : 1.0f;
    p["detuneCents"]  = rng.rangef(6.0f, 18.0f);
    p["width"]        = 0.10f + nov * 0.20f;
    p["ampAtk"]       = rng.rangef(0.004f, 0.018f);
    p["ampRel"]       = rng.rangef(0.008f, 0.020f);
    p["gain"]         = 0.55f;
    return r;
}

StereoBuffer renderGrowl(const Recipe& rc, const Voice& v) {
    const size_t N = lenSamples(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(20.0, v.freqHz);

    const float carrierRatio = rc.get("carrierRatio", 1.0f);
    const bool  carrierSaw   = rc.get("carrierSaw", 1.0f) > 0.5f;
    const float fmIndex      = std::max(0.0f, rc.get("fmIndex", 6.0f));
    const float fmDecay      = rc.get("fmDecay", 0.08f);
    const float drivePre     = rc.get("drivePre", 2.5f);
    const float lpMul        = std::max(1.5f, rc.get("lpMul", 4.0f));
    const float lpQ          = rc.get("lpQ", 1.2f);
    const float lfoRate      = rc.get("lfoRate", 3.0f);
    const float lfoDepth     = rc.get("lfoDepth", 0.5f);
    const float combTuneMul  = std::max(0.5f, rc.get("combTuneMul", 2.0f));
    const float combFb       = clampf(rc.get("combFb", 0.75f), 0.0f, 0.94f);
    const float combDamp     = clampf(rc.get("combDamp", 0.3f), 0.0f, 0.95f);
    const float wsMix        = clampf(rc.get("wsMix", 0.4f), 0.0f, 1.0f);
    const float drivePost    = rc.get("drivePost", 2.0f);
    const float hpFreq       = rc.get("hpFreq", 110.0f);
    const float midGainDb    = rc.get("midGainDb", 4.0f);
    const float midFreq      = rc.get("midFreq", 900.0f);
    const float highShelfDb  = rc.get("highShelfDb", 0.0f);
    const int   unison       = std::clamp(int(rc.get("unison", 1.0f)), 1, 3);
    const float detuneCents  = rc.get("detuneCents", 10.0f);
    const float width        = clampf(rc.get("width", 0.15f), 0.0f, 0.6f);
    const float ampAtk       = rc.get("ampAtk", 0.008f);
    const float ampRel       = rc.get("ampRel", 0.012f);
    const float gain         = rc.get("gain", 0.55f);
    const float mod          = clampf(v.mod, 0.0f, 1.0f);

    // FM voices (unison).
    double carPh[3] = {0, 0, 0}, modPh[3] = {0, 0, 0};
    PhaseOsc sawOsc[3];
    Rng nrng(v.seed);
    double detFactor[3];
    for (int i = 0; i < unison; ++i) {
        double cents = (unison == 1) ? 0.0 : (double(i) - 0.5 * (unison - 1)) * detuneCents;
        detFactor[i] = std::pow(2.0, cents / 1200.0);
        carPh[i] = nrng.uniform();
        modPh[i] = nrng.uniform();
        sawOsc[i].setFreq(freq * detFactor[i], sr);
        sawOsc[i].reset(nrng.uniform());
    }
    SVF svf;
    Comb comb;
    comb.fb = combFb; comb.damp = combDamp;
    comb.setMaxDelay(int(sr / std::max(20.0, freq * combTuneMul * 0.4)) + 8);
    Biquad hp; hp.setHighpass(hpFreq, 0.707, sr);
    Biquad midPk; midPk.setPeak(midFreq, 1.0, midGainDb, sr);
    Biquad hsh; hsh.setHighShelf(4000.0, highShelfDb, sr);
    Allpass1 apR; apR.setCoef(1200.0, sr);
    DCBlock dc;
    LFO lfo; lfo.setRate(lfoRate, sr);
    EnvADSR amp; amp.start(ampAtk, 0.06f, 0.85f, ampRel, sr);
    EnvAD idxEnv; idxEnv.start(0.001f, fmDecay, sr);

    const double baseCut = freq * lpMul;
    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    int coefCtr = 0;

    for (size_t n = 0; n < N; ++n) {
        const bool gate = n < gateN;
        const float ie = idxEnv.tick();
        const float idx = fmIndex * (0.3f + 0.7f * ie);

        // FM oscillator bank.
        double car = 0.0;
        for (int i = 0; i < unison; ++i) {
            double mfreq = freq * detFactor[i] * carrierRatio;
            double cfreq = freq * detFactor[i];
            double m = std::sin(kTwoPi * modPh[i]) * idx;
            double c = std::sin(kTwoPi * carPh[i] + m);
            if (carrierSaw) c = 0.6 * c + 0.4 * sawOsc[i].saw();
            car += c;
            carPh[i] += cfreq / sr; if (carPh[i] >= 1.0) carPh[i] -= 1.0;
            modPh[i] += mfreq / sr; if (modPh[i] >= 1.0) modPh[i] -= 1.0;
        }
        car /= unison;

        float pre = std::tanh(float(car) * drivePre);

        // Keytracked LP, modulated by note.mod and LFO ("talk").
        float lval = lfo.tick();
        if ((coefCtr++ & 7) == 0) {
            double cut = baseCut * (1.0 + 0.95 * mod + lfoDepth * lval);
            svf.set(cut, lpQ, sr);
        }
        svf.process(pre);
        float lp = float(svf.lp);

        // Tuned comb, tune swept by mod.
        double combFreq = freq * combTuneMul * (1.0 + 0.5 * mod);
        float combOut = comb.process(lp, float(sr / combFreq));

        float pt = std::tanh(combOut * drivePost);
        float pf = shFold(combOut * drivePost);
        float post = lerpf(pt, pf, wsMix);

        float h = hp.process(post);
        h = midPk.process(h);
        h = hsh.process(h);
        float mono = dc.tick(h) * amp.tick(gate) * v.velocity * gain;

        float rC = apR.process(mono);
        float l = mono;
        float r = lerpf(mono, rC, width);
        widen(l, r, 1.0f + width);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

// ============================================================ SCREECH
Recipe makeScreechRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Screech; r.seed = rng.next();
    r.name = hexName("screech", rng);
    auto& p = r.p;
    p["fmRatio"]     = rng.rangef(4.0f, 9.0f);
    p["fmIndex"]     = 3.0f + aggr * 6.0f + nov * 3.0f;
    p["fmDecay"]     = rng.rangef(0.08f, 0.3f);
    p["foldDrive"]   = 1.2f + aggr * 2.0f;
    p["hpFreq"]      = rng.rangef(350.0f, 500.0f);
    p["phaserRate"]  = rng.rangef(0.3f, 4.0f);
    p["phaserDepth"] = rng.rangef(0.4f, 0.9f);
    p["phaserCenter"] = rng.rangef(900.0f, 2200.0f);
    p["combFb"]      = rng.rangef(0.3f, 0.7f);
    p["width"]       = 0.5f;
    p["highShelfDb"] = 2.0f - dark * 6.0f;
    p["ampAtk"]      = rng.rangef(0.003f, 0.02f);
    p["gain"]        = 0.4f;
    return r;
}

StereoBuffer renderScreech(const Recipe& rc, const Voice& v) {
    const size_t N = lenSamples(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(30.0, v.freqHz);
    const float ratio = rc.get("fmRatio", 6.0f);
    const float index = rc.get("fmIndex", 6.0f);
    const float fmDecay = rc.get("fmDecay", 0.15f);
    const float foldDrive = rc.get("foldDrive", 2.0f);
    const float hpFreq = rc.get("hpFreq", 400.0f);
    const float phaserRate = rc.get("phaserRate", 1.5f);
    const float phaserDepth = clampf(rc.get("phaserDepth", 0.6f), 0.0f, 1.0f);
    const float phaserCenter = rc.get("phaserCenter", 1400.0f);
    const float combFb = clampf(rc.get("combFb", 0.5f), 0.0f, 0.85f);
    const float width = rc.get("width", 0.5f);
    const float highShelfDb = rc.get("highShelfDb", 0.0f);
    const float ampAtk = rc.get("ampAtk", 0.008f);
    const float gain = rc.get("gain", 0.4f);
    const float mod = clampf(v.mod, 0.0f, 1.0f);

    double carPh = 0, modPh = 0;
    Biquad hp; hp.setHighpass(hpFreq, 0.707, sr);
    Biquad hsh; hsh.setHighShelf(4000.0, highShelfDb, sr);
    Comb comb; comb.fb = combFb; comb.damp = 0.2f;
    comb.setMaxDelay(int(sr / std::max(60.0, freq)) + 8);
    Allpass1 ap1, ap2, ap3;
    LFO lfo; lfo.setRate(phaserRate, sr);
    DCBlock dcL, dcR;
    EnvADSR amp; amp.start(ampAtk, 0.1f, 0.8f, 0.03f, sr);
    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    const double combFreq = freq * (2.0 + mod);

    for (size_t n = 0; n < N; ++n) {
        const bool gate = n < gateN;
        double t = double(n) / sr;
        float ie = std::exp(-t / std::max(0.02f, fmDecay));
        double m = std::sin(kTwoPi * modPh) * index * (0.4 + 0.6 * ie);
        double c = std::sin(kTwoPi * carPh + m);
        carPh += freq / sr; if (carPh >= 1.0) carPh -= 1.0;
        modPh += freq * ratio / sr; if (modPh >= 1.0) modPh -= 1.0;

        float s = shFold(float(c) * foldDrive);
        s = hp.process(s);
        s = comb.process(s, float(sr / combFreq));

        // Phaser: 3 allpass stages swept by LFO.
        float lv = lfo.tick();
        float apf = phaserCenter * std::pow(2.0f, phaserDepth * lv);
        ap1.setCoef(apf, sr); ap2.setCoef(apf * 1.5, sr); ap3.setCoef(apf * 2.2, sr);
        float ph = ap3.process(ap2.process(ap1.process(s)));
        float main = 0.6f * s + 0.4f * ph;
        float alt  = 0.6f * s - 0.4f * ph; // opposite phaser mix for stereo
        float env = amp.tick(gate) * v.velocity * gain;
        float l = dcL.tick(hsh.process(main)) * env;
        float r = lerpf(l, dcR.tick(alt) * env, width);
        widen(l, r, 1.0f + width);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

// ============================================================ SUB
Recipe makeSubRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Sub; r.seed = rng.next();
    r.name = hexName("sub", rng);
    auto& p = r.p;
    p["h2"]     = rng.rangef(0.05f, 0.12f);
    p["h3"]     = rng.rangef(0.03f, 0.10f) * (1.0f - dark * 0.5f);
    p["drive"]  = 1.05f + aggr * 0.6f;
    p["ampAtk"] = rng.rangef(0.002f, 0.006f);
    p["ampRel"] = rng.rangef(0.02f, 0.05f);
    p["gain"]   = 0.7f;
    return r;
}

StereoBuffer renderSub(const Recipe& rc, const Voice& v) {
    const size_t N = lenSamples(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(20.0, v.freqHz);
    const float h2 = rc.get("h2", 0.08f);
    const float h3 = rc.get("h3", 0.05f);
    const float drive = rc.get("drive", 1.2f);
    const float ampAtk = rc.get("ampAtk", 0.004f);
    const float ampRel = rc.get("ampRel", 0.03f);
    const float gain = rc.get("gain", 0.7f);
    double ph = 0;
    EnvADSR amp; amp.start(ampAtk, 0.08f, 0.9f, ampRel, sr);
    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    DCBlock dc;
    for (size_t n = 0; n < N; ++n) {
        const bool gate = n < gateN;
        double s = std::sin(kTwoPi * ph)
                 + h2 * std::sin(kTwoPi * 2.0 * ph)
                 + h3 * std::sin(kTwoPi * 3.0 * ph);
        ph += freq / sr; if (ph >= 1.0) ph -= 1.0;
        float o = std::tanh(float(s) * drive) / std::tanh(drive);
        float mono = dc.tick(o) * amp.tick(gate) * v.velocity * gain;
        out.l[n] = mono; out.r[n] = mono; // strictly mono
    }
    return out;
}

// ============================================================ BASS808
Recipe makeBass808Recipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Bass808; r.seed = rng.next();
    r.name = hexName("808", rng);
    auto& p = r.p;
    p["pitchStart"] = rng.rangef(1.0f, 3.0f);          // semitones
    p["pitchDecay"] = rng.rangef(0.03f, 0.08f);        // s
    p["drive"]      = 1.5f + aggr * 2.5f;
    p["decay"]      = rng.rangef(0.6f, 1.5f);
    p["brightness"] = clampf(0.15f + aggr * 0.5f - dark * 0.2f, 0.0f, 1.0f);
    p["ampAtk"]     = rng.rangef(0.002f, 0.006f);
    p["gain"]       = 0.7f;
    return r;
}

StereoBuffer renderBass808(const Recipe& rc, const Voice& v) {
    const size_t N = lenSamples(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(20.0, v.freqHz);
    const float pitchStart = rc.get("pitchStart", 2.0f);
    const float pitchDecay = rc.get("pitchDecay", 0.05f);
    const float drive = rc.get("drive", 2.5f);
    const float decay = rc.get("decay", 1.0f);
    const float brightness = rc.get("brightness", 0.3f);
    const float ampAtk = rc.get("ampAtk", 0.004f);
    const float gain = rc.get("gain", 0.7f);

    double ph = 0;
    EnvAD amp; amp.start(ampAtk, decay, sr);
    DCBlock dc;
    Biquad tone; tone.setHighShelf(1500.0, brightness * 6.0f, sr);
    const double gateSec = std::max(0.05, v.gateSec);
    const float bend = v.bendSemis;

    for (size_t n = 0; n < N; ++n) {
        double t = double(n) / sr;
        // pitch envelope (fast drop) + linear bend glide across the note.
        double penv = pitchStart * std::exp(-t / std::max(0.005f, pitchDecay));
        double glide = bend * clampd(t / gateSec, 0.0, 1.0);
        double f = freq * std::pow(2.0, (penv + glide) / 12.0);
        double s = std::sin(kTwoPi * ph);
        ph += f / sr; if (ph >= 1.0) ph -= 1.0;
        float o = std::tanh(float(s) * drive) / std::tanh(drive);
        o = tone.process(o);
        float mono = dc.tick(o) * amp.tick() * v.velocity * gain;
        out.l[n] = mono; out.r[n] = mono;
    }
    return out;
}

} // namespace rtg::synth
