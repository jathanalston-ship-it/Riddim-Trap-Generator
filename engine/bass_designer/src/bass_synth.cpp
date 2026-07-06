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

// Encode/decode a vowel path (up to 3 vowel indices 0..4) into recipe params.
static void pickVowelPath(Rng& rng, float& p0, float& p1, float& p2) {
    p0 = float(rng.intRange(0, kVowelCount - 1));
    p1 = float(rng.intRange(0, kVowelCount - 1));
    p2 = float(rng.intRange(0, kVowelCount - 1));
}

// ============================================================ GROWL
Recipe makeGrowlRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Growl; r.seed = rng.next();
    r.name = hexName("growl", rng);
    auto& p = r.p;
    // --- rich source ---
    static const float ratios[] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
    p["carrierRatio"] = ratios[rng.intRange(0, 4)];
    p["fmIndex"]      = 2.0f + aggr * 8.0f + rng.rangef(-1.0f, 1.0f) * (1.0f + nov * 2.5f);
    p["fmDecay"]      = rng.rangef(0.05f, 0.16f);
    p["fmFeedback"]   = rng.rangef(0.0f, 0.5f) * (0.5f + aggr);
    p["carMix"]       = rng.rangef(0.15f, 0.4f);    // analog square leads over FM sine
    p["srcMorph"]     = rng.rangef(0.35f, 0.6f);    // bias to square-center of saw->square->pulse
    p["pulseWidth"]   = rng.rangef(0.2f, 0.5f);
    p["pmAmt"]        = rng.rangef(0.08f, 0.45f) * (0.6f + aggr); // FM the square carrier -> metal
    p["pwmRate"]      = rng.rangef(0.15f, 0.8f);    // slow pulse-width movement
    p["pwmDepth"]     = rng.rangef(0.05f, 0.2f);
    p["unison"]       = float(rng.intRange(1, 3));
    p["detuneCents"]  = rng.rangef(6.0f, 20.0f);
    // --- bitcrush / sample-rate-reduction grit (robotic "talk") ---
    p["gritRate"]     = rng.rangef(3000.0f, 14000.0f);
    p["gritBits"]     = rng.rangef(6.0f, 12.0f);
    p["gritMix"]      = clampf(rng.rangef(0.25f, 0.7f) * (0.5f + aggr), 0.0f, 1.0f);
    // --- tempo-synced amplitude gate (the "wub") ---
    static const float gateDivs[] = {2.0f, 3.0f, 4.0f, 6.0f}; // 8ths / 8th-trips / 16ths / sextuplets
    p["gateDiv"]      = gateDivs[rng.intRange(0, 3)];
    p["gateDepth"]    = rng.rangef(0.45f, 0.85f);
    p["gateShape"]    = (rng.uniform() < 0.5) ? 4.0f : 5.0f;   // 4 square, 5 stepped S&H
    // --- distortion staging ---
    p["drivePre"]     = 1.6f + aggr * 2.6f + rng.rangef(-0.2f, 0.5f);
    p["mudDb"]        = -(2.0f + rng.rangef(0.0f, 4.0f));   // ~300 Hz dip
    p["drivePost"]    = 1.2f + aggr * 2.2f;
    p["wsMix"]        = clampf(aggr * 0.7f + rng.rangef(-0.1f, 0.2f), 0.0f, 1.0f);
    // --- formant / vowel bank (the talk) ---
    float v0, v1, v2; pickVowelPath(rng, v0, v1, v2);
    p["vowel0"] = v0; p["vowel1"] = v1; p["vowel2"] = v2;
    p["formantQ"]     = rng.rangef(5.0f, 11.0f);
    p["formantMix"]   = rng.rangef(0.55f, 0.85f);   // formant vs body path
    p["formantGain"]  = rng.rangef(2.0f, 3.2f);
    p["morphBase"]    = rng.rangef(0.05f, 0.35f);
    p["morphMod"]     = rng.rangef(0.45f, 0.75f);   // note.mod influence
    // --- rhythmic modulation ---
    p["lfoRate"]      = rng.rangef(1.5f, 9.0f);
    p["lfoDepth"]     = rng.rangef(0.30f, 0.6f);
    p["lfoShape"]     = float(rng.intRange(0, 5));
    p["lfoSteps"]     = float(rng.intRange(2, 8));
    // --- body filter ---
    p["lpMul"]        = rng.rangef(3.0f, 7.0f) - dark * 1.5f;
    p["lpQ"]          = rng.rangef(0.7f, 1.6f);
    // --- output EQ / glue ---
    p["hpFreq"]       = rng.rangef(92.0f, 110.0f);
    p["midGainDb"]    = rng.rangef(2.5f, 6.0f);
    p["midFreq"]      = rng.rangef(800.0f, 2200.0f);
    p["highShelfDb"]  = 3.0f - dark * 9.0f;
    p["ottAmt"]       = rng.rangef(0.15f, 0.4f);
    // --- movement polish ---
    p["phaserRate"]   = rng.rangef(0.2f, 1.2f);
    p["phaserMix"]    = rng.rangef(0.15f, 0.4f);
    p["width"]        = 0.08f + nov * 0.18f;
    p["ampAtk"]       = rng.rangef(0.004f, 0.016f);
    p["ampRel"]       = rng.rangef(0.008f, 0.022f);
    p["gain"]         = 0.6f;
    return r;
}

StereoBuffer renderGrowl(const Recipe& rc, const Voice& v) {
    const size_t N = lenSamples(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(20.0, v.freqHz);

    const float carrierRatio = rc.get("carrierRatio", 1.0f);
    const float fmIndex      = std::max(0.0f, rc.get("fmIndex", 6.0f));
    const float fmDecay      = rc.get("fmDecay", 0.10f);
    const float fmFeedback   = clampf(rc.get("fmFeedback", 0.2f), 0.0f, 0.9f);
    const float carMix       = clampf(rc.get("carMix", 0.5f), 0.0f, 1.0f);
    const float srcMorph     = clampf(rc.get("srcMorph", 0.5f), 0.0f, 1.0f);
    const double pulseWidth  = clampd(rc.get("pulseWidth", 0.35f), 0.1, 0.9);
    const float pmAmt        = clampf(rc.get("pmAmt", 0.22f), 0.0f, 1.5f);  // FM->square depth
    const float pwmRate      = std::max(0.0f, rc.get("pwmRate", 0.35f));
    const float pwmDepth     = clampf(rc.get("pwmDepth", 0.12f), 0.0f, 0.4f);
    const double gritRate    = clampd(rc.get("gritRate", 8000.0f), 1000.0, sr * 0.5);
    const float gritBits     = clampf(rc.get("gritBits", 9.0f), 2.0f, 16.0f);
    const float gritMix      = clampf(rc.get("gritMix", 0.4f), 0.0f, 1.0f);
    const float gateDivF     = clampf(rc.get("gateDiv", 4.0f), 1.0f, 16.0f);
    const float gateDepth    = clampf(rc.get("gateDepth", 0.6f), 0.0f, 0.95f);
    const int   gateShape    = std::clamp(int(rc.get("gateShape", 4.0f)), 4, 5);
    const int   unison       = std::clamp(int(rc.get("unison", 1.0f)), 1, 3);
    const float detuneCents  = rc.get("detuneCents", 12.0f);
    const float drivePre     = rc.get("drivePre", 2.5f);
    const float mudDb        = rc.get("mudDb", -3.0f);
    const float drivePost    = rc.get("drivePost", 2.0f);
    const float wsMix        = clampf(rc.get("wsMix", 0.4f), 0.0f, 1.0f);
    const int   vowel0       = int(rc.get("vowel0", 0.0f));
    const int   vowel1       = int(rc.get("vowel1", 1.0f));
    const int   vowel2       = int(rc.get("vowel2", 3.0f));
    const double formantQ    = std::max(1.5, double(rc.get("formantQ", 8.0f)));
    const float formantMix   = clampf(rc.get("formantMix", 0.7f), 0.0f, 1.0f);
    const float formantGain  = rc.get("formantGain", 2.6f);
    const float morphBase    = clampf(rc.get("morphBase", 0.2f), 0.0f, 1.0f);
    const float morphMod     = clampf(rc.get("morphMod", 0.6f), 0.0f, 1.0f);
    const float lfoRate      = rc.get("lfoRate", 4.0f);
    const float lfoDepth     = clampf(rc.get("lfoDepth", 0.45f), 0.0f, 1.0f);
    const int   lfoShape     = std::clamp(int(rc.get("lfoShape", 0.0f)), 0, 5);
    const int   lfoSteps     = std::clamp(int(rc.get("lfoSteps", 4.0f)), 1, 16);
    const float lpMul        = std::max(1.5f, rc.get("lpMul", 4.5f));
    const float lpQ          = rc.get("lpQ", 1.1f);
    const float hpFreq       = rc.get("hpFreq", 100.0f);
    const float midGainDb    = rc.get("midGainDb", 4.0f);
    const float midFreq      = rc.get("midFreq", 1400.0f);
    const float highShelfDb  = rc.get("highShelfDb", 0.0f);
    const float ottAmt       = clampf(rc.get("ottAmt", 0.28f), 0.0f, 1.0f);
    const float phaserRate   = rc.get("phaserRate", 0.6f);
    const float phaserMix    = clampf(rc.get("phaserMix", 0.3f), 0.0f, 1.0f);
    const float width        = clampf(rc.get("width", 0.12f), 0.0f, 0.6f);
    const float ampAtk       = rc.get("ampAtk", 0.008f);
    const float ampRel       = rc.get("ampRel", 0.012f);
    const float gain         = rc.get("gain", 0.6f);
    const float mod          = clampf(v.mod, 0.0f, 1.0f);

    const int vpath[3] = {vowel0, vowel1, vowel2};

    // Source voices (unison): FM carrier/modulator + morphable analog osc.
    double carPh[3] = {0, 0, 0}, modPh[3] = {0, 0, 0};
    float  fbState[3] = {0, 0, 0};
    PhaseOsc osc[3];
    Rng nrng(v.seed);
    double detFactor[3];
    for (int i = 0; i < unison; ++i) {
        double cents = (unison == 1) ? 0.0 : (double(i) - 0.5 * (unison - 1)) * detuneCents;
        detFactor[i] = std::pow(2.0, cents / 1200.0);
        carPh[i] = nrng.uniform();
        modPh[i] = nrng.uniform();
        osc[i].setFreq(freq * detFactor[i], sr);
        osc[i].reset(nrng.uniform());
    }

    FormantBank formant;
    SVF svf;
    Biquad mud;  mud.setPeak(300.0, 0.9, mudDb, sr);
    Biquad hp;   hp.setHighpass(hpFreq, 0.707, sr);
    Biquad loSh; loSh.setLowShelf(210.0, -4.0, sr);   // cede lows to the sub lane
    Biquad midPk; midPk.setPeak(midFreq, 1.1, midGainDb, sr);
    Biquad hsh;  hsh.setHighShelf(4200.0, highShelfDb, sr);
    Allpass1 ap1, ap2, ap3, apR;
    apR.setCoef(1500.0, sr);
    OTTLite ott; ott.set(sr, ottAmt);
    DCBlock dc;
    ShapeLFO lfo; lfo.init(lfoRate, sr, lfoShape, lfoSteps, nrng.next());
    LFO phaser; phaser.setRate(phaserRate, sr);
    LFO pwm; pwm.setRate(pwmRate, sr);            // slow pulse-width movement
    EnvADSR amp; amp.start(ampAtk, 0.06f, 0.85f, ampRel, sr);
    EnvAD idxEnv; idxEnv.start(0.001f, fmDecay, sr);

    // Tempo-synced amplitude gate ("wub"): active only when the caller supplies a
    // beat rate (v.syncHz). Square/stepped shape -> unipolar amp multiplier, declicked.
    const uint64_t gateSeed = nrng.next();       // consumed unconditionally for determinism
    const bool gateOn = v.syncHz > 0.0;
    ShapeLFO gateLfo;
    if (gateOn) gateLfo.init(clampd(v.syncHz * double(gateDivF), 0.1, 200.0),
                             sr, gateShape, 8, gateSeed);
    OnePole gateSmooth; gateSmooth.setTime(0.003f, sr); gateSmooth.reset(1.0f);

    // Bitcrush / sample-rate-reduction grit state (phase-accumulator S&H).
    double gritPhase = 1.0;   // >=1 so the first sample latches a fresh value
    float  gritHold = 0.0f;
    const float gritLevels = std::pow(2.0f, gritBits - 1.0f);

    const double baseCut = freq * lpMul;
    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    int coefCtr = 0;
    float lval = 0.0f;

    for (size_t n = 0; n < N; ++n) {
        const bool gate = n < gateN;
        const float ie = idxEnv.tick();
        const float idx = fmIndex * (0.35f + 0.65f * ie);

        // slow PWM movement on the pulse width (band-limited pulse).
        double pw = clampd(pulseWidth + double(pwmDepth) * double(pwm.tick()), 0.05, 0.95);

        // --- square-dominant source: read the analog osc at a phase-modulated
        //     phase (FM from the sine modulator B into the square carrier). ---
        double sig = 0.0;
        for (int i = 0; i < unison; ++i) {
            double mfreq = freq * detFactor[i] * carrierRatio;
            double cfreq = freq * detFactor[i];
            double m = std::sin(kTwoPi * modPh[i] + double(fbState[i]) * fmFeedback) * idx;
            double c = std::sin(kTwoPi * carPh[i] + m);
            fbState[i] = float(c);
            // morphable analog osc (saw -> square -> pulse); FM applied as a phase
            // offset in cycles, wrapped to [0,1). Same `inc` -> approximate band-limit.
            double inc = osc[i].inc;
            double phm = osc[i].phase + double(pmAmt) * m;   // FM into the square
            phm -= std::floor(phm);                          // wrap to [0,1)
            double saw = 2.0 * phm - 1.0 - polyBlep(phm, inc);
            double t2 = phm + 0.5; if (t2 >= 1.0) t2 -= 1.0;
            double sqr = (phm < 0.5 ? 1.0 : -1.0) + polyBlep(phm, inc) - polyBlep(t2, inc);
            double t3 = phm + (1.0 - pw); if (t3 >= 1.0) t3 -= 1.0;
            double pul = (phm < pw ? 1.0 : -1.0) + polyBlep(phm, inc) - polyBlep(t3, inc);
            osc[i].advance();
            double analog = (srcMorph < 0.5) ? (saw + (sqr - saw) * (srcMorph * 2.0))
                                             : (sqr + (pul - sqr) * ((srcMorph - 0.5) * 2.0));
            sig += carMix * c + (1.0 - carMix) * analog;
            carPh[i] += cfreq / sr; if (carPh[i] >= 1.0) carPh[i] -= 1.0;
            modPh[i] += mfreq / sr; if (modPh[i] >= 1.0) modPh[i] -= 1.0;
        }
        sig /= unison;

        // --- pre-drive then mud cut ---
        float pre = std::tanh(float(sig) * drivePre);
        pre = mud.process(pre);

        // --- vowel morph driven by note.mod + rhythmic LFO ("talk") ---
        if ((coefCtr & 7) == 0) {
            lval = lfo.tick();
            float morph = clampf(morphBase + morphMod * mod + lfoDepth * 0.5f * lval, 0.0f, 1.0f);
            Vowel vw = vowelMorph(vpath, 3, morph, 1.0f);
            formant.set(vw.f1, vw.f2, vw.f3, formantQ, sr);
            double cut = baseCut * (1.0 + 0.6 * mod + 0.4 * lfoDepth * lval);
            svf.set(cut, lpQ, sr);
        }
        ++coefCtr;

        // --- formant path vs body path ---
        float fp = formant.process(pre) * formantGain;
        svf.process(pre);
        float bp = float(svf.lp);
        float filtered = lerpf(bp, fp, formantMix);

        // --- post-drive (soft-clip / fold blend) ---
        float pt = std::tanh(filtered * drivePost);
        float pf = shFold(filtered * drivePost);
        float post = lerpf(pt, pf, wsMix);

        // --- bitcrush / sample-rate reduction grit (robotic "talk") ---
        // Rhythmic LFO nudges the hold rate so grit interacts with the gate.
        // NOT oversampled: the aliasing IS the effect. post is bounded [-1,1] here.
        if (gritMix > 0.0f) {
            double gRate = clampd(gritRate * (1.0 + 0.5 * double(lval)), 1000.0, sr * 0.5);
            gritPhase += gRate / sr;
            if (gritPhase >= 1.0) {
                gritPhase -= std::floor(gritPhase);
                float q = std::round(post * gritLevels) / gritLevels; // bit reduction
                gritHold = q;
            }
            post = lerpf(post, gritHold, gritMix);
        }

        // --- subtle phaser movement ---
        float pv = phaser.tick();
        float apf = 700.0f * std::pow(2.0f, 0.9f * pv);
        ap1.setCoef(apf, sr); ap2.setCoef(apf * 1.6, sr); ap3.setCoef(apf * 2.3, sr);
        float ph3 = ap3.process(ap2.process(ap1.process(post)));
        post = lerpf(post, ph3, phaserMix);

        // --- output EQ + glue ---
        float h = hp.process(post);
        h = loSh.process(h);
        h = midPk.process(h);
        h = hsh.process(h);
        h = ott.process(h) * 0.5f;

        // --- tempo-synced amplitude gate (the "wub") ---
        // Unipolar multiplier in [1-depth, 1], declicked by a ~3 ms one-pole. Only
        // reduces level (never louder). Free-running when v.syncHz == 0.
        float gmul = 1.0f;
        if (gateOn) {
            float g = gateLfo.tick();                       // bipolar [-1,1]
            float uni = 0.5f * (g + 1.0f);                  // [0,1]
            float target = (1.0f - gateDepth) + gateDepth * uni; // [1-depth, 1]
            gmul = gateSmooth.tick(target);
        }
        float mono = dc.tick(h) * amp.tick(gate) * v.velocity * gain * gmul;

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
    p["fmRatio"]      = rng.rangef(3.0f, 8.0f);
    p["fmIndex"]      = 3.0f + aggr * 6.0f + nov * 3.0f;
    p["fmDecay"]      = rng.rangef(0.08f, 0.3f);
    p["foldDrive"]    = 1.2f + aggr * 2.0f;
    p["hpFreq"]       = rng.rangef(320.0f, 480.0f);
    // formant scream (higher octave than growl)
    float v0, v1, v2; pickVowelPath(rng, v0, v1, v2);
    p["vowel0"] = v0; p["vowel1"] = v1; p["vowel2"] = v2;
    p["formantOct"]   = rng.rangef(1.6f, 2.3f);
    p["formantQ"]     = rng.rangef(5.0f, 10.0f);
    p["formantMix"]   = rng.rangef(0.5f, 0.8f);
    p["formantGain"]  = rng.rangef(2.0f, 3.0f);
    p["morphBase"]    = rng.rangef(0.05f, 0.35f);
    p["morphMod"]     = rng.rangef(0.45f, 0.75f);
    p["lfoRate"]      = rng.rangef(2.0f, 9.0f);
    p["lfoDepth"]     = rng.rangef(0.35f, 0.65f);
    p["lfoShape"]     = float(rng.intRange(0, 5));
    p["lfoSteps"]     = float(rng.intRange(2, 8));
    p["phaserRate"]   = rng.rangef(0.3f, 3.0f);
    p["phaserDepth"]  = rng.rangef(0.4f, 0.9f);
    p["phaserCenter"] = rng.rangef(1400.0f, 3000.0f);
    p["combFb"]       = rng.rangef(0.3f, 0.7f);
    p["width"]        = 0.45f;
    p["highShelfDb"]  = 2.0f - dark * 6.0f;
    p["ampAtk"]       = rng.rangef(0.003f, 0.02f);
    p["gain"]         = 0.42f;
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
    const int   vowel0 = int(rc.get("vowel0", 0.0f));
    const int   vowel1 = int(rc.get("vowel1", 2.0f));
    const int   vowel2 = int(rc.get("vowel2", 1.0f));
    const float formantOct = std::max(1.0f, rc.get("formantOct", 1.9f));
    const double formantQ = std::max(1.5, double(rc.get("formantQ", 8.0f)));
    const float formantMix = clampf(rc.get("formantMix", 0.65f), 0.0f, 1.0f);
    const float formantGain = rc.get("formantGain", 2.5f);
    const float morphBase = clampf(rc.get("morphBase", 0.2f), 0.0f, 1.0f);
    const float morphMod = clampf(rc.get("morphMod", 0.6f), 0.0f, 1.0f);
    const float lfoRate = rc.get("lfoRate", 4.0f);
    const float lfoDepth = clampf(rc.get("lfoDepth", 0.5f), 0.0f, 1.0f);
    const int   lfoShape = std::clamp(int(rc.get("lfoShape", 0.0f)), 0, 5);
    const int   lfoSteps = std::clamp(int(rc.get("lfoSteps", 4.0f)), 1, 16);
    const float phaserRate = rc.get("phaserRate", 1.5f);
    const float phaserDepth = clampf(rc.get("phaserDepth", 0.6f), 0.0f, 1.0f);
    const float phaserCenter = rc.get("phaserCenter", 2000.0f);
    const float combFb = clampf(rc.get("combFb", 0.5f), 0.0f, 0.85f);
    const float width = rc.get("width", 0.45f);
    const float highShelfDb = rc.get("highShelfDb", 0.0f);
    const float ampAtk = rc.get("ampAtk", 0.008f);
    const float gain = rc.get("gain", 0.42f);
    const float mod = clampf(v.mod, 0.0f, 1.0f);

    const int vpath[3] = {vowel0, vowel1, vowel2};

    double carPh = 0, modPh = 0;
    Rng nrng(v.seed);
    carPh = nrng.uniform(); modPh = nrng.uniform();
    FormantBank formant;
    Biquad hp; hp.setHighpass(hpFreq, 0.707, sr);
    Biquad hsh; hsh.setHighShelf(4000.0, highShelfDb, sr);
    Comb comb; comb.fb = combFb; comb.damp = 0.2f;
    comb.setMaxDelay(int(sr / std::max(60.0, freq)) + 8);
    Allpass1 ap1, ap2, ap3;
    LFO phaser; phaser.setRate(phaserRate, sr);
    ShapeLFO lfo; lfo.init(lfoRate, sr, lfoShape, lfoSteps, nrng.next());
    DCBlock dcL, dcR;
    EnvADSR amp; amp.start(ampAtk, 0.1f, 0.8f, 0.03f, sr);
    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    const double combFreq = freq * (2.0 + mod);
    int coefCtr = 0; float lval = 0.0f;

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

        // vowel scream: formants morphed by note.mod + rhythmic LFO, octave up.
        if ((coefCtr & 7) == 0) {
            lval = lfo.tick();
            float morph = clampf(morphBase + morphMod * mod + lfoDepth * 0.5f * lval, 0.0f, 1.0f);
            Vowel vw = vowelMorph(vpath, 3, morph, formantOct);
            formant.set(vw.f1, vw.f2, vw.f3, formantQ, sr);
        }
        ++coefCtr;
        float fp = formant.process(s) * formantGain;
        s = lerpf(s, fp, formantMix);

        // Phaser: 3 allpass stages swept by LFO.
        float lv = phaser.tick();
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
