#pragma once
// Deterministic seeded RNG with named child streams (doc 01 §6).
// splitmix64 core — fast, high quality for procedural use, header-only.
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace rtg {

class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    uint64_t next() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /// [0,1)
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    float  rangef(float lo, float hi) { return float(range(lo, hi)); }
    /// inclusive
    int intRange(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + int(next() % uint64_t(hi - lo + 1));
    }
    bool chance(double p) { return uniform() < p; }

    float gaussian() {
        double u1 = uniform(), u2 = uniform();
        if (u1 < 1e-12) u1 = 1e-12;
        return float(std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2));
    }

    /// Weighted pick. temperature 0 → argmax, 1 → proportional, >1 → flatter.
    int pickWeighted(const std::vector<double>& w, double temperature = 1.0) {
        if (w.empty()) return 0;
        if (temperature <= 0.001) {
            int best = 0;
            for (int i = 1; i < (int)w.size(); ++i) if (w[i] > w[best]) best = i;
            return best;
        }
        std::vector<double> t(w.size());
        double sum = 0;
        for (size_t i = 0; i < w.size(); ++i) {
            t[i] = w[i] <= 0 ? 0 : std::pow(w[i], 1.0 / temperature);
            sum += t[i];
        }
        if (sum <= 0) return 0;
        double r = uniform() * sum;
        for (size_t i = 0; i < t.size(); ++i) { r -= t[i]; if (r <= 0) return (int)i; }
        return int(t.size()) - 1;
    }

    /// Deterministic named child stream — the PRNG tree.
    Rng stream(const std::string& name) const {
        uint64_t h = 1469598103934665603ull; // FNV-1a 64
        for (unsigned char c : name) { h ^= c; h *= 1099511628211ull; }
        return Rng(state_ ^ h ^ 0xD1B54A32D192ED03ull);
    }
    Rng stream(const std::string& name, int index) const {
        return stream(name + "[" + std::to_string(index) + "]");
    }

private:
    uint64_t state_;
};

} // namespace rtg
