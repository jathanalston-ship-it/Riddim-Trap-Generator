// Feature extraction (analyze) + role-aware heuristic rating (rate).
// Small radix-2 FFT over 2048-sample Hann frames, hop 1024.
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include "dsp.h"
#include "rtg/synth/synth_engine.h"

namespace rtg::synth {
using namespace dsp;

// ------------------------------------------------------------------ FFT (radix-2)
static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * kPi / double(len);
        std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<double> u = a[i + k];
                std::complex<double> vv = a[i + k + len / 2] * w;
                a[i + k] = u + vv;
                a[i + k + len / 2] = u - vv;
                w *= wl;
            }
        }
    }
}

// ------------------------------------------------------------------ analyze
Features analyze(const StereoBuffer& audio, double sampleRate) {
    Features f;
    const size_t n = audio.size();
    f.durationSec = float(n / sampleRate);
    if (n == 0) return f;

    // Mono sum + overall peak/rms + NaN detection.
    std::vector<float> mono(n);
    double sumSq = 0.0, peak = 0.0;
    bool hasNan = false;
    double dcSum = 0.0;
    long zc = 0;
    float prev = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float s = 0.5f * (audio.l[i] + audio.r[i]);
        if (std::isnan(s) || std::isinf(s)) hasNan = true;
        mono[i] = s;
        sumSq += double(s) * s;
        peak = std::max(peak, double(std::fabs(s)));
        dcSum += s;
        if ((s >= 0.0f) != (prev >= 0.0f)) ++zc;
        prev = s;
    }
    if (hasNan) { f.crestDb = std::nanf(""); return f; }
    double rms = std::sqrt(sumSq / double(n));
    f.crestDb = float(20.0 * std::log10(std::max(peak, 1e-9) / std::max(rms, 1e-9)));
    float zcr = float(double(zc) / double(n)); // ~roughness proxy
    float dcOffset = float(std::fabs(dcSum / double(n)) / std::max(peak, 1e-9));

    // Encode near-silence / DC as centroid=0 so rate() can gate.
    if (rms < 3.16e-3 /* -50 dBFS */ || dcOffset > 0.6f) {
        f.centroidHz = 0.0f;
        return f;
    }

    const size_t FFT = 2048, HOP = 1024;
    std::vector<double> hann(FFT);
    for (size_t i = 0; i < FFT; ++i) hann[i] = 0.5 * (1.0 - std::cos(kTwoPi * i / (FFT - 1)));

    double cNum = 0.0, cDen = 0.0;
    double eSub = 0, eLow = 0, eMid = 0, eHigh = 0;
    std::vector<double> wob;      // 200-3000 Hz band energy per frame
    std::vector<double> frameMag; // sqrt(frame energy) per frame
    const double binHz = sampleRate / double(FFT);

    std::vector<std::complex<double>> buf(FFT);
    size_t frames = 0;
    for (size_t start = 0; start + FFT <= n || (frames == 0 && start == 0); start += HOP) {
        for (size_t i = 0; i < FFT; ++i) {
            double s = (start + i < n) ? double(mono[start + i]) : 0.0;
            buf[i] = std::complex<double>(s * hann[i], 0.0);
        }
        fft(buf);
        double fe = 0, wb = 0;
        for (size_t k = 1; k < FFT / 2; ++k) {
            double mag = std::abs(buf[k]);
            double fq = k * binHz;
            double p2 = mag * mag;
            cNum += fq * mag;
            cDen += mag;
            fe += p2;
            if (fq < 120.0) eSub += p2;
            else if (fq < 500.0) eLow += p2;
            else if (fq < 3000.0) eMid += p2;
            else eHigh += p2;
            if (fq >= 200.0 && fq <= 3000.0) wb += p2;
        }
        wob.push_back(wb);
        frameMag.push_back(std::sqrt(fe));
        ++frames;
        if (start + FFT > n) break;
    }

    f.centroidHz = float(cDen > 0 ? cNum / cDen : 0.0);
    f.brightness = clampf(f.centroidHz / 6000.0f, 0.0f, 1.0f);
    double eTot = eSub + eLow + eMid + eHigh + 1e-12;
    f.subRatio = float(eSub / eTot);
    float highRatio = float(eHigh / eTot);
    float midHigh = float((eMid + eHigh) / eTot);
    f.darkness = clampf(1.0f - f.centroidHz / 4000.0f, 0.0f, 1.0f);

    // aggression: mid-high energy + roughness (zcr) + inverse crest.
    float invCrest = clampf((16.0f - f.crestDb) / 12.0f, 0.0f, 1.0f);
    float rough = clampf(zcr * 6.0f, 0.0f, 1.0f);
    f.aggression = clampf(0.5f * midHigh + 0.28f * rough + 0.22f * invCrest, 0.0f, 1.0f);
    (void)highRatio;

    // movement: normalized variance (coeff of variation) of the wobble band.
    if (wob.size() >= 2) {
        double mean = 0; for (double x : wob) mean += x; mean /= wob.size();
        double var = 0; for (double x : wob) { double d = x - mean; var += d * d; }
        var /= wob.size();
        double cv = (mean > 1e-12) ? std::sqrt(var) / mean : 0.0;
        f.movement = clampf(float(cv) * 1.25f, 0.0f, 1.0f);
    }

    // transient: largest frame-to-frame energy rise (implicit 0 before frame 0).
    if (!frameMag.empty()) {
        double maxV = 0; for (double x : frameMag) maxV = std::max(maxV, x);
        double maxRise = frameMag[0]; // 0 -> first frame
        for (size_t i = 1; i < frameMag.size(); ++i)
            maxRise = std::max(maxRise, frameMag[i] - frameMag[i - 1]);
        f.transient = clampf(float(maxRise / (maxV + 1e-12)), 0.0f, 1.0f);
    }
    return f;
}

// ------------------------------------------------------------------ rating helpers
static float rampUp(float x, float a, float b) {
    if (b <= a) return x >= b ? 1.0f : 0.0f;
    if (x <= a) return 0.0f; if (x >= b) return 1.0f;
    return (x - a) / (b - a);
}
static float rampDown(float x, float a, float b) { return 1.0f - rampUp(x, a, b); }
// plateau 1 in [lo,hi], ramping over `s` on each side.
static float band(float x, float lo, float hi, float s) {
    return std::min(rampUp(x, lo - s, lo), rampDown(x, hi, hi + s));
}

struct Term { float v; float w; };
static float blend(std::initializer_list<Term> terms) {
    float sw = 0, acc = 0;
    for (auto& t : terms) { acc += t.v * t.w; sw += t.w; }
    float avg = sw > 0 ? acc / sw : 0.0f;
    return clampf(0.25f + 0.75f * avg, 0.0f, 1.0f); // generous baseline
}

// ------------------------------------------------------------------ rate
float rate(Role role, const Features& f) {
    // Hard gates.
    if (std::isnan(f.centroidHz) || std::isnan(f.crestDb) ||
        std::isnan(f.movement) || std::isnan(f.subRatio) || std::isnan(f.transient))
        return 0.0f;
    if (f.centroidHz <= 1.0f) return 0.0f;            // silence / DC sentinel
    if (f.crestDb < 0.5f || f.crestDb > 60.0f) return 0.0f;

    const float c = f.centroidHz;
    switch (role) {
        case Role::Growl:
            return blend({
                {rampUp(f.movement, 0.12f, 0.35f), 1.0f},
                {band(c, 300.0f, 1800.0f, 300.0f), 1.0f},
                {rampDown(f.subRatio, 0.40f, 0.65f), 0.8f},
                {band(f.crestDb, 6.0f, 14.0f, 4.0f), 0.7f},
            });
        case Role::Screech:
            return blend({
                {band(c, 1500.0f, 6000.0f, 1200.0f), 1.0f},
                {rampDown(f.subRatio, 0.25f, 0.5f), 0.8f},
                {rampUp(f.movement, 0.1f, 0.4f), 0.5f},
                {band(f.crestDb, 5.0f, 16.0f, 5.0f), 0.5f},
            });
        case Role::Sub:
            return blend({
                {rampUp(f.subRatio, 0.6f, 0.9f), 1.2f},
                {rampDown(f.movement, 0.15f, 0.4f), 0.8f},
                {rampDown(c, 200.0f, 600.0f), 0.8f},
            });
        case Role::Bass808:
            return blend({
                {band(f.subRatio, 0.45f, 0.9f, 0.2f), 1.0f},
                {rampDown(c, 250.0f, 900.0f), 0.8f},
                {band(f.crestDb, 6.0f, 20.0f, 5.0f), 0.6f},
            });
        case Role::Kick:
            return blend({
                {rampUp(f.transient, 0.4f, 0.7f), 1.1f},
                {band(f.subRatio, 0.3f, 0.7f, 0.2f), 1.0f},
                {rampUp(f.crestDb, 6.0f, 12.0f), 0.6f},
            });
        case Role::Snare:
            return blend({
                {rampUp(f.transient, 0.4f, 0.7f), 1.0f},
                {band(c, 800.0f, 4000.0f, 700.0f), 0.9f},
                {rampDown(f.subRatio, 0.3f, 0.55f), 0.7f},
                {rampUp(f.crestDb, 6.0f, 12.0f), 0.5f},
            });
        case Role::HatClosed:
            return blend({
                {rampUp(f.brightness, 0.5f, 0.85f), 1.1f},
                {rampUp(f.transient, 0.4f, 0.7f), 0.9f},
                {rampDown(f.subRatio, 0.1f, 0.3f), 0.8f},
            });
        case Role::HatOpen:
            return blend({
                {rampUp(f.brightness, 0.5f, 0.85f), 1.1f},
                {rampDown(f.subRatio, 0.1f, 0.3f), 0.9f},
                {rampDown(f.movement, 0.3f, 0.7f), 0.4f},
            });
        case Role::Perc:
            return blend({
                {rampUp(f.transient, 0.35f, 0.65f), 1.0f},
                {band(c, 500.0f, 4000.0f, 800.0f), 0.9f},
                {rampDown(f.subRatio, 0.2f, 0.5f), 0.6f},
            });
        case Role::MelodyLead:
            return blend({
                {band(c, 500.0f, 4500.0f, 800.0f), 1.0f},
                {rampUp(f.transient, 0.2f, 0.6f), 0.6f},
                {rampDown(f.subRatio, 0.25f, 0.55f), 0.7f},
            });
        case Role::Pad:
            return blend({
                {band(c, 400.0f, 3500.0f, 700.0f), 1.0f},
                {rampDown(f.subRatio, 0.3f, 0.6f), 0.7f},
                {rampDown(f.transient, 0.4f, 0.8f), 0.6f},
            });
        case Role::Riser:
            return blend({
                {rampDown(f.subRatio, 0.3f, 0.6f), 0.8f},
                {rampUp(f.brightness, 0.15f, 0.6f), 0.8f},
                {band(f.crestDb, 5.0f, 22.0f, 6.0f), 0.5f},
            });
        case Role::Downlifter:
            return blend({
                {rampDown(f.subRatio, 0.35f, 0.7f), 0.7f},
                {band(c, 200.0f, 4000.0f, 800.0f), 0.7f},
                {band(f.crestDb, 5.0f, 22.0f, 6.0f), 0.5f},
            });
        case Role::Impact:
            return blend({
                {rampUp(f.transient, 0.35f, 0.7f), 0.9f},
                {band(f.subRatio, 0.25f, 0.75f, 0.25f), 1.0f},
                {rampUp(f.crestDb, 6.0f, 14.0f), 0.6f},
            });
        case Role::Crash:
            return blend({
                {rampUp(f.brightness, 0.6f, 0.95f), 1.0f},
                {rampDown(f.subRatio, 0.1f, 0.3f), 0.9f},
                {rampUp(f.durationSec, 0.8f, 1.5f), 0.5f},
            });
        default:
            return 0.5f;
    }
}

} // namespace rtg::synth
