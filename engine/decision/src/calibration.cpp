// Reference Calibration: measurement primitives, tolerant flat-JSON I/O, and
// the process-wide active registry. Dependency-free (no JSON lib, no JUCE).
#include "rtg/decision/calibration.h"
#include "rtg/master/master_engine.h"   // measureLufs (integrated loudness)

#include <algorithm>
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

// ---- minimal RBJ biquad (transposed DF-II), local so calibration.cpp stays
// self-contained (mixdsp.h is a private mix-engine header). ------------------
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
    Biquad bq; f0 = std::min(std::max(f0, 5.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2 * Q);
    bq.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    return bq;
}
Biquad makeLP(double fs, double f0, double Q = 0.70710678) {
    Biquad bq; f0 = std::min(std::max(f0, 5.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0), al = s / (2 * Q);
    bq.set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    return bq;
}

std::vector<float> monoSum(const StereoBuffer& a) {
    std::vector<float> m(a.size());
    for (size_t i = 0; i < a.size(); ++i) m[i] = 0.5f * (a.l[i] + a.r[i]);
    return m;
}

std::vector<float> bandLimit(const std::vector<float>& mono, double sr, double lo, double hi) {
    std::vector<float> x = mono;
    if (lo > 20.001) { Biquad hp = makeHP(sr, lo); for (auto& s : x) s = hp.process(s); }
    if (hi < sr * 0.49) { Biquad lp = makeLP(sr, hi); for (auto& s : x) s = lp.process(s); }
    return x;
}

// Per-block (400 ms, non-overlapping) energy of a channel; also returns which
// block indices are the loudest 25% (by broadband energy of `mono`).
struct BlockGrid {
    size_t blk = 0;
    std::vector<size_t> starts;      // block start samples
    std::vector<size_t> loudTop25;   // indices into starts, loudest 25%
};

BlockGrid makeGrid(const std::vector<float>& mono, double sr) {
    BlockGrid g;
    const size_t n = mono.size();
    g.blk = std::max<size_t>(1, size_t(0.400 * sr));
    std::vector<double> energy;
    for (size_t s = 0; s + g.blk <= n; s += g.blk) {
        double e = 0.0;
        for (size_t i = 0; i < g.blk; ++i) { double v = mono[s + i]; e += v * v; }
        g.starts.push_back(s);
        energy.push_back(e);
    }
    if (g.starts.empty()) {                 // signal shorter than one block
        g.blk = std::max<size_t>(1, n);
        g.starts.push_back(0);
        double e = 0.0; for (float v : mono) e += double(v) * v;
        energy.push_back(e);
    }
    std::vector<size_t> order(g.starts.size());
    std::iota(order.begin(), order.end(), size_t(0));
    // NaN-safe descending sort (strict weak ordering; NaN energies sort last).
    // A raw `energy[a] > energy[b]` comparator is UB if any energy is NaN
    // (introsort walks past the array end -> crash); callers should feed finite
    // audio, but harden here too since this is the flagged crash site.
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        double ea = energy[a], eb = energy[b];
        bool na = std::isnan(ea), nb = std::isnan(eb);
        if (na) return false;      // a is NaN -> never before b
        if (nb) return true;       // b is NaN -> a before b
        return ea > eb;
    });
    size_t take = std::max<size_t>(1, order.size() / 4);
    g.loudTop25.assign(order.begin(), order.begin() + take);
    return g;
}

} // namespace

// ---------------------------------------------------------------------------
// measureBandShares
// ---------------------------------------------------------------------------
std::array<float, kCalBands> measureBandShares(const StereoBuffer& audio, double sr) {
    std::array<float, kCalBands> shares{};
    shares.fill(1.0f / kCalBands);
    const size_t n = audio.size();
    if (n == 0) return shares;

    std::vector<float> mono = monoSum(audio);
    std::array<std::vector<float>, kCalBands> band;
    for (int b = 0; b < kCalBands; ++b)
        band[b] = bandLimit(mono, sr, kCalBandEdgesHz[b], kCalBandEdgesHz[b + 1]);

    BlockGrid g = makeGrid(mono, sr);
    std::array<double, kCalBands> sum{}; sum.fill(0.0);
    for (size_t bi : g.loudTop25) {
        size_t s = g.starts[bi];
        size_t end = std::min(n, s + g.blk);
        for (int b = 0; b < kCalBands; ++b)
            for (size_t i = s; i < end; ++i) { double v = band[b][i]; sum[b] += v * v; }
    }
    double grand = std::accumulate(sum.begin(), sum.end(), 0.0);
    if (grand <= 1e-20) return shares;   // silence -> flat default
    for (int b = 0; b < kCalBands; ++b) shares[b] = float(sum[b] / grand);
    return shares;
}

// ---------------------------------------------------------------------------
// measureCrestDb (over the loudest 25% blocks)
// ---------------------------------------------------------------------------
float measureCrestDb(const StereoBuffer& audio, double sr) {
    const size_t n = audio.size();
    if (n == 0) return 0.0f;
    std::vector<float> mono = monoSum(audio);
    BlockGrid g = makeGrid(mono, sr);
    double sq = 0.0; double peak = 0.0; size_t cnt = 0;
    for (size_t bi : g.loudTop25) {
        size_t s = g.starts[bi];
        size_t end = std::min(n, s + g.blk);
        for (size_t i = s; i < end; ++i) {
            double a = std::max(std::fabs(audio.l[i]), std::fabs(audio.r[i]));
            peak = std::max(peak, a);
            sq += 0.5 * (double(audio.l[i]) * audio.l[i] + double(audio.r[i]) * audio.r[i]);
            ++cnt;
        }
    }
    if (cnt == 0) return 0.0f;
    double rms = std::sqrt(sq / double(cnt));
    return float(20.0 * std::log10(std::max(peak, 1e-9) / std::max(rms, 1e-9)));
}

// ---------------------------------------------------------------------------
// measureContrastLu : K-weighted short-term loudness spread
// ---------------------------------------------------------------------------
float measureContrastLu(const StereoBuffer& audio, double sr) {
    const size_t n = audio.size();
    if (n == 0) return 0.0f;

    // K-weighting (BS.1770 constants at 48 kHz; approximation otherwise).
    Biquad hpL, shL;
    if (std::abs(sr - 48000.0) < 1.0) {
        shL.set(1.53512485958697, -2.69169618940638, 1.19839281085285,
                1.0, -1.69065929318241, 0.73248077421585);
        hpL.set(1.0, -2.0, 1.0, 1.0, -1.99004745483398, 0.99007225036621);
    } else {
        hpL = makeHP(sr, 38.0, 0.5);
        // rough high-shelf +4 dB via a resonant HP is not exact; fall back to HP only.
        shL.set(1, 0, 0, 1, 0, 0);
    }
    Biquad hpR = hpL, shR = shL;
    std::vector<float> kl(n), kr(n);
    for (size_t i = 0; i < n; ++i) {
        kl[i] = shL.process(hpL.process(audio.l[i]));
        kr[i] = shR.process(hpR.process(audio.r[i]));
    }
    const size_t block = std::max<size_t>(1, size_t(0.400 * sr));
    const size_t hop = std::max<size_t>(1, size_t(0.100 * sr));
    std::vector<double> loud;   // per-block LUFS-like
    for (size_t s = 0; s + block <= n; s += hop) {
        double acc = 0.0;
        for (size_t i = 0; i < block; ++i)
            acc += double(kl[s + i]) * kl[s + i] + double(kr[s + i]) * kr[s + i];
        double ms = acc / double(block);
        loud.push_back(ms > 0 ? -0.691 + 10.0 * std::log10(ms) : -120.0);
    }
    if (loud.size() < 2) return 0.0f;

    std::vector<double> gated;
    for (double v : loud) if (v >= -70.0) gated.push_back(v);
    if (gated.size() < 2) return 0.0f;

    std::sort(gated.begin(), gated.end());               // ascending
    size_t nQ = std::max<size_t>(1, size_t(gated.size() * 0.40));
    size_t nL = std::max<size_t>(1, size_t(gated.size() * 0.25));
    double quietMean = 0.0;
    for (size_t i = 0; i < nQ; ++i) quietMean += gated[i];
    quietMean /= double(nQ);
    double loudMean = 0.0;
    for (size_t i = 0; i < nL; ++i) loudMean += gated[gated.size() - 1 - i];
    loudMean /= double(nL);
    return float(std::max(0.0, loudMean - quietMean));
}

// ---------------------------------------------------------------------------
// measureSpectralTiltDbPerOct : octave-band log-energy linear fit, 60 Hz..10 kHz
// ---------------------------------------------------------------------------
float measureSpectralTiltDbPerOct(const StereoBuffer& audio, double sr) {
    const size_t n = audio.size();
    if (n == 0) return 0.0f;
    std::vector<float> mono = monoSum(audio);
    std::vector<double> xs, ys;
    const double sqrt2 = std::sqrt(2.0);
    int idx = 0;
    for (double fc = 60.0; fc <= 10000.0 + 1.0; fc *= 2.0, ++idx) {
        std::vector<float> b = bandLimit(mono, sr, fc / sqrt2, fc * sqrt2);
        double e = 0.0; for (float v : b) e += double(v) * v;
        double db = 10.0 * std::log10(std::max(e / double(n), 1e-18));
        xs.push_back(double(idx));   // octave index
        ys.push_back(db);
    }
    // least-squares slope (dB per octave)
    size_t m = xs.size();
    if (m < 2) return 0.0f;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < m; ++i) { sx += xs[i]; sy += ys[i]; sxx += xs[i] * xs[i]; sxy += xs[i] * ys[i]; }
    double denom = m * sxx - sx * sx;
    if (std::fabs(denom) < 1e-12) return 0.0f;
    return float((m * sxy - sx * sy) / denom);
}

// ---------------------------------------------------------------------------
// measureStereoWidth : side/mid RMS ratio
// ---------------------------------------------------------------------------
float measureStereoWidth(const StereoBuffer& audio) {
    const size_t n = audio.size();
    if (n == 0) return 0.0f;
    double mid = 0.0, side = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double m = 0.5 * (double(audio.l[i]) + audio.r[i]);
        double s = 0.5 * (double(audio.l[i]) - audio.r[i]);
        mid += m * m; side += s * s;
    }
    if (mid <= 1e-20) return 0.0f;
    return float(std::sqrt(side / mid));
}

// ===========================================================================
// Shared analyzer aggregation (used by the CLI --analyze-refs and the GUI)
// ===========================================================================
namespace {

float medianOf(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    size_t m = v.size() / 2;
    return (v.size() & 1) ? v[m] : 0.5f * (v[m - 1] + v[m]);
}

// Average two present profiles into the combined fallback.
CalibrationProfile meanProfile(const CalibrationProfile& a, const CalibrationProfile& b) {
    CalibrationProfile o;
    o.present = true;
    auto avg = [](float x, float y) { return 0.5f * (x + y); };
    o.targetLufs = avg(a.targetLufs, b.targetLufs);
    o.crestDb = avg(a.crestDb, b.crestDb);
    for (int i = 0; i < kCalBands; ++i) o.bands[i] = avg(a.bands[i], b.bands[i]);
    o.dropBreakContrastLu = avg(a.dropBreakContrastLu, b.dropBreakContrastLu);
    o.spectralTiltDbPerOct = avg(a.spectralTiltDbPerOct, b.spectralTiltDbPerOct);
    o.stereoWidth = avg(a.stereoWidth, b.stereoWidth);
    o.refCount = a.refCount + b.refCount;
    return o;
}

} // namespace

RefMeasurement Calibration::measureReference(const StereoBuffer& audio48k, double sampleRate) {
    RefMeasurement m;
    m.lufs = measureLufs(audio48k, sampleRate);
    m.crestDb = measureCrestDb(audio48k, sampleRate);
    m.bands = measureBandShares(audio48k, sampleRate);
    m.contrastLu = measureContrastLu(audio48k, sampleRate);
    m.tiltDbPerOct = measureSpectralTiltDbPerOct(audio48k, sampleRate);
    m.width = measureStereoWidth(audio48k);
    return m;
}

CalibrationProfile Calibration::aggregate(const std::vector<RefMeasurement>& measurements) {
    CalibrationProfile prof;
    if (measurements.empty()) { prof.present = false; return prof; }

    std::vector<float> vLufs, vCrest, vContrast, vTilt, vWidth;
    std::array<std::vector<float>, kCalBands> vBands;
    for (const auto& r : measurements) {
        vLufs.push_back(r.lufs);
        vCrest.push_back(r.crestDb);
        vContrast.push_back(r.contrastLu);
        vTilt.push_back(r.tiltDbPerOct);
        vWidth.push_back(r.width);
        for (int b = 0; b < kCalBands; ++b) vBands[b].push_back(r.bands[b]);
    }

    prof.present = true;
    prof.refCount = (int) measurements.size();
    prof.targetLufs = medianOf(vLufs);
    prof.crestDb = medianOf(vCrest);
    prof.dropBreakContrastLu = medianOf(vContrast);
    prof.spectralTiltDbPerOct = medianOf(vTilt);
    prof.stereoWidth = medianOf(vWidth);
    float s = 0.0f;
    for (int b = 0; b < kCalBands; ++b) { prof.bands[b] = medianOf(vBands[b]); s += prof.bands[b]; }
    if (s > 1e-6f) for (int b = 0; b < kCalBands; ++b) prof.bands[b] /= s;   // re-normalize
    return prof;
}

void Calibration::assignProfile(CalibrationTarget target, CalibrationProfile prof) {
    prof.present = true;
    if (target == CalibrationTarget::Riddim || target == CalibrationTarget::Trap) {
        perGenre = true;
        (target == CalibrationTarget::Trap ? trap : riddim) = prof;
        // Recompute combined fallback from whatever genres are present.
        if (riddim.present && trap.present) combined = meanProfile(riddim, trap);
        else combined = prof;
    } else {
        combined = prof;      // combined-only (no genre split)
    }
}

// ===========================================================================
// JSON I/O (hand-rolled, tolerant flat object)
// ===========================================================================
namespace {

void writeProfile(std::ostream& os, const char* prefix, const CalibrationProfile& p) {
    auto kv = [&](const char* key, double v) {
        os << "  \"" << prefix << "." << key << "\": " << v << ",\n";
    };
    kv("present", p.present ? 1 : 0);
    kv("targetLufs", p.targetLufs);
    kv("crestDb", p.crestDb);
    for (int b = 0; b < kCalBands; ++b) {
        char key[16]; std::snprintf(key, sizeof(key), "band%d", b);
        kv(key, p.bands[b]);
    }
    kv("dropBreakContrastLu", p.dropBreakContrastLu);
    kv("spectralTiltDbPerOct", p.spectralTiltDbPerOct);
    kv("stereoWidth", p.stereoWidth);
    kv("refCount", p.refCount);
}

// Tolerant scanner: pull every "key": number pair from a flat object.
std::map<std::string, double> scanFlat(const std::string& text) {
    std::map<std::string, double> out;
    size_t i = 0, n = text.size();
    while (i < n) {
        if (text[i] != '"') { ++i; continue; }
        size_t ks = ++i;
        while (i < n && text[i] != '"') ++i;
        if (i >= n) break;
        std::string key = text.substr(ks, i - ks);
        ++i;                                            // past closing quote
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        if (i >= n || text[i] != ':') continue;         // not a value pair
        ++i;
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        size_t vs = i;
        while (i < n) {
            char c = text[i];
            if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') ++i;
            else break;
        }
        if (i > vs) {
            try { out[key] = std::stod(text.substr(vs, i - vs)); } catch (...) {}
        }
    }
    return out;
}

void readProfile(const std::map<std::string, double>& m, const char* prefix, CalibrationProfile& p) {
    auto get = [&](const char* key, float def) -> float {
        auto it = m.find(std::string(prefix) + "." + key);
        return it != m.end() ? float(it->second) : def;
    };
    p.present = get("present", p.present ? 1.f : 0.f) != 0.0f;
    p.targetLufs = get("targetLufs", p.targetLufs);
    p.crestDb = get("crestDb", p.crestDb);
    for (int b = 0; b < kCalBands; ++b) {
        char key[16]; std::snprintf(key, sizeof(key), "band%d", b);
        p.bands[b] = get(key, p.bands[b]);
    }
    p.dropBreakContrastLu = get("dropBreakContrastLu", p.dropBreakContrastLu);
    p.spectralTiltDbPerOct = get("spectralTiltDbPerOct", p.spectralTiltDbPerOct);
    p.stereoWidth = get("stereoWidth", p.stereoWidth);
    p.refCount = int(std::lround(get("refCount", float(p.refCount))));
}

} // namespace

bool Calibration::saveToFile(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": 1,\n";
    os << "  \"perGenre\": " << (perGenre ? 1 : 0) << ",\n";
    writeProfile(os, "riddim", riddim);
    writeProfile(os, "trap", trap);
    writeProfile(os, "combined", combined);
    os << "  \"_note\": 0\n";     // trailing sentinel keeps every real line comma-terminated
    os << "}\n";
    std::string s = os.str();
    size_t w = std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    return w == s.size();
}

bool Calibration::loadFromFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[4096]; size_t r;
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, r);
    std::fclose(f);

    std::map<std::string, double> m = scanFlat(text);
    auto it = m.find("perGenre");
    perGenre = (it != m.end()) && it->second != 0.0;
    readProfile(m, "riddim", riddim);
    readProfile(m, "trap", trap);
    readProfile(m, "combined", combined);
    return true;
}

// ===========================================================================
// active registry
// ===========================================================================
namespace {
std::optional<Calibration>& activeSlot() {
    static std::optional<Calibration> g;   // set before generation, read during
    return g;
}
} // namespace

void Calibration::setActive(std::optional<Calibration> c) { activeSlot() = std::move(c); }
const std::optional<Calibration>& Calibration::active() { return activeSlot(); }

} // namespace rtg
