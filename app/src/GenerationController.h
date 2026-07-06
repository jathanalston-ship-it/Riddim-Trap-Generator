#pragma once
// Bridge between the JUCE UI and rtg_core: background generation thread,
// preview playback, WAV export, library access. Implemented in
// app/src/GenerationController.cpp. UI components observe via
// juce::ChangeBroadcaster (state changes) and poll progress on a timer.
#include <atomic>
#include <optional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "rtg/generation/pipeline.h"

namespace rtg::app {

class GenerationController : public juce::ChangeBroadcaster {
public:
    GenerationController();   // opens default audio device, loads library from
                              // <userAppData>/RiddimTrapGenerator/library
    ~GenerationController() override;

    // --- Generation ------------------------------------------------------
    void startGeneration(const rtg::Params& params);  // no-op if already running
    void cancelGeneration();
    bool isGenerating() const;
    float progress() const;                 // 0..1
    juce::String progressStage() const;     // "Composing", "Mixing"…
    const std::optional<rtg::GenerationResult>& result() const; // last finished

    // --- Preview playback --------------------------------------------------
    void togglePlayback();                  // play/pause last result
    void stopPlayback();
    bool isPlaying() const;
    double playheadSeconds() const;
    void seekSeconds(double t);

    // --- Export ------------------------------------------------------------
    bool exportWav(const juce::File& destination) const;  // 24-bit WAV

    // --- Services ----------------------------------------------------------
    rtg::SoundLibrary& library();
    juce::AudioDeviceManager& deviceManager();
    juce::File dataDirectory() const;       // per-user app data root

    /// Renders a recipe preview and plays it (Library page audition).
    void auditionSound(const rtg::RatedSound& sound);

    // --- A/B Preference Trainer -------------------------------------------
    // Background-renders two short bass-drop loop candidates for the given
    // genre; poll trainPairReady()/isTrainRendering() and observe via the
    // ChangeBroadcaster. Reuses the preview transport for playback.
    void startTrainingPair(rtg::Genre genre);
    bool trainPairReady() const;       // both candidates rendered & ready
    bool isTrainRendering() const;     // a pair is currently rendering
    void playTrainA();                 // (re)play candidate A
    void playTrainB();                 // (re)play candidate B
    void replayLastTrain();            // replay whichever was last played (A default)
    /// Record a vote (0 = A better, 1 = B better): appends an anonymous vote
    /// then immediately starts rendering the next pair (continuous flow).
    void voteTrain(int choice);
    /// Load votes, train the model, save weights. Returns trainedOn count.
    int trainNow();
    int voteCount() const;             // total anonymous votes recorded locally
    int modelTrainedOn() const;        // trainedOn from saved weights (0 if none)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtg::app
