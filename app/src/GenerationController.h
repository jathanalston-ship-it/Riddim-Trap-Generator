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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtg::app
