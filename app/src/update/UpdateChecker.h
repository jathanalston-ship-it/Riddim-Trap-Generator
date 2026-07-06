#pragma once
// Auto-update system. Checks the GitHub Releases API of this repository for
// a release tag newer than RTG_VERSION, exposes download-with-progress of
// the Windows exe asset, and performs restart-and-install via an updater
// batch swap (download to temp → spawn updater.bat → quit → bat waits,
// replaces the running exe, relaunches). Implemented in
// app/src/update/UpdateChecker.cpp.
//
// Compile-time configuration (set by CMake):
//   RTG_VERSION        e.g. "1.0.0"  (no leading 'v')
//   RTG_GITHUB_OWNER   e.g. "jathanalston-ship-it"
//   RTG_GITHUB_REPO    e.g. "riddim-trap-generator"
//   RTG_UPDATE_ASSET   e.g. "RiddimTrapGenerator-win64.exe"
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rtg::app {

class UpdateChecker : public juce::ChangeBroadcaster {
public:
    enum class State {
        Idle, Checking, UpToDate, UpdateAvailable,
        Downloading, ReadyToInstall, Error
    };

    UpdateChecker();
    ~UpdateChecker() override;

    /// Async GET api.github.com/repos/<owner>/<repo>/releases/latest,
    /// parse tag_name (vX.Y.Z) + asset browser_download_url + release notes,
    /// semver-compare against currentVersion(). Broadcasts on state change.
    void checkForUpdates();

    /// Async download of the update asset to a temp file, with progress.
    void downloadUpdate();

    /// Windows: write+launch updater .bat (wait for our pid → replace exe →
    /// start new exe), then quit the app. Non-Windows dev builds: reveal the
    /// downloaded file instead.
    void restartAndInstall();

    State state() const;
    float downloadProgress() const;          // 0..1 while Downloading
    juce::String currentVersion() const;     // RTG_VERSION
    juce::String latestVersion() const;      // tag without 'v', empty until checked
    juce::String releaseNotes() const;
    juce::String errorMessage() const;

    /// True when a newer semver than the running version exists.
    static bool isNewerVersion(const juce::String& latest, const juce::String& current);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtg::app
