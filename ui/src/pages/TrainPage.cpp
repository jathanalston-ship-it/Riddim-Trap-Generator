#include "TrainPage.h"
#include "../RtgLookAndFeel.h"

namespace rtg::ui {

//==============================================================================
void TrainPage::LoopCard::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lnf ? lnf->accent() : Colors::accentRiddim;
    auto b = getLocalBounds().toFloat().reduced(2.0f);

    g.setColour(down ? Colors::panelBg2.brighter(0.05f) : Colors::panelBg2);
    g.fillRoundedRectangle(b, 12.0f);
    g.setColour(highlighted ? accent : Colors::panelStroke);
    g.drawRoundedRectangle(b, 12.0f, highlighted ? 2.0f : 1.0f);

    // Big letter.
    g.setColour(accent.withAlpha(rendering ? 0.25f : 0.9f));
    g.setFont(rtgSansFont(72.0f, true));
    g.drawText(letter_, b.withTrimmedBottom(b.getHeight() * 0.28f),
               juce::Justification::centred, false);

    if (rendering) {
        // Spinner arc while the loop renders.
        const float r = 16.0f;
        auto c = b.getCentre();
        juce::Path arc;
        arc.addCentredArc(c.x, c.y + b.getHeight() * 0.30f, r, r, 0.0f,
                          spin, spin + 4.2f, true);
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(2.5f));
    } else {
        g.setColour(Colors::dim);
        g.setFont(rtgSansFont(12.0f, false));
        g.drawText("click to play", b.removeFromBottom(b.getHeight() * 0.22f),
                   juce::Justification::centred, false);
    }
}

//==============================================================================
void TrainPage::GenreToggle::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const juce::Colour cardAccent = isTrap ? Colors::accentTrap : Colors::accentRiddim;
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    const bool on = getToggleState();
    g.setColour(on ? cardAccent.withAlpha(0.18f) : Colors::panelBg2);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(on ? cardAccent : (highlighted ? Colors::dim : Colors::panelStroke));
    g.drawRoundedRectangle(b, 8.0f, on ? 2.0f : 1.0f);
    g.setColour(on ? cardAccent : Colors::text);
    g.setFont(rtgSansFont(14.0f, true));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
TrainPage::TrainPage(rtg::app::GenerationController& controller)
    : controller_(controller) {
    controller_.addChangeListener(this);
    setWantsKeyboardFocus(true);

    trapToggle_.isTrap = true;
    for (auto* t : { &riddimToggle_, &trapToggle_ }) {
        addAndMakeVisible(t);
        t->setClickingTogglesState(false);
    }
    riddimToggle_.setToggleState(true, juce::dontSendNotification);
    riddimToggle_.onClick = [this] { applyGenre(rtg::Genre::Riddim, true); };
    trapToggle_.onClick = [this] { applyGenre(rtg::Genre::Trap, true); };

    addAndMakeVisible(cardA_);
    addAndMakeVisible(cardB_);
    cardA_.onClick = [this] { controller_.playTrainA(); };
    cardB_.onClick = [this] { controller_.playTrainB(); };

    voteA_.getProperties().set("primary", true);
    voteB_.getProperties().set("primary", true);
    addAndMakeVisible(voteA_);
    addAndMakeVisible(voteB_);
    voteA_.onClick = [this] { doVote(0); };
    voteB_.onClick = [this] { doVote(1); };

    addAndMakeVisible(trainButton_);
    trainButton_.onClick = [this] {
        int n = controller_.trainNow();
        juce::ignoreUnused(n);
        refresh();
    };

    refresh();
}

TrainPage::~TrainPage() {
    stopTimer();
    controller_.removeChangeListener(this);
}

//==============================================================================
void TrainPage::applyGenre(rtg::Genre g, bool notify) {
    genre_ = g;
    riddimToggle_.setToggleState(g == rtg::Genre::Riddim, juce::dontSendNotification);
    trapToggle_.setToggleState(g == rtg::Genre::Trap, juce::dontSendNotification);
    if (notify && onGenreChanged) onGenreChanged(g);
    startPair();     // render a fresh pair in the newly selected genre
    repaint();
}

void TrainPage::startPair() {
    controller_.startTrainingPair(genre_);
    if (!isTimerRunning()) startTimerHz(24);
    refresh();
}

void TrainPage::doVote(int choice) {
    if (!controller_.trainPairReady()) return;
    controller_.voteTrain(choice);   // appends vote + starts next pair
    if (!isTimerRunning()) startTimerHz(24);
    refresh();
}

//==============================================================================
void TrainPage::visibilityChanged() {
    if (isShowing()) {
        grabKeyboardFocus();
        // Auto-start the first pair when the page opens.
        if (!controller_.trainPairReady() && !controller_.isTrainRendering())
            startPair();
        else
            refresh();
    }
}

bool TrainPage::keyPressed(const juce::KeyPress& k) {
    const auto ch = k.getTextCharacter();
    if (ch == 'a' || ch == 'A') { doVote(0); return true; }
    if (ch == 'b' || ch == 'B') { doVote(1); return true; }
    if (k == juce::KeyPress::spaceKey) { controller_.replayLastTrain(); return true; }
    return false;
}

//==============================================================================
void TrainPage::changeListenerCallback(juce::ChangeBroadcaster*) {
    refresh();
}

void TrainPage::refresh() {
    const bool ready = controller_.trainPairReady();
    const bool rendering = controller_.isTrainRendering();
    cardA_.rendering = rendering || !ready;
    cardB_.rendering = rendering || !ready;
    voteA_.setEnabled(ready);
    voteB_.setEnabled(ready);
    trainButton_.setEnabled(controller_.voteCount() >= 20);

    if (!rendering && isTimerRunning()) stopTimer();
    cardA_.repaint();
    cardB_.repaint();
    repaint();
}

void TrainPage::timerCallback() {
    spin_ += 0.32f;
    cardA_.spin = spin_;
    cardB_.spin = spin_ + 1.6f;
    if (cardA_.rendering) cardA_.repaint();
    if (cardB_.rendering) cardB_.repaint();
    if (!controller_.isTrainRendering()) stopTimer();
}

//==============================================================================
void TrainPage::resized() {
    auto area = getLocalBounds().reduced(24);

    area.removeFromTop(52);   // header (drawn in paint)

    // Genre toggle row.
    auto toggles = area.removeFromTop(34);
    riddimToggle_.setBounds(toggles.removeFromLeft(120));
    toggles.removeFromLeft(8);
    trapToggle_.setBounds(toggles.removeFromLeft(120));
    area.removeFromTop(20);

    // Status / train row at the bottom.
    auto statusRow = area.removeFromBottom(48);
    trainButton_.setBounds(statusRow.removeFromRight(180).withSizeKeepingCentre(180, 36));
    area.removeFromBottom(14);   // hint line space (drawn in paint)
    area.removeFromBottom(20);

    // Two big cards side by side, each with a vote button beneath.
    const int gap = 24;
    const int colW = (area.getWidth() - gap) / 2;
    auto colL = area.removeFromLeft(colW);
    area.removeFromLeft(gap);
    auto colR = area;

    auto voteRowL = colL.removeFromBottom(44);
    auto voteRowR = colR.removeFromBottom(44);
    colL.removeFromBottom(12);
    colR.removeFromBottom(12);
    cardA_.setBounds(colL);
    cardB_.setBounds(colR);
    voteA_.setBounds(voteRowL.withSizeKeepingCentre(juce::jmin(220, voteRowL.getWidth()), 40));
    voteB_.setBounds(voteRowR.withSizeKeepingCentre(juce::jmin(220, voteRowR.getWidth()), 40));
}

void TrainPage::paint(juce::Graphics& g) {
    auto area = getLocalBounds().reduced(24);

    // Header.
    auto header = area.removeFromTop(52);
    g.setColour(Colors::text);
    g.setFont(rtgSansFont(22.0f, true));
    g.drawText("Train your sound \xE2\x80\x94 pick the better bass",
               header, juce::Justification::topLeft, false);
    g.setColour(Colors::dim);
    g.setFont(rtgSansFont(12.0f, false));
    g.drawText("A / B keys vote \xC2\xB7 space replays the last loop",
               header.withTrimmedTop(28), juce::Justification::topLeft, false);

    // Bottom status text (above the train button row).
    auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lnf ? lnf->accent() : Colors::accentRiddim;

    auto statusRow = getLocalBounds().reduced(24);
    statusRow = statusRow.removeFromBottom(48);
    statusRow.removeFromRight(180 + 12);   // train button + gap

    const int votes = controller_.voteCount();
    const int trained = controller_.modelTrainedOn();

    g.setColour(Colors::text);
    g.setFont(rtgSansFont(14.0f, true));
    juce::String line1 = juce::String(votes) + " votes recorded";
    g.drawText(line1, statusRow.removeFromTop(22), juce::Justification::centredLeft, false);

    g.setColour(trained > 0 ? accent : Colors::dim);
    g.setFont(rtgSansFont(12.0f, false));
    juce::String line2 = trained > 0
        ? ("Model trained on " + juce::String(trained) + " votes \xC2\xB7 influences future generations")
        : (votes >= 20 ? "Ready to train \xE2\x80\x94 press Train model now"
                       : ("Vote on " + juce::String(juce::jmax(0, 20 - votes)) + " more pairs to unlock training"));
    g.drawText(line2, statusRow, juce::Justification::centredLeft, false);
}

} // namespace rtg::ui
