// Headless generation CLI: renders a full track with no GUI/audio device.
// Used by CI smoke tests and local verification.
//   rtg_cli --out track.wav [--genre riddim|trap] [--bpm 145] [--seconds 180]
//           [--seed 42] [--energy 65] [--aggression 70] [--darkness 55]
//           [--complexity 50] [--melody 30] [--chaos 25] [--drops 2]
//           [--intro atmospheric|minimal|vocalchop|impact|fakeout]
//           [--library <dir>]
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "rtg/generation/pipeline.h"
#include "rtg/render/wav_writer.h"

using namespace rtg;

int main(int argc, char** argv) {
    Params params;
    std::string out = "rtg_track.wav";
    std::string libDir = "rtg_library";

    auto arg = [&](int& i) -> std::string {
        return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out") out = arg(i);
        else if (a == "--library") libDir = arg(i);
        else if (a == "--genre") params.genre = (arg(i) == "trap") ? Genre::Trap : Genre::Riddim;
        else if (a == "--bpm") params.bpm = std::atof(arg(i).c_str());
        else if (a == "--seconds") params.lengthSec = std::atof(arg(i).c_str());
        else if (a == "--seed") params.seed = std::strtoull(arg(i).c_str(), nullptr, 10);
        else if (a == "--energy") params.energy = std::atoi(arg(i).c_str());
        else if (a == "--aggression") params.aggression = std::atoi(arg(i).c_str());
        else if (a == "--darkness") params.darkness = std::atoi(arg(i).c_str());
        else if (a == "--complexity") params.complexity = std::atoi(arg(i).c_str());
        else if (a == "--melody") params.melody = std::atoi(arg(i).c_str());
        else if (a == "--chaos") params.chaos = std::atoi(arg(i).c_str());
        else if (a == "--drops") params.dropCount = std::atoi(arg(i).c_str());
        else if (a == "--intro") {
            std::string v = arg(i);
            params.introStyle = v == "minimal" ? IntroStyle::Minimal
                              : v == "vocalchop" ? IntroStyle::VocalChop
                              : v == "impact" ? IntroStyle::Impact
                              : v == "fakeout" ? IntroStyle::Fakeout
                              : IntroStyle::Atmospheric;
        }
    }
    if (params.seed == 0) params.seed = 1;

    SoundLibrary library(libDir);
    library.load();
    std::printf("[rtg] library: %d sounds in %s\n", (int)library.all().size(), libDir.c_str());
    std::printf("[rtg] generating: %s %.0f BPM, %.0fs, seed %llu\n",
                genreName(params.genre), params.bpm, params.lengthSec,
                (unsigned long long)params.seed);

    std::atomic<bool> cancel{false};
    auto t0 = std::chrono::steady_clock::now();
    auto result = generateTrack(params, library, cancel,
        [](float p, const std::string& stage) {
            std::printf("[rtg] %3d%%  %s\n", int(p * 100.0f), stage.c_str());
            std::fflush(stdout);
        });
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (!result) { std::printf("[rtg] cancelled/failed\n"); return 1; }

    if (!writeWav16(out, result->master, result->sampleRate)) {
        std::printf("[rtg] ERROR: could not write %s\n", out.c_str());
        return 2;
    }

    const auto& s = result->stats;
    std::printf("[rtg] done in %.1fs (%.1fx realtime)\n", elapsed,
                result->master.size() / result->sampleRate / (elapsed > 0 ? elapsed : 1e-9));
    std::printf("[rtg] length=%.1fs  LUFS=%.1f  truePeak=%.2f dBTP  crest=%.1f dB\n",
                result->master.size() / result->sampleRate, s.integratedLufs,
                s.truePeakDb, s.crestDb);
    std::printf("[rtg] sections: ");
    for (const auto& sec : result->sections)
        std::printf("%s(%.0fs) ", sectionName(sec.type), sec.lengthSec);
    std::printf("\n[rtg] new sounds ingested: %d\n", result->newSoundsIngested);
    std::printf("[rtg] wrote %s\n", out.c_str());

    // Sanity gates for CI:
    const double secs = result->master.size() / result->sampleRate;
    if (secs < params.lengthSec * 0.5 || result->master.peak() < 0.05f) {
        std::printf("[rtg] SMOKE TEST FAIL: too short or near-silent\n");
        return 3;
    }
    std::printf("[rtg] SMOKE TEST PASS\n");
    return 0;
}
