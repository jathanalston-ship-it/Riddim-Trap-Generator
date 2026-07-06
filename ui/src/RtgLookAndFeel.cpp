#include "RtgLookAndFeel.h"

namespace rtg::ui {

juce::Font rtgMonoFont(float height) {
    return juce::Font(juce::FontOptions("Consolas;DejaVu Sans Mono;monospace", height, 0));
}

juce::Font rtgSansFont(float height, bool bold) {
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

RtgLookAndFeel::RtgLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, Colors::windowBg);
    setColour(juce::DocumentWindow::textColourId, Colors::text);

    setColour(juce::Label::textColourId, Colors::text);
    setColour(juce::Slider::textBoxTextColourId, Colors::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, Colors::panelBg2);
    setColour(juce::TextButton::buttonOnColourId, Colors::panelBg2);
    setColour(juce::TextButton::textColourOnId, Colors::text);
    setColour(juce::TextButton::textColourOffId, Colors::text);

    setColour(juce::TextEditor::backgroundColourId, Colors::panelBg2);
    setColour(juce::TextEditor::textColourId, Colors::text);
    setColour(juce::TextEditor::outlineColourId, Colors::panelStroke);
    setColour(juce::TextEditor::focusedOutlineColourId, accent_);
    setColour(juce::TextEditor::highlightColourId, accent_.withAlpha(0.3f));

    setColour(juce::ComboBox::backgroundColourId, Colors::panelBg2);
    setColour(juce::ComboBox::textColourId, Colors::text);
    setColour(juce::ComboBox::outlineColourId, Colors::panelStroke);
    setColour(juce::ComboBox::arrowColourId, Colors::dim);

    setColour(juce::PopupMenu::backgroundColourId, Colors::panelBg2);
    setColour(juce::PopupMenu::textColourId, Colors::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent_.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, Colors::text);

    setColour(juce::ScrollBar::thumbColourId, Colors::panelStroke);

    setColour(juce::ListBox::backgroundColourId, Colors::panelBg);
    setColour(juce::ListBox::textColourId, Colors::text);

    setColour(juce::TableHeaderComponent::backgroundColourId, Colors::panelBg2);
    setColour(juce::TableHeaderComponent::textColourId, Colors::dim);
    setColour(juce::TableHeaderComponent::outlineColourId, Colors::panelStroke);
}

void RtgLookAndFeel::setAccent(juce::Colour c) {
    accent_ = c;
    setColour(juce::TextEditor::focusedOutlineColourId, accent_);
    setColour(juce::TextEditor::highlightColourId, accent_.withAlpha(0.3f));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent_.withAlpha(0.25f));
}

void RtgLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStart, float rotaryEnd,
                                      juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    // Reserve a strip below the knob for the value readout.
    const float readout = 16.0f;
    auto arcArea = bounds.withHeight(juce::jmax(8.0f, bounds.getHeight() - readout));
    const float radius = juce::jmin(arcArea.getWidth(), arcArea.getHeight()) * 0.5f - 2.0f;
    const float cx = arcArea.getCentreX();
    const float cy = arcArea.getCentreY();
    const float angle = rotaryStart + sliderPos * (rotaryEnd - rotaryStart);
    const float thickness = juce::jmax(3.0f, radius * 0.16f);

    juce::Path track;
    track.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStart, rotaryEnd, true);
    g.setColour(Colors::panelStroke);
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    if (slider.isEnabled()) {
        juce::Path value;
        value.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStart, angle, true);
        g.setColour(accent_);
        g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // Pointer dot.
    const float pr = radius - thickness;
    const juce::Point<float> tip(cx + pr * std::cos(angle - juce::MathConstants<float>::halfPi),
                                 cy + pr * std::sin(angle - juce::MathConstants<float>::halfPi));
    g.setColour(slider.isEnabled() ? Colors::text : Colors::dim);
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(tip));

    // Readout below.
    g.setColour(Colors::text);
    g.setFont(rtgMonoFont(13.0f));
    auto txtArea = bounds.withTop(arcArea.getBottom());
    g.drawText(slider.getTextFromValue(slider.getValue()), txtArea.toNearestInt(),
               juce::Justification::centred, false);
}

void RtgLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float, float,
                                      juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar) {
        const float trackH = 4.0f;
        const float cy = y + height * 0.5f;
        juce::Rectangle<float> track(float(x), cy - trackH * 0.5f, float(width), trackH);
        g.setColour(Colors::panelStroke);
        g.fillRoundedRectangle(track, trackH * 0.5f);
        if (slider.isEnabled()) {
            g.setColour(accent_);
            g.fillRoundedRectangle(track.withRight(sliderPos), trackH * 0.5f);
        }
        g.setColour(slider.isEnabled() ? Colors::text : Colors::dim);
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ sliderPos, cy }));
    } else {
        const float trackW = 4.0f;
        const float cx = x + width * 0.5f;
        juce::Rectangle<float> track(cx - trackW * 0.5f, float(y), trackW, float(height));
        g.setColour(Colors::panelStroke);
        g.fillRoundedRectangle(track, trackW * 0.5f);
        if (slider.isEnabled()) {
            g.setColour(accent_);
            g.fillRoundedRectangle(track.withTop(sliderPos), trackW * 0.5f);
        }
        g.setColour(slider.isEnabled() ? Colors::text : Colors::dim);
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ cx, sliderPos }));
    }
}

void RtgLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                          const juce::Colour& backgroundColour,
                                          bool highlighted, bool down) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float corner = 6.0f;
    const bool primary = (bool) button.getProperties().getWithDefault("primary", false);

    juce::Colour fill;
    if (primary) {
        fill = accent_;
        if (down) fill = fill.darker(0.2f);
        else if (highlighted) fill = fill.brighter(0.12f);
    } else {
        fill = backgroundColour;
        if (button.getToggleState()) fill = accent_.withAlpha(0.22f);
        if (down) fill = fill.brighter(0.15f);
        else if (highlighted) fill = fill.brighter(0.08f);
    }
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, corner);

    if (!primary) {
        g.setColour(button.getToggleState() ? accent_ : Colors::panelStroke);
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }
}

juce::Font RtgLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return rtgSansFont(juce::jmin(16.0f, buttonHeight * 0.5f), true);
}

void RtgLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0, 0, (float) width, (float) height).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = width - 16.0f, ay = height * 0.5f;
    arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

juce::Font RtgLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return rtgSansFont(14.0f);
}

void RtgLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.setColour(Colors::panelBg2);
    g.fillRoundedRectangle(0, 0, (float) width, (float) height, 6.0f);
    g.setColour(Colors::panelStroke);
    g.drawRoundedRectangle(0.5f, 0.5f, width - 1.0f, height - 1.0f, 6.0f, 1.0f);
}

} // namespace rtg::ui
