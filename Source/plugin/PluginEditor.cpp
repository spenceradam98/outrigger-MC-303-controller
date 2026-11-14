#include "PluginEditor.h"

namespace outrigger
{
static constexpr int kWidth = 1180, kHeight = 760, kSideWidth = 286;

static juce::String noteName (int n)
{
    return juce::String (n) + "  " + juce::MidiMessage::getMidiNoteName (n, true, true, 4);
}

// ------------------------------------------------------------------ NameTable
juce::File NameTable::folder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Outrigger");
}

void NameTable::load()
{
    tones.clear();
    rhythms.clear();
    auto parse = [] (const juce::File& f, bool rhythm, std::map<std::pair<int, int>, juce::String>& into) {
        juce::StringArray lines;
        f.readLines (lines);
        for (auto line : lines)
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWithChar ('#')) continue;
            juce::StringArray cols;
            cols.addTokens (line, ",", "\"");
            if (cols.size() < (rhythm ? 2 : 3)) continue;
            const int a = cols[0].trim().getIntValue();
            if (rhythm) into[{ -1, a }] = cols[1].trim().unquoted();
            else into[{ a, cols[1].trim().getIntValue() }] = cols[2].trim().unquoted();
        }
    };
    parse (folder().getChildFile ("tones.csv"), false, tones);
    parse (folder().getChildFile ("rhythm.csv"), true, rhythms);
}

void NameTable::writeTemplatesIfMissing() const
{
    folder().createDirectory();
    auto t = folder().getChildFile ("tones.csv");
    if (! t.existsAsFile())
        t.replaceWithText ("# Outrigger tone names: bank,program,name  (bank = CC0 value, program 1-128 as on the unit)\n"
                           "# Fill this in from the tone list in the owner's manual. Example line:\n"
                           "# 64,10,My Bass\n");
    auto r = folder().getChildFile ("rhythm.csv");
    if (! r.existsAsFile())
        r.replaceWithText ("# Outrigger rhythm-set names: program,name  (program 1-128 as on the unit)\n"
                           "# Example line:\n"
                           "# 41,My House Kit\n");
    folder().getChildFile ("Setups").createDirectory();
}

// ------------------------------------------------------------------ Knob
Knob::Knob (const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (15));
    slider.setBounds (r);
}

// ------------------------------------------------------------------ LevelMeter
void LevelMeter::set (float pl, float pr, float rl, float rr, bool clip)
{
    const float p[2] { pl, pr }, rm[2] { rl, rr };
    for (int c = 0; c < 2; ++c)
    {
        peak[c] = p[c];
        rms[c] = rm[c];
        if (p[c] >= hold[c]) { hold[c] = p[c]; holdTicks[c] = 45; }
        else if (--holdTicks[c] <= 0) hold[c] = juce::jmax (p[c], hold[c] * 0.92f);
    }
    if (clip) clipped = true;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto& th = static_cast<ThemedLookAndFeel&> (getLookAndFeel()).theme();
    auto r = getLocalBounds().toFloat();
    auto clipBox = r.removeFromTop (10.0f);
    g.setColour (clipped ? th.meterHigh : th.ledOff);
    g.fillRoundedRectangle (clipBox.reduced (1.0f), 2.0f);
    r.removeFromTop (4.0f);

    auto toY = [&] (float lin) {
        const float db = juce::Decibels::gainToDecibels (lin, -60.0f);
        return juce::jmap (db, -60.0f, 0.0f, r.getBottom(), r.getY());
    };
    const float w = (r.getWidth() - 4.0f) / 2.0f;
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (r.getX() + c * (w + 4.0f), r.getY(), w, r.getHeight());
        g.setColour (th.knobTrack);
        g.fillRoundedRectangle (bar, 2.0f);
        juce::ColourGradient grad (th.meterHigh, bar.getX(), bar.getY(), th.meterLow, bar.getX(), bar.getBottom(), false);
        grad.addColour (0.25, th.meterMid);
        g.setGradientFill (grad);
        const float yr = toY (rms[c]);
        g.setOpacity (0.55f);
        g.fillRect (bar.withTop (yr));
        g.setOpacity (1.0f);
        const float yp = toY (peak[c]);
        g.fillRect (bar.withTop (yp).withHeight (juce::jmax (2.0f, bar.getBottom() - yp > 0 ? 2.0f : 0.0f)));
        g.setColour (th.text);
        g.fillRect (bar.getX(), toY (hold[c]), bar.getWidth(), 1.5f);
    }
    g.setColour (th.textDim);
    g.setFont (juce::FontOptions (9.0f));
    for (int db : { 0, -6, -12, -24, -48 })
    {
        const float y = toY (juce::Decibels::decibelsToGain ((float) db));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }
}

// ------------------------------------------------------------------ PartStrip
static void setupIncDec (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::IncDecButtons);
    s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
    s.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
}

PartStrip::PartStrip (OutriggerProcessor& p, int partIndex, std::function<void (int)> onSelect, const NameTable& nameTable)
    : proc (p), names (nameTable), part (partIndex)
{
    auto& s = p.apvts;
    select.setButtonText (part == kRhythmPart ? "R  Rhythm" : "Part " + partName (part));
    nameLabel.setFont (juce::FontOptions (10.5f));
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (nameLabel);
    select.setClickingTogglesState (false);
    select.onClick = [this, onSelect] { onSelect (part); };
    addAndMakeVisible (select);
    addAndMakeVisible (on);
    onA = std::make_unique<ButtonAttachment> (s, paramId (part, "on"), on);

    for (auto* sl : { &channel, &program }) { setupIncDec (*sl); addAndMakeVisible (*sl); }
    chA = std::make_unique<SliderAttachment> (s, paramId (part, "ch"), channel);
    pcA = std::make_unique<SliderAttachment> (s, paramId (part, "prog"), program);
    // Show programs 1-based like the unit's own display.
    program.textFromValueFunction = [] (double v) { return juce::String ((int) v + 1); };
    program.valueFromTextFunction = [] (const juce::String& t) { return (double) (t.getIntValue() - 1); };
    program.updateText();
    program.onValueChange = [this] { refreshName(); };
    chLabel.setText ("Ch", juce::dontSendNotification);
    pcLabel.setText (part == kRhythmPart ? "Set" : "PC", juce::dontSendNotification);
    for (auto* l : { &chLabel, &pcLabel }) { l->setFont (juce::FontOptions (11.0f)); addAndMakeVisible (*l); }

    for (auto* k : { &rev, &cho, &pan }) { k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); addAndMakeVisible (*k); }
    rev.attachment = std::make_unique<SliderAttachment> (s, paramId (part, "rev"), rev.slider);
    cho.attachment = std::make_unique<SliderAttachment> (s, paramId (part, "cho"), cho.slider);
    pan.attachment = std::make_unique<SliderAttachment> (s, paramId (part, "pan"), pan.slider);
    rev.slider.setPopupDisplayEnabled (true, true, nullptr);
    cho.slider.setPopupDisplayEnabled (true, true, nullptr);
    pan.slider.setPopupDisplayEnabled (true, true, nullptr);

    volume.setSliderStyle (juce::Slider::LinearBarVertical);
    volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 16);
    addAndMakeVisible (volume);
    volA = std::make_unique<SliderAttachment> (s, paramId (part, "vol"), volume);
}

void PartStrip::setActivity (bool a, bool fromUnit)
{
    const float before = led + unitLed;
    led = a ? 1.0f : led * 0.8f;
    unitLed = fromUnit ? 1.0f : unitLed * 0.8f;
    if (std::abs (led + unitLed - before) > 0.01f) repaint();
}

void PartStrip::refreshName()
{
    const int prog = (int) program.getValue() + 1;
    const auto n = part == kRhythmPart ? names.rhythm (prog)
                                       : names.tone ((int) proc.apvts.getRawParameterValue (paramId (part, "bank"))->load(), prog);
    nameLabel.setText (n, juce::dontSendNotification);
}

void PartStrip::paint (juce::Graphics& g)
{
    auto& th = static_cast<ThemedLookAndFeel&> (getLookAndFeel()).theme();
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (th.panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (selected ? th.accent : th.panelEdge);
    g.drawRoundedRectangle (r, 6.0f, selected ? 2.0f : 1.0f);
    // activity LED
    auto ledR = juce::Rectangle<float> (r.getRight() - 16.0f, r.getY() + 37.0f, 9.0f, 9.0f);
    g.setColour (th.ledOff.interpolatedWith (th.accentAlt, led));
    g.fillEllipse (ledR);
    // second LED: MIDI coming back from the unit on this part
    g.setColour (th.ledOff.interpolatedWith (th.accent, unitLed));
    g.fillEllipse (ledR.translated (-13.0f, 0.0f));
}

void PartStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 8);
    select.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    on.setBounds (r.removeFromTop (20).withTrimmedRight (32));
    r.removeFromTop (4);
    auto row = r.removeFromTop (22);
    chLabel.setBounds (row.removeFromLeft (26));
    channel.setBounds (row);
    r.removeFromTop (4);
    row = r.removeFromTop (22);
    pcLabel.setBounds (row.removeFromLeft (26));
    program.setBounds (row);
    nameLabel.setBounds (r.removeFromTop (16));
    volume.setBounds (r.removeFromLeft (34));
    r.removeFromLeft (4);
    const int kh = r.getHeight() / 3;
    rev.setBounds (r.removeFromTop (kh));
    cho.setBounds (r.removeFromTop (kh));
    pan.setBounds (r);
}

// ------------------------------------------------------------------ Editor
OutriggerEditor::OutriggerEditor (OutriggerProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    title.setText ("OUTRIGGER", juce::dontSendNotification);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    subtitle.setText (OutriggerProcessor::isEffectVariant() ? "FX version  |  hardware groovebox bridge" : "hardware groovebox bridge  |  parts out, stereo return in",
                      juce::dontSendNotification);
    names.writeTemplatesIfMissing();
    names.load();
    subtitle.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    for (int i = 0; i < themes().size(); ++i) themeBox.addItem (themes()[i].name, i + 1);
    themeBox.onChange = [this] { applyTheme (themeBox.getSelectedId() - 1); };
    themeBox.setTooltip ("Theme");
    addAndMakeVisible (themeBox);

    outputModeBox.addItemList ({ "MIDI out: host", "MIDI out: direct", "MIDI out: both" }, 1);
    outputModeBox.setSelectedId ((int) proc.getOutputMode() + 1, juce::dontSendNotification);
    outputModeBox.onChange = [this] { proc.setOutputMode ((OutputMode) (outputModeBox.getSelectedId() - 1)); };
    addAndMakeVisible (outputModeBox);

    deviceBox.onChange = [this] {
        const int idx = deviceBox.getSelectedId() - 2;
        proc.setDirectDevice (juce::isPositiveAndBelow (idx, deviceIds.size()) ? deviceIds[idx] : juce::String());
    };
    addAndMakeVisible (deviceBox);
    rescan.onClick = [this] { refreshDevices(); };
    addAndMakeVisible (rescan);
    deviceStatus.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (deviceStatus);
    inputBox.onChange = [this] {
        const int idx = inputBox.getSelectedId() - 2;
        proc.setUnitInputDevice (juce::isPositiveAndBelow (idx, inputIds.size()) ? inputIds[idx] : juce::String());
    };
    inputBox.setTooltip ("MIDI coming back from the unit: its knob moves update these controls");
    addAndMakeVisible (inputBox);
    setupBox.setTextWhenNothingSelected ("Setups...");
    setupBox.onChange = [this] {
        const int id = setupBox.getSelectedId();
        if (id == 1)
        {
            juce::ValueTree init ("OutriggerSetup");
            for (auto* prm : proc.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    juce::ValueTree v ("Param");
                    v.setProperty ("id", r->getParameterID(), nullptr);
                    v.setProperty ("value", r->convertFrom0to1 (r->getDefaultValue()), nullptr);
                    init.appendChild (v, nullptr);
                }
            proc.applySetup (init);
        }
        else if (juce::isPositiveAndBelow (id - 2, setupFiles.size()))
        {
            if (auto xml = juce::XmlDocument::parse (setupFiles[id - 2]))
                proc.applySetup (juce::ValueTree::fromXml (*xml));
        }
        loadDrumNote();
        for (auto* st : strips) st->refreshName();
    };
    addAndMakeVisible (setupBox);
    saveSetupBtn.onClick = [this] { saveSetup(); };
    addAndMakeVisible (saveSetupBtn);
    namesBtn.setTooltip ("Tone and rhythm-set names: edit tones.csv / rhythm.csv in Documents/Outrigger, then click to reload");
    namesBtn.onClick = [this] {
        names.writeTemplatesIfMissing();
        names.load();
        for (auto* st : strips) st->refreshName();
        if (names.count() == 0) NameTable::folder().revealToUser();
    };
    addAndMakeVisible (namesBtn);
    warning.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (warning);
    for (auto* b : { &page1, &page2 })
    {
        b->setClickingTogglesState (false);
        addChildComponent (*b);
    }
    page1.onClick = [this] { setPage (0); };
    page2.onClick = [this] { setPage (1); };
    bankName.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (bankName);

    sendAll.onClick = [this] { proc.requestSendAll(); };
    sendAll.setTooltip ("Resend every part setting to the unit");
    panic.onClick = [this] { proc.requestPanic(); };
    addAndMakeVisible (sendAll);
    addAndMakeVisible (panic);

    for (int i = 0; i < kNumParts; ++i)
        addChildComponent (strips.add (new PartStrip (proc, i, [this] (int part) { selectPart (part); }, names)));

    // detail panel
    detailTitle.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (detailTitle);
    setupIncDec (bank);
    bankLabel.setText ("Bank (CC0)", juce::dontSendNotification);
    bankLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (bank);
    addAndMakeVisible (bankLabel);
    for (auto* k : { &expr, &cutoff, &reso, &atk, &dec, &rel, &vrate, &vdepth, &vdelay, &bend, &coarse, &fine, &ptime })
        addAndMakeVisible (*k);
    addAndMakeVisible (porta);
    addAndMakeVisible (mono);

    setupIncDec (drumNote);
    drumNote.setRange (0, 127, 1);
    drumNote.setValue (36, juce::dontSendNotification);
    drumNote.textFromValueFunction = [] (double v) { return noteName ((int) v); };
    drumNote.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getIntValue(); };
    drumNote.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 20);
    drumNote.updateText();
    drumNote.onValueChange = [this] { loadDrumNote(); };
    drumNoteLabel.setText ("Drum note", juce::dontSendNotification);
    drumNoteLabel.setFont (juce::FontOptions (11.0f));
    drumStatus.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (drumNote);
    addAndMakeVisible (drumNoteLabel);
    addAndMakeVisible (drumStatus);
    for (int k = 0; k < kDrumParams; ++k)
    {
        drumKnobs[(size_t) k] = std::make_unique<Knob> (kDrumParamNames[k]);
        auto& sl = drumKnobs[(size_t) k]->slider;
        sl.setRange (0, 127, 1);
        sl.onValueChange = [this, k] {
            proc.setDrum ((int) drumNote.getValue(), k, (int) drumKnobs[(size_t) k]->slider.getValue());
            loadDrumNote();
        };
        addAndMakeVisible (*drumKnobs[(size_t) k]);
    }
    drumReset.onClick = [this] { for (int k = 0; k < kDrumParams; ++k) proc.setDrum ((int) drumNote.getValue(), k, -1); loadDrumNote(); };
    drumResetAll.onClick = [this] { proc.clearDrumEdits(); loadDrumNote(); };
    drumReset.setTooltip ("Stop sending edits for this note (the unit keeps its current value until reset)");
    addAndMakeVisible (drumReset);
    addAndMakeVisible (drumResetAll);

    // return
    for (auto* l : { &returnLabel, &fxLabel, &routeLabel }) { l->setFont (juce::FontOptions (13.0f, juce::Font::bold)); addAndMakeVisible (*l); }
    returnLabel.setText ("RETURN", juce::dontSendNotification);
    fxLabel.setText ("EFFECTS", juce::dontSendNotification);
    routeLabel.setText ("ROUTING", juce::dontSendNotification);
    addAndMakeVisible (meter);
    addAndMakeVisible (trim);
    addAndMakeVisible (out);
    trim.attachment = std::make_unique<SliderAttachment> (s, "trim", trim.slider);
    out.attachment = std::make_unique<SliderAttachment> (s, "out", out.slider);
    addAndMakeVisible (monitorBtn);
    addAndMakeVisible (monoSumBtn);
    monitorA = std::make_unique<ButtonAttachment> (s, "monitor", monitorBtn);
    monoSumA = std::make_unique<ButtonAttachment> (s, "mono", monoSumBtn);

    latencyLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (latencyLabel);
    latencyMs.setSliderStyle (juce::Slider::LinearHorizontal);
    latencyMs.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
    latencyMs.setRange (0.0, 250.0, 0.1);
    latencyMs.setTextValueSuffix (" ms");
    latencyMs.setValue (1000.0 * proc.getHardwareLatency() / juce::jmax (1.0, proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0), juce::dontSendNotification);
    latencyMs.onValueChange = [this] {
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        proc.setHardwareLatency (juce::roundToInt (latencyMs.getValue() * sr / 1000.0));
    };
    latencyMs.setTooltip ("Round-trip delay of the hardware, reported to the host for compensation");
    addAndMakeVisible (latencyMs);
    measure.setTooltip ("Plays a side stick on the rhythm part and times its arrival on the return input");
    measure.onClick = [this] { measuring = true; proc.startLatencyMeasure (37); };
    measure.setTooltip ("Plays 3 side-stick hits on the rhythm part and times their arrival on the return input (median)");
    addAndMakeVisible (measure);

    // effects
    for (auto* c : { &revType, &choType, &routeBox, &focusBox }) addAndMakeVisible (*c);
    for (auto* t : kReverbTypes) revType.addItem (t, revType.getNumItems() + 1);
    for (auto* t : kChorusTypes) choType.addItem (t, choType.getNumItems() + 1);
    revTypeA = std::make_unique<ComboAttachment> (s, "rev_type", revType);
    choTypeA = std::make_unique<ComboAttachment> (s, "cho_type", choType);
    addAndMakeVisible (revLevel);
    addAndMakeVisible (choLevel);
    revLevel.attachment = std::make_unique<SliderAttachment> (s, "rev_level", revLevel.slider);
    choLevel.attachment = std::make_unique<SliderAttachment> (s, "cho_level", choLevel.slider);
    experimental.setToggleState (proc.getExperimentalSysex(), juce::dontSendNotification);
    experimental.onClick = [this] {
        proc.setExperimentalSysex (experimental.getToggleState());
        revLevel.setEnabled (experimental.getToggleState());
        choLevel.setEnabled (experimental.getToggleState());
    };
    experimental.onClick();
    addAndMakeVisible (experimental);
    setupIncDec (sysexDev);
    sysexDev.setRange (0, 31, 1);
    sysexDev.setValue (proc.getDeviceId(), juce::dontSendNotification);
    sysexDev.textFromValueFunction = [] (double v) { return juce::String::toHexString ((int) v).toUpperCase().paddedLeft ('0', 2) + "H"; };
    sysexDev.valueFromTextFunction = [] (const juce::String& t) { return (double) t.upToFirstOccurrenceOf ("H", false, true).getHexValue32(); };
    sysexDev.updateText();
    sysexDev.onValueChange = [this] { proc.setDeviceId ((int) sysexDev.getValue()); proc.requestSendAll(); };
    sysexDevLabel.setText ("SysEx device ID", juce::dontSendNotification);
    sysexDevLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (sysexDev);
    addAndMakeVisible (sysexDevLabel);

    // routing
    routeBox.addItemList ({ "Passthrough", "Focus part" }, 1);
    for (int i = 0; i < kNumParts; ++i) focusBox.addItem ("Part " + partName (i), i + 1);
    for (auto* b : { &moduleBtn, &followBtn, &thruBtn }) addAndMakeVisible (*b);
    moduleA = std::make_unique<ButtonAttachment> (s, "module_mode", moduleBtn);
    followA = std::make_unique<ButtonAttachment> (s, "unit_follow", followBtn);
    thruA = std::make_unique<ButtonAttachment> (s, "unit_thru", thruBtn);
    moduleBtn.setTooltip ("Hold PLAY MODE while powering on the unit to enter Sound Module mode: 16 parts, rhythm on part 10");
    moduleBtn.onStateChange = [this] { if (! moduleBtn.getToggleState() && page != 0) setPage (0); else resized(); };
    thruBtn.setTooltip ("Pass MIDI from the unit to the host's MIDI input of this plugin's track (host must accept plugin MIDI out)");
    routeA = std::make_unique<ComboAttachment> (s, "route", routeBox);
    focusA = std::make_unique<ComboAttachment> (s, "focus", focusBox);
    routeBox.setTooltip ("Passthrough keeps the DAW's MIDI channels. Focus sends everything to one part.");
    addAndMakeVisible (clockBtn);
    clockA = std::make_unique<ButtonAttachment> (s, "clock", clockBtn);

    refreshDevices();
    refreshSetups();
    themeBox.setSelectedId (proc.getThemeIndex() + 1, juce::dontSendNotification);
    applyTheme (proc.getThemeIndex());
    selectPart (1);
    setResizable (true, true);
    setResizeLimits (kWidth - 120, kHeight - 60, kWidth * 2, kHeight * 2);
    setSize (kWidth, kHeight);
    startTimerHz (30);
}

OutriggerEditor::~OutriggerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OutriggerEditor::applyTheme (int index)
{
    index = juce::jlimit (0, themes().size() - 1, index);
    proc.setThemeIndex (index);
    lnf.setTheme (themes()[index]);
    title.setColour (juce::Label::textColourId, lnf.theme().accent);
    subtitle.setColour (juce::Label::textColourId, lnf.theme().textDim);
    for (auto* l : { &returnLabel, &fxLabel, &routeLabel, &detailTitle })
        l->setColour (juce::Label::textColourId, lnf.theme().accent);
    sendLookAndFeelChange();
    repaint();
}

void OutriggerEditor::refreshDevices()
{
    deviceBox.clear (juce::dontSendNotification);
    deviceIds.clear();
    deviceBox.addItem ("MIDI to unit: (none)", 1);
    int selected = 1;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        deviceIds.add (d.identifier);
        deviceBox.addItem (d.name, deviceIds.size() + 1);
        if (d.identifier == proc.getDirectDeviceId()) selected = deviceIds.size() + 1;
    }
    deviceBox.setSelectedId (selected, juce::dontSendNotification);

    inputBox.clear (juce::dontSendNotification);
    inputIds.clear();
    inputBox.addItem ("MIDI from unit: (none)", 1);
    selected = 1;
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        inputIds.add (d.identifier);
        inputBox.addItem (d.name, inputIds.size() + 1);
        if (d.identifier == proc.getUnitInputId()) selected = inputIds.size() + 1;
    }
    inputBox.setSelectedId (selected, juce::dontSendNotification);
}

void OutriggerEditor::refreshSetups()
{
    setupBox.clear (juce::dontSendNotification);
    setupFiles.clear();
    setupBox.addItem ("Init (all defaults)", 1);
    auto dir = NameTable::folder().getChildFile ("Setups");
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.outrigger"))
        setupFiles.add (f);
    setupFiles.sort();
    for (int i = 0; i < setupFiles.size(); ++i) setupBox.addItem (setupFiles[i].getFileNameWithoutExtension(), i + 2);
}

void OutriggerEditor::saveSetup()
{
    auto dir = NameTable::folder().getChildFile ("Setups");
    dir.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save setup", dir.getChildFile ("My setup.outrigger"), "*.outrigger");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("outrigger");
                              if (auto xml = proc.createSetup().createXml()) xml->writeTo (f);
                              refreshSetups();
                          });
}

void OutriggerEditor::setPage (int newPage)
{
    page = newPage;
    page1.setToggleState (page == 0, juce::dontSendNotification);
    page2.setToggleState (page == 1, juce::dontSendNotification);
    if ((selectedPart >= kNormalParts) != (page == 1)) selectPart (page == 1 ? kNormalParts : 1);
    resized();
}

void OutriggerEditor::selectPart (int p)
{
    if (p >= proc.activeParts()) p = 1;
    selectedPart = p;
    page = p >= kNormalParts ? 1 : 0;
    for (int i = 0; i < strips.size(); ++i) strips[i]->setSelected (i == p);
    auto& s = proc.apvts;
    const bool rhythm = p == kRhythmPart;
    detailTitle.setText (rhythm ? "Rhythm part R  (rhythm set, drum-note edits)" : "Part " + partName (p) + "  (tone, filter, envelope, vibrato, pitch)",
                         juce::dontSendNotification);

    bankA = std::make_unique<SliderAttachment> (s, paramId (p, "bank"), bank);
    struct { Knob* k; const char* id; } binds[] = {
        { &expr, "expr" }, { &cutoff, "cutoff" }, { &reso, "reso" }, { &atk, "atk" }, { &dec, "dec" }, { &rel, "rel" },
        { &vrate, "vrate" }, { &vdepth, "vdepth" }, { &vdelay, "vdelay" }, { &bend, "bend" }, { &coarse, "coarse" },
        { &fine, "fine" }, { &ptime, "ptime" } };
    for (auto& b : binds)
    {
        b.k->attachment.reset();
        b.k->attachment = std::make_unique<SliderAttachment> (s, paramId (p, b.id), b.k->slider);
    }
    portaA = std::make_unique<ButtonAttachment> (s, paramId (p, "porta"), porta);
    monoA = std::make_unique<ButtonAttachment> (s, paramId (p, "mono"), mono);

    bank.setVisible (! rhythm);
    bankLabel.setVisible (! rhythm);
    for (auto* c : std::initializer_list<juce::Component*> { &vrate, &vdepth, &vdelay, &bend, &coarse, &fine, &ptime, &porta, &mono })
        c->setVisible (! rhythm);
    for (auto* c : std::initializer_list<juce::Component*> { &drumNote, &drumNoteLabel, &drumStatus, &drumReset, &drumResetAll })
        c->setVisible (rhythm);
    for (auto& k : drumKnobs) k->setVisible (rhythm);
    if (rhythm) loadDrumNote();
    bankName.setVisible (! rhythm);
    resized();
}

void OutriggerEditor::loadDrumNote()
{
    const int note = (int) drumNote.getValue();
    static constexpr int neutral[kDrumParams] { 64, 127, 64, 64, 0 };
    int edited = 0;
    for (int k = 0; k < kDrumParams; ++k)
    {
        const int v = proc.getDrum (note, k);
        if (v >= 0) ++edited;
        auto& sl = drumKnobs[(size_t) k]->slider;
        sl.setValue (v >= 0 ? v : neutral[k], juce::dontSendNotification);
        drumKnobs[(size_t) k]->label.setColour (juce::Label::textColourId, v >= 0 ? lnf.theme().accent : lnf.theme().textDim);
    }
    drumStatus.setText (edited > 0 ? juce::String (edited) + " edited value(s) sent for this note" : "Untouched: nothing sent for this note",
                        juce::dontSendNotification);
}

void OutriggerEditor::paint (juce::Graphics& g)
{
    const auto& th = lnf.theme();
    g.fillAll (th.background);
    g.setColour (th.panel.darker (0.15f));
    g.fillRect (headerArea);
    g.setColour (th.accent);
    g.fillRect (headerArea.removeFromBottom (2));

    g.setColour (th.panel.darker (0.15f));
    g.fillRect (footerArea);
    for (auto r : { detailArea, sideArea })
    {
        g.setColour (th.panel);
        g.fillRoundedRectangle (r.toFloat().reduced (2.0f), 6.0f);
        g.setColour (th.panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (2.0f), 6.0f, 1.0f);
    }
}

void OutriggerEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (88);
    footerArea = r.removeFromBottom (24);
    {
        auto h = headerArea.reduced (12, 8);
        auto left = h.removeFromLeft (330);
        title.setBounds (left.removeFromTop (30));
        subtitle.setBounds (left.removeFromTop (18));
        auto row1 = h.removeFromTop (30).reduced (0, 2);
        h.removeFromTop (6);
        auto row2 = h.removeFromTop (30).reduced (0, 2);

        panic.setBounds (row1.removeFromRight (64));
        row1.removeFromRight (6);
        sendAll.setBounds (row1.removeFromRight (80));
        row1.removeFromRight (12);
        themeBox.setBounds (row1.removeFromRight (120));
        row1.removeFromRight (12);
        outputModeBox.setBounds (row1.removeFromLeft (140));
        row1.removeFromLeft (6);
        rescan.setBounds (row1.removeFromRight (64));
        row1.removeFromRight (6);
        deviceBox.setBounds (row1);

        namesBtn.setBounds (row2.removeFromRight (64));
        row2.removeFromRight (6);
        saveSetupBtn.setBounds (row2.removeFromRight (94));
        row2.removeFromRight (6);
        setupBox.setBounds (row2.removeFromRight (180));
        row2.removeFromRight (12);
        inputBox.setBounds (row2);
    }
    {
        auto f = footerArea.reduced (12, 2);
        warning.setBounds (f.removeFromRight (f.getWidth() / 2));
        deviceStatus.setBounds (f);
    }

    r.reduce (8, 4);
    sideArea = r.removeFromRight (kSideWidth);
    r.removeFromRight (8);
    const bool module = proc.activeParts() > kNormalParts;
    page1.setVisible (module);
    page2.setVisible (module);
    if (module)
    {
        auto tabs = r.removeFromTop (24);
        page1.setBounds (tabs.removeFromLeft (120));
        tabs.removeFromLeft (4);
        page2.setBounds (tabs.removeFromLeft (120));
        r.removeFromTop (4);
    }
    else page = 0;
    page1.setToggleState (page == 0, juce::dontSendNotification);
    page2.setToggleState (page == 1, juce::dontSendNotification);

    mixerArea = r.removeFromTop (juce::jmax (310, r.getHeight() / 2));
    r.removeFromTop (8);
    detailArea = r;

    const int sw = mixerArea.getWidth() / kNormalParts;
    auto m = mixerArea;
    for (int i = 0; i < strips.size(); ++i)
    {
        const bool show = (i / kNormalParts) == page;
        strips[i]->setVisible (show);
        if (show) strips[i]->setBounds (m.removeFromLeft (sw));
    }

    layoutDetail (detailArea.reduced (12, 8));

    // side column
    auto sd = sideArea.reduced (12, 10);
    returnLabel.setBounds (sd.removeFromTop (20));
    auto ret = sd.removeFromTop (150);
    meter.setBounds (ret.removeFromLeft (44));
    ret.removeFromLeft (10);
    auto knobs = ret.removeFromTop (96);
    trim.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    out.setBounds (knobs);
    ret.removeFromTop (4);
    monitorBtn.setBounds (ret.removeFromTop (22));
    monoSumBtn.setBounds (ret.removeFromTop (22));
    sd.removeFromTop (6);
    latencyLabel.setBounds (sd.removeFromTop (18));
    auto lat = sd.removeFromTop (26);
    measure.setBounds (lat.removeFromRight (76));
    lat.removeFromRight (6);
    latencyMs.setBounds (lat);

    sd.removeFromTop (10);
    fxLabel.setBounds (sd.removeFromTop (20));
    auto fxRow = sd.removeFromTop (24);
    revType.setBounds (fxRow.removeFromLeft (fxRow.getWidth() / 2 - 3));
    fxRow.removeFromLeft (6);
    choType.setBounds (fxRow);
    sd.removeFromTop (4);
    auto lv = sd.removeFromTop (72);
    revLevel.setBounds (lv.removeFromLeft (lv.getWidth() / 2));
    choLevel.setBounds (lv);
    experimental.setBounds (sd.removeFromTop (22));
    auto dev = sd.removeFromTop (24);
    sysexDevLabel.setBounds (dev.removeFromLeft (110));
    sysexDev.setBounds (dev.removeFromLeft (110));

    sd.removeFromTop (10);
    routeLabel.setBounds (sd.removeFromTop (20));
    auto rt = sd.removeFromTop (24);
    routeBox.setBounds (rt.removeFromLeft (rt.getWidth() / 2 - 3));
    rt.removeFromLeft (6);
    focusBox.setBounds (rt);
    sd.removeFromTop (4);
    for (auto* b : { &moduleBtn, &clockBtn, &followBtn, &thruBtn }) b->setBounds (sd.removeFromTop (22));
}

void OutriggerEditor::layoutDetail (juce::Rectangle<int> a)
{
    detailTitle.setBounds (a.removeFromTop (22));
    a.removeFromTop (6);
    const int rowH = (a.getHeight() - 8) / 2;
    auto row1 = a.removeFromTop (rowH);
    a.removeFromTop (8);
    auto row2 = a;
    const int kw = 78;

    auto bankCol = row1.removeFromLeft (120);
    bankLabel.setBounds (bankCol.removeFromTop (16));
    bank.setBounds (bankCol.removeFromTop (24));
    bankName.setBounds (bankCol.removeFromTop (18));
    for (auto* k : { &expr, &cutoff, &reso, &atk, &dec, &rel }) { k->setBounds (row1.removeFromLeft (kw)); row1.removeFromLeft (4); }

    if (selectedPart == kRhythmPart)
    {
        auto col = row2.removeFromLeft (180);
        drumNoteLabel.setBounds (col.removeFromTop (16));
        drumNote.setBounds (col.removeFromTop (24));
        col.removeFromTop (6);
        drumReset.setBounds (col.removeFromTop (24).withWidth (84));
        col.removeFromTop (4);
        drumResetAll.setBounds (col.removeFromTop (24).withWidth (84));
        auto knobRow = row2.removeFromTop (juce::jmin (row2.getHeight(), 100));
        for (auto& k : drumKnobs) { k->setBounds (knobRow.removeFromLeft (kw)); knobRow.removeFromLeft (4); }
        drumStatus.setBounds (row2.removeFromTop (18));
    }
    else
    {
        auto toggles = row2.removeFromLeft (120);
        porta.setBounds (toggles.removeFromTop (22));
        mono.setBounds (toggles.removeFromTop (22));
        for (auto* k : { &ptime, &vrate, &vdepth, &vdelay, &bend, &coarse, &fine }) { k->setBounds (row2.removeFromLeft (kw)); row2.removeFromLeft (4); }
    }
}

void OutriggerEditor::updateWarnings()
{
    juce::String w;
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (! proc.processingRuns())
        w = "Host isn't processing audio: MIDI and the return are paused";
    else if (! proc.hostGivesAudioInput())
        w = OutriggerProcessor::isEffectVariant() ? "This slot gets no audio input from the host"
                                                  : "No audio input in this slot: try Outrigger FX in an effect slot";
    else if (lastNoteMs != 0 && nowMs - lastNoteMs > 600 && nowMs - lastNoteMs < 4000 && returnPeakSinceNote < 0.0005f)
        w = "Notes sent but no return audio: check the interface input";
    else if (clockBtn.getToggleState() && ! proc.hostGivesTransport())
        w = "Host sends no tempo or transport, so MIDI clock is idle";
    warning.setText (w, juce::dontSendNotification);
    warning.setColour (juce::Label::textColourId, lnf.theme().meterHigh);
}

void OutriggerEditor::timerCallback()
{
    meter.set (proc.meterPeak (0), proc.meterPeak (1), proc.meterRms (0), proc.meterRms (1), proc.takeClip());

    const auto act = proc.takeActivity();
    const auto unitAct = proc.takeUnitInputActivity();
    for (int i = 0; i < strips.size(); ++i) strips[i]->setActivity ((act >> i) & 1u, (unitAct >> i) & 1u);

    const auto nowMs = juce::Time::getMillisecondCounter();
    if (act != 0) { lastNoteMs = nowMs; returnPeakSinceNote = 0.0f; }
    returnPeakSinceNote = juce::jmax (returnPeakSinceNote, proc.meterPeak (0), proc.meterPeak (1));

    if (++slowTick % 15 == 0)    // twice a second: names (bank may have changed) and module-mode layout
    {
        for (auto* s : strips) s->refreshName();
        if (selectedPart != kRhythmPart)
        {
            const int prog = (int) proc.apvts.getRawParameterValue (paramId (selectedPart, "prog"))->load() + 1;
            bankName.setText (names.tone ((int) bank.getValue(), prog), juce::dontSendNotification);
        }
        if (page1.isVisible() != (proc.activeParts() > kNormalParts)) resized();
    }

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const auto st = proc.latencyState();
    if (measuring && st == LatencyProbe::State::Done)
    {
        measuring = false;
        proc.setHardwareLatency (proc.lastMeasuredLatency());
        latencyMs.setValue (1000.0 * proc.getHardwareLatency() / sr, juce::dontSendNotification);
    }
    else if (measuring && st == LatencyProbe::State::TimedOut)
        measuring = false;

    juce::String latText = "Latency: " + juce::String (proc.getHardwareLatency()) + " samples";
    if (measuring) latText = "Measuring... (listening on return)";
    else if (st == LatencyProbe::State::TimedOut) latText << "  (last measure: no signal)";
    else if (st == LatencyProbe::State::Done)
        latText << "  (" << proc.lastMeasurePings() << " hits, spread " << juce::String (1000.0 * proc.lastMeasureSpread() / sr, 1) << " ms)";
    latencyLabel.setText (latText, juce::dontSendNotification);

    juce::String status;
    const auto mode = proc.getOutputMode();
    if (mode == OutputMode::Host) status = "MIDI out: through the host";
    else if (proc.getDirectDeviceId().isEmpty()) status = "Pick the MIDI port the unit is connected to";
    else status = proc.isDirectConnected() ? "MIDI out: device connected" : "MIDI out device not available: rescan or reconnect";
    if (proc.getUnitInputId().isNotEmpty())
        status << (proc.isUnitInputConnected() ? "  |  MIDI in: connected" : "  |  MIDI in: not available");
    if (proc.syncPending()) status << "  |  sending settings...";
    deviceStatus.setText (status, juce::dontSendNotification);
    deviceStatus.setColour (juce::Label::textColourId, lnf.theme().textDim);
    updateWarnings();
}
} // namespace outrigger
