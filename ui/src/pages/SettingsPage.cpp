#include "SettingsPage.h"
#include "../RtgLookAndFeel.h"

namespace rtg::ui {

using State = rtg::app::UpdateChecker::State;

SettingsPage::SettingsPage(rtg::app::GenerationController& controller,
                           rtg::app::UpdateChecker& updater)
    : controller_(controller), updater_(updater) {
    updater_.addChangeListener(this);
    controller_.addChangeListener(this);

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

    // ---- Reference Calibration panel ----
    calStatusLabel_.setFont(rtgSansFont(14.0f, true));
    calStatusLabel_.setColour(juce::Label::textColourId, Colors::text);
    addAndMakeVisible(calStatusLabel_);

    calGenreBox_.addItem("Riddim", 1);
    calGenreBox_.addItem("Trap", 2);
    calGenreBox_.addItem("Both", 3);
    calGenreBox_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(calGenreBox_);

    calAddButton_.getProperties().set("primary", true);
    calAddButton_.onClick = [this] { chooseReferenceFiles(); };
    addAndMakeVisible(calAddButton_);

    calClearButton_.onClick = [this] {
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon,
            "Clear calibration",
            "Remove the active reference calibration and return to built-in defaults?",
            "Clear", "Cancel", this,
            juce::ModalCallbackFunction::create([this](int result) {
                if (result == 1) { controller_.clearCalibration(); refreshCalibrationUI(); }
            }));
    };
    addAndMakeVisible(calClearButton_);

    calResultLabel_.setFont(rtgSansFont(12.0f));
    calResultLabel_.setColour(juce::Label::textColourId, Colors::dim);
    addAndMakeVisible(calResultLabel_);

    calProgressBar_.setColour(juce::ProgressBar::backgroundColourId, Colors::panelBg2);
    addChildComponent(calProgressBar_);

    refreshUpdateUI();
    refreshCalibrationUI();
}

SettingsPage::~SettingsPage() {
    stopTimer();
    updater_.removeChangeListener(this);
    controller_.removeChangeListener(this);
}

void SettingsPage::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &updater_) refreshUpdateUI();
    else                     refreshCalibrationUI();
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

void SettingsPage::refreshCalibrationUI() {
    const auto sum = controller_.calibrationSummary();
    const bool analyzing = controller_.isAnalyzingReferences();

    juce::String status;
    if (!sum.active) {
        status = juce::String::fromUTF8("No calibration — generations use built-in defaults");
    } else if (sum.perGenre) {
        status = juce::String::fromUTF8("Active: riddim ") + juce::String(sum.riddimRefs)
               + juce::String::fromUTF8(" refs · trap ") + juce::String(sum.trapRefs)
               + juce::String::fromUTF8(" refs · target ") + juce::String(sum.targetLufs, 1) + " LUFS";
    } else {
        status = juce::String::fromUTF8("Active: combined ") + juce::String(sum.combinedRefs)
               + juce::String::fromUTF8(" refs · target ") + juce::String(sum.targetLufs, 1) + " LUFS";
    }
    calStatusLabel_.setText(status, juce::dontSendNotification);

    calProgressValue_ = controller_.referenceProgress();
    calProgressBar_.setVisible(analyzing);
    calResultLabel_.setText(controller_.referenceStatusText(), juce::dontSendNotification);

    calAddButton_.setEnabled(!analyzing);
    calGenreBox_.setEnabled(!analyzing);
    calClearButton_.setEnabled(!analyzing && sum.active);

    if (analyzing && !isTimerRunning()) startTimerHz(15);
    if (!analyzing && isTimerRunning()) stopTimer();
    repaint();
}

void SettingsPage::chooseReferenceFiles() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select reference tracks",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.ogg");
    const int chooserFlags = juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectFiles
                           | juce::FileBrowserComponent::canSelectMultipleItems;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        const juce::Array<juce::File> results = fc.getResults();
        if (results.isEmpty()) return;
        const int genreSel = calGenreBox_.getSelectedId() - 1;   // 0 riddim, 1 trap, 2 both
        controller_.analyzeReferencesAsync(results, genreSel);
        refreshCalibrationUI();
    });
}

void SettingsPage::timerCallback() {
    calProgressValue_ = controller_.referenceProgress();
    calProgressBar_.repaint();
    calResultLabel_.setText(controller_.referenceStatusText(), juce::dontSendNotification);
    if (!controller_.isAnalyzingReferences()) refreshCalibrationUI();
}

// Lays out the bottom Reference-Calibration band. Mirrors the row sequence used
// by paint() so headers/copy and controls line up. Returns nothing; positions
// child components. `cal` is consumed.
void SettingsPage::resized() {
    auto area = getLocalBounds().reduced(16);

    // Reserve the bottom band for the reference-calibration panel.
    auto cal = area.removeFromBottom(185);
    area.removeFromBottom(12);

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

    // Data row at bottom of the updates column.
    auto dataRow = up.removeFromBottom(30);
    openDataButton_.setBounds(dataRow.removeFromRight(80));
    dataRow.removeFromRight(8);
    dataPathLabel_.setBounds(dataRow);

    // ---- Reference calibration band (row sequence shared with paint()) ----
    cal.removeFromTop(24);                       // header  (paint)
    cal.removeFromTop(4);
    calStatusLabel_.setBounds(cal.removeFromTop(22));
    cal.removeFromTop(6);
    cal.removeFromTop(32);                        // explanatory copy (paint)
    cal.removeFromTop(8);
    auto row = cal.removeFromTop(32);
    calGenreBox_.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(10);
    calAddButton_.setBounds(row.removeFromLeft(210));
    row.removeFromLeft(10);
    calClearButton_.setBounds(row.removeFromLeft(150));
    cal.removeFromTop(10);
    calProgressBar_.setBounds(cal.removeFromTop(18));
    cal.removeFromTop(4);
    calResultLabel_.setBounds(cal.removeFromTop(20));
}

void SettingsPage::paint(juce::Graphics& g) {
    auto area = getLocalBounds().reduced(16);

    auto cal = area.removeFromBottom(185);
    area.removeFromBottom(12);

    g.setColour(Colors::text);
    g.setFont(rtgSansFont(16.0f, true));
    auto audio = area.removeFromLeft(area.getWidth() / 2 - 8);
    g.drawText("Audio device", audio.removeFromTop(24), juce::Justification::topLeft, false);
    area.removeFromLeft(16);
    g.drawText("Updates", area.removeFromTop(24), juce::Justification::topLeft, false);

    // Reference calibration band.
    g.setColour(Colors::panelStroke);
    g.drawLine((float) cal.getX(), (float) cal.getY() - 6.0f,
               (float) cal.getRight(), (float) cal.getY() - 6.0f, 1.0f);

    g.setColour(Colors::text);
    g.setFont(rtgSansFont(16.0f, true));
    g.drawText("Reference Calibration", cal.removeFromTop(24), juce::Justification::topLeft, false);
    cal.removeFromTop(4);
    cal.removeFromTop(22);   // status label (component)
    cal.removeFromTop(6);
    g.setColour(Colors::dim);
    g.setFont(rtgSansFont(12.0f));
    g.drawFittedText(juce::String::fromUTF8(
        "Drop in 3–5 commercial tracks you love. The engine measures their loudness and tonal "
        "balance and targets that character. Analysis only — audio never leaves your computer."),
        cal.removeFromTop(32), juce::Justification::topLeft, 2);
}

} // namespace rtg::ui
