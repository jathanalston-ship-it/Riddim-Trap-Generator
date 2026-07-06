#include "WaveformView.h"
#include "../RtgLookAndFeel.h"

namespace rtg::ui {

static juce::Colour sectionColour(rtg::SectionType t, juce::Colour accent) {
    switch (t) {
        case rtg::SectionType::Intro: return Colors::sectIntro;
        case rtg::SectionType::Build: return Colors::sectBuild;
        case rtg::SectionType::Drop:  return accent;
        case rtg::SectionType::Break: return Colors::sectBreak;
        case rtg::SectionType::Outro: return Colors::sectOutro;
    }
    return Colors::sectIntro;
}

WaveformView::WaveformView(rtg::app::GenerationController& controller)
    : controller_(controller) {
    controller_.addChangeListener(this);
    startTimerHz(30);
}

WaveformView::~WaveformView() {
    controller_.removeChangeListener(this);
}

void WaveformView::changeListenerCallback(juce::ChangeBroadcaster*) {
    rebuildPeaks();
    repaint();
}

double WaveformView::totalSeconds() const {
    const auto& r = controller_.result();
    if (!r.has_value() || r->sampleRate <= 0) return 0.0;
    return double(r->master.size()) / r->sampleRate;
}

void WaveformView::rebuildPeaks() {
    const auto& r = controller_.result();
    if (!r.has_value()) { peaksResultKey_ = nullptr; peaksMin_.clear(); peaksMax_.clear(); return; }
    if (peaksResultKey_ == &r.value()) return; // already cached for this result

    const auto& m = r->master;
    const int n = (int) m.size();
    const int columns = 2048;
    peaksMin_.assign(columns, 0.0f);
    peaksMax_.assign(columns, 0.0f);
    if (n <= 0) { peaksResultKey_ = &r.value(); return; }

    for (int c = 0; c < columns; ++c) {
        const int a = (int) ((int64_t) c * n / columns);
        const int b = juce::jmax(a + 1, (int) ((int64_t) (c + 1) * n / columns));
        float lo = 0.0f, hi = 0.0f;
        for (int i = a; i < b && i < n; ++i) {
            const float s = 0.5f * (m.l[i] + m.r[i]);
            lo = juce::jmin(lo, s);
            hi = juce::jmax(hi, s);
        }
        peaksMin_[c] = lo;
        peaksMax_[c] = hi;
    }
    peaksResultKey_ = &r.value();
}

void WaveformView::timerCallback() {
    const bool gen = controller_.isGenerating();
    if (gen) { repaint(); wasGenerating_ = true; return; }
    if (wasGenerating_) { wasGenerating_ = false; repaint(); }
    if (controller_.isPlaying()) repaint();
}

void WaveformView::mouseDown(const juce::MouseEvent& e) {
    const double total = totalSeconds();
    if (total <= 0.0 || controller_.isGenerating()) return;
    const double t = juce::jlimit(0.0, total, (e.position.x / (double) getWidth()) * total);
    if (onSeek) onSeek(t);
}

void WaveformView::paint(juce::Graphics& g) {
    auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lnf ? lnf->accent() : Colors::accentRiddim;
    auto bounds = getLocalBounds().toFloat();

    g.setColour(Colors::panelBg);
    g.fillRoundedRectangle(bounds, 8.0f);

    const auto& r = controller_.result();

    if (controller_.isGenerating()) {
        // Staged progress overlay.
        g.setColour(Colors::text);
        g.setFont(rtgSansFont(18.0f, true));
        auto centre = bounds.reduced(40.0f);
        g.drawText(controller_.progressStage() + "…",
                   centre.removeFromTop(centre.getHeight() * 0.5f).toNearestInt(),
                   juce::Justification::centredBottom, false);

        auto barArea = juce::Rectangle<float>(bounds.getCentreX() - 180.0f,
                                              bounds.getCentreY() + 8.0f, 360.0f, 8.0f);
        g.setColour(Colors::panelStroke);
        g.fillRoundedRectangle(barArea, 4.0f);
        g.setColour(accent);
        g.fillRoundedRectangle(barArea.withWidth(barArea.getWidth() * controller_.progress()), 4.0f);

        g.setColour(Colors::dim);
        g.setFont(rtgMonoFont(13.0f));
        g.drawText(juce::String(juce::roundToInt(controller_.progress() * 100.0f)) + "%",
                   barArea.translated(0, 18.0f).withHeight(16.0f).toNearestInt(),
                   juce::Justification::centred, false);
        return;
    }

    if (!r.has_value()) {
        // Placeholder grid + prompt.
        g.setColour(Colors::panelStroke.withAlpha(0.5f));
        for (float x = bounds.getX() + 40; x < bounds.getRight(); x += 40)
            g.drawVerticalLine((int) x, bounds.getY() + 12, bounds.getBottom() - 12);
        g.drawHorizontalLine((int) bounds.getCentreY(), bounds.getX() + 12, bounds.getRight() - 12);
        g.setColour(Colors::dim);
        g.setFont(rtgSansFont(16.0f, true));
        g.drawText("Configure and press GENERATE", bounds.toNearestInt(),
                   juce::Justification::centred, false);
        return;
    }

    // --- Result: section bands + waveform + chip + playhead -------------------
    const double total = totalSeconds();
    auto wave = bounds.reduced(2.0f);

    // Section bands.
    for (const auto& s : r->sections) {
        if (total <= 0) break;
        const float x0 = wave.getX() + (float) (s.startSec / total) * wave.getWidth();
        const float w = (float) (s.lengthSec / total) * wave.getWidth();
        auto band = juce::Rectangle<float>(x0, wave.getY(), w, wave.getHeight());
        g.setColour(sectionColour(s.type, accent).withAlpha(0.14f));
        g.fillRect(band);
        g.setColour(sectionColour(s.type, accent).withAlpha(0.35f));
        g.drawVerticalLine((int) x0, wave.getY(), wave.getBottom());
        g.setColour(Colors::dim);
        g.setFont(rtgSansFont(11.0f));
        g.drawText(rtg::sectionName(s.type),
                   band.removeFromTop(16.0f).reduced(4.0f, 0).toNearestInt(),
                   juce::Justification::topLeft, false);
    }

    // Waveform.
    if (!peaksMax_.empty()) {
        const float midY = wave.getCentreY();
        const float amp = wave.getHeight() * 0.42f;
        const int columns = (int) peaksMax_.size();
        g.setColour(Colors::text.withAlpha(0.85f));
        juce::RectangleList<float> bars;
        for (int px = 0; px < (int) wave.getWidth(); ++px) {
            const int c = juce::jlimit(0, columns - 1,
                                       (int) ((int64_t) px * columns / (int) wave.getWidth()));
            const float x = wave.getX() + px;
            const float hi = midY - peaksMax_[c] * amp;
            const float lo = midY - peaksMin_[c] * amp;
            bars.addWithoutMerging({ x, juce::jmin(hi, lo), 1.0f, juce::jmax(1.0f, std::abs(lo - hi)) });
        }
        g.fillRectList(bars);
    }

    // Readout chip (LUFS / TP / crest).
    {
        const auto& st = r->stats;
        juce::String txt = "LUFS " + juce::String(st.integratedLufs, 1)
                         + "   TP " + juce::String(st.truePeakDb, 1)
                         + "   CREST " + juce::String(st.crestDb, 1);
        g.setFont(rtgMonoFont(12.0f));
        const int w = juce::jmax(180, g.getCurrentFont().getStringWidth(txt) + 20);
        auto chip = juce::Rectangle<int>(getWidth() - w - 10, 10, w, 22);
        g.setColour(Colors::windowBg.withAlpha(0.75f));
        g.fillRoundedRectangle(chip.toFloat(), 5.0f);
        g.setColour(Colors::dim);
        g.drawText(txt, chip, juce::Justification::centred, false);
    }

    // Playhead.
    if (total > 0.0) {
        const float px = wave.getX() + (float) (controller_.playheadSeconds() / total) * wave.getWidth();
        g.setColour(accent);
        g.drawVerticalLine((int) px, wave.getY(), wave.getBottom());
    }
}

} // namespace rtg::ui
