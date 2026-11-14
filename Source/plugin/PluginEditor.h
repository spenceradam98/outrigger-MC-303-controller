#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace outrigger
{
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

// A labelled rotary knob.
struct Knob : public juce::Component
{
    explicit Knob (const juce::String& text);
    void resized() override;
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<SliderAttachment> attachment;
};

// Stereo peak/RMS meter.
class LevelMeter : public juce::Component
{
public:
    void set (float pl, float pr, float rl, float rr, bool clip);
    void paint (juce::Graphics&) override;
private:
    float peak[2] {}, rms[2] {}, hold[2] {};
    int holdTicks[2] {};
    bool clipped = false;
};

// One mixer strip per part.
class PartStrip : public juce::Component
{
public:
    PartStrip (OutriggerProcessor&, int part, std::function<void (int)> onSelect);
    void resized() override;
    void paint (juce::Graphics&) override;
    void setSelected (bool s) { selected = s; repaint(); }
    void setActivity (bool a);

private:
    int part;
    bool selected = false;
    float led = 0.0f;
    juce::TextButton select;
    juce::ToggleButton on { "On" };
    juce::Slider channel, program;
    juce::Label chLabel, pcLabel;
    Knob rev { "Rev" }, cho { "Cho" }, pan { "Pan" };
    juce::Slider volume;
    std::unique_ptr<ButtonAttachment> onA;
    std::unique_ptr<SliderAttachment> chA, pcA, volA;
};

class OutriggerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OutriggerEditor (OutriggerProcessor&);
    ~OutriggerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showPart (int p) { selectPart (p); }
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    void selectPart (int p);
    void applyTheme (int index);
    void refreshDevices();
    void loadDrumNote();
    void layoutDetail (juce::Rectangle<int> area);

    OutriggerProcessor& proc;
    ThemedLookAndFeel lnf;
    int selectedPart = 1;

    // header
    juce::Label title, subtitle;
    juce::ComboBox themeBox, outputModeBox, deviceBox;
    juce::TextButton rescan { "Rescan" }, sendAll { "Send All" }, panic { "Panic" };
    juce::Label deviceStatus;
    juce::StringArray deviceIds;

    // mixer
    juce::OwnedArray<PartStrip> strips;

    // detail for the selected part
    juce::Label detailTitle;
    juce::Slider bank;
    juce::Label bankLabel;
    Knob expr { "Expression" }, cutoff { "Cutoff" }, reso { "Resonance" }, atk { "Attack" }, dec { "Decay" }, rel { "Release" },
         vrate { "Vib Rate" }, vdepth { "Vib Depth" }, vdelay { "Vib Delay" }, bend { "Bend Rng" }, coarse { "Coarse" }, fine { "Fine" },
         ptime { "Porta Time" };
    juce::ToggleButton porta { "Portamento" }, mono { "Mono" };
    std::unique_ptr<SliderAttachment> bankA;
    std::unique_ptr<ButtonAttachment> portaA, monoA;

    // drum-note editor (rhythm part)
    juce::Slider drumNote;
    juce::Label drumNoteLabel, drumStatus;
    std::array<std::unique_ptr<Knob>, kDrumParams> drumKnobs;
    juce::TextButton drumReset { "Reset note" }, drumResetAll { "Reset all" };

    // return + latency
    LevelMeter meter;
    Knob trim { "Trim" }, out { "Output" };
    juce::ToggleButton monitorBtn { "Monitor" }, monoSumBtn { "Mono sum" };
    std::unique_ptr<ButtonAttachment> monitorA, monoSumA;
    juce::Label latencyLabel;
    juce::Slider latencyMs;
    juce::TextButton measure { "Measure" };
    bool measuring = false;

    // effects + routing
    juce::ComboBox revType, choType, routeBox, focusBox;
    std::unique_ptr<ComboAttachment> revTypeA, choTypeA, routeA, focusA;
    Knob revLevel { "Rev Level" }, choLevel { "Cho Level" };
    juce::ToggleButton experimental { "GS level SysEx (experimental)" }, clockBtn { "Send MIDI clock" };
    std::unique_ptr<ButtonAttachment> clockA;
    juce::Slider sysexDev;
    juce::Label sysexDevLabel, fxLabel, routeLabel, returnLabel;

    juce::Rectangle<int> mixerArea, detailArea, sideArea, headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutriggerEditor)
};
} // namespace outrigger
