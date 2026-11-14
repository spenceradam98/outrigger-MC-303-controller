#include "Theme.h"

namespace outrigger
{
using juce::Colour;

const juce::Array<Theme>& themes()
{
    static const juce::Array<Theme> list = [] {
        juce::Array<Theme> a;
        // name        background          panel               edge                text                dim                 accent              accentAlt           track               ledOff              meter low/mid/high
        a.add (Theme { "Studio Grey", Colour (0xff2b2c2f), Colour (0xff3a3c40), Colour (0xff55585e), Colour (0xffeceae4), Colour (0xff9a9890), Colour (0xffff7a1a), Colour (0xff3fc1c9), Colour (0xff1d1e20), Colour (0xff4a3020), Colour (0xff59d36b), Colour (0xffe8c547), Colour (0xffe8503a) });
        a.add (Theme { "Midnight", Colour (0xff0e1424), Colour (0xff17203a), Colour (0xff2b3a63), Colour (0xffdce6ff), Colour (0xff7d8bb0), Colour (0xff4fd6ff), Colour (0xffb58cff), Colour (0xff0a0f1c), Colour (0xff1c2c44), Colour (0xff4fd6ff), Colour (0xffb58cff), Colour (0xffff5c8a) });
        a.add (Theme { "Acid", Colour (0xff0b0b0b), Colour (0xff171717), Colour (0xff2f2f2f), Colour (0xffe9ffe0), Colour (0xff86a07c), Colour (0xffb6ff1a), Colour (0xffff3cac), Colour (0xff050505), Colour (0xff23331a), Colour (0xffb6ff1a), Colour (0xffffe31a), Colour (0xffff3cac) });
        a.add (Theme { "Paper", Colour (0xfff2ede3), Colour (0xfffbf8f2), Colour (0xffcfc6b5), Colour (0xff2a2622), Colour (0xff7d7468), Colour (0xffc8372d), Colour (0xff2d6cc8), Colour (0xffe2dbcc), Colour (0xffe8d8d0), Colour (0xff3f9a52), Colour (0xffd19a1f), Colour (0xffc8372d) });
        return a;
    }();
    return list;
}

void ThemedLookAndFeel::setTheme (const Theme& t)
{
    th = t;
    const bool dark = t.background.getPerceivedBrightness() < 0.5f;
    setColourScheme (dark ? getDarkColourScheme() : getLightColourScheme());
    auto& s = getCurrentColourScheme();
    s.setUIColour (ColourScheme::windowBackground, t.background);
    s.setUIColour (ColourScheme::widgetBackground, t.panel);
    s.setUIColour (ColourScheme::menuBackground, t.panel);
    s.setUIColour (ColourScheme::outline, t.panelEdge);
    s.setUIColour (ColourScheme::defaultText, t.text);
    s.setUIColour (ColourScheme::defaultFill, t.accent);
    s.setUIColour (ColourScheme::highlightedText, t.background);
    s.setUIColour (ColourScheme::highlightedFill, t.accent);
    s.setUIColour (ColourScheme::menuText, t.text);
    setColourScheme (s);

    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, t.knobTrack);
    setColour (juce::Slider::trackColourId, t.accent);
    setColour (juce::Slider::backgroundColourId, t.knobTrack);
    setColour (juce::Slider::thumbColourId, t.text);
    setColour (juce::Label::textColourId, t.text);
    setColour (juce::ComboBox::backgroundColourId, t.knobTrack);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.panelEdge);
    setColour (juce::ComboBox::arrowColourId, t.accent);
    setColour (juce::PopupMenu::backgroundColourId, t.panel);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, t.background);
    setColour (juce::TextButton::buttonColourId, t.knobTrack);
    setColour (juce::TextButton::buttonOnColourId, t.accent);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, t.background);
    setColour (juce::ToggleButton::textColourId, t.text);
    setColour (juce::ToggleButton::tickColourId, t.accent);
}

void ThemedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float stroke = juce::jmax (2.0f, r * 0.16f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - stroke * 0.5f, r - stroke * 0.5f, 0.0f, start, end, true);
    g.setColour (th.knobTrack);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters fill from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path fill;
    fill.addCentredArc (c.x, c.y, r - stroke * 0.5f, r - stroke * 0.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? th.accent : th.textDim);
    g.strokePath (fill, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float capR = r - stroke * 1.6f;
    g.setColour (th.panel.brighter (0.08f));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (th.panelEdge);
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);
    juce::Point<float> tip (c.x + std::sin (angle) * capR * 0.85f, c.y - std::cos (angle) * capR * 0.85f);
    g.setColour (th.text);
    g.drawLine ({ c, tip }, 2.0f);
}

void ThemedLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float led = juce::jmin (12.0f, r.getHeight() - 4.0f);
    auto ledBox = r.removeFromLeft (led + 6.0f).withSizeKeepingCentre (led, led);
    g.setColour (b.getToggleState() ? th.accent : th.ledOff);
    g.fillRoundedRectangle (ledBox, 3.0f);
    if (highlighted) { g.setColour (th.text.withAlpha (0.4f)); g.drawRoundedRectangle (ledBox, 3.0f, 1.0f); }
    g.setColour (b.isEnabled() ? th.text : th.textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centredLeft, 1);
}

void ThemedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? th.accent : bg;
    if (down) c = c.brighter (0.2f); else if (highlighted) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}
} // namespace outrigger
