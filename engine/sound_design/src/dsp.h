#pragma once
// Private DSP primitives for the sound-design engine. Header-only, deterministic,
// per-sample at 48 kHz. No global state; all randomness flows from rtg::Rng.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "rtg/utils/rng.h"

namespace rtg::synth::dsp {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

inline double midiToFreq(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// ------------------------------------------------------------------ oscillators
inline double polyBlep(double t, double dt) {
    if (dt <= 0.0) return 0.0;
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

struct PhaseOsc {
    double phase = 0.0, inc = 0.0;
    void setFreq(double f, double sr) { inc = (sr > 0.0) ? f / sr : 0.0; }
    void reset(double p = 0.0) { phase = p; }
    void advance() {
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        else if (phase < 0.0) phase += 1.0;
    }
    double sine() { double v = std::sin(kTwoPi * phase); advance(); return v; }
    // saw ramp -1..1, band-limited via polyBLEP.
    double saw() {
        double v = 2.0 * phase - 1.0;
        v -= polyBlep(phase, inc);
        advance();
        return v;
    }
    double square() {
        double v = phase < 0.5 ? 1.0 : -1.0;
        v += polyBlep(phase, inc);
        double t2 = phase + 0.5; if (t2 >= 1.0) t2 -= 1.0;
        v -= polyBlep(t2, inc);
        advance();
        return v;
    }
    double triangle() {
        double v = phase < 0.5 ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase);
        advance();
        return v;
    }
    // variable-width pulse (band-limited via polyBLEP). width in (0,1).
    double pulse(double width) {
        width = clampd(width, 0.05, 0.95);
        double v = phase < width ? 1.0 : -1.0;
        v += polyBlep(phase, inc);
        double t2 = phase + (1.0 - width); if (t2 >= 1.0) t2 -= 1.0;
        v -= polyBlep(t2, inc);
        advance();
        return v;
    }
    // sample sine at an arbitrary phase offset (for FM) without advancing.
    double sineAt(double extraPhase) const { return std::sin(kTwoPi * phase + extraPhase); }
};

// ------------------------------------------------------------------ noise
struct WhiteNoise {
    Rng rng;
    explicit WhiteNoise(uint64_t seed) : rng(seed) {}
    float tick() { return float(rng.uniform() * 2.0 - 1.0); }
};

struct PinkNoise {
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0;
    explicit PinkNoise(uint64_t seed) : rng(seed) {}
    float tick() {
        float w = float(rng.uniform() * 2.0 - 1.0);
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.2f;
    }
};

// ------------------------------------------------------------------ SVF (cytomic/TPT)
struct SVF {
    double ic1 = 0, ic2 = 0;
    double g = 0, k = 0, a1 = 0, a2 = 0, a3 = 0;
    double lp = 0, bp = 0, hp = 0;
    void set(double cutoff, double q, double sr) {
        cutoff = clampd(cutoff, 8.0, sr * 0.49);
        q = std::max(q, 0.30);
        g = std::tan(kPi * cutoff / sr);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void process(double v0) {
        double v3 = v0 - ic2;
        double v1 = a1 * ic1 + a2 * v3;
        double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
};

// ------------------------------------------------------------------ biquad (RBJ)
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    float process(float x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return float(y);
    }
    void reset() { z1 = z2 = 0; }
    void setLowpass(double f, double q, double sr) {
        f = clampd(f, 8.0, sr * 0.49);
        double w0 = kTwoPi * f / sr, c = std::cos(w0), al = std::sin(w0) / (2.0 * q);
        double B0 = (1 - c) / 2, B1 = 1 - c, B2 = (1 - c) / 2;
        double A0 = 1 + al, A1 = -2 * c, A2 = 1 - al;
        norm(B0, B1, B2, A0, A1, A2);
    }
    void setHighpass(double f, double q, double sr) {
        f = clampd(f, 8.0, sr * 0.49);
        double w0 = kTwoPi * f / sr, c = std::cos(w0), al = std::sin(w0) / (2.0 * q);
        double B0 = (1 + c) / 2, B1 = -(1 + c), B2 = (1 + c) / 2;
        double A0 = 1 + al, A1 = -2 * c, A2 = 1 - al;
        norm(B0, B1, B2, A0, A1, A2);
    }
    void setBandpass(double f, double q, double sr) {
        f = clampd(f, 8.0, sr * 0.49);
        double w0 = kTwoPi * f / sr, c = std::cos(w0), al = std::sin(w0) / (2.0 * q);
        double B0 = al, B1 = 0, B2 = -al;
        double A0 = 1 + al, A1 = -2 * c, A2 = 1 - al;
        norm(B0, B1, B2, A0, A1, A2);
    }
    void setPeak(double f, double q, double gainDb, double sr) {
        f = clampd(f, 8.0, sr * 0.49);
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = kTwoPi * f / sr, c = std::cos(w0), al = std::sin(w0) / (2.0 * q);
        double B0 = 1 + al * A, B1 = -2 * c, B2 = 1 - al * A;
        double A0 = 1 + al / A, A1 = -2 * c, A2 = 1 - al / A;
        norm(B0, B1, B2, A0, A1, A2);
    }
    void setLowShelf(double f, double gainDb, double sr) {
        f = clampd(f, 8.0, sr * 0.49);
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = kTwoPi * f / sr, c = std::cos(w0), s = std::sin(w0);
        double al = s / 2.0 * std::sqrt((A + 1 / A) * (1.0 / 0.9 - 1) + 2);
        double tsa = 2 * std::sqrt(A) * al;
        double B0 = A * ((A + 1) - (A - 1) * c + tsa);
        double B1 = 2 * A * ((A - 1) - (A + 1) * c);
        double B2 = A * ((A + 1) - (A - 1) * c - tsa);
        double A0 = (A + 1) + (A - 1) * c + tsa;
        double A1 = -2 * ((A - 1) + (A + 1) * c);
        double A2 = (A + 1) + (A - 1) * c - tsa;
        norm(B0, B1, B2, A0, A1, A2);
    }
    void setHighShelf(double f, double gainDb, double sr) {
        f = clampd(f, 8.0, sr * 0.49);
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = kTwoPi * f / sr, c = std::cos(w0), s = std::sin(w0);
        double al = s / 2.0 * std::sqrt((A + 1 / A) * (1.0 / 0.9 - 1) + 2);
        double tsa = 2 * std::sqrt(A) * al;
        double B0 = A * ((A + 1) + (A - 1) * c + tsa);
        double B1 = -2 * A * ((A - 1) + (A + 1) * c);
        double B2 = A * ((A + 1) + (A - 1) * c - tsa);
        double A0 = (A + 1) - (A - 1) * c + tsa;
        double A1 = 2 * ((A - 1) - (A + 1) * c);
        double A2 = (A + 1) - (A - 1) * c - tsa;
        norm(B0, B1, B2, A0, A1, A2);
    }
private:
    void norm(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

// ------------------------------------------------------------------ comb (tuned)
struct Comb {
    std::vector<float> buf;
    int w = 0;
    float fb = 0.6f;
    float damp = 0.0f;   // feedback lowpass amount 0..1
    float lp = 0.0f;
    void setMaxDelay(int maxSamples) {
        buf.assign(std::max(maxSamples + 4, 8), 0.0f);
        w = 0; lp = 0;
    }
    float process(float x, float delaySamples) {
        int n = int(buf.size());
        delaySamples = clampf(delaySamples, 1.0f, float(n - 2));
        double rp = double(w) - delaySamples;
        while (rp < 0) rp += n;
        int i0 = int(rp);
        double frac = rp - i0;
        int i1 = i0 + 1; if (i1 >= n) i1 -= n;
        float d = float(buf[i0] + (buf[i1] - buf[i0]) * frac);
        lp = d + damp * (lp - d);
        float y = x + fb * lp;
        buf[w] = y;
        if (++w >= n) w = 0;
        return y;
    }
};

// ------------------------------------------------------------------ delay line
struct DelayLine {
    std::vector<float> buf;
    int w = 0;
    void setSize(int n) { buf.assign(std::max(n, 2), 0.0f); w = 0; }
    void write(float x) { buf[w] = x; if (++w >= int(buf.size())) w = 0; }
    float read(int d) const {
        int n = int(buf.size());
        int i = w - 1 - d; while (i < 0) i += n; while (i >= n) i -= n;
        return buf[i];
    }
    float readFrac(double d) const {
        int n = int(buf.size());
        double rp = double(w) - 1.0 - d;
        while (rp < 0) rp += n;
        int i0 = int(rp); double f = rp - i0;
        int i1 = i0 + 1; if (i1 >= n) i1 -= n;
        return float(buf[i0] + (buf[i1] - buf[i0]) * f);
    }
};

// ------------------------------------------------------------------ allpass (phaser stage)
struct Allpass1 {
    double a = 0.0, z = 0.0;
    void setCoef(double cutoff, double sr) {
        double t = std::tan(kPi * clampd(cutoff, 8.0, sr * 0.49) / sr);
        a = (t - 1.0) / (t + 1.0);
    }
    float process(float x) {
        double y = a * x + z;
        z = x - a * y;
        return float(y);
    }
};

// ------------------------------------------------------------------ waveshapers
inline float shTanh(float x, float drive) { return std::tanh(x * drive); }
inline float shHard(float x) { return clampf(x, -1.0f, 1.0f); }
inline float shFold(float x) {
    for (int i = 0; i < 12 && (x > 1.0f || x < -1.0f); ++i) {
        if (x > 1.0f) x = 2.0f - x;
        else if (x < -1.0f) x = -2.0f - x;
    }
    return clampf(x, -1.0f, 1.0f);
}

// ------------------------------------------------------------------ envelopes
inline float envCoef(float timeSec, double sr) {
    timeSec = std::max(timeSec, 1.0e-4f);
    return float(std::exp(-1.0 / (double(timeSec) * sr)));
}

// Percussive attack (linear, crisp) -> exponential decay-to-zero.
struct EnvAD {
    double sr = 48000;
    float atkInc = 1.0f, cd = 0;
    int stage = 0; // 0 idle, 1 atk, 2 dec
    float level = 0;
    void start(float atk, float dec, double s) {
        sr = s;
        atkInc = (atk <= 0.0f) ? 1.0f : float(1.0 / (double(atk) * s));
        cd = envCoef(dec, s);
        stage = 1; level = 0;
    }
    float tick() {
        if (stage == 1) {
            level += atkInc;
            if (level >= 1.0f) { level = 1.0f; stage = 2; }
            return level;
        }
        if (stage == 2) {
            level *= cd;
            if (level < 1.0e-5f) { level = 0; stage = 0; }
            return level;
        }
        return 0.0f;
    }
    bool done() const { return stage == 0; }
};

// Gated ADSR (exponential) for sustained roles.
struct EnvADSR {
    double sr = 48000;
    float atk = 0.01f, dec = 0.1f, sus = 0.7f, rel = 0.1f;
    float ca = 0, cd = 0, cr = 0;
    int stage = 0; // 0 idle,1 atk,2 dec,3 sus,4 rel
    float level = 0;
    void start(float a, float d, float s, float r, double sampleRate) {
        sr = sampleRate; atk = a; dec = d; sus = s; rel = r;
        ca = envCoef(a, sr); cd = envCoef(d, sr); cr = envCoef(r, sr);
        stage = 1; level = 0;
    }
    float tick(bool gate) {
        if (!gate && stage != 0 && stage != 4) stage = 4;
        switch (stage) {
            case 1:
                level = 1.0f + (level - 1.0f) * ca;
                if (level >= 0.999f) { level = 1.0f; stage = 2; }
                break;
            case 2:
                level = sus + (level - sus) * cd;
                if (std::fabs(level - sus) < 0.001f) { level = sus; stage = 3; }
                break;
            case 3: level = sus; break;
            case 4:
                level *= cr;
                if (level < 1.0e-5f) { level = 0; stage = 0; }
                break;
            default: level = 0;
        }
        return level;
    }
    bool done() const { return stage == 0; }
};

// ------------------------------------------------------------------ one-pole smoother
struct OnePole {
    float z = 0, a = 0.99f;
    void setTime(float t, double sr) { a = envCoef(t, sr); }
    float tick(float x) { z = x + (z - x) * a; return z; }
    void reset(float v = 0) { z = v; }
};

// ------------------------------------------------------------------ DC blocker
struct DCBlock {
    float x1 = 0, y1 = 0, R = 0.9975f;
    float tick(float x) { float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
};

// ------------------------------------------------------------------ LFO
struct LFO {
    double phase = 0, inc = 0;
    int shape = 0; // 0 sine, 1 tri
    void setRate(double hz, double sr) { inc = (sr > 0) ? hz / sr : 0; }
    void reset(double p = 0) { phase = p; }
    float tick() {
        float v;
        if (shape == 0) v = float(std::sin(kTwoPi * phase));
        else v = float(4.0 * std::fabs(phase - 0.5) - 1.0);
        phase += inc; phase -= std::floor(phase);   // wrap any sign into [0,1)
        return v;
    }
};

// ------------------------------------------------------------------ Linkwitz-Riley split
// LR 2nd-order crossover (two cascaded Butterworth 1st-order == 2nd order, Q=0.5).
// split() fills lo/hi; sum reconstructs (allpass-flat magnitude). Chain two for
// a 3-band split: (lo1) | (hi1 -> lo2) | (hi2).
struct LR2 {
    Biquad lp, hp;
    void set(double f, double sr) {
        lp.setLowpass(f, 0.5, sr);
        hp.setHighpass(f, 0.5, sr);
    }
    void split(float x, float& lo, float& hi) { lo = lp.process(x); hi = hp.process(x); }
};

struct Split3 {
    LR2 low, high;
    void set(double fLo, double fHi, double sr) { low.set(fLo, sr); high.set(fHi, sr); }
    void split(float x, float& lo, float& mid, float& hi) {
        float l, rest;
        low.split(x, l, rest);
        float m, h;
        high.split(rest, m, h);
        lo = l; mid = m; hi = h;
    }
};

// ------------------------------------------------------------------ vowel formants
// Sung-vowel formant centres F1/F2/F3 (Hz). The "talking" growl morphs between
// these; a small parallel band-pass bank realises each vowel (see FormantBank).
struct Vowel { float f1, f2, f3; };
constexpr int kVowelCount = 5;
inline const Vowel& vowelTable(int i) {
    static const Vowel V[kVowelCount] = {
        {730.0f, 1090.0f, 2440.0f}, // A  (ah)
        {530.0f, 1840.0f, 2480.0f}, // E  (eh)
        {390.0f, 1990.0f, 2550.0f}, // I  (ee)
        {570.0f,  840.0f, 2410.0f}, // O  (oh)
        {440.0f, 1020.0f, 2240.0f}, // U  (oo)
    };
    return V[((i % kVowelCount) + kVowelCount) % kVowelCount];
}
// Interpolate a vowel along a path of `count` vowel indices at pos in [0,1].
inline Vowel vowelMorph(const int* path, int count, float pos, float octave = 1.0f) {
    count = std::max(1, count);
    pos = clampf(pos, 0.0f, 1.0f);
    float x = pos * float(count - 1);
    int i0 = std::min(int(x), count - 1);
    int i1 = std::min(i0 + 1, count - 1);
    float t = x - float(i0);
    const Vowel& a = vowelTable(path[i0]);
    const Vowel& b = vowelTable(path[i1]);
    Vowel v;
    v.f1 = lerpf(a.f1, b.f1, t) * octave;
    v.f2 = lerpf(a.f2, b.f2, t) * octave;
    v.f3 = lerpf(a.f3, b.f3, t) * octave;
    return v;
}

// Parallel band-pass formant bank (3 resonant peaks) — the vocal filter.
struct FormantBank {
    Biquad bp1, bp2, bp3;
    float g1 = 1.0f, g2 = 0.72f, g3 = 0.42f;
    void set(float f1, float f2, float f3, double q, double sr) {
        bp1.setBandpass(clampd(f1, 90.0, sr * 0.45), q, sr);
        bp2.setBandpass(clampd(f2, 120.0, sr * 0.45), q * 0.9, sr);
        bp3.setBandpass(clampd(f3, 150.0, sr * 0.45), q * 0.8, sr);
    }
    float process(float x) {
        return g1 * bp1.process(x) + g2 * bp2.process(x) + g3 * bp3.process(x);
    }
};

// ------------------------------------------------------------------ shape/step LFO
// Rhythmic modulator: sine/tri/ramp-up/ramp-down/square/stepped-S&H. The stepped
// mode uses a deterministic per-voice pattern of `steps` values (repeats each
// cycle) for musical, "talking" movement rather than a plain sine wobble.
struct ShapeLFO {
    double phase = 0.0, inc = 0.0;
    int shape = 0;   // 0 sine,1 tri,2 rampUp,3 rampDown,4 square,5 stepped S&H
    int steps = 4;
    std::vector<float> pat;   // stepped pattern (bipolar)
    void init(double hz, double sr, int shp, int stepCount, uint64_t seed) {
        inc = (sr > 0.0) ? hz / sr : 0.0;
        shape = shp;
        steps = std::max(1, stepCount);
        phase = 0.0;
        pat.resize(size_t(steps));
        Rng r(seed ? seed : 1);
        for (auto& x : pat) x = float(r.uniform() * 2.0 - 1.0);
    }
    void reset(double p = 0.0) { phase = p; }
    float tick() {
        float v;
        switch (shape) {
            case 1: v = float(4.0 * std::fabs(phase - 0.5) - 1.0); break; // tri
            case 2: v = float(2.0 * phase - 1.0); break;                  // ramp up
            case 3: v = float(1.0 - 2.0 * phase); break;                  // ramp down
            case 4: v = phase < 0.5 ? 1.0f : -1.0f; break;               // square
            case 5: {                                                     // stepped
                int s = int(phase * steps);
                s = s < 0 ? 0 : (s >= steps ? steps - 1 : s);   // guard both ends
                v = pat[size_t(s)]; break;
            }
            default: v = float(std::sin(kTwoPi * phase)); break;          // sine
        }
        phase += inc; phase -= std::floor(phase);   // wrap any sign into [0,1)
        return v;
    }
};

// ------------------------------------------------------------------ envelope follower
struct EnvFollow {
    float env = 0.0f, atkA = 0.0f, relA = 0.0f;
    void set(float atk, float rel, double sr) { atkA = envCoef(atk, sr); relA = envCoef(rel, sr); }
    float tick(float x) {
        float a = std::fabs(x);
        env = (a > env) ? a + (env - a) * atkA : a + (env - a) * relA;
        return env;
    }
};

// ------------------------------------------------------------------ OTT-lite (3-band)
// Compact upward/downward multiband compressor: pulls each band's level toward a
// target, gluing the sound and pushing the formant mids forward. amount 0..1.
struct OTTLite {
    Split3 split;
    EnvFollow eLo, eMid, eHi;
    float amount = 0.3f;
    void set(double sr, float amt) {
        split.set(180.0, 1500.0, sr);
        eLo.set(0.010f, 0.10f, sr);
        eMid.set(0.005f, 0.06f, sr);
        eHi.set(0.002f, 0.04f, sr);
        amount = clampf(amt, 0.0f, 1.0f);
    }
    static float comp(EnvFollow& e, float x, float amt, float target) {
        float env = e.tick(x); if (env < 1e-5f) env = 1e-5f;
        float g = std::pow(target / env, amt);
        g = clampf(g, 0.30f, 3.5f);
        return x * g;
    }
    float process(float x) {
        float lo, mid, hi; split.split(x, lo, mid, hi);
        return comp(eLo, lo, amount * 0.55f, 0.22f)
             + comp(eMid, mid, amount, 0.25f)
             + comp(eHi, hi, amount * 0.75f, 0.20f);
    }
};

// ------------------------------------------------------------------ stereo widener
inline void widen(float& l, float& r, float width) {
    float m = 0.5f * (l + r);
    float s = 0.5f * (l - r) * width;
    l = m + s; r = m - s;
}

} // namespace rtg::synth::dsp
