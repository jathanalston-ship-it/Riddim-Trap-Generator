#pragma once
// Train page: the in-app A/B Preference Trainer. The user listens to two short
// bass-drop loop candidates (A / B), votes which sounds better, and a tiny
// local logistic model learns their taste (see rtg::PreferenceModel). Once
// trained (>= 20 votes) the model reweights fresh-candidate selection in the
// generation pipeline. Votes are anonymous, audio-free feature comparisons.
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GenerationController.h"

namespace rtg::ui {

class TrainPage : public juce::Component,
                  public juce::ChangeListener,
                  private juce::Timer {
public:
    explicit TrainPage(rtg::app::GenerationController& controller);
    ~TrainPage() override;

    std::function<void(rtg::Genre)> onGenreChanged; // MainComponent updates accent

    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress&) override;

    rtg::Genre genre() const { return genre_; }

private:
    // Big clickable candidate card ("A" / "B"). Click = (re)play that loop.
    struct LoopCard : public juce::Button {
        LoopCard(const juce::String& letter) : juce::Button(letter), letter_(letter) {}
        juce::String letter_;
        bool rendering = false;
        float spin = 0.0f;
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    struct GenreToggle : public juce::Button {
        explicit GenreToggle(const juce::String& t) : juce::Button(t) {}
        bool isTrap = false;
        void paintButton(juce::Graphics&, bool, bool) override;
    };

    void applyGenre(rtg::Genre g, bool notify);
    void startPair();
    void doVote(int choice);
    void refresh();

    rtg::app::GenerationController& controller_;
    rtg::Genre genre_ = rtg::Genre::Riddim;

    GenreToggle riddimToggle_ { "RIDDIM" };
    GenreToggle trapToggle_ { "TRAP" };

    LoopCard cardA_ { "A" };
    LoopCard cardB_ { "B" };
    juce::TextButton voteA_ { "This one" };
    juce::TextButton voteB_ { "This one" };

    juce::TextButton trainButton_ { "Train model now" };

    void timerCallback() override;
    float spin_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrainPage)
};

} // namespace rtg::ui
