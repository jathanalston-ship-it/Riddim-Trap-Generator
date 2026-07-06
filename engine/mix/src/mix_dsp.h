#pragma once
// Private DSP primitives shared inside the mix engine (biquads, saturation,
// OTT-lite multiband, mid/side width, a cheap Schroeder diffusion). Header-only,
// no external deps beyond the core audio buffer. Not part of any public API.
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>
#include "rtg/utils/audio.h"

namespace rtg::mixdsp {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Biquad (RBJ cookbook), transposed direct form II, per-channel state.
// ---------------------------------------------------------------------------
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process(float x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return float(y);
    }

    void setCoeffs(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0;
        a1 = A1 / A0; a2 = A2 / A0;
    }
};

inline Biquad makeLowpass(double fs, double f0, double Q = 0.70710678) {
    Biquad bq;
    f0 = std::min(std::max(f0, 10.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0);
    double alpha = s / (2 * Q);
    bq.setCoeffs((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    return bq;
}

inline Biquad makeHighpass(double fs, double f0, double Q = 0.70710678) {
    Biquad bq;
    f0 = std::min(std::max(f0, 5.0), fs * 0.49);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0);
    double alpha = s / (2 * Q);
    bq.setCoeffs((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    return bq;
}

inline Biquad makeLowShelf(double fs, double f0, double gainDb, double S = 0.9) {
    Biquad bq;
    f0 = std::min(std::max(f0, 10.0), fs * 0.49);
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0);
    double alpha = s / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2);
    double tsa = 2 * std::sqrt(A) * alpha;
    bq.setCoeffs(A * ((A + 1) - (A - 1) * c + tsa),
                 2 * A * ((A - 1) - (A + 1) * c),
                 A * ((A + 1) - (A - 1) * c - tsa),
                 (A + 1) + (A - 1) * c + tsa,
                 -2 * ((A - 1) + (A + 1) * c),
                 (A + 1) + (A - 1) * c - tsa);
    return bq;
}

inline Biquad makeHighShelf(double fs, double f0, double gainDb, double S = 0.9) {
    Biquad bq;
    f0 = std::min(std::max(f0, 10.0), fs * 0.49);
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0);
    double alpha = s / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2);
    double tsa = 2 * std::sqrt(A) * alpha;
    bq.setCoeffs(A * ((A + 1) + (A - 1) * c + tsa),
                 -2 * A * ((A - 1) + (A + 1) * c),
                 A * ((A + 1) + (A - 1) * c - tsa),
                 (A + 1) - (A - 1) * c + tsa,
                 2 * ((A - 1) - (A + 1) * c),
                 (A + 1) - (A - 1) * c - tsa);
    return bq;
}

// Apply a freshly-designed biquad to a whole channel vector.
inline void applyChannel(Biquad bq, std::vector<float>& ch) {
    for (auto& s : ch) s = bq.process(s);
}
// Convenience: apply the same filter design (independent state) to both channels.
inline void applyStereo(const Biquad& proto, StereoBuffer& buf) {
    Biquad l = proto, r = proto;
    for (size_t i = 0; i < buf.size(); ++i) { buf.l[i] = l.process(buf.l[i]); buf.r[i] = r.process(buf.r[i]); }
}

// ---------------------------------------------------------------------------
// Nonlinear color.
// ---------------------------------------------------------------------------
inline double channelRms(const std::vector<float>& c) {
    if (c.empty()) return 0.0;
    double a = 0.0; for (float s : c) a += double(s) * s;
    return std::sqrt(a / double(c.size()));
}

// tanh saturation with RMS output compensation across the stereo buffer.
inline void tanhSaturate(StereoBuffer& buf, double drive) {
    if (buf.empty() || drive <= 0) return;
    double preL = channelRms(buf.l), preR = channelRms(buf.r);
    double pre = 0.5 * (preL + preR);
    double invT = 1.0 / std::tanh(drive); // normalize so full-scale maps to ~1
    for (auto& s : buf.l) s = float(std::tanh(drive * s) * invT);
    for (auto& s : buf.r) s = float(std::tanh(drive * s) * invT);
    double post = 0.5 * (channelRms(buf.l) + channelRms(buf.r));
    if (post > 1e-9 && pre > 1e-9) {
        float g = float(pre / post);
        buf.applyGain(g);
    }
}

// ---------------------------------------------------------------------------
// Mid/side stereo width. width 0 → mono, 1 → unchanged, >1 → wider.
// ---------------------------------------------------------------------------
inline void applyWidth(StereoBuffer& buf, float width) {
    for (size_t i = 0; i < buf.size(); ++i) {
        float mid = 0.5f * (buf.l[i] + buf.r[i]);
        float side = 0.5f * (buf.l[i] - buf.r[i]) * width;
        buf.l[i] = mid + side;
        buf.r[i] = mid - side;
    }
}

// Fold everything below `hz` to mono (elliptical-ish): remove the low band from
// the side channel so bass is centered while highs keep their width.
inline void monoBelow(StereoBuffer& buf, double fs, double hz) {
    Biquad lp = makeLowpass(fs, hz, 0.6);
    for (size_t i = 0; i < buf.size(); ++i) {
        float mid = 0.5f * (buf.l[i] + buf.r[i]);
        float side = 0.5f * (buf.l[i] - buf.r[i]);
        float sideLow = lp.process(side);
        float sideHi = side - sideLow;      // low content dropped from side
        buf.l[i] = mid + sideHi;
        buf.r[i] = mid - sideHi;
    }
}

// ---------------------------------------------------------------------------
// OTT-lite: 3-band upward+downward compression on a mono-summed control, but
// applied per stereo band. Splits at split0/split1 Hz. depth 0..~0.6.
// ---------------------------------------------------------------------------
inline void ottLite(StereoBuffer& buf, double fs, double split0, double split1, double depth) {
    const size_t n = buf.size();
    if (n == 0 || depth <= 0) return;
    depth = std::min(depth, 0.8);

    // Split into 3 bands with cascaded 2nd-order filters (per channel state).
    auto splitBand = [&](int band) {
        StereoBuffer b(n);
        b.l = buf.l; b.r = buf.r;
        if (band == 0) {                       // low: LP @ split0
            applyStereo(makeLowpass(fs, split0), b);
        } else if (band == 1) {                // mid: HP @ split0 then LP @ split1
            applyStereo(makeHighpass(fs, split0), b);
            applyStereo(makeLowpass(fs, split1), b);
        } else {                               // high: HP @ split1
            applyStereo(makeHighpass(fs, split1), b);
        }
        return b;
    };

    std::array<StereoBuffer, 3> bands = { splitBand(0), splitBand(1), splitBand(2) };

    const double thrDb = -24.0;          // pivot for up/down
    const double atk = std::exp(-1.0 / (0.005 * fs));   // 5 ms
    const double rel = std::exp(-1.0 / (0.080 * fs));   // 80 ms
    const double ratioDown = 3.0;        // downward
    const double ratioUp = 2.0;          // upward
    const double maxUpDb = 12.0;

    for (auto& b : bands) {
        double env = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double x = std::max(std::abs(b.l[i]), std::abs(b.r[i]));
            double coef = (x > env) ? atk : rel;
            env = coef * env + (1 - coef) * x;
            double envDb = 20.0 * std::log10(std::max(env, 1e-7));
            double gDb;
            if (envDb > thrDb)
                gDb = -(envDb - thrDb) * (1.0 - 1.0 / ratioDown) * depth;   // downward
            else
                gDb = std::min(maxUpDb, (thrDb - envDb) * (1.0 - 1.0 / ratioUp)) * depth; // upward
            float g = std::pow(10.0f, float(gDb) / 20.0f);
            b.l[i] *= g; b.r[i] *= g;
        }
    }

    // Recombine + light makeup for the perceived density lift.
    float makeup = std::pow(10.0f, float(2.5 * depth) / 20.0f);
    for (size_t i = 0; i < n; ++i) {
        buf.l[i] = (bands[0].l[i] + bands[1].l[i] + bands[2].l[i]) * makeup;
        buf.r[i] = (bands[0].r[i] + bands[1].r[i] + bands[2].r[i]) * makeup;
    }
}

// ---------------------------------------------------------------------------
// Cheap mono Schroeder diffusion (3 comb + 2 allpass) for the fx send.
// ---------------------------------------------------------------------------
struct Comb {
    std::vector<float> buf; size_t idx = 0; float fb = 0.7f, damp = 0.3f, store = 0.f;
    void init(size_t len, float feedback, float damping) { buf.assign(std::max<size_t>(len, 1), 0.f); idx = 0; fb = feedback; damp = damping; store = 0.f; }
    inline float process(float x) {
        float y = buf[idx];
        store = y * (1.f - damp) + store * damp;
        buf[idx] = x + store * fb;
        if (++idx >= buf.size()) idx = 0;
        return y;
    }
};
struct Allpass {
    std::vector<float> buf; size_t idx = 0; float fb = 0.5f;
    void init(size_t len, float feedback) { buf.assign(std::max<size_t>(len, 1), 0.f); idx = 0; fb = feedback; }
    inline float process(float x) {
        float y = buf[idx];
        float out = -x + y;
        buf[idx] = x + y * fb;
        if (++idx >= buf.size()) idx = 0;
        return out;
    }
};

struct Diffuser {
    Comb c0, c1, c2; Allpass a0, a1;
    void init(double fs) {
        double k = fs / 44100.0;
        c0.init(size_t(1116 * k), 0.78f, 0.28f);
        c1.init(size_t(1277 * k), 0.80f, 0.30f);
        c2.init(size_t(1491 * k), 0.82f, 0.33f);
        a0.init(size_t(556 * k), 0.5f);
        a1.init(size_t(441 * k), 0.5f);
    }
    inline float process(float x) {
        float s = c0.process(x) + c1.process(x) + c2.process(x);
        s *= 0.333f;
        s = a0.process(s);
        s = a1.process(s);
        return s;
    }
};

} // namespace rtg::mixdsp
