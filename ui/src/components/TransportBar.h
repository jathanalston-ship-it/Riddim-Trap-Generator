#pragma once
// Persistent bottom transport (doc 09 §9): play/pause + stop glyph buttons,
// elapsed/total monospace time, thin seek bar mirroring the waveform.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GenerationController.h"

namespace rtg::ui {

class TransportBar : public juce::Component,
                     public juce::ChangeListener,
                     private juce::Timer {
public:
    explicit TransportBar(rtg::app::GenerationController& controller);
    ~TransportBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;
    double totalSeconds() const;
    bool hasResult() const;
    juce::Rectangle<int> seekBarBounds() const;
    void seekFromMouse(const juce::MouseEvent&);

    rtg::app::GenerationController& controller_;
    juce::DrawableButton playButton_ { "play", juce::DrawableButton::ImageFitted };
    juce::DrawableButton stopButton_ { "stop", juce::DrawableButton::ImageFitted };
    bool showingPlayGlyph_ = true;

    void updatePlayGlyph();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace rtg::ui
