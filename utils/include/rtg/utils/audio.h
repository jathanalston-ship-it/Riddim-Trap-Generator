#pragma once
// Core audio buffer + dB helpers. rtg_core is JUCE-free; everything speaks
// StereoBuffer (interleaving-free, 32-bit float, 48 kHz by convention).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rtg {

constexpr double kSampleRate = 48000.0;

struct StereoBuffer {
    std::vector<float> l, r;

    StereoBuffer() = default;
    explicit StereoBuffer(size_t n) : l(n, 0.0f), r(n, 0.0f) {}

    size_t size() const { return l.size(); }
    bool empty() const { return l.empty(); }
    void resize(size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }

    void addFrom(const StereoBuffer& other, size_t destStart = 0, float gain = 1.0f) {
        const size_t n = std::min(other.size(), size() > destStart ? size() - destStart : 0);
        for (size_t i = 0; i < n; ++i) {
            l[destStart + i] += other.l[i] * gain;
            r[destStart + i] += other.r[i] * gain;
        }
    }
    void applyGain(float g) {
        for (auto& s : l) s *= g;
        for (auto& s : r) s *= g;
    }
    float peak() const {
        float p = 0.0f;
        for (auto s : l) p = std::max(p, std::abs(s));
        for (auto s : r) p = std::max(p, std::abs(s));
        return p;
    }
    double rms() const {
        if (empty()) return 0.0;
        double acc = 0.0;
        for (size_t i = 0; i < size(); ++i) acc += 0.5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
        return std::sqrt(acc / double(size()));
    }
};

inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }

} // namespace rtg
