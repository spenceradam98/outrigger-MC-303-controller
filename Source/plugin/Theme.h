#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace outrigger
{
struct Theme
{
    juce::String name;
    juce::Colour background, panel, panelEdge, text, textDim, accent, accentAlt, knobTrack, ledOff, meterLow, meterMid, meterHigh;
};

const juce::Array<Theme>& themes();

class ThemedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void setTheme (const Theme& t);
    const Theme& theme() const { return th; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;

private:
    Theme th;
};
} // namespace outrigger
