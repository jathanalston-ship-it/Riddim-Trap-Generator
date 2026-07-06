#pragma once
// Evolving sound library (doc 06, simplified v1): persistent store of rated
// recipes with metadata; selection by role + character window; admission
// threshold + per-role caps; favorites; usage tracking. Text-file
// persistence (one .rtgsound per asset) — no external DB dependency in v1.
#include <mutex>
#include <string>
#include <vector>
#include "rtg/synth/recipe.h"
#include "rtg/utils/rng.h"

namespace rtg {

class SoundLibrary {
public:
    /// dir: writable directory for .rtgsound files (created if missing).
    explicit SoundLibrary(std::string dir);

    void load();          // scan dir, parse all assets (tolerant of bad files)
    void saveAll() const; // persist every in-memory asset

    /// Best candidates for a role whose features sit near the requested
    /// character window, fitness-and-favorite weighted, rng-diversified.
    /// May return fewer than `count` (caller synthesizes the shortfall).
    std::vector<RatedSound> pick(Role role, float aggression01, float darkness01,
                                 int count, Rng& rng) const;

    /// Admission: accept if score >= threshold and (under per-role cap or
    /// better than the weakest non-favorite, which it displaces).
    /// Assigns id, persists to disk. Returns true if admitted.
    bool maybeIngest(RatedSound sound);

    void noteUsed(const std::string& id);          // bump usage counter
    void setFavorite(const std::string& id, bool fav);
    bool remove(const std::string& id);            // favorites refuse removal

    std::vector<RatedSound> all() const;           // snapshot (UI Library page)
    int countForRole(Role role) const;
    const std::string& directory() const { return dir_; }

    // v1 policy knobs:
    static constexpr float kAdmissionThreshold = 0.55f;
    static constexpr int kPerRoleCap = 64;

private:
    std::string dir_;
    std::vector<RatedSound> sounds_;
    mutable std::mutex mutex_;
};

} // namespace rtg
