#pragma once
// A/B Preference Trainer model (in-app taste learning, JUCE-free, rtg_core).
//
// The user votes on pairs of short bass-drop loops; each vote records the
// analysis Features of both candidates (audio-free, anonymous) plus which one
// won. A tiny logistic model is trained by pairwise SGD on those votes and
// then blended into fresh-candidate selection in the generation pipeline so
// generations drift toward the user's taste.
//
// Vote data is deliberately mergeable and anonymous (feature vectors only, no
// audio, no user identity beyond a random install id) so a future opt-in
// uploader can aggregate votes across the whole userbase and ship back
// retrained global weights. See library/TRAINING.md for the full schema and
// aggregation design.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "rtg/synth/recipe.h"   // rtg::Features

namespace rtg {

// One anonymous A/B comparison. featA/featB are the extracted (versioned)
// feature vectors of the two candidates; choice: 0 = A won, 1 = B won.
struct Vote {
    int v = 1;                              // schema version
    int featv = 1;                          // feature-order version
    std::string install;                    // anonymous per-install uuid (hex-32)
    long long ts = 0;                       // unix seconds
    std::string genre;                      // "riddim" / "trap"
    std::string role;                       // recipe role name, e.g. "Growl"
    std::array<float, 8> featA{};
    std::array<float, 8> featB{};
    int choice = 0;                         // 0 = A, 1 = B
};

class PreferenceModel {
public:
    static constexpr int kFeatureDims = 8;              // feature vector length
    static constexpr int kWeights = kFeatureDims + 1;   // + bias term
    static constexpr int kFeatV = 1;                    // feature-order version
    static constexpr int kSchemaV = 1;                  // vote/weights schema version

    PreferenceModel();   // weights zeroed, trainedOn = 0

    // Versioned feature extraction (featv=1):
    // [brightness, aggression, darkness, movement, subRatio, transient,
    //  crestDb/20, centroidHz/8000 (clamped 0..1)].
    static std::array<float, kFeatureDims> extract(const Features& f);

    // sigmoid(w·x + bias), in 0..1.
    float score(const Features& f) const;
    float scoreVec(const std::array<float, kFeatureDims>& x) const;

    // Pairwise logistic SGD: P(A beats B) = sigmoid(w·(xA-xB)); the label is
    // taken from vote.choice. L2 weight decay, fixed learning rate. Sets
    // trainedOn to votes.size(). Deterministic (fixed traversal order).
    void train(const std::vector<Vote>& votes, int epochs, float lr, float l2);

    int trainedOn() const { return trainedOn_; }
    const std::vector<float>& weights() const { return w_; }

    // --- Persistence (hand-rolled minimal JSON) --------------------------------
    // <dataDir>/training/preference_weights.json : {v,featv,w:[...],trainedOn}.
    bool save(const std::string& path) const;   // creates parent dir
    bool load(const std::string& path);          // false on missing/invalid/featv mismatch

    // --- Votes (append-only JSONL) --------------------------------------------
    // One vote per line at <dataDir>/training/votes.jsonl.
    static bool appendVote(const std::string& votesPath, const Vote& v); // creates parent dir
    static std::vector<Vote> loadVotes(const std::string& path);          // tolerant of bad lines
    // Merge every *.jsonl in a directory, deduped by (install,ts,featA[0],featB[0]).
    static std::vector<Vote> mergeVotes(const std::string& dirOfJsonlFiles);

    // Anonymous per-install id (hex-32). Read from <dataDir>/training/install_id,
    // created (random) on first use.
    static std::string installId(const std::string& dataDir);

    // Convenience paths under a data directory.
    static std::string trainingDir(const std::string& dataDir);
    static std::string weightsPath(const std::string& dataDir);
    static std::string votesPath(const std::string& dataDir);

    // Trained weights exist and cover >= 20 votes.
    static bool available(const std::string& dataDir);

private:
    std::vector<float> w_;   // size kWeights; w_[kFeatureDims] is the bias
    int trainedOn_ = 0;
};

} // namespace rtg
