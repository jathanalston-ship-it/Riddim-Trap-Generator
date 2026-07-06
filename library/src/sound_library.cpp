// Evolving sound library (doc 06 v1): text-file persistence (one .rtgsound per
// asset), role+character weighted selection, admission threshold, per-role cap
// with weakest-non-favorite displacement, favorites, usage tracking.
#include "rtg/library/sound_library.h"
#include "rtg/decision/calibration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace rtg {

namespace {

constexpr const char* kExt = ".rtgsound";

std::string hex8(const std::string& name, uint64_t seed, float score) {
    uint64_t h = 1469598103934665603ull;                 // FNV-1a 64
    auto feed = [&](const unsigned char* p, size_t n) {
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    };
    feed(reinterpret_cast<const unsigned char*>(name.data()), name.size());
    feed(reinterpret_cast<const unsigned char*>(&seed), sizeof(seed));
    uint32_t sbits; std::memcpy(&sbits, &score, sizeof(sbits));
    feed(reinterpret_cast<const unsigned char*>(&sbits), sizeof(sbits));
    uint32_t v = uint32_t(h ^ (h >> 32));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return std::string(buf);
}

void serialize(std::ostream& os, const RatedSound& s) {
    const Recipe& r = s.recipe;
    const Features& f = s.features;
    os << "id=" << s.id << "\n"
       << "role=" << int(r.role) << "\n"
       << "name=" << r.name << "\n"
       << "seed=" << r.seed << "\n"
       << "score=" << s.score << "\n"
       << "favorite=" << (s.favorite ? 1 : 0) << "\n"
       << "uses=" << s.uses << "\n"
       << "features.centroidHz=" << f.centroidHz << "\n"
       << "features.brightness=" << f.brightness << "\n"
       << "features.aggression=" << f.aggression << "\n"
       << "features.darkness=" << f.darkness << "\n"
       << "features.movement=" << f.movement << "\n"
       << "features.subRatio=" << f.subRatio << "\n"
       << "features.transient=" << f.transient << "\n"
       << "features.crestDb=" << f.crestDb << "\n"
       << "features.durationSec=" << f.durationSec << "\n";
    for (const auto& kv : r.p) os << "p." << kv.first << "=" << kv.second << "\n";
}

// Returns false if the file could not be parsed into a usable asset.
bool deserialize(std::istream& is, RatedSound& out) {
    RatedSound s;
    bool haveRole = false;
    std::string line;
    try {
        while (std::getline(is, line)) {
            if (line.empty()) continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;        // tolerate garbage lines
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "id") s.id = val;
            else if (key == "role") { s.recipe.role = Role(std::stoi(val)); haveRole = true; }
            else if (key == "name") s.recipe.name = val;
            else if (key == "seed") s.recipe.seed = std::stoull(val);
            else if (key == "score") s.score = std::stof(val);
            else if (key == "favorite") s.favorite = std::stoi(val) != 0;
            else if (key == "uses") s.uses = std::stoi(val);
            else if (key == "features.centroidHz") s.features.centroidHz = std::stof(val);
            else if (key == "features.brightness") s.features.brightness = std::stof(val);
            else if (key == "features.aggression") s.features.aggression = std::stof(val);
            else if (key == "features.darkness") s.features.darkness = std::stof(val);
            else if (key == "features.movement") s.features.movement = std::stof(val);
            else if (key == "features.subRatio") s.features.subRatio = std::stof(val);
            else if (key == "features.transient") s.features.transient = std::stof(val);
            else if (key == "features.crestDb") s.features.crestDb = std::stof(val);
            else if (key == "features.durationSec") s.features.durationSec = std::stof(val);
            else if (key.rfind("p.", 0) == 0) s.recipe.p[key.substr(2)] = std::stof(val);
            // unknown keys silently ignored
        }
    } catch (...) {
        return false;                                     // malformed number => skip file
    }
    if (!haveRole || s.id.empty()) return false;
    int ri = int(s.recipe.role);
    if (ri < 0 || ri >= kRoleCount) return false;
    out = std::move(s);
    return true;
}

} // namespace

SoundLibrary::SoundLibrary(std::string dir) : dir_(std::move(dir)) {}

void SoundLibrary::load() {
    std::lock_guard<std::mutex> lk(mutex_);
    sounds_.clear();
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (!fs::exists(dir_)) return;
    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != kExt) continue;
        std::ifstream in(entry.path());
        if (!in) continue;
        RatedSound s;
        if (deserialize(in, s)) sounds_.push_back(std::move(s));
        // parse failure => skip file (tolerant)
    }

    // Reference calibration: if <dir>/calibration.json exists, load it and make
    // it the active calibration so the engines target the measured references.
    // Headless-safe: no logging, tolerant of a missing/garbage file.
    {
        fs::path calPath = fs::path(dir_) / "calibration.json";
        std::error_code cec;
        if (fs::exists(calPath, cec)) {
            Calibration cal;
            if (cal.loadFromFile(calPath.string()))
                Calibration::setActive(std::move(cal));
        }
    }
}

void SoundLibrary::saveAll() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::error_code ec;
    fs::create_directories(dir_, ec);
    for (const auto& s : sounds_) {
        std::ofstream out(fs::path(dir_) / (s.id + kExt), std::ios::trunc);
        if (out) serialize(out, s);
    }
}

std::vector<RatedSound> SoundLibrary::pick(Role role, float aggression01, float darkness01,
                                           int count, Rng& rng) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<int> cand;                                 // indices into sounds_
    for (int i = 0; i < (int)sounds_.size(); ++i)
        if (sounds_[i].recipe.role == role) cand.push_back(i);

    std::vector<double> weight(cand.size());
    for (size_t i = 0; i < cand.size(); ++i) {
        const RatedSound& s = sounds_[cand[i]];
        double fitness = s.score
                       + 0.15 * (s.favorite ? 1.0 : 0.0)
                       + 0.05 * std::log1p(double(s.uses))
                       - 0.50 * std::fabs(double(s.features.aggression) - aggression01)
                       - 0.35 * std::fabs(double(s.features.darkness) - darkness01);
        weight[i] = std::exp(fitness);                     // softmax basis
    }

    std::vector<RatedSound> out;
    int want = std::min(count, (int)cand.size());
    for (int n = 0; n < want; ++n) {
        int pick = rng.pickWeighted(weight, 0.5);          // temperature 0.5, diversified
        if (pick < 0 || pick >= (int)cand.size()) break;
        out.push_back(sounds_[cand[pick]]);
        // sample without replacement
        weight[pick] = 0.0;
        cand[pick] = cand.back(); cand.pop_back();
        weight[pick] = weight.back(); weight.pop_back();
    }
    return out;
}

bool SoundLibrary::maybeIngest(RatedSound sound) {
    if (sound.score < kAdmissionThreshold) return false;

    std::lock_guard<std::mutex> lk(mutex_);
    Role role = sound.recipe.role;

    int roleCount = 0, weakest = -1;
    for (int i = 0; i < (int)sounds_.size(); ++i) {
        if (sounds_[i].recipe.role != role) continue;
        ++roleCount;
        if (sounds_[i].favorite) continue;
        if (weakest < 0 || sounds_[i].score < sounds_[weakest].score) weakest = i;
    }

    if (roleCount >= kPerRoleCap) {
        if (weakest < 0) return false;                     // all favorites, no room
        if (sound.score <= sounds_[weakest].score) return false;
        // Displace the weakest non-favorite (delete its file too).
        std::error_code ec;
        fs::remove(fs::path(dir_) / (sounds_[weakest].id + kExt), ec);
        sounds_.erase(sounds_.begin() + weakest);
    }

    sound.id = std::string(roleName(role)) + "_" +
               hex8(sound.recipe.name, sound.recipe.seed, sound.score);
    std::error_code ec;
    fs::create_directories(dir_, ec);
    { std::ofstream out(fs::path(dir_) / (sound.id + kExt), std::ios::trunc);
      if (out) serialize(out, sound); }
    sounds_.push_back(std::move(sound));
    return true;
}

void SoundLibrary::noteUsed(const std::string& id) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& s : sounds_) {
        if (s.id != id) continue;
        ++s.uses;
        std::ofstream out(fs::path(dir_) / (s.id + kExt), std::ios::trunc);
        if (out) serialize(out, s);
        return;
    }
}

void SoundLibrary::setFavorite(const std::string& id, bool fav) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& s : sounds_) {
        if (s.id != id) continue;
        s.favorite = fav;
        std::ofstream out(fs::path(dir_) / (s.id + kExt), std::ios::trunc);
        if (out) serialize(out, s);
        return;
    }
}

bool SoundLibrary::remove(const std::string& id) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto it = sounds_.begin(); it != sounds_.end(); ++it) {
        if (it->id != id) continue;
        if (it->favorite) return false;                    // favorites refuse removal
        std::error_code ec;
        fs::remove(fs::path(dir_) / (it->id + kExt), ec);
        sounds_.erase(it);
        return true;
    }
    return false;
}

std::vector<RatedSound> SoundLibrary::all() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return sounds_;
}

int SoundLibrary::countForRole(Role role) const {
    std::lock_guard<std::mutex> lk(mutex_);
    int c = 0;
    for (const auto& s : sounds_) if (s.recipe.role == role) ++c;
    return c;
}

} // namespace rtg
