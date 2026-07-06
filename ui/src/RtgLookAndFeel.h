#pragma once
// Dark "near-black" design system for Riddim Trap Generator (doc 09 §1).
// One accent colour follows the selected genre (teal Riddim / amber Trap).
#include <juce_gui_basics/juce_gui_basics.h>

namespace rtg::ui {

namespace Colors {
    inline const juce::Colour windowBg   { 0xFF0E0F12 };
    inline const juce::Colour panelBg     { 0xFF16181D };
    inline const juce::Colour panelBg2    { 0xFF1E2128 };
    inline const juce::Colour panelStroke { 0xFF272B33 };
    inline const juce::Colour text        { 0xFFE8EAF0 };
    inline const juce::Colour dim         { 0xFF8A8F9C };
    inline const juce::Colour accentRiddim{ 0xFF19D3C5 };
    inline const juce::Colour accentTrap  { 0xFFFFB02E };
    // Section band tints (translucent when drawn):
    inline const juce::Colour sectIntro { 0xFF5A6472 }; // slate
    inline const juce::Colour sectBuild { 0xFF8B5CF6 }; // violet
    inline const juce::Colour sectBreak { 0xFF64748B }; // blue-grey
    inline const juce::Colour sectOutro { 0xFF5A6472 }; // slate
}

/// Monospace font for numeric values / seed (doc 09 §1).
juce::Font rtgMonoFont(float height = 15.0f);
juce::Font rtgSansFont(float height = 15.0f, bool bold = false);

class RtgLookAndFeel : public juce::LookAndFeel_V4 {
public:
    RtgLookAndFeel();

    void setAccent(juce::Colour c);
    juce::Colour accent() const { return accent_; }

    // Serum-style rotary: dim arc track + accent value arc + readout below.
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStart, float rotaryEnd,
                          juce::Slider&) override;

    // Slim linear sliders.
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    // Flat rounded buttons; accent fill when the button has property "primary".
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;

private:
    juce::Colour accent_ { Colors::accentRiddim };
};

} // namespace rtg::ui
