#pragma once
// Settings page (doc 09 §5, v1): Audio device panel, Updates panel, Data row.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GenerationController.h"
#include "update/UpdateChecker.h"

namespace rtg::ui {

class SettingsPage : public juce::Component,
                     public juce::ChangeListener {
public:
    SettingsPage(rtg::app::GenerationController& controller,
                 rtg::app::UpdateChecker& updater);
    ~SettingsPage() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    void refreshUpdateUI();

    rtg::app::GenerationController& controller_;
    rtg::app::UpdateChecker& updater_;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;

    juce::Label versionLabel_;
    juce::Label updateStatus_;
    juce::TextButton checkButton_ { "Check for updates" };
    juce::TextButton downloadButton_ { "Download update" };
    juce::TextButton installButton_ { "Restart & install" };
    juce::TextEditor releaseNotes_;

    juce::Label dataPathLabel_;
    juce::TextButton openDataButton_ { "Open" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};

} // namespace rtg::ui
