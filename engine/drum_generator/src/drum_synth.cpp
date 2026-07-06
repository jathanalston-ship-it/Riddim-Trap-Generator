// Drum-role factories and renderers: Kick, Snare, HatClosed, HatOpen, Perc,
// Crash. Deterministic one-shots; drums ignore Voice.freqHz for pitch except
// where a tuned body applies. All variation from Recipe params + Voice seed.
//
// Design philosophy (see engine/drum_generator/TECHNIQUES.md): punch comes from
// LAYERING + envelope/transient shaping + EQ, never from drive. Saturation is
// gentle, output-compensated glue only, and the body is low-passed so nothing
// adds high-frequency "fizz". Clean sine/click/noise layers, transient-forward.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../../sound_design/src/dsp.h"
#include "../../sound_design/src/voices.h"
#include "rtg/drums/drum_profile.h"   // DrumProfile::active() — reference targets

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

// Output-compensated soft saturation used ONLY as gentle glue on drums.
// Unity slope for tiny drive; peak of a full-scale input stays ~1 (no makeup
// blowup), and it adds only mild low-order harmonics — no hard-clip fizz.
static inline float softGlue(float x, float drive) {
    if (drive <= 1.0001f) return x;
    return std::tanh(x * drive) * (1.0f / std::tanh(drive));
}

// ============================================================ KICK
Recipe makeKickRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Kick; r.seed = rng.next();
    r.name = dName("kick", rng);
    auto& p = r.p;
    // Ranges centered on the active drum reference profile (±20-30% seeded
    // variety); aggression still modulates within the band.
    const auto& dp = rtg::DrumProfile::active();
    const float body = dp.kickBodyHz;                 // ~55 Hz (refs 47-63)
    const float decay = dp.kickDecayMs * 0.001f;      // ~0.09 s, biased short
    p["startHz"]   = rng.rangef(body * 2.1f, body * 2.9f);          // glide start
    p["endHz"]     = rng.rangef(body * 0.86f, body * 1.14f);        // settles ~body
    p["pitchMs"]   = rng.rangef(0.020f, 0.045f);
    // Decay from the profile, biased short; trap a touch boomier.
    p["bodyDecay"] = rng.rangef(decay * 0.78f, decay * 1.22f) + (1.0f - aggr) * 0.035f;
    // "Punch": low harmonics (3f-5f, ~140-260 Hz) on a medium env give clean
    // low-mid body that pulls the sound out of pure-sub territory (no fizz).
    p["punchAmt"]  = rng.rangef(0.85f, 1.2f) + aggr * 0.2f;
    p["punchMs"]   = rng.rangef(0.07f, 0.12f);
    // Mid "knock" tone (chest thump) — a sustained low-mid layer.
    p["knockHz"]   = rng.rangef(155.0f, 235.0f);
    p["knockAmt"]  = rng.rangef(0.85f, 1.2f);
    p["knockMs"]   = rng.rangef(0.08f, 0.14f);
    // Beater click/knock: band-limited 2-6 kHz noise burst. The references have
    // an audible click (2-8 kHz share ~4-7%); this restores it. Level is
    // calibrated (kClickCal) so the rendered kick's 2-8 kHz energy share lands
    // near the profile's kickClickShare. Band-limited to <=6 kHz — no >8k fizz.
    const float kClickCal = 5.0f;   // maps clickShare target -> layer amplitude
    p["clickAmt"]  = kClickCal * dp.kickClickShare * (0.85f + 0.45f * aggr);
    p["clickHz"]   = rng.rangef(3000.0f, 4200.0f);
    p["clickMs"]   = rng.rangef(0.004f, 0.008f);    // longer than a tick -> real 2-6k energy
    p["bodyLP"]    = rng.rangef(2800.0f, 3800.0f);  // guarantees no top-end fizz
    p["drive"]     = 1.0f + aggr * 0.6f;            // <=1.3 @0.5, <=1.6 @1.0
    p["gain"]      = 0.85f;
    return r;
}

StereoBuffer renderKick(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float startHz = rc.get("startHz", 140.0f);
    const float endHz = rc.get("endHz", 48.0f);
    const float pitchMs = rc.get("pitchMs", 0.03f);
    const float bodyDecay = rc.get("bodyDecay", 0.24f);
    const float punchAmt = rc.get("punchAmt", 0.5f);
    const float punchMs = rc.get("punchMs", 0.04f);
    const float knockHz = rc.get("knockHz", 190.0f);
    const float knockAmt = rc.get("knockAmt", 0.4f);
    const float knockMs = rc.get("knockMs", 0.025f);
    const float clickAmt = rc.get("clickAmt", 0.55f);
    const float clickHz = rc.get("clickHz", 2600.0f);
    const float bodyLP = rc.get("bodyLP", 3200.0f);
    const float drive = rc.get("drive", 1.3f);
    const float gain = rc.get("gain", 0.85f);

    double ph = 0, kph = 0;
    EnvAD body;  body.start(0.0006f, bodyDecay, sr);
    EnvAD punch; punch.start(0.0004f, punchMs, sr);
    EnvAD knock; knock.start(0.0006f, knockMs, sr);
    EnvAD click; click.start(0.0002f, 0.0028f, sr);
    Biquad bodyLp; bodyLp.setLowpass(bodyLP, 0.707, sr);
    Biquad clickBp; clickBp.setBandpass(clickHz, 1.1, sr);
    Biquad clickLp; clickLp.setLowpass(6500.0, 0.707, sr); // keep click off the top
    WhiteNoise noise(v.seed ^ 0x9911u);
    DCBlock dc;
    const double pdrop = std::max(0.004f, pitchMs);
    for (size_t n = 0; n < N; ++n) {
        double t = double(n) / sr;
        double f = endHz + (startHz - endHz) * std::exp(-t / pdrop);
        // Fundamental sine + short-lived low harmonics (2f-5f, all <= ~250 Hz)
        // that vanish quickly for a clean sub tail.
        double fund = std::sin(kTwoPi * ph);
        // Use 3f-5f only: 2f of a ~48 Hz fundamental is still sub. 3f-5f land in
        // the 140-260 Hz low band, adding clean body without raising sub energy.
        double h3 = std::sin(kTwoPi * 3.0 * ph);
        double h4 = std::sin(kTwoPi * 4.0 * ph);
        double h5 = std::sin(kTwoPi * 5.0 * ph);
        ph += f / sr; if (ph >= 1.0) ph -= 1.0;
        float pe = punch.tick();
        double harm = (0.6 * h3 + 0.4 * h4 + 0.25 * h5) * pe * punchAmt;
        // Mid "knock" resonance for chest thump (clean sine, short).
        double kn = std::sin(kTwoPi * kph) * knock.tick() * knockAmt;
        kph += knockHz / sr; if (kph >= 1.0) kph -= 1.0;

        float be = body.tick();
        float bodyOut = bodyLp.process(float(fund) * be + float(harm) + float(kn));
        bodyOut = softGlue(bodyOut, drive);
        // Beater click — band-limited noise burst.
        float clk = clickLp.process(clickBp.process(noise.tick())) * click.tick() * clickAmt;
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
    p["bodyHz"]    = rng.rangef(175.0f, 235.0f);
    p["bodyDecay"] = rng.rangef(0.06f, 0.11f);
    p["bodyMix"]   = rng.rangef(0.30f, 0.5f);
    // Crack: hot band-pass noise 2-5 kHz, very short.
    p["crackHz"]   = rng.rangef(2600.0f, 4200.0f) - dark * 1000.0f;
    p["crackDecay"] = rng.rangef(0.006f, 0.014f);
    p["crackAmt"]  = rng.rangef(0.9f, 1.3f);
    // Tail: HP'd noise with a falling LP.
    p["tailDecay"] = rng.rangef(0.14f, 0.30f);
    p["tailHp"]    = rng.rangef(1400.0f, 2200.0f);
    p["tailLp0"]   = rng.rangef(7000.0f, 9000.0f) - dark * 2000.0f; // start bright
    p["tailAmt"]   = rng.rangef(0.35f, 0.55f);
    // Riddim metal: only when very aggressive, and subtle.
    p["metal"]     = aggr > 0.75f ? rng.rangef(0.12f, 0.28f) : 0.0f;
    p["gain"]      = 0.7f;
    return r;
}

StereoBuffer renderSnare(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float bodyHz = rc.get("bodyHz", 205.0f);
    const float bodyDecay = rc.get("bodyDecay", 0.085f);
    const float bodyMix = rc.get("bodyMix", 0.4f);
    const float crackHz = rc.get("crackHz", 3400.0f);
    const float crackDecay = rc.get("crackDecay", 0.01f);
    const float crackAmt = rc.get("crackAmt", 1.1f);
    const float tailDecay = rc.get("tailDecay", 0.22f);
    const float tailHp = rc.get("tailHp", 1800.0f);
    const float tailLp0 = rc.get("tailLp0", 8000.0f);
    const float tailAmt = rc.get("tailAmt", 0.45f);
    const float metal = rc.get("metal", 0.0f);
    const float gain = rc.get("gain", 0.7f);
    const float vel = clampf(v.velocity, 0.05f, 1.0f);

    double ph1 = 0, ph2 = 0;
    // Velocity rides crack level (harder = snappier) and tail length.
    const float crackLevel = crackAmt * (0.55f + 0.45f * vel);
    const float tDecay = tailDecay * (0.6f + 0.4f * vel);
    EnvAD body;  body.start(0.0009f, bodyDecay, sr);
    EnvAD crack; crack.start(0.0003f, crackDecay, sr);
    EnvAD tail;  tail.start(0.0015f, tDecay, sr);
    Biquad crackBpL, crackBpR;
    crackBpL.setBandpass(crackHz, 1.4, sr); crackBpR.setBandpass(crackHz, 1.4, sr);
    Biquad tailHpL, tailHpR;
    tailHpL.setHighpass(tailHp, 0.707, sr); tailHpR.setHighpass(tailHp, 0.707, sr);
    // Falling LP on the tail (SVF, cutoff modulated by the tail envelope).
    SVF tailLpL, tailLpR;
    const double tailLpEnd = 2600.0;
    WhiteNoise nCrackL(v.seed ^ 0x5533u), nCrackR(v.seed ^ 0xA1B2u);
    WhiteNoise nTailL(v.seed ^ 0x7788u), nTailR(v.seed ^ 0xC4D5u);
    Comb metalComb; metalComb.fb = 0.6f; metalComb.damp = 0.25f;
    metalComb.setMaxDelay(int(sr / 300.0) + 8);
    DCBlock dcL, dcR;
    int cc = 0;
    float lpCut = tailLp0;
    for (size_t n = 0; n < N; ++n) {
        // --- tuned body (thump): fundamental + detuned partner.
        double b = std::sin(kTwoPi * ph1) * 0.7 + std::sin(kTwoPi * ph2) * 0.3;
        ph1 += bodyHz / sr; if (ph1 >= 1.0) ph1 -= 1.0;
        ph2 += bodyHz * 1.5 / sr; if (ph2 >= 1.0) ph2 -= 1.0;
        float bodyOut = float(b) * body.tick() * bodyMix;

        // --- crack transient (hot, short band-pass noise).
        float ce = crack.tick() * crackLevel;
        float crL = crackBpL.process(nCrackL.tick()) * ce;
        float crR = crackBpR.process(nCrackR.tick()) * ce;

        // --- tail (HP'd noise, falling LP).
        float te = tail.tick();
        if ((cc++ & 7) == 0) lpCut = float(tailLpEnd + (tailLp0 - tailLpEnd) * te);
        tailLpL.set(lpCut, 0.707, sr); tailLpR.set(lpCut, 0.707, sr);
        tailLpL.process(tailHpL.process(nTailL.tick())); float tL = float(tailLpL.lp) * te * tailAmt;
        tailLpR.process(tailHpR.process(nTailR.tick())); float tR = float(tailLpR.lp) * te * tailAmt;

        float l = bodyOut + crL + tL;
        float r = bodyOut + crR + tR;
        if (metal > 0.0f) {
            float m = metalComb.process(crL, float(sr / 340.0)) * te * metal * 0.35f;
            l += m; r += m;
        }
        out.l[n] = dcL.tick(l) * vel * gain;
        out.r[n] = dcR.tick(r) * vel * gain;
    }
    return out;
}

// ------------------------------------------------------------ metallic hat core
static StereoBuffer renderHat(const Recipe& rc, const Voice& v, float decay) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float hpFreq = rc.get("hpFreq", 7500.0f);
    const float ring = rc.get("ring", 0.5f);
    const int partials = std::clamp(int(rc.get("partials", 6.0f)), 5, 7);
    const float gain = rc.get("gain", 0.5f);
    const float mod = clampf(v.mod, 0.0f, 1.0f);
    // note.mod tilts BOTH brightness (HP cutoff) and decay length.
    const float hp = hpFreq * (0.75f + 0.5f * mod);
    const float dec = decay * (0.7f + 0.6f * mod);

    Rng prng(v.seed ^ 0x7c7cu);
    double php[7], pf[7];
    // Inharmonic metallic partials spread across ~3-11 kHz (808-style smear).
    for (int i = 0; i < partials; ++i) {
        php[i] = prng.uniform();
        pf[i] = (3200.0 + i * 1250.0) * (0.85 + prng.uniform() * 0.35);
    }

    Biquad hpfL, hpfR;
    hpfL.setHighpass(hp, 0.707, sr);
    hpfR.setHighpass(hp, 0.707, sr);
    WhiteNoise nzL(v.seed ^ 0x11u), nzR(v.seed ^ 0x22u);
    EnvAD env; env.start(0.0004f, dec, sr);
    DCBlock dcL, dcR;
    for (size_t n = 0; n < N; ++n) {
        double metalPart = 0.0;
        for (int i = 0; i < partials; ++i) {
            metalPart += (php[i] < 0.5 ? 1.0 : -1.0); // detuned square partials
            php[i] += pf[i] / sr; if (php[i] >= 1.0) php[i] -= 1.0;
        }
        metalPart /= partials;
        float e = env.tick();
        // Ring-modulate the metal smear with noise for shimmer + grit.
        float nL = nzL.tick(), nR = nzR.tick();
        float mL = float(metalPart) * (1.0f - ring) + float(metalPart) * nL * ring;
        float mR = float(metalPart) * (1.0f - ring) + float(metalPart) * nR * ring;
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
    p["hpFreq"] = rng.rangef(6500.0f, 9000.0f) - dark * 1500.0f;
    p["ring"]   = rng.rangef(0.35f, 0.6f);
    p["partials"] = float(rng.intRange(5, 7));
    p["decay"]  = rng.rangef(0.028f, 0.06f);
    p["gain"]   = 0.5f;
    return r;
}
StereoBuffer renderHatClosed(const Recipe& rc, const Voice& v) {
    return renderHat(rc, v, rc.get("decay", 0.045f));
}

Recipe makeHatOpenRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::HatOpen; r.seed = rng.next();
    r.name = dName("hato", rng);
    auto& p = r.p;
    p["hpFreq"] = rng.rangef(6500.0f, 9000.0f) - dark * 1500.0f;
    p["ring"]   = rng.rangef(0.4f, 0.65f);
    p["partials"] = float(rng.intRange(5, 7));
    p["decay"]  = rng.rangef(0.18f, 0.4f);
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
    p["noiseMix"] = rng.rangef(0.15f, 0.4f);
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
    const float noiseMix = rc.get("noiseMix", 0.3f);
    const float gain = rc.get("gain", 0.55f);
    double ph = 0;
    EnvAD env; env.start(0.0008f, decay, sr);
    EnvAD noiseEnv; noiseEnv.start(0.0004f, decay * 0.4f, sr); // noise = transient only
    Biquad bp; bp.setBandpass(toneHz, bpQ, sr);
    WhiteNoise noise(v.seed ^ 0x3131u);
    DCBlock dc;
    for (size_t n = 0; n < N; ++n) {
        double tri = ph < 0.5 ? (4.0 * ph - 1.0) : (3.0 - 4.0 * ph);
        ph += toneHz / sr; if (ph >= 1.0) ph -= 1.0;
        float tone = float(tri) * env.tick() * (1.0f - noiseMix);
        float ne = noise.tick() * noiseEnv.tick() * noiseMix;
        float mono = dc.tick(bp.process(tone + ne)) * v.velocity * gain;
        out.l[n] = mono; out.r[n] = mono;
    }
    return out;
}

// ============================================================ CRASH
Recipe makeCrashRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Crash; r.seed = rng.next();
    r.name = dName("crash", rng);
    auto& p = r.p;
    p["hpFreq"] = rng.rangef(5000.0f, 6500.0f) - dark * 1200.0f;
    p["decay"]  = rng.rangef(1.3f, 2.4f);
    p["shimmer"] = rng.rangef(0.3f, 0.55f); // comb-resonated metal partials
    p["width"]  = 0.9f;
    p["gain"]   = 0.5f;
    return r;
}
StereoBuffer renderCrash(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float hpFreq = rc.get("hpFreq", 5500.0f);
    const float decay = rc.get("decay", 1.9f);
    const float shimmer = rc.get("shimmer", 0.4f);
    const float width = rc.get("width", 0.9f);
    const float gain = rc.get("gain", 0.5f);
    Biquad hpL, hpR, shL, shR;
    hpL.setHighpass(hpFreq, 0.707, sr); hpR.setHighpass(hpFreq, 0.707, sr);
    shL.setHighShelf(10000.0, 3.0, sr); shR.setHighShelf(10000.0, 3.0, sr);
    // Comb pair adds inharmonic metallic shimmer instead of pure white wash.
    Comb combL, combR;
    combL.fb = 0.55f; combL.damp = 0.15f; combL.setMaxDelay(int(sr / 900.0) + 8);
    combR.fb = 0.55f; combR.damp = 0.15f; combR.setMaxDelay(int(sr / 760.0) + 8);
    WhiteNoise nzL(v.seed ^ 0xC1u), nzR(v.seed ^ 0xC2u);
    EnvAD env; env.start(0.002f, decay, sr); // fast attack, long exp decay
    DCBlock dcL, dcR;
    for (size_t n = 0; n < N; ++n) {
        float e = env.tick();
        float rawL = nzL.tick(), rawR = nzR.tick();
        float shimL = combL.process(rawL, float(sr / 900.0));
        float shimR = combR.process(rawR, float(sr / 760.0));
        float sL = rawL * (1.0f - shimmer) + shimL * shimmer;
        float sR = rawR * (1.0f - shimmer) + shimR * shimmer;
        float l = dcL.tick(shL.process(hpL.process(sL))) * e * v.velocity * gain;
        float r = dcR.tick(shR.process(hpR.process(sR))) * e * v.velocity * gain;
        widen(l, r, 1.0f + width);
        out.l[n] = l; out.r[n] = r;
    }
    return out;
}

} // namespace rtg::synth
