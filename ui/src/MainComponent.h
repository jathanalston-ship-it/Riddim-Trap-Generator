#pragma once
// Root UI: left icon rail (Generate/Library/Settings), page viewport, bottom
// transport, top update banner. Owns the controller, updater, LookAndFeel.
#include <array>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GenerationController.h"
#include "update/UpdateChecker.h"
#include "RtgLookAndFeel.h"
#include "components/TransportBar.h"
#include "pages/GeneratePage.h"
#include "pages/LibraryPage.h"
#include "pages/SettingsPage.h"

namespace rtg::ui {

class MainComponent : public juce::Component,
                      public juce::ChangeListener {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    class NavButton;
    class UpdateBanner;

    void showPage(int index);
    void updateAccent(rtg::Genre);

    RtgLookAndFeel lookAndFeel_;
    rtg::app::GenerationController controller_;
    rtg::app::UpdateChecker updater_;

    std::array<std::unique_ptr<NavButton>, 3> navButtons_;
    int currentPage_ = 0;

    std::unique_ptr<GeneratePage> generatePage_;
    std::unique_ptr<LibraryPage> libraryPage_;
    std::unique_ptr<SettingsPage> settingsPage_;
    std::unique_ptr<TransportBar> transport_;
    std::unique_ptr<UpdateBanner> banner_;
    bool bannerDismissed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace rtg::ui
