// rtg_drumprof — extract a DrumProfile from a WAV and print a report.
//
//   rtg_drumprof <in.wav> [--out profile.json] [--bpm N]
//
// Reads the WAV via the CLI's dependency-free reader (resampled to 48 kHz),
// runs rtg::extractDrumProfile, prints a human-readable table (with the kick
// count that proves the transient gate works), and optionally writes flat JSON.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "rtg/drums/drum_profile.h"
#include "rtg/synth/synth_engine.h"     // makeRecipe, renderPreview
#include "rtg/render/wav_writer.h"      // writeWav16
#include "rtg/utils/audio.h"
#include "rtg/utils/rng.h"
#include "../rtg_cli/wav_reader.h"      // rtg::readWavToStereo48k

namespace {

// ---- minimal RBJ biquad for isolated one-shot band-energy measurement ------
struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process(float x) { double y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y; return float(y); }
};
BQ mkHP(double fs, double f0) { BQ q; double w=2*M_PI*f0/fs,c=std::cos(w),s=std::sin(w),al=s/1.41421356;
    double A=1+al; q.b0=(1+c)/2/A; q.b1=-(1+c)/A; q.b2=(1+c)/2/A; q.a1=-2*c/A; q.a2=(1-al)/A; return q; }
BQ mkLP(double fs, double f0) { BQ q; double w=2*M_PI*f0/fs,c=std::cos(w),s=std::sin(w),al=s/1.41421356;
    double A=1+al; q.b0=(1-c)/2/A; q.b1=(1-c)/A; q.b2=(1-c)/2/A; q.a1=-2*c/A; q.a2=(1-al)/A; return q; }

double bandEnergy(const rtg::StereoBuffer& b, double sr, double lo, double hi, size_t maxN) {
    BQ hp = mkHP(sr, lo), lp = mkLP(sr, hi);
    double acc = 0.0; size_t n = std::min(maxN, b.size());
    for (size_t i = 0; i < n; ++i) { float m = 0.5f*(b.l[i]+b.r[i]); float y = lp.process(hp.process(m)); acc += double(y)*y; }
    return acc;
}

// Render kick/snare/hat one-shots + report the kick's ISOLATED 2-8 kHz share.
int runPreview(const std::string& dir, uint64_t seed) {
    const double sr = rtg::kSampleRate;
    rtg::Rng rng(seed);
    struct Item { const char* name; rtg::Role role; double sec; };
    const Item items[] = {
        { "kick",     rtg::Role::Kick,      0.60 },
        { "snare",    rtg::Role::Snare,     0.45 },
        { "hatclosed",rtg::Role::HatClosed, 0.20 },
        { "hatopen",  rtg::Role::HatOpen,   0.50 },
    };
    std::printf("== One-shot previews (isolated) -> %s ==\n", dir.c_str());
    for (const Item& it : items) {
        rtg::Recipe rc = rtg::synth::makeRecipe(it.role, 0.7f, 0.5f, 0.5f, rng);
        rtg::StereoBuffer os = rtg::synth::renderPreview(rc, 29, it.sec, sr);
        std::string path = dir + "/" + it.name + ".wav";
        rtg::writeWav16(path, os, sr);
        size_t w60 = size_t(sr * 0.060);
        double eFull = bandEnergy(os, sr, 30.0, 16000.0, w60);
        double eClick = bandEnergy(os, sr, 2000.0, 8000.0, w60);
        double eSub = bandEnergy(os, sr, 30.0, 120.0, w60);
        std::printf("  %-9s wrote %s", it.name, path.c_str());
        if (it.role == rtg::Role::Kick && eFull > 1e-15)
            std::printf("   [isolated 2-8k share=%.1f%%  sub(30-120)=%.0f%%]",
                        100.0*eClick/eFull, 100.0*eSub/eFull);
        std::printf("\n");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--preview") == 0) {
        uint64_t seed = 42;
        for (int i = 3; i < argc; ++i)
            if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        return runPreview(argv[2], seed);
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: rtg_drumprof <in.wav> [--out profile.json] [--bpm N]\n");
        return 2;
    }
    std::string inPath, outPath;
    double bpmHint = 0.0;
    inPath = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outPath = argv[++i];
        else if (std::strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) bpmHint = std::atof(argv[++i]);
        else { std::fprintf(stderr, "unknown/incomplete arg: %s\n", argv[i]); return 2; }
    }

    rtg::StereoBuffer audio;
    if (!rtg::readWavToStereo48k(inPath, audio) || audio.empty()) {
        std::fprintf(stderr, "error: could not read WAV '%s'\n", inPath.c_str());
        return 1;
    }
    const double sr = rtg::kSampleRate; // reader delivers 48 kHz

    rtg::DrumProfileStats st;
    rtg::DrumProfile p = rtg::extractDrumProfile(audio, sr, bpmHint, &st);

    std::printf("== Drum profile: %s ==\n", inPath.c_str());
    std::printf("  window        : %.1f s  (%.0f beats @ %.1f BPM%s)\n",
                st.windowSec, st.windowBeats, st.bpm, bpmHint > 0 ? ", hinted" : "");
    std::printf("  KICK  count   : %d   (~%.0f expected @ 1/bar = beats/4)\n",
                st.kickCount, st.windowBeats / 4.0);
    std::printf("        bodyHz  : %.1f   decayMs: %.0f   clickShare(2-8k): %.1f%%   subShare(30-120): %.0f%%\n",
                p.kickBodyHz, p.kickDecayMs, 100.0 * p.kickClickShare, 100.0 * p.kickSubShare);
    std::printf("  SNARE count   : %d\n", st.snareCount);
    std::printf("        bodyHz  : %.1f   crackShare(2-5k): %.1f%%   crackDecayMs: %.0f   tonality(flatness): %.2f\n",
                p.snareBodyHz, 100.0 * p.snareCrackShare, p.snareCrackDecayMs, p.snareTonality);
    std::printf("  HATS  count   : %d\n", st.hatCount);
    std::printf("        density : %.2f events/beat   decayMs: %.0f   centroidHz: %.0f\n",
                p.hatDensityPerBeat, p.hatDecayMs, p.hatCentroidHz);

    if (!outPath.empty()) {
        if (p.saveToFile(outPath)) std::printf("  wrote %s\n", outPath.c_str());
        else { std::fprintf(stderr, "error: could not write '%s'\n", outPath.c_str()); return 1; }
    }
    return 0;
}
