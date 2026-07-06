// A/B Preference Trainer model — pairwise logistic taste model + anonymous,
// mergeable vote persistence (JUCE-free, rtg_core). See preference_model.h and
// library/TRAINING.md.
#include "rtg/library/preference_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace rtg {

namespace {

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

inline double sigmoid(double z) { return 1.0 / (1.0 + std::exp(-z)); }

// Compact float formatting for our hand-rolled JSON (round-trip safe).
std::string fmtFloat(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return std::string(buf);
}

// --- Minimal JSON field readers (tolerant, for our own controlled format) ----

// Returns the string value of "key":"..." or empty if absent.
std::string readString(const std::string& s, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    auto k = s.find(pat);
    if (k == std::string::npos) return {};
    auto c = s.find(':', k + pat.size());
    if (c == std::string::npos) return {};
    auto q1 = s.find('"', c + 1);
    if (q1 == std::string::npos) return {};
    auto q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return s.substr(q1 + 1, q2 - q1 - 1);
}

// Reads a numeric "key": value. Returns fallback if absent/unparseable.
bool readNumber(const std::string& s, const std::string& key, double& out) {
    const std::string pat = "\"" + key + "\"";
    auto k = s.find(pat);
    if (k == std::string::npos) return false;
    auto c = s.find(':', k + pat.size());
    if (c == std::string::npos) return false;
    size_t i = c + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    size_t start = i;
    while (i < s.size() && (std::isdigit((unsigned char) s[i]) || s[i] == '-' ||
                            s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
        ++i;
    if (i == start) return false;
    try { out = std::stod(s.substr(start, i - start)); }
    catch (...) { return false; }
    return true;
}

// Reads a numeric array "key":[a,b,c,...] into out. Returns count parsed.
size_t readArray(const std::string& s, const std::string& key, std::vector<double>& out) {
    out.clear();
    const std::string pat = "\"" + key + "\"";
    auto k = s.find(pat);
    if (k == std::string::npos) return 0;
    auto lb = s.find('[', k + pat.size());
    if (lb == std::string::npos) return 0;
    auto rb = s.find(']', lb + 1);
    if (rb == std::string::npos) return 0;
    std::string body = s.substr(lb + 1, rb - lb - 1);
    std::stringstream ss(body);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        // trim
        size_t a = tok.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = tok.find_last_not_of(" \t\r\n");
        try { out.push_back(std::stod(tok.substr(a, b - a + 1))); }
        catch (...) { /* skip bad token */ }
    }
    return out.size();
}

std::string joinPath(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

} // namespace

//==============================================================================
PreferenceModel::PreferenceModel() : w_(kWeights, 0.0f), trainedOn_(0) {}

std::array<float, PreferenceModel::kFeatureDims>
PreferenceModel::extract(const Features& f) {
    std::array<float, kFeatureDims> x{};
    x[0] = f.brightness;
    x[1] = f.aggression;
    x[2] = f.darkness;
    x[3] = f.movement;
    x[4] = f.subRatio;
    x[5] = f.transient;
    x[6] = f.crestDb / 20.0f;
    x[7] = clamp01(f.centroidHz / 8000.0f);
    return x;
}

float PreferenceModel::scoreVec(const std::array<float, kFeatureDims>& x) const {
    double z = (w_.size() > (size_t) kFeatureDims) ? w_[kFeatureDims] : 0.0; // bias
    for (int i = 0; i < kFeatureDims; ++i) z += double(w_[i]) * double(x[i]);
    return float(sigmoid(z));
}

float PreferenceModel::score(const Features& f) const { return scoreVec(extract(f)); }

//==============================================================================
void PreferenceModel::train(const std::vector<Vote>& votes, int epochs, float lr, float l2) {
    if (w_.size() != (size_t) kWeights) w_.assign(kWeights, 0.0f);
    if (votes.empty()) { trainedOn_ = 0; return; }

    for (int e = 0; e < epochs; ++e) {
        for (const Vote& v : votes) {
            // Diff vector (bias cancels in the pairwise difference).
            double d[kFeatureDims];
            for (int i = 0; i < kFeatureDims; ++i)
                d[i] = double(v.featA[i]) - double(v.featB[i]);
            // Model probability that A beats B.
            double z = 0.0;
            for (int i = 0; i < kFeatureDims; ++i) z += double(w_[i]) * d[i];
            double p = sigmoid(z);
            // Label: A won → 1, B won → 0.
            double y = (v.choice == 0) ? 1.0 : 0.0;
            double g = p - y;   // dL/dz
            for (int i = 0; i < kFeatureDims; ++i) {
                double grad = g * d[i] + double(l2) * double(w_[i]);
                w_[i] = float(double(w_[i]) - double(lr) * grad);
            }
            // Bias gets only weight decay (does not enter the pairwise loss).
            w_[kFeatureDims] = float(double(w_[kFeatureDims]) * (1.0 - double(lr) * double(l2)));
        }
    }
    trainedOn_ = (int) votes.size();
}

//==============================================================================
bool PreferenceModel::save(const std::string& path) const {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "{\"v\":" << kSchemaV << ",\"featv\":" << kFeatV
        << ",\"trainedOn\":" << trainedOn_ << ",\"w\":[";
    for (size_t i = 0; i < w_.size(); ++i) {
        if (i) out << ",";
        out << fmtFloat(w_[i]);
    }
    out << "]}\n";
    return (bool) out;
}

bool PreferenceModel::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string s = ss.str();
    if (s.empty()) return false;

    double featv = 0;
    if (readNumber(s, "featv", featv) && int(featv) != kFeatV) return false;

    std::vector<double> w;
    if (readArray(s, "w", w) < (size_t) kWeights) return false;
    w_.assign(kWeights, 0.0f);
    for (int i = 0; i < kWeights; ++i) w_[i] = float(w[i]);

    double trained = 0;
    trainedOn_ = readNumber(s, "trainedOn", trained) ? int(trained) : 0;
    return true;
}

//==============================================================================
bool PreferenceModel::appendVote(const std::string& votesPath, const Vote& v) {
    std::error_code ec;
    fs::create_directories(fs::path(votesPath).parent_path(), ec);
    std::ofstream out(votesPath, std::ios::app);
    if (!out) return false;
    out << "{\"v\":" << v.v << ",\"featv\":" << v.featv
        << ",\"install\":\"" << v.install << "\""
        << ",\"ts\":" << v.ts
        << ",\"genre\":\"" << v.genre << "\""
        << ",\"role\":\"" << v.role << "\""
        << ",\"featA\":[";
    for (int i = 0; i < kFeatureDims; ++i) { if (i) out << ","; out << fmtFloat(v.featA[i]); }
    out << "],\"featB\":[";
    for (int i = 0; i < kFeatureDims; ++i) { if (i) out << ","; out << fmtFloat(v.featB[i]); }
    out << "],\"choice\":" << v.choice << "}\n";
    return (bool) out;
}

std::vector<Vote> PreferenceModel::loadVotes(const std::string& path) {
    std::vector<Vote> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        // Skip blank / obviously non-object lines (tolerant).
        if (line.find('{') == std::string::npos) continue;
        std::vector<double> fa, fb;
        if (readArray(line, "featA", fa) < (size_t) kFeatureDims) continue;
        if (readArray(line, "featB", fb) < (size_t) kFeatureDims) continue;
        Vote v;
        double n = 0;
        v.v      = readNumber(line, "v", n) ? int(n) : 1;
        v.featv  = readNumber(line, "featv", n) ? int(n) : 1;
        v.install = readString(line, "install");
        v.ts     = readNumber(line, "ts", n) ? (long long) n : 0;
        v.genre  = readString(line, "genre");
        v.role   = readString(line, "role");
        v.choice = readNumber(line, "choice", n) ? int(n) : 0;
        for (int i = 0; i < kFeatureDims; ++i) { v.featA[i] = float(fa[i]); v.featB[i] = float(fb[i]); }
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<Vote> PreferenceModel::mergeVotes(const std::string& dirOfJsonlFiles) {
    std::vector<Vote> merged;
    std::error_code ec;
    if (!fs::exists(dirOfJsonlFiles, ec)) return merged;

    auto keyOf = [](const Vote& v) {
        std::ostringstream k;
        k << v.install << '|' << v.ts << '|'
          << fmtFloat(v.featA[0]) << '|' << fmtFloat(v.featB[0]);
        return k.str();
    };
    std::vector<std::string> seen;

    for (const auto& entry : fs::directory_iterator(dirOfJsonlFiles, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".jsonl") continue;
        for (Vote& v : loadVotes(entry.path().string())) {
            std::string key = keyOf(v);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            merged.push_back(std::move(v));
        }
    }
    return merged;
}

//==============================================================================
std::string PreferenceModel::trainingDir(const std::string& dataDir) {
    return joinPath(dataDir, "training");
}
std::string PreferenceModel::weightsPath(const std::string& dataDir) {
    return joinPath(trainingDir(dataDir), "preference_weights.json");
}
std::string PreferenceModel::votesPath(const std::string& dataDir) {
    return joinPath(trainingDir(dataDir), "votes.jsonl");
}

std::string PreferenceModel::installId(const std::string& dataDir) {
    const std::string path = joinPath(trainingDir(dataDir), "install_id");
    {
        std::ifstream in(path);
        if (in) {
            std::string id;
            std::getline(in, id);
            // trim whitespace
            size_t a = id.find_first_not_of(" \t\r\n");
            size_t b = id.find_last_not_of(" \t\r\n");
            if (a != std::string::npos) id = id.substr(a, b - a + 1);
            if (id.size() >= 8) return id;
        }
    }
    // Generate a fresh random hex-32 (128-bit) id.
    std::random_device rd;
    std::mt19937_64 gen((uint64_t(rd()) << 32) ^ uint64_t(rd()) ^
                        uint64_t(std::random_device{}()));
    uint64_t hi = gen(), lo = gen();
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  (unsigned long long) hi, (unsigned long long) lo);
    std::string id(buf);

    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out) out << id << "\n";
    return id;
}

bool PreferenceModel::available(const std::string& dataDir) {
    PreferenceModel m;
    if (!m.load(weightsPath(dataDir))) return false;
    return m.trainedOn() >= 20;
}

} // namespace rtg
