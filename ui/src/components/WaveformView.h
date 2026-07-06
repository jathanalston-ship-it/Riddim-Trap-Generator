#pragma once
// Center visualisation of the Generate page: placeholder grid before a result,
// staged progress overlay during generation, and a downsampled min/max waveform
// with coloured section bands + readout chip + playhead after (doc 09 §2).
#include <functional>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GenerationController.h"

namespace rtg::ui {

class RtgLookAndFeel;

class WaveformView : public juce::Component,
                     public juce::ChangeListener,
                     private juce::Timer {
public:
    explicit WaveformView(rtg::app::GenerationController& controller);
    ~WaveformView() override;

    std::function<void(double)> onSeek;   // seconds

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;
    void rebuildPeaks();
    double totalSeconds() const;

    rtg::app::GenerationController& controller_;
    const void* peaksResultKey_ = nullptr;  // identity of the result peaks were built for
    std::vector<float> peaksMin_, peaksMax_; // downsampled, one pair per column
    bool wasGenerating_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};

} // namespace rtg::ui
