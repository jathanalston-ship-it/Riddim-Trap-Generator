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
    p["startHz"]   = rng.rangef(body * 2.1f, body * 2.9f);          // glide start (legacy)
    p["endHz"]     = rng.rangef(body * 0.86f, body * 1.14f);        // root: settles ~body
    p["pitchMs"]   = rng.rangef(0.020f, 0.045f);                    // legacy glide tau
    // SHARP EDM kick: a SMALL, fast pitch drop for a tight punch (a big drop
    // rings/booms like a gong — the sub bass now owns the deep low, so the kick
    // is a crisp transient, not a resonating tail).
    p["pitchDropSemis"] = rng.rangef(5.0f, 9.0f) + aggr * 3.0f;     // ~5-12 st (was 12-23)
    p["pitchDropMs"]    = rng.rangef(0.012f, 0.025f);              // faster -> snappier
    // Tight/short body decay so the kick doesn't ring under the chug.
    p["bodyDecay"] = rng.rangef(decay * 0.42f, decay * 0.68f) + (1.0f - aggr) * 0.015f;
    // "Punch": low harmonics (3f-5f) on a SHORT env — click-forward body, no
    // long low-mid resonance.
    p["punchAmt"]  = rng.rangef(0.7f, 1.0f) + aggr * 0.2f;
    p["punchMs"]   = rng.rangef(0.04f, 0.07f);
    // Mid "knock" (chest thump) — kept SHORT so it snaps instead of sustaining.
    p["knockHz"]   = rng.rangef(160.0f, 240.0f);
    p["knockAmt"]  = rng.rangef(0.5f, 0.8f);
    p["knockMs"]   = rng.rangef(0.04f, 0.075f);
    // Beater click/knock: band-limited 2-6 kHz noise burst. The references have
    // an audible click (2-8 kHz share ~4-7%); this restores it. Level is
    // calibrated (kClickCal) so the rendered kick's 2-8 kHz energy share lands
    // near the profile's kickClickShare. Band-limited to <=6 kHz — no >8k fizz.
    const float kClickCal = 82.0f;   // maps clickShare target -> layer amplitude
    p["clickAmt"]  = kClickCal * dp.kickClickShare * (0.85f + 0.45f * aggr);
    p["clickHz"]   = rng.rangef(3000.0f, 4200.0f);
    p["clickMs"]   = rng.rangef(0.004f, 0.008f);    // longer than a tick -> real 2-6k energy
    // Attack CLICK/SNAP burst: HP'd noise, ultra-short — pure attack definition.
    p["clickAmount"] = rng.rangef(0.55f, 0.85f) + aggr * 0.35f;
    p["bodyLP"]    = rng.rangef(2800.0f, 3800.0f);  // guarantees no top-end fizz
    // PARALLEL distortion grit: a copy HP'd at ~220 Hz, saturated, mixed back —
    // grit WITHOUT muddying the low end. Hotter when aggressive.
    p["parallelDriveMix"] = rng.rangef(0.14f, 0.26f) + aggr * 0.18f;  // 0..~0.44
    p["drive"]     = 1.0f + aggr * 0.6f;            // <=1.3 @0.5, <=1.6 @1.0
    p["gain"]      = 0.82f;
    return r;
}

StereoBuffer renderKick(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float endHz = rc.get("endHz", 48.0f);        // root the pitch env lands on
    const float pitchMs = rc.get("pitchMs", 0.03f);    // legacy fallback for tau
    const float pitchDropSemis = rc.get("pitchDropSemis", 14.0f);
    const float pitchDropMs = rc.get("pitchDropMs", pitchMs);
    const float bodyDecay = rc.get("bodyDecay", 0.24f);
    const float punchAmt = rc.get("punchAmt", 0.5f);
    const float punchMs = rc.get("punchMs", 0.04f);
    const float knockHz = rc.get("knockHz", 190.0f);
    const float knockAmt = rc.get("knockAmt", 0.4f);
    const float knockMs = rc.get("knockMs", 0.025f);
    const float clickAmt = rc.get("clickAmt", 0.55f);
    const float clickAmount = rc.get("clickAmount", 0.7f);
    const float clickHz = rc.get("clickHz", 3500.0f);
    const float clickMs = rc.get("clickMs", 0.006f);
    const float bodyLP = rc.get("bodyLP", 3200.0f);
    const float parallelDriveMix = clampf(rc.get("parallelDriveMix", 0.22f), 0.0f, 0.8f);
    const float drive = rc.get("drive", 1.3f);
    const float gain = rc.get("gain", 0.82f);

    // TOM/TIMPANI mode: a Kick note written at a real pitch (>80 Hz, i.e. a
    // tuned drum, not the low kick root) becomes a tuned tom — it lands ON the
    // note pitch with only a small pitch drop. This gives the tribal/bongo
    // percussion for jungle intros without a separate lane. Low kick notes
    // (rootSub, <80 Hz) are unaffected.
    // Tom body is TIGHT/DRY, not a long ringing boom: perceptual A/B (tools/
    // earview/toms.py) showed the reference's tribal toms decay ~14 ms (tom-band
    // 1/e) — short, staccato, punchy — while our old floor of 0.34 s rang ~82 ms
    // and smeared/boomed. So the tom body is a short, punchy decay (~a bit longer
    // than the kick body for pitch, but nowhere near a ring).
    const bool tomMode = (double(v.freqHz) > 80.0);
    const float effDropSemis = tomMode ? std::min(pitchDropSemis, 3.0f) : pitchDropSemis;
    const float effBodyDecay = tomMode ? std::clamp(bodyDecay * 1.3f + 0.012f, 0.035f, 0.07f)
                                       : bodyDecay;
    const float effClickAmt  = tomMode ? clickAmt * 0.4f : clickAmt;      // less beater click
    const float effClickAmount = tomMode ? clickAmount * 0.4f : clickAmount;

    // Pitch envelope: instantaneous freq = root * 2^((dropSemis * exp(-t/tau))/12).
    // Starts +dropSemis above root and falls exponentially to root — the fast
    // downward "laser" transient that gives the kick its punch.
    const double root = tomMode ? double(v.freqHz) : std::max(20.0f, endHz);
    const double tau = std::max(0.004f, pitchDropMs);

    double ph = 0, kph = 0;
    EnvAD body;  body.start(0.0006f, effBodyDecay, sr);
    EnvAD punch; punch.start(0.0004f, punchMs, sr);
    EnvAD knock; knock.start(0.0006f, knockMs, sr);
    EnvAD click; click.start(0.0002f, clickMs, sr);
    EnvAD snap;  snap.start(0.00005f, 0.0035f, sr);          // ultra-fast attack burst
    Biquad bodyLp; bodyLp.setLowpass(bodyLP, 0.707, sr);
    Biquad clickBp; clickBp.setBandpass(clickHz, 0.9, sr);   // broad 2-6k knock
    Biquad clickHp; clickHp.setHighpass(1800.0, 0.707, sr);  // keep click out of low band
    Biquad clickLp; clickLp.setLowpass(6000.0, 0.707, sr);   // hard cap: no >6k fizz
    Biquad snapHp; snapHp.setHighpass(4000.0, 0.707, sr);    // top-end snap definition
    Biquad distHp; distHp.setHighpass(220.0, 0.707, sr);     // parallel grit stays off lows
    // Output EQ: roll off subsonics <50 Hz, bump ~100 Hz weight and ~5 kHz click.
    Biquad eqHp; eqHp.setHighpass(48.0, 0.707, sr);
    Biquad eqWeight; eqWeight.setPeak(100.0, 0.9, 3.0, sr);
    Biquad eqClick; eqClick.setPeak(5000.0, 0.8, 3.5, sr);
    WhiteNoise noise(v.seed ^ 0x9911u);
    WhiteNoise snapN(v.seed ^ 0x4477u);
    DCBlock dc;
    const float pDrive = 3.0f + 3.0f * parallelDriveMix;    // grit intensity
    for (size_t n = 0; n < N; ++n) {
        double t = double(n) / sr;
        double semi = double(effDropSemis) * std::exp(-t / tau);
        double f = root * std::pow(2.0, semi / 12.0);
        // Fundamental sine + short-lived low harmonics (3f-5f, all <= ~250 Hz)
        // that vanish quickly for a clean sub tail.
        double fund = std::sin(kTwoPi * ph);
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
        float bodyRaw = float(fund) * be + float(harm) + float(kn);
        float bodyOut = softGlue(bodyLp.process(bodyRaw), drive);
        // Parallel grit path: HP a copy at 220 Hz, saturate, mix back. The HP
        // keeps the distortion off the sub so the low end stays clean/tight.
        float grit = shTanh(distHp.process(bodyRaw), pDrive) * parallelDriveMix;
        // Beater click/knock — 2-6 kHz band-limited noise burst.
        float clk = clickLp.process(clickHp.process(clickBp.process(noise.tick())))
                    * click.tick() * effClickAmt;
        // Attack snap — HP'd noise, ultra-short, for transient definition.
        float snp = snapHp.process(snapN.tick()) * snap.tick() * effClickAmount;
        float mix = bodyOut + grit + clk + snp;
        mix = eqClick.process(eqWeight.process(eqHp.process(mix)));
        float mono = dc.tick(mix) * v.velocity * gain;
        if (!std::isfinite(mono)) mono = 0.0f;
        out.l[n] = mono; out.r[n] = mono;
    }
    return out;
}

// ============================================================ SNARE
Recipe makeSnareRecipe(float aggr, float dark, float nov, Rng& rng) {
    Recipe r; r.role = Role::Snare; r.seed = rng.next();
    r.name = dName("snare", rng);
    auto& p = r.p;
    const auto& dp = rtg::DrumProfile::active();
    const float body = dp.snareBodyHz;                  // ~145 Hz (refs 140-150)
    // Body fundamental centered on the profile (legacy key).
    p["bodyHz"]    = rng.rangef(body * 0.95f, body * 1.07f);
    // Tuned chest-slam BODY: 180-215 Hz sine/triangle burst for tearout punch.
    p["bodyFreq"]  = rng.rangef(180.0f, 215.0f);
    p["bodyDecay"] = rng.rangef(0.07f, 0.13f);
    // More TONAL: the pitched body dominates (raised mix; noise tail cut below).
    p["bodyMix"]   = rng.rangef(0.55f, 0.80f);
    // Crack: hot band-pass noise 2-5 kHz. Decay from the profile (kept snappy).
    const float crackDec = dp.snareCrackDecayMs * 0.001f;  // ~0.11 s
    p["crackHz"]   = rng.rangef(2600.0f, 4200.0f) - dark * 1000.0f;
    p["crackDecay"] = clampf(rng.rangef(crackDec * 0.35f, crackDec * 0.65f), 0.02f, 0.09f);
    p["crackAmt"]  = rng.rangef(0.9f, 1.3f);                    // legacy key
    p["crackAmount"] = rng.rangef(1.0f, 1.4f) + aggr * 0.3f;    // hot 2-5k crack
    // Transient boost: ultra-short HP'd noise snap for a cracky onset.
    p["transientBoost"] = rng.rangef(0.4f, 0.8f) + aggr * 0.4f;
    // OTT-lite glue (expansive/chest-slam) + short plate tail (<200 ms).
    p["ott"]       = rng.rangef(0.28f, 0.45f);
    p["roomAmt"]   = rng.rangef(0.09f, 0.16f);
    p["roomMs"]    = rng.rangef(0.06f, 0.11f);
    // Tail: HP'd noise with a falling LP — reduced ~6 dB for a tonal, low-noise
    // snare (was 0.35-0.55; halved so the pitched body/crack dominate).
    p["tailDecay"] = rng.rangef(0.14f, 0.30f);
    p["tailHp"]    = rng.rangef(1400.0f, 2200.0f);
    p["tailLp0"]   = rng.rangef(7000.0f, 9000.0f) - dark * 2000.0f; // start bright
    p["tailAmt"]   = rng.rangef(0.16f, 0.28f);
    // Riddim metal: only when very aggressive, and subtle.
    p["metal"]     = aggr > 0.75f ? rng.rangef(0.12f, 0.28f) : 0.0f;
    p["gain"]      = 0.7f;
    return r;
}

StereoBuffer renderSnare(const Recipe& rc, const Voice& v) {
    const size_t N = drumLen(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const float bodyFreq = rc.get("bodyFreq", rc.get("bodyHz", 200.0f));  // tuned body
    const float bodyDecay = rc.get("bodyDecay", 0.085f);
    const float bodyMix = rc.get("bodyMix", 0.4f);
    const float crackHz = rc.get("crackHz", 3400.0f);
    const float crackDecay = rc.get("crackDecay", 0.01f);
    const float crackAmount = rc.get("crackAmount", rc.get("crackAmt", 1.1f));
    const float transientBoost = rc.get("transientBoost", 0.6f);
    const float tailDecay = rc.get("tailDecay", 0.22f);
    const float tailHp = rc.get("tailHp", 1800.0f);
    const float tailLp0 = rc.get("tailLp0", 8000.0f);
    const float tailAmt = rc.get("tailAmt", 0.45f);
    const float ottAmt = clampf(rc.get("ott", 0.35f), 0.0f, 1.0f);
    const float roomAmt = clampf(rc.get("roomAmt", 0.13f), 0.0f, 0.6f);
    const float roomMs = clampf(rc.get("roomMs", 0.09f), 0.03f, 0.19f);
    const float metal = rc.get("metal", 0.0f);
    const float gain = rc.get("gain", 0.7f);
    const float vel = clampf(v.velocity, 0.05f, 1.0f);

    double ph1 = 0, ph2 = 0;
    // Velocity rides crack level (harder = snappier) and tail length.
    const float crackLevel = crackAmount * (0.55f + 0.45f * vel);
    const float tDecay = tailDecay * (0.6f + 0.4f * vel);
    EnvAD body;  body.start(0.0009f, bodyDecay, sr);
    EnvAD crack; crack.start(0.0003f, crackDecay, sr);
    EnvAD tail;  tail.start(0.0015f, tDecay, sr);
    EnvAD snap;  snap.start(0.00005f, 0.004f, sr);          // transient-boost snap
    EnvAD roomEnv; roomEnv.start(0.002f, roomMs, sr);       // hard-caps tail <200 ms
    Biquad crackBpL, crackBpR;
    crackBpL.setBandpass(crackHz, 1.4, sr); crackBpR.setBandpass(crackHz, 1.4, sr);
    Biquad tailHpL, tailHpR;
    tailHpL.setHighpass(tailHp, 0.707, sr); tailHpR.setHighpass(tailHp, 0.707, sr);
    Biquad snapHpL, snapHpR;
    snapHpL.setHighpass(3500.0, 0.707, sr); snapHpR.setHighpass(3500.0, 0.707, sr);
    // Falling LP on the tail (SVF, cutoff modulated by the tail envelope).
    SVF tailLpL, tailLpR;
    const double tailLpEnd = 2600.0;
    WhiteNoise nCrackL(v.seed ^ 0x5533u), nCrackR(v.seed ^ 0xA1B2u);
    WhiteNoise nTailL(v.seed ^ 0x7788u), nTailR(v.seed ^ 0xC4D5u);
    WhiteNoise nSnapL(v.seed ^ 0x2A2Au), nSnapR(v.seed ^ 0x9E9Eu);
    Comb metalComb; metalComb.fb = 0.6f; metalComb.damp = 0.25f;
    metalComb.setMaxDelay(int(sr / 300.0) + 8);
    // Short plate/room: two low-feedback combs + allpass diffusion per channel,
    // gated by roomEnv so the tail is a snappy slam, never a wash.
    Comb roomL1, roomL2, roomR1, roomR2;
    Comb* rooms[4] = {&roomL1, &roomL2, &roomR1, &roomR2};
    for (int i = 0; i < 4; ++i) {
        rooms[i]->fb = 0.45f; rooms[i]->damp = 0.4f;
        rooms[i]->setMaxDelay(int(sr * 0.030) + 8);
    }
    Allpass1 apL, apR; apL.setCoef(2200.0, sr); apR.setCoef(1900.0, sr);
    const float dL1 = float(sr * 0.0197), dL2 = float(sr * 0.0263);
    const float dR1 = float(sr * 0.0223), dR2 = float(sr * 0.0291);
    // OTT-lite glue per channel (chest-slam expansion).
    OTTLite ottL, ottR; ottL.set(sr, ottAmt); ottR.set(sr, ottAmt);
    DCBlock dcL, dcR;
    int cc = 0;
    float lpCut = tailLp0;
    for (size_t n = 0; n < N; ++n) {
        // --- tuned body (chest slam): fundamental + detuned partner.
        double b = std::sin(kTwoPi * ph1) * 0.7 + std::sin(kTwoPi * ph2) * 0.3;
        ph1 += bodyFreq / sr; if (ph1 >= 1.0) ph1 -= 1.0;
        ph2 += bodyFreq * 1.5 / sr; if (ph2 >= 1.0) ph2 -= 1.0;
        float bodyOut = float(b) * body.tick() * bodyMix;

        // --- crack transient (hot, short band-pass noise 2-5 kHz).
        float ce = crack.tick() * crackLevel;
        float crL = crackBpL.process(nCrackL.tick()) * ce;
        float crR = crackBpR.process(nCrackR.tick()) * ce;

        // --- transient boost: ultra-short HP'd noise snap for a cracky onset.
        float se = snap.tick() * transientBoost;
        float snL = snapHpL.process(nSnapL.tick()) * se;
        float snR = snapHpR.process(nSnapR.tick()) * se;

        // --- tail (HP'd noise, falling LP).
        float te = tail.tick();
        if ((cc++ & 7) == 0) lpCut = float(tailLpEnd + (tailLp0 - tailLpEnd) * te);
        tailLpL.set(lpCut, 0.707, sr); tailLpR.set(lpCut, 0.707, sr);
        tailLpL.process(tailHpL.process(nTailL.tick())); float tL = float(tailLpL.lp) * te * tailAmt;
        tailLpR.process(tailHpR.process(nTailR.tick())); float tR = float(tailLpR.lp) * te * tailAmt;

        float dryL = bodyOut + crL + snL + tL;
        float dryR = bodyOut + crR + snR + tR;
        if (metal > 0.0f) {
            float m = metalComb.process(crL, float(sr / 340.0)) * te * metal * 0.35f;
            dryL += m; dryR += m;
        }
        // Short plate tail fed by the transient content, gated hard by roomEnv.
        float re = roomEnv.tick();
        float roomInL = crL + bodyOut, roomInR = crR + bodyOut;
        float wL = apL.process(0.5f * (roomL1.process(roomInL, dL1) + roomL2.process(roomInL, dL2)));
        float wR = apR.process(0.5f * (roomR1.process(roomInR, dR1) + roomR2.process(roomInR, dR2)));
        // OTT-lite glue on the dry slam, then add the short room.
        float l = ottL.process(dryL) + wL * re * roomAmt;
        float r = ottR.process(dryR) + wR * re * roomAmt;
        l = dcL.tick(l) * vel * gain;
        r = dcR.tick(r) * vel * gain;
        if (!std::isfinite(l)) l = 0.0f;
        if (!std::isfinite(r)) r = 0.0f;
        out.l[n] = l; out.r[n] = r;
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
    // Partial base scales with the profile-driven brightness so the hat-band
    // spectral centroid tracks the reference (~8.6-9 kHz).
    const float partBase = rc.get("partBase", 3800.0f);
    const float mod = clampf(v.mod, 0.0f, 1.0f);
    // note.mod tilts BOTH brightness (HP cutoff) and decay length.
    const float hp = hpFreq * (0.75f + 0.5f * mod);
    const float dec = decay * (0.7f + 0.6f * mod);

    Rng prng(v.seed ^ 0x7c7cu);
    double php[7], pf[7];
    // Inharmonic metallic partials spread upward from partBase (808-style smear).
    for (int i = 0; i < partials; ++i) {
        php[i] = prng.uniform();
        pf[i] = (partBase + i * 1450.0) * (0.85 + prng.uniform() * 0.35);
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
    const auto& dp = rtg::DrumProfile::active();
    const float dec = dp.hatDecayMs * 0.001f;                 // ~0.078 s
    p["hpFreq"] = rng.rangef(6500.0f, 9000.0f) - dark * 1500.0f;
    p["partBase"] = dp.hatCentroidHz * rng.rangef(0.44f, 0.52f);  // centroid -> partials
    p["ring"]   = rng.rangef(0.35f, 0.6f);
    p["partials"] = float(rng.intRange(5, 7));
    // Closed hat: shorter than the all-hat median decay.
    p["decay"]  = rng.rangef(dec * 0.55f, dec * 0.95f);
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
    const auto& dp = rtg::DrumProfile::active();
    p["hpFreq"] = rng.rangef(6500.0f, 9000.0f) - dark * 1500.0f;
    p["partBase"] = dp.hatCentroidHz * rng.rangef(0.44f, 0.52f);
    p["ring"]   = rng.rangef(0.4f, 0.65f);
    p["partials"] = float(rng.intRange(5, 7));
    p["decay"]  = rng.rangef(0.18f, 0.4f);   // open hat stays long
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
    p["decay"]  = rng.rangef(0.7f, 1.3f);   // shorter — a subtle accent, not a washy cheese-crash
    p["shimmer"] = rng.rangef(0.3f, 0.55f); // comb-resonated metal partials
    p["width"]  = 0.55f;                     // narrower/less splashy
    p["gain"]   = 0.32f;                     // sit it back in the mix
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
    shL.setHighShelf(10000.0, 1.0, sr); shR.setHighShelf(10000.0, 1.0, sr); // less splashy top
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
