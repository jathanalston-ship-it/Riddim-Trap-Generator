#include "SettingsPage.h"
#include "../RtgLookAndFeel.h"

namespace rtg::ui {

using State = rtg::app::UpdateChecker::State;

SettingsPage::SettingsPage(rtg::app::GenerationController& controller,
                           rtg::app::UpdateChecker& updater)
    : controller_(controller), updater_(updater) {
    updater_.addChangeListener(this);

    deviceSelector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        controller_.deviceManager(),
        /*minInput*/ 0, /*maxInput*/ 0,
        /*minOutput*/ 2, /*maxOutput*/ 2,
        /*showMidiIn*/ false, /*showMidiOut*/ false,
        /*stereoPairs*/ true, /*hideAdvanced*/ false);
    addAndMakeVisible(*deviceSelector_);

    versionLabel_.setFont(rtgMonoFont(14.0f));
    versionLabel_.setColour(juce::Label::textColourId, Colors::text);
    versionLabel_.setText("Version " + updater_.currentVersion(), juce::dontSendNotification);
    addAndMakeVisible(versionLabel_);

    updateStatus_.setFont(rtgSansFont(13.0f));
    updateStatus_.setColour(juce::Label::textColourId, Colors::dim);
    addAndMakeVisible(updateStatus_);

    checkButton_.onClick = [this] { updater_.checkForUpdates(); };
    downloadButton_.onClick = [this] { updater_.downloadUpdate(); };
    installButton_.onClick = [this] { updater_.restartAndInstall(); };
    addAndMakeVisible(checkButton_);
    addChildComponent(downloadButton_);
    addChildComponent(installButton_);

    releaseNotes_.setMultiLine(true);
    releaseNotes_.setReadOnly(true);
    releaseNotes_.setFont(rtgSansFont(13.0f));
    releaseNotes_.setColour(juce::TextEditor::backgroundColourId, Colors::panelBg2);
    addChildComponent(releaseNotes_);

    dataPathLabel_.setFont(rtgMonoFont(12.0f));
    dataPathLabel_.setColour(juce::Label::textColourId, Colors::dim);
    addAndMakeVisible(dataPathLabel_);
    openDataButton_.onClick = [this] {
        controller_.dataDirectory().getChildFile("library").revealToUser();
    };
    addAndMakeVisible(openDataButton_);

    refreshUpdateUI();
}

SettingsPage::~SettingsPage() {
    updater_.removeChangeListener(this);
}

void SettingsPage::changeListenerCallback(juce::ChangeBroadcaster*) {
    refreshUpdateUI();
}

void SettingsPage::refreshUpdateUI() {
    juce::String status;
    switch (updater_.state()) {
        case State::Idle:            status = "No update check yet."; break;
        case State::Checking:        status = "Checking for updates…"; break;
        case State::UpToDate:        status = "You are up to date."; break;
        case State::UpdateAvailable: status = "Update " + updater_.latestVersion() + " available."; break;
        case State::Downloading:     status = "Downloading… "
                                            + juce::String(juce::roundToInt(updater_.downloadProgress() * 100.0f)) + "%"; break;
        case State::ReadyToInstall:  status = "Ready to install " + updater_.latestVersion() + "."; break;
        case State::Error:           status = "Update error: " + updater_.errorMessage(); break;
    }
    updateStatus_.setText(status, juce::dontSendNotification);

    downloadButton_.setVisible(updater_.state() == State::UpdateAvailable
                               || updater_.state() == State::Downloading);
    downloadButton_.setEnabled(updater_.state() == State::UpdateAvailable);
    installButton_.setVisible(updater_.state() == State::ReadyToInstall);

    const auto notes = updater_.releaseNotes();
    releaseNotes_.setVisible(notes.isNotEmpty());
    if (notes.isNotEmpty() && releaseNotes_.getText() != notes)
        releaseNotes_.setText(notes, false);

    const int count = (int) controller_.library().all().size();
    dataPathLabel_.setText(controller_.dataDirectory().getChildFile("library").getFullPathName()
                           + "   (" + juce::String(count) + " sounds)",
                           juce::dontSendNotification);
    resized();
    repaint();
}

void SettingsPage::resized() {
    auto area = getLocalBounds().reduced(16);

    // Audio panel (left half).
    auto audio = area.removeFromLeft(area.getWidth() / 2 - 8);
    audio.removeFromTop(28); // header text drawn in paint
    if (deviceSelector_) deviceSelector_->setBounds(audio.reduced(4));

    area.removeFromLeft(16);

    // Updates panel (right).
    auto up = area;
    up.removeFromTop(28); // header
    versionLabel_.setBounds(up.removeFromTop(24));
    updateStatus_.setBounds(up.removeFromTop(22));
    up.removeFromTop(6);
    auto btnRow = up.removeFromTop(30);
    checkButton_.setBounds(btnRow.removeFromLeft(150));
    btnRow.removeFromLeft(8);
    if (downloadButton_.isVisible()) downloadButton_.setBounds(btnRow.removeFromLeft(150));
    else if (installButton_.isVisible()) installButton_.setBounds(btnRow.removeFromLeft(150));
    up.removeFromTop(10);

    if (releaseNotes_.isVisible())
        releaseNotes_.setBounds(up.removeFromTop(juce::jmax(0, up.getHeight() - 70)));
    up.removeFromBottom(4);

    // Data row at bottom.
    auto dataRow = up.removeFromBottom(30);
    openDataButton_.setBounds(dataRow.removeFromRight(80));
    dataRow.removeFromRight(8);
    dataPathLabel_.setBounds(dataRow);
}

void SettingsPage::paint(juce::Graphics& g) {
    auto area = getLocalBounds().reduced(16);
    g.setColour(Colors::text);
    g.setFont(rtgSansFont(16.0f, true));

    auto audio = area.removeFromLeft(area.getWidth() / 2 - 8);
    g.drawText("Audio device", audio.removeFromTop(24), juce::Justification::topLeft, false);
    area.removeFromLeft(16);
    g.drawText("Updates", area.removeFromTop(24), juce::Justification::topLeft, false);
}

} // namespace rtg::ui
