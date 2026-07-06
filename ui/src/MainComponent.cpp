#include "MainComponent.h"

namespace rtg::ui {

using State = rtg::app::UpdateChecker::State;

//==============================================================================
// Left rail nav button — simple Path glyphs, accent when selected.
class MainComponent::NavButton : public juce::Button {
public:
    enum class Glyph { Generate, Train, Library, Settings };
    NavButton(const juce::String& name, Glyph glyph) : juce::Button(name), glyph_(glyph) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
        auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accent = lnf ? lnf->accent() : Colors::accentRiddim;
        const bool on = getToggleState();
        auto b = getLocalBounds().toFloat();
        if (on) {
            g.setColour(accent.withAlpha(0.14f));
            g.fillRoundedRectangle(b.reduced(8.0f, 6.0f), 8.0f);
            g.setColour(accent);
            g.fillRoundedRectangle(b.getX() + 2.0f, b.getCentreY() - 12.0f, 3.0f, 24.0f, 1.5f);
        }
        const juce::Colour ink = on ? accent : (highlighted ? Colors::text : Colors::dim);
        g.setColour(ink);
        auto icon = b.withSizeKeepingCentre(24.0f, 24.0f);
        juce::Path p;
        switch (glyph_) {
            case Glyph::Generate: // waveform bars
                for (int i = 0; i < 5; ++i) {
                    const float h = (i == 2 ? 20.0f : (i % 2 ? 10.0f : 16.0f));
                    p.addRoundedRectangle(icon.getX() + i * 5.0f, icon.getCentreY() - h / 2, 2.4f, h, 1.0f);
                }
                break;
            case Glyph::Train: { // two opposed bars (an A/B scale)
                p.addRoundedRectangle(icon.getX() + 2.0f, icon.getY() + 3.0f, 20.0f, 6.0f, 2.0f);
                p.addRoundedRectangle(icon.getX() + 2.0f, icon.getBottom() - 9.0f, 20.0f, 6.0f, 2.0f);
                p.addEllipse(icon.getX() + 4.0f, icon.getY() + 2.0f, 8.0f, 8.0f);
                p.addEllipse(icon.getRight() - 12.0f, icon.getBottom() - 10.0f, 8.0f, 8.0f);
                break;
            }
            case Glyph::Library: // stacked rows
                for (int i = 0; i < 3; ++i)
                    p.addRoundedRectangle(icon.getX(), icon.getY() + i * 8.0f, 24.0f, 4.0f, 1.5f);
                break;
            case Glyph::Settings: { // gear-ish ring
                p.addEllipse(icon.reduced(3.0f));
                p.addEllipse(icon.reduced(9.0f));
                break;
            }
        }
        g.fillPath(p);
    }
private:
    Glyph glyph_;
};

//==============================================================================
// Slim top banner shown when an update is available / downloading / ready.
class MainComponent::UpdateBanner : public juce::Component {
public:
    explicit UpdateBanner(rtg::app::UpdateChecker& u) : updater_(u) {
        action_.getProperties().set("primary", true);
        action_.onClick = [this] {
            if (updater_.state() == State::UpdateAvailable) updater_.downloadUpdate();
            else if (updater_.state() == State::ReadyToInstall) updater_.restartAndInstall();
        };
        addAndMakeVisible(action_);
        dismiss_.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(dismiss_);
    }

    std::function<void()> onDismiss;

    void refresh() {
        switch (updater_.state()) {
            case State::UpdateAvailable:
                message_ = "Update " + updater_.latestVersion() + " available";
                action_.setButtonText("Download"); action_.setVisible(true); break;
            case State::Downloading:
                message_ = "Downloading update… "
                         + juce::String(juce::roundToInt(updater_.downloadProgress() * 100.0f)) + "%";
                action_.setVisible(false); break;
            case State::ReadyToInstall:
                message_ = "Update " + updater_.latestVersion() + " ready";
                action_.setButtonText("Restart & install"); action_.setVisible(true); break;
            default:
                message_ = {}; action_.setVisible(false); break;
        }
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accent = lnf ? lnf->accent() : Colors::accentRiddim;
        g.setColour(accent.withAlpha(0.18f));
        g.fillRect(getLocalBounds());
        g.setColour(Colors::text);
        g.setFont(rtgSansFont(13.0f, true));
        g.drawText(message_, getLocalBounds().withTrimmedLeft(14).withTrimmedRight(240),
                   juce::Justification::centredLeft, true);
    }

    void resized() override {
        auto b = getLocalBounds().reduced(4);
        dismiss_.setBounds(b.removeFromRight(28).withSizeKeepingCentre(22, 22));
        if (action_.isVisible()) { b.removeFromRight(6); action_.setBounds(b.removeFromRight(150).reduced(0, 2)); }
    }

private:
    rtg::app::UpdateChecker& updater_;
    juce::String message_;
    juce::TextButton action_ { "Download" };
    juce::TextButton dismiss_ { juce::CharPointer_UTF8("\xC3\x97") };
};

//==============================================================================
MainComponent::MainComponent() {
    setLookAndFeel(&lookAndFeel_);

    const char* names[kNumPages] = { "Generate", "Train", "Library", "Settings" };
    NavButton::Glyph glyphs[kNumPages] = { NavButton::Glyph::Generate, NavButton::Glyph::Train,
                                           NavButton::Glyph::Library, NavButton::Glyph::Settings };
    for (int i = 0; i < kNumPages; ++i) {
        navButtons_[i] = std::make_unique<NavButton>(names[i], glyphs[i]);
        navButtons_[i]->onClick = [this, i] { showPage(i); };
        addAndMakeVisible(*navButtons_[i]);
    }

    generatePage_ = std::make_unique<GeneratePage>(controller_);
    trainPage_ = std::make_unique<TrainPage>(controller_);
    libraryPage_ = std::make_unique<LibraryPage>(controller_);
    settingsPage_ = std::make_unique<SettingsPage>(controller_, updater_);
    transport_ = std::make_unique<TransportBar>(controller_);
    banner_ = std::make_unique<UpdateBanner>(updater_);

    generatePage_->onGenreChanged = [this](rtg::Genre g) { updateAccent(g); };
    trainPage_->onGenreChanged = [this](rtg::Genre g) { updateAccent(g); };
    banner_->onDismiss = [this] { bannerDismissed_ = true; resized(); };

    addChildComponent(*generatePage_);
    addChildComponent(*trainPage_);
    addChildComponent(*libraryPage_);
    addChildComponent(*settingsPage_);
    addAndMakeVisible(*transport_);
    addChildComponent(*banner_);

    updater_.addChangeListener(this);

    showPage(0);
    updateAccent(rtg::Genre::Riddim);
    setSize(1180, 760);

    updater_.checkForUpdates();
}

MainComponent::~MainComponent() {
    updater_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void MainComponent::updateAccent(rtg::Genre g) {
    lookAndFeel_.setAccent(g == rtg::Genre::Riddim ? Colors::accentRiddim : Colors::accentTrap);
    repaint();
    for (auto& b : navButtons_) if (b) b->repaint();
    if (banner_) banner_->repaint();
    if (transport_) transport_->repaint();
    if (generatePage_) generatePage_->repaint();
    if (trainPage_) trainPage_->repaint();
}

void MainComponent::showPage(int index) {
    currentPage_ = index;
    for (int i = 0; i < kNumPages; ++i)
        navButtons_[i]->setToggleState(i == index, juce::dontSendNotification);
    generatePage_->setVisible(index == 0);
    trainPage_->setVisible(index == 1);
    libraryPage_->setVisible(index == 2);
    settingsPage_->setVisible(index == 3);
    for (auto& b : navButtons_) if (b) b->repaint();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    const bool show = !bannerDismissed_
                   && (updater_.state() == State::UpdateAvailable
                    || updater_.state() == State::Downloading
                    || updater_.state() == State::ReadyToInstall);
    banner_->setVisible(show);
    if (show) banner_->refresh();
    resized();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(Colors::windowBg);
    // Rail background.
    g.setColour(Colors::panelBg);
    g.fillRect(0, 0, 64, getHeight());
    g.setColour(Colors::panelStroke);
    g.drawVerticalLine(64, 0.0f, (float) getHeight());
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    auto rail = area.removeFromLeft(64);

    // Nav buttons in rail.
    rail.removeFromTop(16);
    for (auto& b : navButtons_) {
        b->setBounds(rail.removeFromTop(56));
        rail.removeFromTop(4);
    }

    // Bottom transport.
    transport_->setBounds(area.removeFromBottom(56));

    // Top banner (28 px) if visible.
    if (banner_ && banner_->isVisible())
        banner_->setBounds(area.removeFromTop(28));

    generatePage_->setBounds(area);
    trainPage_->setBounds(area);
    libraryPage_->setBounds(area);
    settingsPage_->setBounds(area);
}

} // namespace rtg::ui
