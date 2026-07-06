#pragma once
// Generate page (doc 09 §2): parameter panel (left), waveform (centre),
// action zone (right). Builds rtg::Params from the controls and drives
// GenerationController.
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GenerationController.h"
#include "../components/WaveformView.h"

namespace rtg::ui {

class RtgLookAndFeel;

class GeneratePage : public juce::Component,
                     public juce::ChangeListener {
public:
    explicit GeneratePage(rtg::app::GenerationController& controller);
    ~GeneratePage() override;

    std::function<void(rtg::Genre)> onGenreChanged; // MainComponent updates accent
    std::function<void(double)> onSeek;             // forwarded to WaveformView

    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    rtg::Genre genre() const { return genre_; }

private:
    struct GenreCard : public juce::Button {
        explicit GenreCard(const juce::String& t) : juce::Button(t) {}
        bool isTrap = false;
        void paintButton(juce::Graphics&, bool, bool) override;
    };

    // Small self-contained padlock button drawn with juce::Path.
    // Accent-tinted when locked, dim when unlocked.
    struct LockButton : public juce::Button {
        LockButton() : juce::Button("seedLock") {}
        bool locked = false;
        void paintButton(juce::Graphics&, bool, bool) override;
    };

    void applyGenre(rtg::Genre g, bool notify);
    void onGeneratePressed();
    void onExportPressed();
    rtg::Params buildParams();
    void updateFromResult();

    // Seed UX helpers.
    void styleSeedField(bool lastSeedStyle);                       // greyed-italic vs normal
    void setSeedDisplay(const juce::String& text, bool lastSeedStyle); // programmatic, no auto-lock
    void updateSeedHint();

    rtg::app::GenerationController& controller_;

    // Left column controls.
    GenreCard riddimCard_ { "RIDDIM" };
    GenreCard trapCard_ { "TRAP" };
    rtg::Genre genre_ = rtg::Genre::Riddim;

    juce::Slider bpmKnob_;
    juce::Slider lengthSlider_;
    juce::Slider dropsStepper_;
    juce::ComboBox introCombo_;

    static constexpr int kNumMacros = 6;
    std::array<juce::Slider, kNumMacros> macroKnobs_;
    std::array<juce::String, kNumMacros> macroNames_
        { "ENERGY", "AGGRESSION", "DARKNESS", "COMPLEXITY", "MELODY", "CHAOS" };

    juce::TextEditor seedField_;
    juce::TextButton diceButton_ { "\xF0\x9F\x8E\xB2" };
    LockButton seedLockButton_;
    juce::Label seedHint_;
    bool seedLocked_ = false;        // default UNLOCKED: fresh seed every generate
    bool suppressSeedNotify_ = false; // guard so programmatic setText never auto-locks

    // Centre.
    WaveformView waveform_;

    // Right column.
    juce::TextButton generateButton_ { "GENERATE" };
    juce::TextButton exportButton_ { "Export WAV\xE2\x80\xA6" };
    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneratePage)
};

} // namespace rtg::ui
