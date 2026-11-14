#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>

namespace outrigger
{
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

// Tone and rhythm-set names, loaded from CSV files the user keeps in Documents/Outrigger.
// tones.csv: bank,program,name   rhythm.csv: program,name   (program numbers 1-128 as on the unit)
class NameTable
{
public:
    static juce::File folder();
    void load();
    void writeTemplatesIfMissing() const;
    juce::String tone (int bank, int program1) const { return tones[{ bank, program1 }]; }
    juce::String rhythm (int program1) const { return rhythms[{ -1, program1 }]; }
    int count() const { return (int) (tones.size() + rhythms.size()); }

private:
    mutable std::map<std::pair<int, int>, juce::String> tones, rhythms;
};

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
    PartStrip (OutriggerProcessor&, int part, std::function<void (int)> onSelect, const NameTable& names);
    void resized() override;
    void paint (juce::Graphics&) override;
    void setSelected (bool s) { selected = s; repaint(); }
    void setActivity (bool a, bool fromUnit);
    void refreshName();

private:
    OutriggerProcessor& proc;
    const NameTable& names;
    juce::Label nameLabel;
    int part;
    bool selected = false;
    float led = 0.0f, unitLed = 0.0f;
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
    void refreshSetups();
    void saveSetup();
    void setPage (int page);
    void updateWarnings();

    OutriggerProcessor& proc;
    ThemedLookAndFeel lnf;
    int selectedPart = 1;

    // header
    juce::Label title, subtitle;
    juce::ComboBox themeBox, outputModeBox, deviceBox;
    juce::TextButton rescan { "Rescan" }, sendAll { "Send All" }, panic { "Panic" };
    juce::Label deviceStatus, warning;
    juce::StringArray deviceIds, inputIds;
    juce::ComboBox inputBox, setupBox;
    juce::TextButton saveSetupBtn { "Save setup" }, namesBtn { "Names" };
    juce::Array<juce::File> setupFiles;
    std::unique_ptr<juce::FileChooser> chooser;
    NameTable names;
    juce::Label bankName;
    juce::TextButton page1 { "Parts R, 1-7" }, page2 { "Parts 8-16" };
    int page = 0;
    uint32_t lastNoteMs = 0;
    int slowTick = 0;
    float returnPeakSinceNote = 0.0f;

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
    juce::ToggleButton moduleBtn { "Sound Module mode (16 parts)" }, followBtn { "Follow unit knobs" }, thruBtn { "Unit MIDI to host" };
    std::unique_ptr<ButtonAttachment> clockA, moduleA, followA, thruA;
    juce::Slider sysexDev;
    juce::Label sysexDevLabel, fxLabel, routeLabel, returnLabel;

    juce::Rectangle<int> mixerArea, detailArea, sideArea, headerArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutriggerEditor)
};
} // namespace outrigger
