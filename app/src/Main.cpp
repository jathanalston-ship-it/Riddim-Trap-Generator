// Application entry point: JUCEApplication + a dark DocumentWindow hosting the
// MainComponent. (doc 09 — desktop UI.)
#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

namespace rtg::app {

class RtgApplication : public juce::JUCEApplication {
public:
    RtgApplication() = default;

    const juce::String getApplicationName() override    { return "Riddim Trap Generator"; }
    const juce::String getApplicationVersion() override { return RTG_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow_ = nullptr; }

    void systemRequestedQuit() override { quit(); }

    //==========================================================================
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : juce::DocumentWindow(name,
                                   juce::Colour(0xFF0E0F12),
                                   juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new rtg::ui::MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(1100, 720, 10000, 10000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace rtg::app

START_JUCE_APPLICATION(rtg::app::RtgApplication)
