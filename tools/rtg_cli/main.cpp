// Headless generation CLI: renders a full track with no GUI/audio device.
// Used by CI smoke tests and local verification.
//   rtg_cli --out track.wav [--genre riddim|trap] [--bpm 145] [--seconds 180]
//           [--seed 42] [--energy 65] [--aggression 70] [--darkness 55]
//           [--complexity 50] [--melody 30] [--chaos 25] [--drops 2]
//           [--intro atmospheric|minimal|vocalchop|impact|fakeout]
//           [--library <dir>] [--calibration <calibration.json>]
//
// Reference-calibration analyzer mode (analysis-only):
//   rtg_cli --analyze-refs <folder> [--genre riddim|trap] [--calib-out <path>]
//     Analyzes every .wav in <folder> and writes calibration.json. See
//     docs/CALIBRATION.md.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "rtg/decision/calibration.h"
#include "rtg/generation/pipeline.h"
#include "rtg/master/master_engine.h"   // measureLufs
#include "rtg/render/wav_writer.h"
#include "wav_reader.h"

using namespace rtg;
namespace fs = std::filesystem;

namespace {

int runAnalyze(const std::string& folder, bool genreGiven, Genre genre,
               const std::string& calibOut) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(folder, ec)) {
        std::printf("[rtg] --analyze-refs: folder not found: %s\n", folder.c_str());
        return 2;
    }
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".wav") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("[rtg] --analyze-refs: no .wav files in %s\n", folder.c_str());
        return 2;
    }

    std::printf("[rtg] analyzing %d reference file(s) in %s\n", (int)files.size(), folder.c_str());
    std::printf("%-28s %7s %6s  %5s %5s %5s %5s %5s  %6s %6s %6s  %5s %7s %6s\n",
                "file", "LUFS", "crest", "sub", "loM", "mid", "hiM", "hi",
                "contr", "tilt", "width", "odd", "wob", "cen");

    std::vector<RefMeasurement> measurements;
    int ok = 0;
    for (const auto& p : files) {
        StereoBuffer buf;
        if (!readWavToStereo48k(p.string(), buf)) {
            std::printf("%-28s  (unreadable — skipped)\n", p.filename().string().c_str());
            continue;
        }
        RefMeasurement m = Calibration::measureReference(buf, kSampleRate);
        measurements.push_back(m);
        ++ok;

        std::string name = p.filename().string();
        if (name.size() > 27) name = name.substr(0, 27);
        std::printf("%-28s %7.1f %6.1f  %5.2f %5.2f %5.2f %5.2f %5.2f  %6.1f %6.2f %6.2f  %5.2f %6.1f %6.0f\n",
                    name.c_str(), m.lufs, m.crestDb, m.bands[0], m.bands[1], m.bands[2],
                    m.bands[3], m.bands[4], m.contrastLu, m.tiltDbPerOct, m.width,
                    m.growlOdd, m.growlWobbleHz, m.growlCentroidHz);
    }
    if (ok == 0) { std::printf("[rtg] no readable references — nothing written\n"); return 3; }

    // Aggregate = median per field (shared with the GUI analyzer).
    CalibrationProfile prof = Calibration::aggregate(measurements);

    std::printf("[rtg] aggregate (median of %d): LUFS=%.1f crest=%.1f bands=[%.2f %.2f %.2f %.2f %.2f] "
                "contrast=%.1f tilt=%.2f width=%.2f odd=%.2f wob=%.1fHz cen=%.0fHz\n",
                ok, prof.targetLufs, prof.crestDb, prof.bands[0], prof.bands[1], prof.bands[2],
                prof.bands[3], prof.bands[4], prof.dropBreakContrastLu, prof.spectralTiltDbPerOct,
                prof.stereoWidth, prof.growlOdd, prof.growlWobbleHz, prof.growlCentroidHz);

    // Merge into existing calibration (preserve the other genre if present).
    Calibration cal;
    cal.loadFromFile(calibOut);   // tolerant: ignored if missing
    cal.assignProfile(genreGiven
                          ? (genre == Genre::Trap ? CalibrationTarget::Trap : CalibrationTarget::Riddim)
                          : CalibrationTarget::Combined,
                      prof);

    if (!cal.saveToFile(calibOut)) {
        std::printf("[rtg] ERROR: could not write %s\n", calibOut.c_str());
        return 4;
    }
    std::printf("[rtg] wrote %s (%s)\n", calibOut.c_str(),
                genreGiven ? genreName(genre) : "combined");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Params params;
    std::string out = "rtg_track.wav";
    std::string libDir = "rtg_library";
    std::string analyzeRefs, calibOut, calibrationIn;
    bool genreGiven = false;
    bool libGiven = false;

    auto arg = [&](int& i) -> std::string {
        return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out") out = arg(i);
        else if (a == "--library") { libDir = arg(i); libGiven = true; }
        else if (a == "--analyze-refs") analyzeRefs = arg(i);
        else if (a == "--calib-out") calibOut = arg(i);
        else if (a == "--calibration") calibrationIn = arg(i);
        else if (a == "--genre") { params.genre = (arg(i) == "trap") ? Genre::Trap : Genre::Riddim; genreGiven = true; }
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

    // ---- Reference-calibration analyzer mode -------------------------------
    if (!analyzeRefs.empty()) {
        std::string dest = !calibOut.empty() ? calibOut
                         : libGiven ? (fs::path(libDir) / "calibration.json").string()
                         : std::string("calibration.json");
        return runAnalyze(analyzeRefs, genreGiven, params.genre, dest);
    }

    // ---- Generation mode --------------------------------------------------
    if (params.seed == 0) params.seed = 1;

    SoundLibrary library(libDir);
    library.load();   // also auto-loads <libDir>/calibration.json if present
    // Explicit --calibration overrides the library's auto-loaded calibration.
    if (!calibrationIn.empty()) {
        Calibration cal;
        if (cal.loadFromFile(calibrationIn)) {
            Calibration::setActive(std::move(cal));
            std::printf("[rtg] calibration: loaded %s\n", calibrationIn.c_str());
        } else {
            std::printf("[rtg] WARNING: could not load calibration %s\n", calibrationIn.c_str());
        }
    }
    if (Calibration::active()) {
        const CalibrationProfile& p = Calibration::active()->forGenre(params.genre);
        std::printf("[rtg] calibration active: target LUFS=%.1f (%d refs)\n", p.targetLufs, p.refCount);
    }
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
