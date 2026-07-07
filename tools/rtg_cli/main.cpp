// Headless generation CLI: renders a full track with no GUI/audio device.
// Used by CI smoke tests and local verification.
//   rtg_cli --out track.wav [--genre riddim|trap] [--bpm 145] [--seconds 180]
//           [--seed 42] [--energy 65] [--aggression 70] [--darkness 55]
//           [--complexity 50] [--melody 30] [--chaos 25] [--drops 2]
//           [--intro atmospheric|minimal|vocalchop|impact|fakeout]
//           [--library <dir>] [--calibration <calibration.json>]
//
// Background library-evolution mode (no audio output):
//   rtg_cli --evolve <N> --library <dir>
//     Runs N idle-style evolution cycles (mutate/breed/prune) over the library
//     in place, prints a per-cycle report, and exits. Deterministic per
//     (library contents, cycle index). See docs/architecture/06-library-evolution.md.
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
#include "rtg/library/ab_log.h"
#include "rtg/generation/pipeline.h"
#include "rtg/library/evolution.h"
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

// Background library evolution mode (doc 06 §5): run N idle-style cycles that
// mutate/breed/prune the library in place, print a per-cycle report, exit. No
// audio output. Deterministic per (library contents, cycle seed).
int runEvolve(const std::string& libDir, int cycles) {
    SoundLibrary library(libDir);
    library.load();

    // Cycle seed = cycle index + a stable hash of the library *contents* (its
    // sorted asset ids), NOT the directory path. This differentiates distinct
    // libraries onto separate rng trajectories yet makes two identical copies of
    // the same library evolve byte-identically regardless of where they live on
    // disk (doc 06 §8 determinism guarantee).
    uint64_t contentHash = 1469598103934665603ull;           // FNV-1a 64
    {
        std::vector<std::string> ids;
        for (const auto& s : library.all()) ids.push_back(s.id);
        std::sort(ids.begin(), ids.end());
        for (const auto& id : ids) {
            for (unsigned char c : id) { contentHash ^= c; contentHash *= 1099511628211ull; }
            contentHash ^= 0xff; contentHash *= 1099511628211ull;   // id separator
        }
    }

    std::printf("[rtg] evolve: %d cycle(s) over %d sound(s) in %s\n",
                cycles, (int)library.all().size(), libDir.c_str());

    EvolutionEngine engine(library);
    int totalAdmitted = 0, totalPruned = 0;
    for (int i = 0; i < cycles; ++i) {
        uint64_t seed = uint64_t(i) + contentHash;
        EvolutionEngine::CycleReport r = engine.runCycle(seed);
        totalAdmitted += r.admitted;
        totalPruned += r.pruned;
        std::printf("[rtg] cycle %2d/%d  focus=%-10s candidates=%d admitted=%d pruned=%d  (role now %d)\n",
                    i + 1, cycles, roleName(r.focusedRole), r.candidates, r.admitted,
                    r.pruned, library.countForRole(r.focusedRole));
        std::fflush(stdout);
    }
    std::printf("[rtg] evolve done: +%d admitted, -%d pruned, %d sound(s) total\n",
                totalAdmitted, totalPruned, (int)library.all().size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Params params;
    std::string out = "rtg_track.wav";
    std::string libDir = "rtg_library";
    std::string analyzeRefs, calibOut, calibrationIn, stemsDir, scoreOut;
    bool genreGiven = false;
    bool libGiven = false;
    int evolveCycles = 0;
    std::string abSummary;   // --ab-summary <ab_drops.jsonl>: analyze A/B drop tests
    int abLast = 0;          // --ab-last <N>: only the most recent N tests (0 = all)

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
        else if (a == "--evolve") evolveCycles = std::atoi(arg(i).c_str());
        else if (a == "--ab-summary") abSummary = arg(i);
        else if (a == "--ab-last") abLast = std::atoi(arg(i).c_str());
        else if (a == "--stems") stemsDir = arg(i);
        else if (a == "--score") scoreOut = arg(i);
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

    // ---- Background library evolution mode --------------------------------
    if (evolveCycles > 0) {
        return runEvolve(libDir, evolveCycles);
    }

    // ---- A/B drop-test preference summary ---------------------------------
    // Reads the global A/B drop log and prints the mean winner-minus-loser
    // deltas per design param / measured feature — the analysis behind
    // "look at the last N A/B tests and overhaul song design".
    if (!abSummary.empty()) {
        std::vector<rtg::ABResult> results = rtg::abLogLoad(abSummary);
        rtg::ABSummary s = rtg::abLogSummarize(results, abLast);
        std::printf("[rtg] %zu A/B drop test(s) in %s\n", results.size(), abSummary.c_str());
        std::printf("%s", s.text.c_str());
        return 0;
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
    std::array<StereoBuffer, kLaneCount> stems;
    auto t0 = std::chrono::steady_clock::now();
    auto result = generateTrack(params, library, cancel,
        [](float p, const std::string& stage) {
            std::printf("[rtg] %3d%%  %s\n", int(p * 100.0f), stage.c_str());
            std::fflush(stdout);
        }, /*ingest*/ true, stemsDir.empty() ? nullptr : &stems);
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (!result) { std::printf("[rtg] cancelled/failed\n"); return 1; }

    if (!writeWav16(out, result->master, result->sampleRate)) {
        std::printf("[rtg] ERROR: could not write %s\n", out.c_str());
        return 2;
    }

    // --- per-lane stems (raw, pre-mix) for analysis (earview) ---------------
    if (!stemsDir.empty()) {
        std::error_code ec; fs::create_directories(stemsDir, ec);
        int wrote = 0;
        for (int li = 0; li < kLaneCount; ++li) {
            if (stems[li].empty() || stems[li].peak() < 1e-4f) continue;
            std::string sp = (fs::path(stemsDir) / (std::string("stem_")
                              + laneName(Lane(li)) + ".wav")).string();
            if (writeWav16(sp, stems[li], result->sampleRate)) ++wrote;
        }
        std::printf("[rtg] wrote %d stem(s) to %s\n", wrote, stemsDir.c_str());
    }

    // --- symbolic score export (note grid) for the earview overlay ----------
    if (!scoreOut.empty()) {
        if (FILE* f = std::fopen(scoreOut.c_str(), "w")) {
            std::fprintf(f, "# bpm %g beatsPerBar %g totalBeats %g\n", result->plan.bpm,
                         result->plan.beatsPerBar, result->score.totalBeats);
            std::fprintf(f, "# lane startBeat lengthBeats midi velocity mod\n");
            for (int li = 0; li < kLaneCount; ++li)
                for (const Note& n : result->score.notes(Lane(li)))
                    std::fprintf(f, "%s %.4f %.4f %d %.3f %.3f\n", laneName(Lane(li)),
                                 n.startBeat, n.lengthBeats, n.midi, n.velocity, n.mod);
            std::fclose(f);
            std::printf("[rtg] wrote score %s\n", scoreOut.c_str());
        }
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
