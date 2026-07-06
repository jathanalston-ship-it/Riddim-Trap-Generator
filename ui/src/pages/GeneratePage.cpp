#include "GeneratePage.h"
#include "../RtgLookAndFeel.h"

namespace rtg::ui {

//==============================================================================
void GeneratePage::GenreCard::paintButton(juce::Graphics& g, bool highlighted, bool) {
    auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
    const juce::Colour cardAccent = isTrap ? Colors::accentTrap : Colors::accentRiddim;
    (void) lnf;
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    const bool on = getToggleState();
    g.setColour(on ? cardAccent.withAlpha(0.18f) : Colors::panelBg2);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(on ? cardAccent : (highlighted ? Colors::dim : Colors::panelStroke));
    g.drawRoundedRectangle(b, 8.0f, on ? 2.0f : 1.0f);
    g.setColour(on ? cardAccent : Colors::text);
    g.setFont(rtgSansFont(20.0f, true));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
GeneratePage::GeneratePage(rtg::app::GenerationController& controller)
    : controller_(controller), waveform_(controller) {
    controller_.addChangeListener(this);

    // Genre cards.
    trapCard_.isTrap = true;
    for (auto* c : { &riddimCard_, &trapCard_ }) {
        addAndMakeVisible(c);
        c->setClickingTogglesState(false);
    }
    riddimCard_.setToggleState(true, juce::dontSendNotification);
    riddimCard_.onClick = [this] { applyGenre(rtg::Genre::Riddim, true); };
    trapCard_.onClick = [this] { applyGenre(rtg::Genre::Trap, true); };

    // BPM knob.
    bpmKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bpmKnob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpmKnob_.setRange(140.0, 150.0, 0.5);
    bpmKnob_.setValue(145.0, juce::dontSendNotification);
    bpmKnob_.textFromValueFunction = [](double v) { return juce::String(v, 1) + " BPM"; };
    addAndMakeVisible(bpmKnob_);

    // Length slider (mm:ss).
    lengthSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    lengthSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    lengthSlider_.setRange(60.0, 300.0, 1.0);
    lengthSlider_.setValue(180.0, juce::dontSendNotification);
    lengthSlider_.textFromValueFunction = [](double v) {
        int s = (int) v; return juce::String::formatted("%d:%02d", s / 60, s % 60);
    };
    addAndMakeVisible(lengthSlider_);

    // Drops stepper.
    dropsStepper_.setSliderStyle(juce::Slider::IncDecButtons);
    dropsStepper_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 22);
    dropsStepper_.setRange(1.0, 4.0, 1.0);
    dropsStepper_.setValue(2.0, juce::dontSendNotification);
    dropsStepper_.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
    addAndMakeVisible(dropsStepper_);

    // Intro combo.
    for (int i = 0; i <= (int) rtg::IntroStyle::Fakeout; ++i)
        introCombo_.addItem(rtg::introStyleName((rtg::IntroStyle) i), i + 1);
    introCombo_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(introCombo_);

    // Macro knobs.
    static const int defaults[kNumMacros] = { 65, 70, 55, 50, 30, 25 };
    for (int i = 0; i < kNumMacros; ++i) {
        auto& k = macroKnobs_[i];
        k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        k.setRange(0.0, 100.0, 1.0);
        k.setValue(defaults[i], juce::dontSendNotification);
        addAndMakeVisible(k);
    }

    // Seed row.
    seedField_.setFont(rtgMonoFont(15.0f));
    seedField_.setText("auto", juce::dontSendNotification);
    seedField_.setInputRestrictions(0, "0123456789autoAUTO");
    seedField_.setJustification(juce::Justification::centredLeft);
    addAndMakeVisible(seedField_);
    diceButton_.onClick = [this] {
        seedField_.setText(juce::String(juce::Random::getSystemRandom().nextInt64() & 0x7fffffffffffffffLL));
    };
    addAndMakeVisible(diceButton_);

    // Centre.
    addAndMakeVisible(waveform_);
    waveform_.onSeek = [this](double t) { if (onSeek) onSeek(t); else controller_.seekSeconds(t); };

    // Right action zone.
    generateButton_.getProperties().set("primary", true);
    generateButton_.onClick = [this] { onGeneratePressed(); };
    addAndMakeVisible(generateButton_);
    exportButton_.onClick = [this] { onExportPressed(); };
    exportButton_.setEnabled(false);
    addChildComponent(exportButton_);

    applyGenre(rtg::Genre::Riddim, false);
}

GeneratePage::~GeneratePage() {
    controller_.removeChangeListener(this);
}

//==============================================================================
void GeneratePage::applyGenre(rtg::Genre g, bool notify) {
    genre_ = g;
    riddimCard_.setToggleState(g == rtg::Genre::Riddim, juce::dontSendNotification);
    trapCard_.setToggleState(g == rtg::Genre::Trap, juce::dontSendNotification);

    const double cur = bpmKnob_.getValue();
    if (g == rtg::Genre::Riddim) bpmKnob_.setRange(140.0, 150.0, 0.5);
    else                         bpmKnob_.setRange(130.0, 170.0, 0.5);
    bpmKnob_.setValue(juce::jlimit(bpmKnob_.getMinimum(), bpmKnob_.getMaximum(),
                                   g == rtg::Genre::Riddim ? juce::jlimit(140.0, 150.0, cur)
                                                           : cur),
                      juce::dontSendNotification);

    if (notify && onGenreChanged) onGenreChanged(g);
    repaint();
}

//==============================================================================
rtg::Params GeneratePage::buildParams() {
    rtg::Params p;
    p.genre = genre_;
    p.bpm = bpmKnob_.getValue();
    p.lengthSec = lengthSlider_.getValue();
    p.energy     = (int) macroKnobs_[0].getValue();
    p.aggression = (int) macroKnobs_[1].getValue();
    p.darkness   = (int) macroKnobs_[2].getValue();
    p.complexity = (int) macroKnobs_[3].getValue();
    p.melody     = (int) macroKnobs_[4].getValue();
    p.chaos      = (int) macroKnobs_[5].getValue();
    p.dropCount  = (int) dropsStepper_.getValue();
    p.introStyle = (rtg::IntroStyle) (introCombo_.getSelectedId() - 1);

    juce::String seedTxt = seedField_.getText().trim();
    uint64_t seed = 0;
    if (!seedTxt.equalsIgnoreCase("auto") && seedTxt.isNotEmpty())
        seed = (uint64_t) seedTxt.getLargeIntValue();
    if (seed == 0) {
        seed = (uint64_t) (juce::Random::getSystemRandom().nextInt64()) | 1ull;
        seedField_.setText(juce::String((juce::int64) seed), juce::dontSendNotification);
    }
    p.seed = seed;
    return p;
}

void GeneratePage::onGeneratePressed() {
    if (controller_.isGenerating()) { controller_.cancelGeneration(); return; }
    controller_.startGeneration(buildParams());
    generateButton_.setButtonText("CANCEL");
    repaint();
}

void GeneratePage::onExportPressed() {
    const auto& r = controller_.result();
    if (!r.has_value()) return;
    juce::String name = juce::String(rtg::genreName(r->plan.params.genre)) + "_"
                      + juce::String(juce::roundToInt(r->plan.params.bpm)) + "bpm_seed"
                      + juce::String((juce::int64) r->plan.params.seed) + ".wav";
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export WAV", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(name),
        "*.wav");
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
               | juce::FileBrowserComponent::warnAboutOverwriting;
    juce::Component::SafePointer<GeneratePage> safe(this);
    fileChooser_->launchAsync(flags, [safe](const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f != juce::File()) safe->controller_.exportWav(f);
    });
}

//==============================================================================
void GeneratePage::changeListenerCallback(juce::ChangeBroadcaster*) {
    updateFromResult();
}

void GeneratePage::updateFromResult() {
    if (controller_.isGenerating()) {
        generateButton_.setButtonText("CANCEL");
    } else {
        generateButton_.setButtonText("GENERATE");
    }
    const bool hasResult = controller_.result().has_value();
    exportButton_.setVisible(hasResult);
    exportButton_.setEnabled(hasResult);
    if (hasResult) {
        // reflect the seed actually used
        seedField_.setText(juce::String((juce::int64) controller_.result()->plan.params.seed),
                           juce::dontSendNotification);
    }
    repaint();
}

//==============================================================================
void GeneratePage::resized() {
    auto area = getLocalBounds().reduced(12);
    auto left = area.removeFromLeft(300);
    area.removeFromLeft(12);
    auto right = area.removeFromRight(240);
    area.removeFromRight(12);
    waveform_.setBounds(area);

    // Left column.
    {
        auto l = left;
        auto cards = l.removeFromTop(64);
        riddimCard_.setBounds(cards.removeFromLeft(cards.getWidth() / 2 - 4));
        trapCard_.setBounds(cards.removeFromRight(cards.getWidth() - 8));
        l.removeFromTop(14);

        auto bpmRow = l.removeFromTop(96);
        bpmKnob_.setBounds(bpmRow.removeFromLeft(96));
        // (label drawn in paint)
        l.removeFromTop(6);

        l.removeFromTop(18);                       // "LENGTH" label space
        lengthSlider_.setBounds(l.removeFromTop(24));
        l.removeFromTop(10);

        auto dropRow = l.removeFromTop(26);
        dropsStepper_.setBounds(dropRow.removeFromLeft(150));
        l.removeFromTop(10);

        l.removeFromTop(16);                        // "INTRO" label space
        introCombo_.setBounds(l.removeFromTop(26));
        l.removeFromTop(16);

        // 2x3 knob grid.
        auto grid = l.removeFromTop(2 * 92 + 8);
        for (int row = 0; row < 2; ++row) {
            auto rrow = grid.removeFromTop(92);
            if (row == 0) grid.removeFromTop(8);
            const int cw = rrow.getWidth() / 3;
            for (int col = 0; col < 3; ++col) {
                auto cell = rrow.removeFromLeft(cw);
                cell.removeFromTop(16); // name label
                macroKnobs_[row * 3 + col].setBounds(cell);
            }
        }
        l.removeFromTop(12);

        auto seedRow = l.removeFromTop(28);
        diceButton_.setBounds(seedRow.removeFromRight(32));
        seedRow.removeFromRight(6);
        seedField_.setBounds(seedRow);
    }

    // Right column.
    {
        auto r = right;
        generateButton_.setBounds(r.removeFromTop(56));
        r.removeFromTop(12);
        exportButton_.setBounds(r.removeFromTop(34));
    }
}

void GeneratePage::paint(juce::Graphics& g) {
    g.setColour(Colors::dim);
    g.setFont(rtgSansFont(11.0f, true));

    auto label = [&](juce::Rectangle<int> r, const juce::String& t, juce::Justification j) {
        g.drawText(t, r, j, false);
    };

    // Left panel labels near controls.
    label(bpmKnob_.getBounds().translated(bpmKnob_.getWidth() + 8, 8).withHeight(16).withWidth(120),
          "TEMPO", juce::Justification::topLeft);
    label(lengthSlider_.getBounds().translated(0, -16).withHeight(14), "LENGTH", juce::Justification::topLeft);
    label(dropsStepper_.getBounds().translated(dropsStepper_.getWidth() + 8, 4).withHeight(16).withWidth(80),
          "DROPS", juce::Justification::topLeft);
    label(introCombo_.getBounds().translated(0, -15).withHeight(14), "INTRO", juce::Justification::topLeft);

    for (int i = 0; i < kNumMacros; ++i) {
        auto kb = macroKnobs_[i].getBounds();
        label({ kb.getX(), kb.getY() - 16, kb.getWidth(), 14 }, macroNames_[i], juce::Justification::centred);
    }

    label(seedField_.getBounds().translated(0, -15).withHeight(14), "SEED", juce::Justification::topLeft);

    // Right stats mini-table (after a result).
    const auto& res = controller_.result();
    if (res.has_value() && exportButton_.isVisible()) {
        auto r = exportButton_.getBounds().withTop(exportButton_.getBottom() + 14)
                     .withHeight(160).withWidth(exportButton_.getWidth())
                     .withX(exportButton_.getX());
        g.setColour(Colors::panelBg);
        g.fillRoundedRectangle(r.toFloat(), 8.0f);
        auto inner = r.reduced(12, 10);
        const auto& st = res->stats;
        auto rowH = 22;
        auto statRow = [&](const juce::String& k, const juce::String& v) {
            auto row = inner.removeFromTop(rowH);
            g.setColour(Colors::dim); g.setFont(rtgSansFont(12.0f));
            g.drawText(k, row.removeFromLeft(110), juce::Justification::centredLeft, false);
            g.setColour(Colors::text); g.setFont(rtgMonoFont(12.0f));
            g.drawText(v, row, juce::Justification::centredRight, false);
        };
        statRow("LUFS", juce::String(st.integratedLufs, 1));
        statRow("True peak", juce::String(st.truePeakDb, 1) + " dB");
        statRow("Crest", juce::String(st.crestDb, 1) + " dB");
        statRow("Render", juce::String(res->renderSeconds, 2) + " s");
        statRow("New sounds", juce::String(res->newSoundsIngested));
        auto row = inner.removeFromTop(rowH);
        g.setColour(Colors::dim); g.setFont(rtgSansFont(12.0f));
        g.drawText("Seed", row.removeFromLeft(70), juce::Justification::centredLeft, false);
        g.setColour(Colors::text); g.setFont(rtgMonoFont(11.0f));
        g.drawText(juce::String((juce::int64) res->plan.params.seed), row,
                   juce::Justification::centredRight, false);
    }
}

} // namespace rtg::ui
