// Bass-role factories and renderers: Growl (riddim signature), Screech, Sub,
// Bass808. Deterministic; all variation from Recipe params + Voice seed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../../sound_design/src/dsp.h"
#include "../../sound_design/src/voices.h"
#include "rtg/decision/calibration.h"   // reference-growl fingerprint (JOB 1)

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
    // === HARD-RIDDIM CHUG ==================================================
    // A chug is a SHORT, PUNCHY, TONAL square-wave STAB — not a wobbling/talking
    // growl. The rhythm ("chug-chug-chug") comes from the NOTE PATTERN the
    // composer places (short stabs on the beat/half-beat), NOT from an LFO gate
    // chopping a sustain. So: square-dominant source, fast attack + short decay,
    // aggression from serial distortion (tanh + moderate fold/dirty) kept TONAL
    // and odd-harmonic, and the wub/talk/comb/phaser stages disabled.
    // --- square-dominant source (the tonal mid chug) ---
    static const float ratios[] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
    p["carrierRatio"] = ratios[rng.intRange(0, 4)];
    p["fmIndex"]      = 1.5f + aggr * 3.5f + rng.rangef(-0.5f, 0.5f) * (0.5f + nov * 1.0f); // modest bite, not a scream
    p["fmDecay"]      = rng.rangef(0.02f, 0.06f);   // short: FM bite only on the transient
    p["fmFeedback"]   = rng.rangef(0.0f, 0.25f) * (0.4f + aggr * 0.6f);
    p["carMix"]       = rng.rangef(0.08f, 0.18f);   // keep LOW: analog SQUARE LEADS the FM sine
    p["srcMorph"]     = clampf(0.5f + rng.rangef(-0.05f, 0.05f), 0.42f, 0.58f); // lock to square center
    p["pulseWidth"]   = rng.rangef(0.35f, 0.5f);
    p["pmAmt"]        = rng.rangef(0.08f, 0.22f) * (0.6f + aggr * 0.8f); // light metallic bite, scales w/ aggr
    p["pwmRate"]      = rng.rangef(0.0f, 0.2f);     // near-static width (no wub movement)
    p["pwmDepth"]     = rng.rangef(0.0f, 0.04f);    // ~static per hit
    p["unison"]       = float(rng.intRange(1, 2));
    p["detuneCents"]  = rng.rangef(4.0f, 12.0f);
    // --- grit: minimal (the robotic "talk" is stripped; keep only a touch of edge) ---
    p["gritRate"]     = rng.rangef(6000.0f, 16000.0f);
    p["gritBits"]     = rng.rangef(9.0f, 14.0f);
    p["gritMix"]      = clampf(rng.rangef(0.0f, 0.12f) * (0.4f + aggr * 0.6f), 0.0f, 0.25f);
    // --- tempo-synced amplitude gate: STRIPPED (gateDepth -> 0, no wub) ---
    // gateDiv/gateShape are still drawn/retained so the calibration block below
    // and legacy recipes stay well-formed, but renderGrowl no longer applies the
    // gate: the chug rhythm comes from the note pattern, not this LFO.
    static const float gateDivs[] = {2.0f, 3.0f, 4.0f, 6.0f};
    int   gdAlt       = rng.intRange(0, 3);                    // drawn unconditionally (determinism)
    p["gateDiv"]      = (rng.uniform() < 0.7f) ? 2.0f : gateDivs[gdAlt];
    p["gateDepth"]    = 0.0f;                                  // NO wub — pure percussive stab
    p["gateShape"]    = (rng.uniform() < 0.85f) ? 4.0f : 5.0f;
    // --- distortion staging: tonal odd-harmonic CHUG edge ---
    // Serial stack (pre-tanh -> post tanh/fold blend -> wavefold -> 2nd dirty tanh),
    // but tuned MODERATE and tanh-leaning so the square stays TONAL/pitched, tight
    // and mid-focused. All bounded/normalised so it stays hard, not just louder.
    p["drivePre"]     = 1.6f + aggr * 2.2f + rng.rangef(-0.2f, 0.4f);
    p["mudDb"]        = -(2.0f + rng.rangef(0.0f, 3.0f));   // ~300 Hz dip
    p["drivePost"]    = 1.4f + aggr * 2.0f;
    p["wsMix"]        = clampf(0.15f + aggr * 0.18f + rng.rangef(-0.04f, 0.06f), 0.0f, 0.6f); // tanh-leaning: ODD harmonics (square)
    p["dirtyDrive"]   = 2.0f + aggr * 2.8f + rng.rangef(-0.2f, 0.3f);  // 2nd serial tanh drive
    p["dirtyMix"]     = clampf(0.12f + aggr * 0.45f + rng.rangef(-0.03f, 0.08f), 0.0f, 0.75f); // dirtier w/ aggr
    // --- formant / vowel bank: TALK STRIPPED (minimal static color, low mix) ---
    // Vowel path still drawn for determinism, but morph movement is disabled and
    // the formant is blended in only faintly so the chug stays a static square.
    float v0, v1, v2; pickVowelPath(rng, v0, v1, v2);
    p["vowel0"] = v0; p["vowel1"] = v1; p["vowel2"] = v2;
    p["formantQ"]     = rng.rangef(4.0f, 8.0f);
    p["formantMix"]   = rng.rangef(0.05f, 0.15f);   // minimal — NO vocal talk
    p["formantGain"]  = rng.rangef(1.4f, 2.2f);
    p["formantShift"] = clampf(rng.rangef(1.0f, 1.15f), 0.95f, 1.25f); // no octave-up scream
    p["morphBase"]    = rng.rangef(0.1f, 0.4f);
    p["morphMod"]     = rng.rangef(0.0f, 0.08f);    // ~0: no talk morph movement
    // --- rhythmic modulation: vowel/LFO movement disabled (static per hit) ---
    p["lfoRate"]      = rng.rangef(1.0f, 4.0f);
    p["lfoDepth"]     = rng.rangef(0.0f, 0.06f);    // ~0: no wobble
    p["lfoShape"]     = float(rng.intRange(0, 5));
    p["lfoSteps"]     = float(rng.intRange(2, 8));
    // --- body filter ---
    p["lpMul"]        = rng.rangef(5.0f, 9.0f) - dark * 1.5f;
    p["lpQ"]          = rng.rangef(0.7f, 1.4f);
    // --- output EQ / glue: HP keeps the SUB owning the lows; mid-focused chug ---
    // The tonal chug sits ~150 Hz-2 kHz; HP at 120-150 cedes everything below to
    // the sub lane. Mid peak centred lower (700-1400 Hz) for a punchy chug body.
    p["hpFreq"]       = rng.rangef(120.0f, 150.0f);
    p["midGainDb"]    = rng.rangef(3.0f, 6.0f) + aggr * 1.5f;
    p["midFreq"]      = rng.rangef(700.0f, 1400.0f);          // mid-focused chug body
    p["highShelfDb"]  = 2.0f - dark * 7.0f;
    p["ottAmt"]       = rng.rangef(0.1f, 0.3f);
    // --- movement polish: phaser DISABLED (static chug) ---
    p["phaserRate"]   = rng.rangef(0.2f, 1.0f);
    p["phaserMix"]    = 0.0f;
    p["width"]        = 0.05f + nov * 0.12f;
    // --- PUNCHY STAB amp env: fast attack, SHORT decay to a low tail, short release.
    // Each note is a percussive chug that DIES QUICKLY (not a sustained wub).
    p["ampAtk"]       = clampf(0.002f - aggr * 0.0012f + rng.rangef(-0.0003f, 0.0008f), 0.0006f, 0.004f);
    p["ampDec"]       = rng.rangef(0.035f, 0.075f);           // short percussive decay
    p["ampSus"]       = clampf(0.10f - aggr * 0.05f + rng.rangef(-0.02f, 0.04f), 0.0f, 0.18f); // low tail
    p["ampRel"]       = rng.rangef(0.01f, 0.03f);
    p["gain"]         = 0.62f;
    // --- wavefolder: MODERATE, keeps the square tonal (odd-dominant), not a scream ---
    p["foldDrive"]    = 1.3f + aggr * 1.1f + rng.rangef(-0.1f, 0.2f);
    p["foldMix"]      = clampf(0.06f + aggr * 0.14f + rng.rangef(-0.02f, 0.05f), 0.02f, 0.4f);
    p["foldPre"]      = (rng.uniform() < 0.25f) ? 1.0f : 0.0f; // bias POST so fold+dirty stack serially
    // --- swept comb-notch: DISABLED (the "watery" moving-notch wonkiness is gone) ---
    p["combMix"]      = 0.0f;
    p["combG"]        = rng.rangef(0.3f, 0.7f);
    p["combFb"]       = rng.rangef(0.0f, 0.3f);
    p["combRateHz"]   = rng.rangef(0.1f, 1.0f);
    p["combBaseMs"]   = rng.rangef(1.5f, 7.0f);

    // ================= reference-growl match (calibration fingerprint) =========
    // Applied AFTER every rng draw above, so the rng sequence (and determinism)
    // is identical whether or not a calibration is active. All nudges below are
    // pure post-hoc arithmetic on already-drawn params — no new randomness.
    if (const auto& cal = rtg::Calibration::active(); cal.has_value()) {
        const CalibrationProfile& fp = cal->forGenre(rtg::Genre::Riddim);
        if (fp.present) {
            // SQUARE-NESS: a very odd-harmonic reference => push the morph toward
            // the square center and lean on the analog square over the FM sine.
            if (fp.growlOdd > 0.60f) {
                float t = clampf((fp.growlOdd - 0.60f) / 0.30f, 0.0f, 1.0f);
                p["srcMorph"] = clampf(lerpf(p["srcMorph"], 0.5f,  t), 0.3f, 0.7f);
                p["carMix"]   = clampf(lerpf(p["carMix"],   0.12f, t), 0.1f, 0.5f);
            }
            // BRIGHTNESS: map the growl-band centroid (~1900 Hz neutral for a
            // real growl) to a gentle nudge of the body filter + high shelf.
            float bright = clampf((fp.growlCentroidHz - 1900.0f) / 1800.0f, -1.0f, 1.0f);
            p["lpMul"]       = clampf(p["lpMul"]       + bright * 1.5f, 2.0f, 8.0f);
            p["highShelfDb"] = clampf(p["highShelfDb"] + bright * 3.0f, -6.0f, 4.0f);
            // WOBBLE: snap the gate division to the reference wobble rate (145 BPM
            // quarter = 2.42 Hz) and make the wub pronounced.
            if (fp.growlWobbleHz > 0.5f) {
                float xq = fp.growlWobbleHz / 2.42f;
                static const float divs[] = {2.0f, 3.0f, 4.0f, 6.0f};
                float best = divs[0], bestErr = std::fabs(xq - divs[0]);
                for (float d : divs) {
                    float e = std::fabs(xq - d);
                    if (e < bestErr) { bestErr = e; best = d; }
                }
                p["gateDiv"]   = best;
                p["gateDepth"] = clampf(lerpf(p["gateDepth"], 0.85f, 0.5f), 0.45f, 0.85f);
            }
        }
    }
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
    const float formantShift = clampf(rc.get("formantShift", 1.0f), 0.9f, 1.6f);
    const float morphBase    = clampf(rc.get("morphBase", 0.2f), 0.0f, 1.0f);
    const float morphMod     = clampf(rc.get("morphMod", 0.6f), 0.0f, 1.0f);
    const float lfoRate      = std::max(0.02f, rc.get("lfoRate", 4.0f));  // never negative (mutation-safe)
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
    const float phaserRate   = std::max(0.02f, rc.get("phaserRate", 0.6f));
    const float phaserMix    = clampf(rc.get("phaserMix", 0.3f), 0.0f, 1.0f);
    const float width        = clampf(rc.get("width", 0.12f), 0.0f, 0.6f);
    const float ampAtk       = rc.get("ampAtk", 0.008f);
    const float ampDec       = std::max(1.0e-4f, rc.get("ampDec", 0.05f));   // CHUG: short percussive decay
    const float ampSus       = clampf(rc.get("ampSus", 0.10f), 0.0f, 1.0f);  // low tail (percussive stab)
    const float ampRel       = rc.get("ampRel", 0.012f);
    const float gain         = rc.get("gain", 0.6f);
    // Wavefolder (defaults keep the stage OFF so legacy recipes are unchanged).
    const float foldDrive    = clampf(rc.get("foldDrive", 1.6f), 1.0f, 4.0f);
    const float foldMix      = clampf(rc.get("foldMix", 0.0f), 0.0f, 0.6f);
    const bool  foldPre      = rc.get("foldPre", 0.0f) > 0.5f;
    // Second serial "dirty" tanh stage stacked AFTER the wavefold (Serum
    // "Sine-Fold -> Dirty" order). Symmetric tanh => ODD harmonics (square-ness).
    // Peak-normalised by 1/tanh(drive) so it stays bounded (no level blow-up).
    // dirtyMix default 0 keeps the stage OFF for legacy recipes.
    const float dirtyDrive   = clampf(rc.get("dirtyDrive", 3.0f), 1.0f, 8.0f);
    const float dirtyMix     = clampf(rc.get("dirtyMix", 0.0f), 0.0f, 0.9f);
    const float dirtyNorm    = 1.0f / std::max(0.2f, std::tanh(dirtyDrive));
    // Swept comb-notch (defaults keep the stage OFF for legacy recipes).
    const float combMix      = clampf(rc.get("combMix", 0.0f), 0.0f, 0.4f);
    const float combG        = clampf(rc.get("combG", 0.5f), 0.0f, 0.85f);
    const float combFb       = clampf(rc.get("combFb", 0.0f), 0.0f, 0.7f);
    const float combRateHz   = clampf(rc.get("combRateHz", 0.3f), 0.01f, 4.0f);
    const float combBaseMs   = clampf(rc.get("combBaseMs", 4.0f), 0.5f, 12.0f);
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
    // Up-tilt the formant bank so the vocal energy centres on F2/F3 (the
    // 800-2500 Hz presence lane) instead of the low F1 that fed the mud.
    formant.g1 = 0.55f; formant.g2 = 1.00f; formant.g3 = 0.68f;
    SVF svf;
    Biquad mud;  mud.setPeak(300.0, 0.9, mudDb, sr);
    Biquad hp;   hp.setHighpass(hpFreq, 0.707, sr);
    Biquad loSh; loSh.setLowShelf(340.0, -7.0, sr);   // cede low-mids to the sub lane
    Biquad midPk; midPk.setPeak(midFreq, 1.1, midGainDb, sr);
    Biquad hsh;  hsh.setHighShelf(4200.0, highShelfDb, sr);
    Allpass1 ap1, ap2, ap3, apR;
    apR.setCoef(1500.0, sr);
    OTTLite ott; ott.set(sr, ottAmt);
    DCBlock dc;
    ShapeLFO lfo; lfo.init(lfoRate, sr, lfoShape, lfoSteps, nrng.next());
    LFO phaser; phaser.setRate(phaserRate, sr);
    LFO pwm; pwm.setRate(pwmRate, sr);            // slow pulse-width movement
    // Percussive STAB envelope: fast attack, SHORT decay to a low sustain (ampSus),
    // short release. Even when the note is held, the chug decays fast to a quiet
    // tail — a punchy hit, not a sustained wub.
    EnvADSR amp; amp.start(ampAtk, ampDec, ampSus, ampRel, sr);
    EnvAD idxEnv; idxEnv.start(0.001f, fmDecay, sr);

    // Tempo-synced amplitude gate ("wub") is STRIPPED for the CHUG: the wobble is
    // gone — rhythm now comes entirely from the note pattern (short stabs on the
    // beat). gateSeed is still drawn so the downstream nrng draw order (the comb
    // LFO phase seed below) is unchanged; the gate params are retained but inert.
    const uint64_t gateSeed = nrng.next();       // consumed unconditionally for determinism
    (void)gateSeed; (void)gateDivF; (void)gateDepth; (void)gateShape;

    // New tone stages. FoldOS2 has no random state. The comb LFO phase is seeded
    // from nrng AFTER gateSeed so the existing draw order (and legacy sound) is
    // unchanged; combLfo is the only new nrng consumer.
    FoldOS2 folder;
    DCBlock foldDc;                              // fold adds even harmonics/DC
    DelayLine comb;
    const int combMax = int(std::ceil(double(combBaseMs) * 2.0 * 0.001 * sr)) + 4;
    comb.setSize(std::max(combMax, 8));
    LFO combLfo; combLfo.setRate(combRateHz, sr); combLfo.reset(nrng.uniform());

    // Bitcrush / sample-rate-reduction grit state (phase-accumulator S&H).
    double gritPhase = 1.0;   // >=1 so the first sample latches a fresh value
    float  gritHold = 0.0f;
    const float gritLevels = std::pow(2.0f, gritBits - 1.0f);

    // Body lowpass floor: for a low growl fundamental, freq*lpMul lands the body
    // path entirely in the mud (120-500 Hz). Floor it so the body carries real
    // mid harmonics up into the presence lane.
    const double baseCut = std::max(freq * double(lpMul), 480.0);
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

        // --- wavefolder (PRE-formant placement, seed flag) ---
        // 2x-oversampled fold blended low; DC-blocked (fold makes even harmonics).
        if (foldPre && foldMix > 0.0f) {
            float folded = foldDc.tick(folder.process(pre, foldDrive));
            pre = lerpf(pre, folded, foldMix);
        }

        // --- vowel morph driven by note.mod + rhythmic LFO ("talk") ---
        if ((coefCtr & 7) == 0) {
            lval = lfo.tick();
            float morph = clampf(morphBase + morphMod * mod + lfoDepth * 0.5f * lval, 0.0f, 1.0f);
            Vowel vw = vowelMorph(vpath, 3, morph, formantShift);
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

        // --- wavefolder (POST-formant placement, seed flag) ---
        if (!foldPre && foldMix > 0.0f) {
            float folded = foldDc.tick(folder.process(post, foldDrive));
            post = lerpf(post, folded, foldMix);
        }

        // --- second serial "dirty" tanh stage (tearout distortion stack) ---
        // Stacked AFTER the wavefold. Symmetric (odd-harmonic) drive, peak-normalised
        // so |out| <= 1 (bounded). `post` is already in [-1,1] entering here.
        if (dirtyMix > 0.0f) {
            float d = std::tanh(post * dirtyDrive) * dirtyNorm;
            post = lerpf(post, d, dirtyMix);
        }

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

        // --- swept comb-notch ("watery" moving notches) ---
        // Feed-forward comb y = (x + g*x[n-L]) / (1+g) with a fractional delay L
        // swept by a slow LFO; the /(1+g) normalization keeps unity gain so this
        // only sculpts notches, never boosts level. Light feedback for resonance.
        if (combMix > 0.0f) {
            float lv = combLfo.tick();                       // [-1,1]
            float ms = combBaseMs * (1.0f + 0.5f * lv);      // sweep +/-50%
            double L = clampd(double(ms) * 0.001 * sr, 1.0, double(comb.buf.size()) - 2.0);
            float d = comb.readFrac(L);
            float y = (post + combG * d) / (1.0f + combG);
            comb.write(post + combFb * d);
            post = lerpf(post, y, combMix);
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

        // --- amplitude: pure percussive STAB (no wub gate) ---
        float mono = dc.tick(h) * amp.tick(gate) * v.velocity * gain;
        if (!std::isfinite(mono)) mono = 0.0f;              // NaN/Inf guard

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
    p["fmRatio"]      = rng.rangef(3.5f, 9.0f);
    p["fmIndex"]      = 4.0f + aggr * 8.0f + nov * 3.0f;   // more FM index -> metallic scream
    p["fmDecay"]      = rng.rangef(0.08f, 0.3f);
    p["foldDrive"]    = 1.5f + aggr * 3.0f;                // harder fold
    p["dirtyDrive"]   = 2.0f + aggr * 3.0f;                // 2nd serial tanh (metallic)
    p["dirtyMix"]     = clampf(0.25f + aggr * 0.5f, 0.0f, 0.85f);
    p["hpFreq"]       = rng.rangef(320.0f, 480.0f);
    // formant scream (higher octave than growl)
    float v0, v1, v2; pickVowelPath(rng, v0, v1, v2);
    p["vowel0"] = v0; p["vowel1"] = v1; p["vowel2"] = v2;
    p["formantOct"]   = rng.rangef(1.7f, 2.4f) + aggr * 0.1f;   // octave-up scream, higher w/ aggr
    p["formantQ"]     = rng.rangef(5.0f, 10.0f);
    p["formantMix"]   = rng.rangef(0.5f, 0.8f);
    p["formantGain"]  = rng.rangef(2.2f, 3.2f) + aggr * 0.3f;
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
    // Second serial "dirty" tanh stage (metallic tearout). dirtyMix 0 => legacy off.
    const float dirtyDrive = clampf(rc.get("dirtyDrive", 2.0f), 1.0f, 6.0f);
    const float dirtyMix   = clampf(rc.get("dirtyMix", 0.0f), 0.0f, 0.9f);
    const float dirtyNorm  = 1.0f / std::max(0.3f, std::tanh(dirtyDrive));
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
    const float lfoRate = std::max(0.02f, rc.get("lfoRate", 4.0f));  // never negative (mutation-safe)
    const float lfoDepth = clampf(rc.get("lfoDepth", 0.5f), 0.0f, 1.0f);
    const int   lfoShape = std::clamp(int(rc.get("lfoShape", 0.0f)), 0, 5);
    const int   lfoSteps = std::clamp(int(rc.get("lfoSteps", 4.0f)), 1, 16);
    const float phaserRate = std::max(0.02f, rc.get("phaserRate", 1.5f));
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
        // second serial dirty tanh (odd-harmonic metallic stack), bounded/normalised.
        if (dirtyMix > 0.0f) {
            float d = std::tanh(s * dirtyDrive) * dirtyNorm;
            s = lerpf(s, d, dirtyMix);
        }
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
    // BOOMING CHUG sub: sine fundamental + a little harmonic weight, driven /
    // saturated for grit and body, kept in the sub band, hitting punchy WITH the
    // square mid chug. Mono, mostly <120 Hz. Drive scales with aggr.
    p["h2"]        = rng.rangef(0.08f, 0.18f);                     // more weight than a pure sine
    p["h3"]        = rng.rangef(0.04f, 0.12f) * (1.0f - dark * 0.4f);
    p["drive"]     = 1.3f + aggr * 1.6f + rng.rangef(-0.1f, 0.2f); // aggressive saturation, scales w/ aggr
    p["pitchStart"]= rng.rangef(0.0f, 1.5f);                       // short pitch blip (semitones), adds boom
    p["pitchDecay"]= rng.rangef(0.015f, 0.04f);                    // fast blip decay
    p["lpFreq"]    = rng.rangef(110.0f, 150.0f);                   // contain distortion in the sub band
    // punchy env with body/boom: fast attack, short punch decay, solid sustained body.
    p["ampAtk"]    = rng.rangef(0.0015f, 0.004f);
    p["ampDec"]    = rng.rangef(0.04f, 0.09f);
    p["ampSus"]    = rng.rangef(0.65f, 0.82f);                     // booming body under the chug
    p["ampRel"]    = rng.rangef(0.02f, 0.05f);
    p["gain"]      = 0.72f;
    return r;
}

StereoBuffer renderSub(const Recipe& rc, const Voice& v) {
    const size_t N = lenSamples(v);
    StereoBuffer out(N);
    const double sr = v.sr;
    const double freq = std::max(20.0, v.freqHz);
    const float h2 = rc.get("h2", 0.08f);
    const float h3 = rc.get("h3", 0.05f);
    const float drive = std::max(1.0f, rc.get("drive", 1.2f));
    const float pitchStart = std::max(0.0f, rc.get("pitchStart", 0.0f));
    const float pitchDecay = std::max(0.005f, rc.get("pitchDecay", 0.03f));
    const float lpFreq = clampf(rc.get("lpFreq", 130.0f), 60.0f, 300.0f);
    const float ampAtk = rc.get("ampAtk", 0.004f);
    const float ampDec = std::max(1.0e-4f, rc.get("ampDec", 0.06f));
    const float ampSus = clampf(rc.get("ampSus", 0.75f), 0.0f, 1.0f);
    const float ampRel = rc.get("ampRel", 0.03f);
    const float gain = rc.get("gain", 0.7f);
    const float driveNorm = 1.0f / std::tanh(drive);   // peak-normalise the saturation (bounded)
    double ph = 0;
    EnvADSR amp; amp.start(ampAtk, ampDec, ampSus, ampRel, sr);
    const size_t gateN = size_t(std::llround(v.gateSec * sr));
    Biquad lp; lp.setLowpass(lpFreq, 0.707, sr);       // keep the driven sub warm, not fizzy
    DCBlock dc;
    for (size_t n = 0; n < N; ++n) {
        const bool gate = n < gateN;
        double t = double(n) / sr;
        // short pitch blip for extra "boom" on the transient (0 by default -> no blip).
        double penv = pitchStart * std::exp(-t / double(pitchDecay));
        double f = freq * std::pow(2.0, penv / 12.0);
        double s = std::sin(kTwoPi * ph)
                 + h2 * std::sin(kTwoPi * 2.0 * ph)
                 + h3 * std::sin(kTwoPi * 3.0 * ph);
        ph += f / sr; if (ph >= 1.0) ph -= 1.0;
        // aggressive saturation (weight + grit), peak-normalised so |o| stays bounded.
        float o = std::tanh(float(s) * drive) * driveNorm;
        o = lp.process(o);                             // contain harmonics in the sub band (<~150 Hz)
        float mono = dc.tick(o) * amp.tick(gate) * v.velocity * gain;
        if (!std::isfinite(mono)) mono = 0.0f;         // NaN/Inf guard
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
