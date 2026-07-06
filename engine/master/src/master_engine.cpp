// Automated Master Engine (doc 08): tilt EQ -> saturation -> soft clipper ->
// lookahead true-peak limiter -> LUFS/TP conformance loop. Also provides the
// shared loudness + true-peak meters (measureLufs / measureTruePeakDb).
#include "rtg/master/master_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace rtg {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---- minimal RBJ biquad (transposed DF-II) --------------------------------
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process(float x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return float(y);
    }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

Biquad highpass(double fs, double f0, double Q) {
    Biquad bq; f0 = std::min(std::max(f0, 5.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2 * Q);
    bq.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    return bq;
}
Biquad lowShelf(double fs, double f0, double gainDb, double S = 0.9) {
    Biquad bq; f0 = std::min(std::max(f0, 10.0), fs * 0.49);
    double A = std::pow(10.0, gainDb / 40.0), w0 = 2 * kPi * f0 / fs;
    double c = std::cos(w0), s = std::sin(w0);
    double al = s / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2), t = 2 * std::sqrt(A) * al;
    bq.set(A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c),
           A * ((A + 1) - (A - 1) * c - t), (A + 1) + (A - 1) * c + t,
           -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    return bq;
}
Biquad highShelf(double fs, double f0, double gainDb, double S = 0.9) {
    Biquad bq; f0 = std::min(std::max(f0, 10.0), fs * 0.49);
    double A = std::pow(10.0, gainDb / 40.0), w0 = 2 * kPi * f0 / fs;
    double c = std::cos(w0), s = std::sin(w0);
    double al = s / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2), t = 2 * std::sqrt(A) * al;
    bq.set(A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c),
           A * ((A + 1) + (A - 1) * c - t), (A + 1) - (A - 1) * c + t,
           2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    return bq;
}

double rmsOf(const StereoBuffer& b) {
    if (b.empty()) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < b.size(); ++i) a += 0.5 * (double(b.l[i]) * b.l[i] + double(b.r[i]) * b.r[i]);
    return std::sqrt(a / double(b.size()));
}

bool finiteBuf(const StereoBuffer& b) {
    for (float s : b.l) if (!std::isfinite(s)) return false;
    for (float s : b.r) if (!std::isfinite(s)) return false;
    return true;
}

// ---- soft clipper: cubic knee above a linear threshold --------------------
inline float cubicClip(float x, float thr) {
    float a = std::fabs(x), s = x < 0 ? -1.f : 1.f;
    if (a <= thr) return x;
    float range = 1.0f - thr;
    if (range <= 1e-6f) return s * thr;
    float u = (a - thr) / range;
    if (u > 1.f) u = 1.f;
    float comp = (u - u * u * u / 3.0f) * range; // slope 1 at knee, caps at range*2/3
    return s * (thr + comp);
}

void softClipStage(StereoBuffer& buf, float thr, float drive) {
    float inv = 1.0f / std::max(drive, 1e-3f);
    for (auto& s : buf.l) s = cubicClip(s * drive, thr) * inv;
    for (auto& s : buf.r) s = cubicClip(s * drive, thr) * inv;
}

// ---- lookahead peak limiter (linked channels) -----------------------------
// Backward attack-ramp builds the anticipation (no explicit signal delay);
// exponential release. Ceiling is linear.
void limiterStage(StereoBuffer& buf, double fs, float ceiling, double relSec) {
    const size_t n = buf.size();
    if (n == 0) return;
    const int look = std::max(1, int(0.002 * fs));      // 2 ms
    const double slope = 1.0 / look;                    // ramp rate per sample

    std::vector<float> target(n);
    for (size_t i = 0; i < n; ++i) {
        float a = std::max(std::fabs(buf.l[i]), std::fabs(buf.r[i]));
        target[i] = (a > ceiling) ? float(ceiling / a) : 1.0f;
    }
    // Backward pass: gain may rise by at most `slope` per sample -> attack ramp
    // that completes exactly when the peak arrives (lookahead anticipation).
    for (size_t i = n - 1; i-- > 0;) {
        float allowed = target[i + 1] + float(slope);
        if (target[i] > allowed) target[i] = allowed;
    }
    const double rel = std::exp(-1.0 / (relSec * fs));
    double g = 1.0;
    for (size_t i = 0; i < n; ++i) {
        double t = target[i];
        if (t < g) g = t;                       // instant attack (pre-ramped)
        else g = t + (g - t) * rel;             // release toward higher gain
        buf.l[i] = float(buf.l[i] * g);
        buf.r[i] = float(buf.r[i] * g);
    }
}

} // namespace

// ===========================================================================
// measureLufs : K-weighted-ish integrated loudness (BS.1770 gating).
// ===========================================================================
float measureLufs(const StereoBuffer& audio, double sampleRate) {
    const size_t n = audio.size();
    if (n == 0) return -70.0f;

    // K-weight approximation: 2nd-order HP @ 60 Hz (Q 0.5) + high shelf +4 dB @ 1.5 kHz.
    Biquad hpL = highpass(sampleRate, 60.0, 0.5), hpR = hpL;
    Biquad shL = highShelf(sampleRate, 1500.0, 4.0), shR = shL;
    std::vector<float> kl(n), kr(n);
    for (size_t i = 0; i < n; ++i) {
        kl[i] = shL.process(hpL.process(audio.l[i]));
        kr[i] = shR.process(hpR.process(audio.r[i]));
    }

    const size_t block = std::max<size_t>(1, size_t(0.400 * sampleRate));
    const size_t hop = std::max<size_t>(1, size_t(0.100 * sampleRate)); // 75% overlap
    std::vector<double> z; // mean-square per block
    for (size_t start = 0; start + block <= n; start += hop) {
        double acc = 0.0;
        for (size_t i = 0; i < block; ++i)
            acc += double(kl[start + i]) * kl[start + i] + double(kr[start + i]) * kr[start + i];
        z.push_back(acc / double(block));
    }
    if (z.empty()) { // signal shorter than one block: single measurement
        double acc = 0.0;
        for (size_t i = 0; i < n; ++i)
            acc += double(kl[i]) * kl[i] + double(kr[i]) * kr[i];
        z.push_back(acc / double(n));
    }

    auto loud = [](double ms) { return ms > 0 ? -0.691 + 10.0 * std::log10(ms) : -1000.0; };

    // Absolute gate @ -70 LUFS.
    double sum1 = 0.0; int cnt1 = 0;
    for (double ms : z) if (loud(ms) >= -70.0) { sum1 += ms; ++cnt1; }
    if (cnt1 == 0) return -70.0f;
    double relThr = -0.691 + 10.0 * std::log10(sum1 / cnt1) - 10.0; // -10 LU relative

    double sum2 = 0.0; int cnt2 = 0;
    for (double ms : z) if (loud(ms) >= -70.0 && loud(ms) >= relThr) { sum2 += ms; ++cnt2; }
    if (cnt2 == 0) return -70.0f;

    double integ = -0.691 + 10.0 * std::log10(sum2 / cnt2);
    return float(integ);
}

// ===========================================================================
// measureTruePeakDb : 4x oversample via 8-tap windowed-sinc polyphase.
// ===========================================================================
float measureTruePeakDb(const StereoBuffer& audio) {
    const size_t n = audio.size();
    if (n == 0) return -70.0f;
    constexpr int kPhases = 4, kTaps = 8;
    static const auto coeffs = [] {
        std::array<std::array<double, kTaps>, kPhases> h{};
        for (int p = 0; p < kPhases; ++p) {
            double frac = double(p) / kPhases, sum = 0.0;
            for (int k = 0; k < kTaps; ++k) {
                double x = frac + 3.0 - k;            // distance from output pos
                double sinc = (std::fabs(x) < 1e-9) ? 1.0 : std::sin(kPi * x) / (kPi * x);
                double w = 0.5 - 0.5 * std::cos(2 * kPi * (k + 0.5) / kTaps); // Hann
                h[p][k] = sinc * w;
                sum += h[p][k];
            }
            for (int k = 0; k < kTaps; ++k) h[p][k] /= sum; // unity DC gain
        }
        return h;
    }();

    double maxAbs = 0.0;
    auto scan = [&](const std::vector<float>& c) {
        for (size_t i = 0; i < c.size(); ++i) {
            for (int p = 0; p < kPhases; ++p) {
                double acc = 0.0;
                for (int k = 0; k < kTaps; ++k) {
                    long idx = long(i) - 3 + k;
                    if (idx < 0 || idx >= long(c.size())) continue;
                    acc += c[idx] * coeffs[p][k];
                }
                maxAbs = std::max(maxAbs, std::fabs(acc));
            }
        }
    };
    scan(audio.l);
    scan(audio.r);
    if (maxAbs < 1e-9) return -70.0f;
    return float(20.0 * std::log10(maxAbs));
}

// ===========================================================================
// masterize
// ===========================================================================
StereoBuffer masterize(const StereoBuffer& premaster, const Plan& plan,
                       double sampleRate, MasterStats* stats) {
    StereoBuffer base = premaster;
    const size_t n = base.size();
    if (n == 0) {
        if (stats) *stats = MasterStats{};
        return base;
    }

    // [1] Tilt EQ. tilt = (darkness01 - 0.5)*3 spans +/-1.5; darker => warm top.
    double tilt = (double(plan.darkness01) - 0.5) * 3.0;
    {
        Biquad lsL = lowShelf(sampleRate, 120.0, tilt), lsR = lsL;
        Biquad hsL = highShelf(sampleRate, 6000.0, -tilt), hsR = hsL;
        for (size_t i = 0; i < n; ++i) {
            base.l[i] = hsL.process(lsL.process(base.l[i]));
            base.r[i] = hsR.process(lsR.process(base.r[i]));
        }
    }

    // [2] Saturation, RMS-compensated to within ~0.2 dB.
    {
        double drive = 1.0 + double(plan.mixAggression) * 1.2;
        double pre = rmsOf(base);
        double invT = 1.0 / std::tanh(drive);
        for (auto& s : base.l) s = float(std::tanh(drive * s) * invT);
        for (auto& s : base.r) s = float(std::tanh(drive * s) * invT);
        double post = rmsOf(base);
        if (post > 1e-9 && pre > 1e-9) base.applyGain(float(pre / post));
    }

    // [3] Soft-clip drive from measured crest (adapt once).
    float crestDb;
    {
        double r = rmsOf(base), p = base.peak();
        crestDb = float(20.0 * std::log10(std::max(p, 1e-9) / std::max(r, 1e-9)));
    }
    const float clipThr = std::pow(10.0f, -3.0f / 20.0f);   // -3 dBFS
    float clipDrive = std::clamp(1.0f + (crestDb - 6.0f) * 0.08f, 1.1f, 2.5f);

    const float ceiling = std::pow(10.0f, -1.2f / 20.0f);   // -1.2 dBFS internal
    const double relSec = 0.080 * (145.0 / std::max(1.0, plan.bpm)); // program-scaled
    const float target = plan.masterTargetLufs;

    // [6] Conformance loop.
    StereoBuffer out;
    float inGainDb = 0.0f;
    for (int pass = 0; pass < 3; ++pass) {
        out = base;
        out.applyGain(std::pow(10.0f, inGainDb / 20.0f));
        softClipStage(out, clipThr, clipDrive);          // [4]
        limiterStage(out, sampleRate, ceiling, relSec);  // [5]
        float lufs = measureLufs(out, sampleRate);
        if (std::fabs(target - lufs) <= 0.7f) break;
        float delta = std::clamp(target - lufs, -6.0f, 6.0f);
        inGainDb += delta;
    }

    // Final true-peak conformance: trim below -1.0 dBTP if needed.
    float tp = measureTruePeakDb(out);
    if (tp > -1.0f) {
        out.applyGain(std::pow(10.0f, (-1.0f - tp) / 20.0f));
    }

    if (!finiteBuf(out)) out = premaster; // paranoid guard; never emit NaN

    if (stats) {
        stats->integratedLufs = measureLufs(out, sampleRate);
        stats->truePeakDb = measureTruePeakDb(out);
        double r = rmsOf(out), p = out.peak();
        stats->crestDb = float(20.0 * std::log10(std::max(p, 1e-9) / std::max(r, 1e-9)));
    }
    return out;
}

} // namespace rtg
