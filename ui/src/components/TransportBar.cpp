#include "TransportBar.h"
#include "../RtgLookAndFeel.h"

namespace rtg::ui {

static std::unique_ptr<juce::Drawable> makeGlyph(std::function<void(juce::Path&)> build,
                                                 juce::Colour colour) {
    juce::Path p;
    build(p);
    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath(p);
    d->setFill(colour);
    return d;
}

static void playPath(juce::Path& p)  { p.addTriangle(6, 3, 6, 21, 20, 12); }
static void pausePath(juce::Path& p) { p.addRectangle(6, 3, 4, 18); p.addRectangle(14, 3, 4, 18); }
static void stopPath(juce::Path& p)  { p.addRectangle(5, 5, 14, 14); }

TransportBar::TransportBar(rtg::app::GenerationController& controller)
    : controller_(controller) {
    controller_.addChangeListener(this);

    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    playButton_.onClick = [this] { controller_.togglePlayback(); };
    stopButton_.onClick = [this] { controller_.stopPlayback(); };
    stopButton_.setImages(makeGlyph(stopPath, Colors::text).get());
    updatePlayGlyph();

    startTimerHz(30);
}

TransportBar::~TransportBar() {
    controller_.removeChangeListener(this);
}

bool TransportBar::hasResult() const { return controller_.result().has_value(); }

double TransportBar::totalSeconds() const {
    const auto& r = controller_.result();
    if (!r.has_value() || r->sampleRate <= 0) return 0.0;
    return double(r->master.size()) / r->sampleRate;
}

void TransportBar::updatePlayGlyph() {
    const bool wantPlay = !controller_.isPlaying();
    if (wantPlay == showingPlayGlyph_ && playButton_.getCurrentImage() != nullptr) return;
    showingPlayGlyph_ = wantPlay;
    playButton_.setImages(makeGlyph(wantPlay ? playPath : pausePath, Colors::text).get());
}

void TransportBar::changeListenerCallback(juce::ChangeBroadcaster*) {
    const bool enabled = hasResult();
    playButton_.setEnabled(enabled);
    stopButton_.setEnabled(enabled);
    updatePlayGlyph();
    repaint();
}

void TransportBar::timerCallback() {
    updatePlayGlyph();
    if (controller_.isPlaying()) repaint();
}

void TransportBar::resized() {
    auto b = getLocalBounds().reduced(10, 0);
    const int sz = juce::jmin(36, b.getHeight() - 8);
    playButton_.setBounds(b.removeFromLeft(sz).withSizeKeepingCentre(sz, sz));
    b.removeFromLeft(6);
    stopButton_.setBounds(b.removeFromLeft(sz).withSizeKeepingCentre(sz, sz));
}

juce::Rectangle<int> TransportBar::seekBarBounds() const {
    auto b = getLocalBounds().reduced(10, 0);
    b.removeFromLeft(36 + 6 + 36 + 12);        // buttons
    b.removeFromLeft(76);                        // elapsed time
    b.removeFromRight(76);                        // total time
    return b.withSizeKeepingCentre(b.getWidth(), 6);
}

void TransportBar::paint(juce::Graphics& g) {
    auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lnf ? lnf->accent() : Colors::accentRiddim;

    g.setColour(Colors::panelBg);
    g.fillRect(getLocalBounds());
    g.setColour(Colors::panelStroke);
    g.drawHorizontalLine(0, 0.0f, (float) getWidth());

    const double total = totalSeconds();
    const double pos = controller_.playheadSeconds();
    const bool enabled = hasResult();

    auto fmt = [](double s) {
        int t = juce::jmax(0, (int) s);
        return juce::String::formatted("%02d:%02d", t / 60, t % 60);
    };

    g.setFont(rtgMonoFont(14.0f));
    g.setColour(enabled ? Colors::text : Colors::dim);
    auto b = getLocalBounds().reduced(10, 0);
    b.removeFromLeft(36 + 6 + 36 + 12);
    g.drawText(fmt(pos), b.removeFromLeft(76), juce::Justification::centred, false);
    g.setColour(Colors::dim);
    g.drawText(fmt(total), b.removeFromRight(76), juce::Justification::centred, false);

    // Seek bar.
    auto seek = seekBarBounds().toFloat();
    g.setColour(Colors::panelStroke);
    g.fillRoundedRectangle(seek, 3.0f);
    if (enabled && total > 0.0) {
        g.setColour(accent);
        g.fillRoundedRectangle(seek.withWidth(seek.getWidth() * (float) (pos / total)), 3.0f);
    }
}

void TransportBar::seekFromMouse(const juce::MouseEvent& e) {
    const double total = totalSeconds();
    auto seek = seekBarBounds();
    if (!hasResult() || total <= 0.0 || seek.getWidth() <= 0) return;
    const double f = juce::jlimit(0.0, 1.0, (e.position.x - seek.getX()) / (double) seek.getWidth());
    controller_.seekSeconds(f * total);
    repaint();
}

void TransportBar::mouseDown(const juce::MouseEvent& e) { seekFromMouse(e); }
void TransportBar::mouseDrag(const juce::MouseEvent& e) { seekFromMouse(e); }

} // namespace rtg::ui
