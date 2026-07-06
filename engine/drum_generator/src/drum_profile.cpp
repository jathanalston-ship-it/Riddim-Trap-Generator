// Drum Reference Profile: baked medians, tolerant flat-JSON I/O, the process-
// wide active registry, and the reference extractor. Dependency-free (no JSON
// library, no JUCE) — a self-contained RBJ biquad + time-domain onset detector
// keep rtg_core self-sufficient (same philosophy as calibration.cpp).
#include "rtg/drums/drum_profile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
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
Biquad makeHP(double fs, double f0, double Q = 0.70710678) {
    Biquad bq; f0 = std::min(std::max(f0, 3.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2 * Q);
    bq.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    return bq;
}
Biquad makeLP(double fs, double f0, double Q = 0.70710678) {
    Biquad bq; f0 = std::min(std::max(f0, 3.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2 * Q);
    bq.set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    return bq;
}
Biquad makeBP(double fs, double f0, double Q) {   // constant skirt-gain BP
    Biquad bq; f0 = std::min(std::max(f0, 3.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2 * Q);
    bq.set(al, 0.0, -al, 1 + al, -2 * c, 1 - al);
    return bq;
}

std::vector<float> monoSum(const StereoBuffer& a) {
    std::vector<float> m(a.size());
    for (size_t i = 0; i < a.size(); ++i) m[i] = 0.5f * (a.l[i] + a.r[i]);
    return m;
}

// Band-limit via HP∘LP (2nd order each). lo<=20 skips HP; hi>=nyq skips LP.
std::vector<float> bandLimit(const std::vector<float>& mono, double sr, double lo, double hi) {
    std::vector<float> x = mono;
    if (lo > 20.001) { Biquad hp = makeHP(sr, lo); for (auto& s : x) s = hp.process(s); }
    if (hi < sr * 0.49) { Biquad lp = makeLP(sr, hi); for (auto& s : x) s = lp.process(s); }
    return x;
}

// Rectified one-pole envelope follower (attack≈release) of a signal.
std::vector<float> envelope(const std::vector<float>& x, double sr, double smoothMs) {
    std::vector<float> e(x.size());
    double a = std::exp(-1.0 / std::max(1.0, smoothMs * 0.001 * sr));
    double y = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double r = std::fabs(double(x[i]));
        y = a * y + (1.0 - a) * r;
        e[i] = float(y);
    }
    return e;
}

// Peak-pick onsets from a full-rate envelope. Returns onset sample indices.
// Frame-hopped spectral-flux-style: positive envelope increase that is a local
// max and clears a LOCAL adaptive threshold, respecting a minimum inter-onset
// gap. The threshold is local (trailing mean of flux) rather than a fraction of
// the global maximum, so one very loud transient elsewhere in the band cannot
// suppress detection of the rest — detection in one band stays robust to level
// changes in another (e.g. a hotter kick click must not hide snares).
std::vector<size_t> pickOnsets(const std::vector<float>& env, double sr,
                               double minGapSec, double threshMult) {
    std::vector<size_t> onsets;
    const size_t n = env.size();
    if (n < 4) return onsets;
    const size_t hop = std::max<size_t>(1, size_t(std::llround(sr * 0.003))); // ~3 ms
    // Frame maxima.
    std::vector<float> f;
    std::vector<size_t> fpos;
    for (size_t s = 0; s + hop <= n; s += hop) {
        float m = 0.0f;
        for (size_t i = 0; i < hop; ++i) m = std::max(m, env[s + i]);
        f.push_back(m);
        fpos.push_back(s);
    }
    const size_t nf = f.size();
    if (nf < 3) return onsets;
    // Positive flux.
    std::vector<float> flux(nf, 0.0f);
    double meanFlux = 0.0; float gmax = 0.0f;
    for (size_t i = 1; i < nf; ++i) {
        flux[i] = std::max(0.0f, f[i] - f[i - 1]);
        meanFlux += flux[i]; gmax = std::max(gmax, flux[i]);
    }
    if (gmax <= 1e-12f) return onsets;
    meanFlux /= double(nf);
    // Local threshold: trailing-window mean of flux, times a sensitivity mult,
    // with a small global floor to reject noise-floor ripple.
    const size_t wf = std::max<size_t>(4, size_t(std::llround(0.35 / (double(hop) / sr))));
    const float floor = 0.05f * float(meanFlux) + 1e-9f;
    const size_t gapFrames = std::max<size_t>(1, size_t(std::llround(minGapSec / (double(hop) / sr))));
    size_t last = 0; bool have = false;
    double run = 0.0; size_t runCnt = 0; size_t head = 0;
    for (size_t i = 1; i + 1 < nf; ++i) {
        // advance trailing window [i-wf, i)
        while (head < i) { run += flux[head]; ++runCnt; ++head; }
        if (i > wf) { run -= flux[i - wf - 1]; --runCnt; }
        double localMean = runCnt ? run / double(runCnt) : meanFlux;
        float thr = std::max(floor, float(threshMult * localMean));
        if (flux[i] < thr) continue;
        if (flux[i] < flux[i - 1] || flux[i] < flux[i + 1]) continue; // local max
        if (have && (i - last) < gapFrames) {
            if (flux[i] > flux[last]) { last = i; onsets.back() = fpos[i]; } // keep stronger
            continue;
        }
        onsets.push_back(fpos[i]);
        last = i; have = true;
    }
    return onsets;
}

// Dominant frequency of a segment via zero-crossing rate (robust for a single
// dominant partial). Counts sign changes where the signal clears a small gate.
double dominantHzZCR(const std::vector<float>& x, size_t s, size_t e, double sr) {
    if (e <= s + 4) return 0.0;
    double peak = 0.0;
    for (size_t i = s; i < e; ++i) peak = std::max(peak, std::fabs(double(x[i])));
    if (peak < 1e-7) return 0.0;
    const double gate = peak * 0.15;
    int crossings = 0; int lastSign = 0;
    for (size_t i = s; i < e; ++i) {
        double v = x[i];
        int sign = (v > gate) ? 1 : (v < -gate ? -1 : 0);
        if (sign != 0) {
            if (lastSign != 0 && sign != lastSign) ++crossings;
            lastSign = sign;
        }
    }
    double T = double(e - s) / sr;
    if (T <= 0.0) return 0.0;
    return 0.5 * double(crossings) / T;
}

// Envelope decay time (s): from the segment peak down to frac*peak.
double decayTime(const std::vector<float>& env, size_t s, size_t e, double sr, double frac) {
    if (e <= s + 2) return 0.0;
    size_t pk = s; float pv = env[s];
    for (size_t i = s; i < e; ++i) if (env[i] > pv) { pv = env[i]; pk = i; }
    if (pv < 1e-7f) return 0.0;
    float target = pv * float(frac);
    for (size_t i = pk; i < e; ++i)
        if (env[i] <= target) return double(i - pk) / sr;
    return double(e - pk) / sr;
}

double energySq(const std::vector<float>& x, size_t s, size_t e) {
    double acc = 0.0;
    for (size_t i = s; i < e && i < x.size(); ++i) acc += double(x[i]) * x[i];
    return acc;
}

float medianOf(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    size_t m = v.size() / 2;
    return (v.size() & 1) ? v[m] : 0.5f * (v[m - 1] + v[m]);
}

// Third-octave filterbank energies over [s,e) of a mono segment across a set of
// centre frequencies. Used for spectral flatness (tonality) and centroid.
std::vector<double> filterbankEnergy(const std::vector<float>& mono, double sr,
                                     size_t s, size_t e, const std::vector<double>& fcs) {
    std::vector<double> out(fcs.size(), 0.0);
    e = std::min(e, mono.size());
    if (e <= s) return out;
    for (size_t b = 0; b < fcs.size(); ++b) {
        Biquad bp = makeBP(sr, fcs[b], 4.0);
        double acc = 0.0;
        for (size_t i = s; i < e; ++i) { float y = bp.process(mono[i]); acc += double(y) * y; }
        out[b] = acc / double(e - s);
    }
    return out;
}

} // namespace

// ===========================================================================
// extractDrumProfile
// ===========================================================================
DrumProfile extractDrumProfile(const StereoBuffer& audio, double sr, double bpmHint,
                               DrumProfileStats* stats) {
    DrumProfile prof = DrumProfile::builtinReference();  // sensible fallbacks
    prof.refCount = 1;
    const size_t total = audio.size();
    if (total < size_t(sr * 2.0)) return prof;   // too short to analyze

    std::vector<float> monoFull = monoSum(audio);

    // ---- Loudest ~60 s window (1 s blocks) --------------------------------
    const size_t win = std::min<size_t>(total, size_t(sr * 60.0));
    size_t winStart = 0;
    {
        const size_t blk = std::max<size_t>(1, size_t(sr));
        std::vector<double> be;
        for (size_t s = 0; s + blk <= total; s += blk) {
            double e = 0.0; for (size_t i = 0; i < blk; ++i) { double v = monoFull[s + i]; e += v * v; }
            be.push_back(e);
        }
        const size_t nblk = std::max<size_t>(1, win / blk);
        if (be.size() > nblk) {
            double run = 0.0; for (size_t i = 0; i < nblk; ++i) run += be[i];
            double best = run; size_t bestBlk = 0;
            for (size_t i = nblk; i < be.size(); ++i) {
                run += be[i] - be[i - nblk];
                if (run > best) { best = run; bestBlk = i - nblk + 1; }
            }
            winStart = bestBlk * blk;
        }
    }
    const size_t W = std::min(win, total - winStart);
    std::vector<float> mono(monoFull.begin() + winStart, monoFull.begin() + winStart + W);

    // ---- Band signals + envelopes -----------------------------------------
    std::vector<float> xFull  = bandLimit(mono, sr, 30.0, 16000.0);
    std::vector<float> xSub   = bandLimit(mono, sr, 30.0, 120.0);
    std::vector<float> xLow   = bandLimit(mono, sr, 45.0, 100.0);   // kick-gate sub attack
    std::vector<float> xSnare = bandLimit(mono, sr, 125.0, 260.0);  // snare body
    std::vector<float> xCrack = bandLimit(mono, sr, 2000.0, 5000.0);
    std::vector<float> xBroad = bandLimit(mono, sr, 2000.0, 8000.0);// kick-gate transient
    std::vector<float> xHat   = bandLimit(mono, sr, 6500.0, 14000.0);

    std::vector<float> eLow   = envelope(xLow,   sr, 4.0);
    std::vector<float> eBroad = envelope(xBroad, sr, 3.0);
    std::vector<float> eCrack = envelope(xCrack, sr, 3.0);
    std::vector<float> eHat   = envelope(xHat,   sr, 2.5);
    std::vector<float> eSnare = envelope(xSnare, sr, 4.0);
    std::vector<float> eSub   = envelope(xSub,   sr, 4.0);

    // ---- Tempo -------------------------------------------------------------
    double bpm = bpmHint;
    if (!(bpm > 0.0)) {
        // Onset strength = summed positive flux of broad + hat envelopes on a
        // ~5 ms frame grid, then autocorrelate over the 120..160 BPM beat band.
        const size_t hop = std::max<size_t>(1, size_t(sr * 0.005));
        std::vector<double> os;
        float pb = 0, ph = 0;
        for (size_t s = 0; s + hop <= W; s += hop) {
            float mb = 0, mh = 0;
            for (size_t i = 0; i < hop; ++i) { mb = std::max(mb, eBroad[s + i]); mh = std::max(mh, eHat[s + i]); }
            os.push_back(std::max(0.0f, mb - pb) + std::max(0.0f, mh - ph));
            pb = mb; ph = mh;
        }
        const double frameSec = double(hop) / sr;
        double bestLagSec = 60.0 / 142.0, bestScore = -1.0;
        for (double beatSec = 60.0 / 160.0; beatSec <= 60.0 / 120.0; beatSec += 0.002) {
            int lag = int(std::llround(beatSec / frameSec));
            if (lag < 2 || lag >= int(os.size())) continue;
            double acc = 0.0; int cnt = 0;
            for (int i = lag; i < int(os.size()); ++i) { acc += os[i] * os[i - lag]; ++cnt; }
            if (cnt == 0) continue;
            double score = acc / cnt;
            if (score > bestScore) { bestScore = score; bestLagSec = beatSec; }
        }
        bpm = 60.0 / bestLagSec;
    }
    const double beatSec = 60.0 / bpm;
    const double winBeats = double(W) / sr / beatSec;

    // ---- Onsets ------------------------------------------------------------
    std::vector<size_t> onLow   = pickOnsets(eLow,   sr, 0.090, 3.8);
    std::vector<size_t> onBroad = pickOnsets(eBroad, sr, 0.070, 3.6);
    std::vector<size_t> onCrack = pickOnsets(eCrack, sr, 0.170, 4.2);
    std::vector<size_t> onHat   = pickOnsets(eHat,   sr, 0.050, 3.0);

    // ---- KICK: low-band onset WITH coincident 2..8 kHz transient (±12 ms) --
    const size_t tol = size_t(sr * 0.012);
    std::vector<size_t> kicks;   // anchored at the click transient (broad onset)
    for (size_t lo : onLow) {
        size_t bestBr = 0; bool broadNear = false;
        for (size_t br : onBroad) {
            size_t d = (br > lo) ? (br - lo) : (lo - br);
            if (d <= tol) { broadNear = true; bestBr = br; break; }
        }
        if (broadNear) {
            // Anchor ON the click transient (broad onset) so the click-share
            // window captures the beater click rather than a misaligned sub/bass
            // onset (in a full mix the coincident bass onset can precede it).
            if (!kicks.empty() && bestBr > kicks.back() &&
                bestBr - kicks.back() < size_t(sr * 0.12)) continue;
            if (!kicks.empty() && bestBr <= kicks.back()) continue;   // keep ascending
            kicks.push_back(bestBr);
        }
    }

    // ---- Per-kick measurements --------------------------------------------
    std::vector<float> vBody, vDecay, vClick, vSub;
    for (size_t k = 0; k < kicks.size(); ++k) {
        size_t s = kicks[k];
        size_t next = (k + 1 < kicks.size()) ? kicks[k + 1] : W;
        size_t e = std::min<size_t>({ s + size_t(sr * 0.28), next, W });
        if (e <= s + size_t(sr * 0.02)) continue;
        // Body freq over the settled tail (skip first 15 ms of pitch glide).
        size_t bodyS = std::min(e, s + size_t(sr * 0.010));
        double hz = dominantHzZCR(xSub, bodyS, e, sr);
        if (hz > 30.0 && hz < 130.0) vBody.push_back(float(hz));
        vDecay.push_back(float(decayTime(eLow, s, e, sr, 0.25) * 1000.0));
        // Click/knock share = 2-8 kHz energy fraction over the CLICK TRANSIENT
        // window (~14 ms): the beater click is a sharp 2-8 kHz burst up front,
        // while the ~50 Hz sub has barely completed a cycle this early, so the
        // short window best isolates the kick's own click.
        size_t shC = std::min(e, s + size_t(sr * 0.014));
        double denomC = energySq(xFull, s, shC);
        if (denomC > 1e-12) vClick.push_back(float(energySq(xBroad, s, shC) / denomC));
        // Sub share reflects the sustained body -> longer 60 ms window.
        size_t shS = std::min(e, s + size_t(sr * 0.060));
        double denomS = energySq(xFull, s, shS);
        if (denomS > 1e-12) vSub.push_back(float(energySq(xSub, s, shS) / denomS));
    }

    // ---- SNARE: crack-band onsets with strong 100..260 Hz body, not a kick -
    std::vector<size_t> snares;
    for (size_t c : onCrack) {
        bool isKick = false;
        for (size_t kk : kicks) { size_t d = (kk > c) ? kk - c : c - kk; if (d <= tol) { isKick = true; break; } }
        if (isKick) continue;
        // Snare = crack transient whose 100..260 Hz BODY dominates the hat band
        // (rejects hats/shakers) AND rings at a plausible snare fundamental
        // (rejects toms/congas/vocal-mid percussion that skews the median).
        size_t bs = (c > size_t(sr * 0.005)) ? c - size_t(sr * 0.005) : 0;
        size_t be = std::min(W, c + size_t(sr * 0.045));
        double bodyE = energySq(xSnare, bs, be);
        double hatE  = energySq(xHat, bs, be);
        if (bodyE < 1e-9 || bodyE < hatE) continue;
        double hz = dominantHzZCR(xSnare, std::min(be, c + size_t(sr * 0.006)), be, sr);
        if (hz < 125.0 || hz > 205.0) continue;
        snares.push_back(c);
    }

    std::vector<float> vSnBody, vCrackShare, vCrackDecay, vTonal;
    const std::vector<double> flatFcs = { 1000, 1260, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000 };
    for (size_t k = 0; k < snares.size(); ++k) {
        size_t s = snares[k];
        size_t e = std::min<size_t>(s + size_t(sr * 0.20), W);
        if (e <= s + size_t(sr * 0.02)) continue;
        size_t bodyS = std::min(e, s + size_t(sr * 0.006));
        size_t bodyE = std::min(e, s + size_t(sr * 0.050));   // early body, pre-ringdown
        double hz = dominantHzZCR(xSnare, bodyS, bodyE, sr);
        if (hz > 100.0 && hz < 280.0) vSnBody.push_back(float(hz));
        double denom = energySq(xFull, s, e);
        if (denom > 1e-12) vCrackShare.push_back(float(energySq(xCrack, s, e) / denom));
        vCrackDecay.push_back(float(decayTime(eCrack, s, e, sr, 0.25) * 1000.0));
        // Tonality = spectral flatness of 1..8 kHz (geo mean / arith mean).
        std::vector<double> be = filterbankEnergy(mono, sr, s, e, flatFcs);
        double logSum = 0.0, lin = 0.0; int cnt = 0;
        for (double v : be) { if (v > 1e-15) { logSum += std::log(v); lin += v; ++cnt; } }
        if (cnt > 0) {
            double geo = std::exp(logSum / cnt);
            double ar = lin / cnt;
            if (ar > 1e-15) vTonal.push_back(float(geo / ar));
        }
    }

    // ---- HATS --------------------------------------------------------------
    std::vector<float> vHatDecay;
    for (size_t k = 0; k < onHat.size(); ++k) {
        size_t s = onHat[k];
        size_t next = (k + 1 < onHat.size()) ? onHat[k + 1] : W;
        size_t e = std::min<size_t>({ s + size_t(sr * 0.16), next, W });
        vHatDecay.push_back(float(decayTime(eHat, s, e, sr, 0.25) * 1000.0));
    }
    // Hat-band spectral centroid over the whole window (energy-weighted).
    double hatCentroid = 8800.0;
    {
        const std::vector<double> hf = { 5500, 6500, 7500, 8500, 9500, 11000, 13000 };
        std::vector<double> be = filterbankEnergy(mono, sr, 0, W, hf);
        double num = 0.0, den = 0.0;
        for (size_t b = 0; b < hf.size(); ++b) { num += hf[b] * be[b]; den += be[b]; }
        if (den > 1e-15) hatCentroid = num / den;
    }

    // ---- Fold into the profile (median per field) --------------------------
    if (!vBody.empty())       prof.kickBodyHz      = medianOf(vBody);
    if (!vDecay.empty())      prof.kickDecayMs     = medianOf(vDecay);
    if (!vClick.empty())      prof.kickClickShare  = medianOf(vClick);
    if (!vSub.empty())        prof.kickSubShare    = medianOf(vSub);
    if (!vSnBody.empty())     prof.snareBodyHz     = medianOf(vSnBody);
    if (!vCrackShare.empty()) prof.snareCrackShare = medianOf(vCrackShare);
    if (!vCrackDecay.empty()) prof.snareCrackDecayMs = medianOf(vCrackDecay);
    if (!vTonal.empty())      prof.snareTonality   = medianOf(vTonal);
    if (winBeats > 1.0)       prof.hatDensityPerBeat = float(double(onHat.size()) / winBeats);
    if (!vHatDecay.empty())   prof.hatDecayMs      = medianOf(vHatDecay);
    prof.hatCentroidHz = float(hatCentroid);

    if (stats) {
        stats->kickCount   = int(kicks.size());
        stats->snareCount  = int(snares.size());
        stats->hatCount    = int(onHat.size());
        stats->bpm         = bpm;
        stats->windowSec   = double(W) / sr;
        stats->windowBeats = winBeats;
    }
    return prof;
}

// ===========================================================================
// builtinReference — baked medians from the two commercial riddim references
// ===========================================================================
const DrumProfile& DrumProfile::builtinReference() {
    static const DrumProfile p = [] {
        DrumProfile d;
        d.kickBodyHz        = 55.0f;
        d.kickDecayMs       = 90.0f;
        d.kickClickShare    = 0.055f;
        d.kickSubShare      = 0.71f;
        d.snareBodyHz       = 145.0f;
        d.snareCrackShare   = 0.05f;
        d.snareCrackDecayMs = 110.0f;
        d.snareTonality     = 0.25f;
        d.hatDensityPerBeat = 2.8f;
        d.hatDecayMs        = 78.0f;
        d.hatCentroidHz     = 8800.0f;
        d.refCount          = 2;
        return d;
    }();
    return p;
}

// ===========================================================================
// active registry
// ===========================================================================
namespace {
std::optional<DrumProfile>& activeSlot() {
    static std::optional<DrumProfile> g;   // set before generation, read during
    return g;
}
} // namespace

void DrumProfile::setActive(std::optional<DrumProfile> p) { activeSlot() = std::move(p); }
const DrumProfile& DrumProfile::active() {
    const auto& s = activeSlot();
    return s ? *s : builtinReference();
}

// ===========================================================================
// JSON I/O (hand-rolled, tolerant flat object)
// ===========================================================================
namespace {

std::map<std::string, double> scanFlat(const std::string& text) {
    std::map<std::string, double> out;
    size_t i = 0, n = text.size();
    while (i < n) {
        if (text[i] != '"') { ++i; continue; }
        size_t ks = ++i;
        while (i < n && text[i] != '"') ++i;
        if (i >= n) break;
        std::string key = text.substr(ks, i - ks);
        ++i;
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        if (i >= n || text[i] != ':') continue;
        ++i;
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        size_t vs = i;
        while (i < n) {
            char c = text[i];
            if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') ++i;
            else break;
        }
        if (i > vs) { try { out[key] = std::stod(text.substr(vs, i - vs)); } catch (...) {} }
    }
    return out;
}

} // namespace

bool DrumProfile::saveToFile(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": 1,\n";
    auto kv = [&](const char* k, double v) { os << "  \"" << k << "\": " << v << ",\n"; };
    kv("kickBodyHz", kickBodyHz);
    kv("kickDecayMs", kickDecayMs);
    kv("kickClickShare", kickClickShare);
    kv("kickSubShare", kickSubShare);
    kv("snareBodyHz", snareBodyHz);
    kv("snareCrackShare", snareCrackShare);
    kv("snareCrackDecayMs", snareCrackDecayMs);
    kv("snareTonality", snareTonality);
    kv("hatDensityPerBeat", hatDensityPerBeat);
    kv("hatDecayMs", hatDecayMs);
    kv("hatCentroidHz", hatCentroidHz);
    os << "  \"refCount\": " << refCount << "\n";
    os << "}\n";
    std::string s = os.str();
    size_t w = std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    return w == s.size();
}

bool DrumProfile::loadFromFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text; char buf[4096]; size_t r;
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, r);
    std::fclose(f);
    std::map<std::string, double> m = scanFlat(text);
    auto g = [&](const char* k, float def) -> float {
        auto it = m.find(k); return it != m.end() ? float(it->second) : def;
    };
    kickBodyHz        = g("kickBodyHz", kickBodyHz);
    kickDecayMs       = g("kickDecayMs", kickDecayMs);
    kickClickShare    = g("kickClickShare", kickClickShare);
    kickSubShare      = g("kickSubShare", kickSubShare);
    snareBodyHz       = g("snareBodyHz", snareBodyHz);
    snareCrackShare   = g("snareCrackShare", snareCrackShare);
    snareCrackDecayMs = g("snareCrackDecayMs", snareCrackDecayMs);
    snareTonality     = g("snareTonality", snareTonality);
    hatDensityPerBeat = g("hatDensityPerBeat", hatDensityPerBeat);
    hatDecayMs        = g("hatDecayMs", hatDecayMs);
    hatCentroidHz     = g("hatCentroidHz", hatCentroidHz);
    refCount          = int(std::lround(g("refCount", float(refCount))));
    return true;
}

} // namespace rtg
