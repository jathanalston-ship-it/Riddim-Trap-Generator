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
        phase += inc; if (phase >= 1.0) phase -= 1.0;
        return v;
    }
};

// ------------------------------------------------------------------ stereo widener
inline void widen(float& l, float& r, float width) {
    float m = 0.5f * (l + r);
    float s = 0.5f * (l - r) * width;
    l = m + s; r = m - s;
}

} // namespace rtg::synth::dsp
