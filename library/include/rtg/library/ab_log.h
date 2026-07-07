#pragma once
// Global A/B full-drop test log (doc: user-guided design feedback).
//
// Every in-app A/B DROP comparison appends ONE compact record here: the two
// candidates' design parameters + measured character, and which the user
// preferred. The format is a tiny append-only JSONL (one object per line, short
// keys) so 50 tests are only a few KB, yet fully parseable and coherent.
//
// The point of the log is analysis: abLogSummarize() folds the last N results
// into "what characteristics win" (mean winner-minus-loser deltas per feature
// and per design param), which is exactly what "look at the last 50 A/B tests
// and overhaul song design" consumes. Dependency-free (no JSON lib, no JUCE).
#include <cstdint>
#include <string>
#include <vector>

namespace rtg {

// Compact measured character of one rendered ~20 s drop clip. Same measurements
// the calibration analyzer speaks (so references and generations are comparable).
struct ABFeat {
    float lufs = 0.0f;            // integrated loudness
    float crest = 0.0f;          // peak/rms over the loud blocks (punch)
    float contrast = 0.0f;       // loud-vs-quiet spread (drop slam)
    float tilt = 0.0f;           // spectral tilt dB/oct
    float width = 0.0f;          // side/mid ratio
    float bands[5] = {0, 0, 0, 0, 0};   // 5-band energy shares (sub..air)
    float growlOdd = 0.0f;       // growl odd-harmonic fraction (square-ness)
    float growlWobbleHz = 0.0f;  // growl wobble/gate rate
    float growlCentroidHz = 0.0f;// growl brightness centroid
};

// The design parameters that shaped a drop (the "design vector"). Order is
// fixed so the log stays compact and the summary can name each axis.
enum ABParam {
    AB_Energy = 0, AB_Aggression, AB_Darkness, AB_Complexity,
    AB_Melody, AB_Chaos, AB_Bpm, AB_ParamCount
};
inline const char* abParamName(int i) {
    static const char* n[AB_ParamCount] = {
        "energy", "aggression", "darkness", "complexity", "melody", "chaos", "bpm" };
    return (i >= 0 && i < AB_ParamCount) ? n[i] : "?";
}

// One A/B full-drop comparison result.
struct ABResult {
    long long ts = 0;                    // unix seconds (0 = unset)
    int genre = 0;                       // 0 riddim, 1 trap
    uint64_t seedA = 0, seedB = 0;
    float paramsA[AB_ParamCount] = {0};  // design vector of A
    float paramsB[AB_ParamCount] = {0};  // design vector of B
    ABFeat featA, featB;
    int winner = 0;                      // 0 = A preferred, 1 = B preferred
};

// Canonical global log path under the app data dir (<dataDir>/ab_drops.jsonl).
std::string abLogPath(const std::string& dataDir);

// Append one record (compact one-line JSON). Creates parent dirs. Returns false
// only on I/O failure. Single-writer append is atomic enough for this use.
bool abLogAppend(const std::string& path, const ABResult& r);

// Load all records (tolerant: malformed lines skipped).
std::vector<ABResult> abLogLoad(const std::string& path);

// Preference analysis over the last `lastN` records (0 = all). featDelta /
// paramDelta hold the mean (winner - loser) value per axis: positive means the
// user's preferred drops tend to have MORE of that characteristic. `text` is a
// ready-to-read report ranking the strongest preferences.
struct ABSummary {
    int n = 0;
    ABFeat featDelta;                    // mean winner-minus-loser per feature
    float paramDelta[AB_ParamCount] = {0};
    std::string text;
};
ABSummary abLogSummarize(const std::vector<ABResult>& results, int lastN = 0);

} // namespace rtg
