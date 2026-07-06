// rtg_drumprof — extract a DrumProfile from a WAV and print a report.
//
//   rtg_drumprof <in.wav> [--out profile.json] [--bpm N]
//
// Reads the WAV via the CLI's dependency-free reader (resampled to 48 kHz),
// runs rtg::extractDrumProfile, prints a human-readable table (with the kick
// count that proves the transient gate works), and optionally writes flat JSON.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "rtg/drums/drum_profile.h"
#include "rtg/utils/audio.h"
#include "../rtg_cli/wav_reader.h"      // rtg::readWavToStereo48k

int main(int argc, char** argv) {
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
