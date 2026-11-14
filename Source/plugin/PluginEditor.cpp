#include "PluginEditor.h"

namespace outrigger
{
static constexpr int kWidth = 1180, kHeight = 690, kSideWidth = 286;

static juce::String noteName (int n)
{
    return juce::String (n) + "  " + juce::MidiMessage::getMidiNoteName (n, true, true, 4);
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

PartStrip::PartStrip (OutriggerProcessor& p, int partIndex, std::function<void (int)> onSelect) : part (partIndex)
{
    auto& s = p.apvts;
    select.setButtonText (part == kRhythmPart ? "R  Rhythm" : "Part " + partName (part));
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

void PartStrip::setActivity (bool a)
{
    const float before = led;
    led = a ? 1.0f : led * 0.8f;
    if (std::abs (led - before) > 0.01f) repaint();
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
}

void PartStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 8);
    select.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    on.setBounds (r.removeFromTop (20).withTrimmedRight (20));
    r.removeFromTop (4);
    auto row = r.removeFromTop (22);
    chLabel.setBounds (row.removeFromLeft (26));
    channel.setBounds (row);
    r.removeFromTop (4);
    row = r.removeFromTop (22);
    pcLabel.setBounds (row.removeFromLeft (26));
    program.setBounds (row);
    r.removeFromTop (8);
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
    subtitle.setText ("hardware groovebox bridge  |  8 parts out, stereo return in", juce::dontSendNotification);
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
    sendAll.onClick = [this] { proc.requestSendAll(); };
    sendAll.setTooltip ("Resend every part setting to the unit");
    panic.onClick = [this] { proc.requestPanic(); };
    addAndMakeVisible (sendAll);
    addAndMakeVisible (panic);

    for (int i = 0; i < kNumParts; ++i)
        addAndMakeVisible (strips.add (new PartStrip (proc, i, [this] (int part) { selectPart (part); })));

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
    routeA = std::make_unique<ComboAttachment> (s, "route", routeBox);
    focusA = std::make_unique<ComboAttachment> (s, "focus", focusBox);
    routeBox.setTooltip ("Passthrough keeps the DAW's MIDI channels. Focus sends everything to one part.");
    addAndMakeVisible (clockBtn);
    clockA = std::make_unique<ButtonAttachment> (s, "clock", clockBtn);

    refreshDevices();
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
    deviceBox.addItem ("(no direct device)", 1);
    int selected = 1;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        deviceIds.add (d.identifier);
        deviceBox.addItem (d.name, deviceIds.size() + 1);
        if (d.identifier == proc.getDirectDeviceId()) selected = deviceIds.size() + 1;
    }
    deviceBox.setSelectedId (selected, juce::dontSendNotification);
}

void OutriggerEditor::selectPart (int p)
{
    selectedPart = p;
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
    headerArea = r.removeFromTop (58);
    {
        auto h = headerArea.reduced (12, 8);
        auto left = h.removeFromLeft (360);
        title.setBounds (left.removeFromTop (26));
        subtitle.setBounds (left);
        panic.setBounds (h.removeFromRight (64).withSizeKeepingCentre (64, 26));
        h.removeFromRight (6);
        sendAll.setBounds (h.removeFromRight (80).withSizeKeepingCentre (80, 26));
        h.removeFromRight (12);
        themeBox.setBounds (h.removeFromRight (120).withSizeKeepingCentre (120, 26));
        h.removeFromRight (12);
        auto top = h.removeFromTop (26);
        outputModeBox.setBounds (top.removeFromLeft (140));
        top.removeFromLeft (6);
        rescan.setBounds (top.removeFromRight (64));
        top.removeFromRight (6);
        deviceBox.setBounds (top);
        deviceStatus.setBounds (h);
    }

    r.reduce (8, 8);
    sideArea = r.removeFromRight (kSideWidth);
    r.removeFromRight (8);
    mixerArea = r.removeFromTop (juce::jmax (300, r.getHeight() / 2));
    r.removeFromTop (8);
    detailArea = r;

    const int sw = mixerArea.getWidth() / kNumParts;
    auto m = mixerArea;
    for (auto* s : strips) s->setBounds (m.removeFromLeft (sw));

    layoutDetail (detailArea.reduced (12, 8));

    // side column
    auto sd = sideArea.reduced (12, 10);
    returnLabel.setBounds (sd.removeFromTop (20));
    auto ret = sd.removeFromTop (160);
    meter.setBounds (ret.removeFromLeft (44));
    ret.removeFromLeft (10);
    auto knobs = ret.removeFromTop (100);
    trim.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    out.setBounds (knobs);
    ret.removeFromTop (6);
    monitorBtn.setBounds (ret.removeFromTop (22));
    monoSumBtn.setBounds (ret.removeFromTop (22));
    sd.removeFromTop (8);
    latencyLabel.setBounds (sd.removeFromTop (18));
    auto lat = sd.removeFromTop (26);
    measure.setBounds (lat.removeFromRight (76));
    lat.removeFromRight (6);
    latencyMs.setBounds (lat);

    sd.removeFromTop (12);
    fxLabel.setBounds (sd.removeFromTop (20));
    auto fxRow = sd.removeFromTop (24);
    revType.setBounds (fxRow.removeFromLeft (fxRow.getWidth() / 2 - 3));
    fxRow.removeFromLeft (6);
    choType.setBounds (fxRow);
    sd.removeFromTop (4);
    auto lv = sd.removeFromTop (78);
    revLevel.setBounds (lv.removeFromLeft (lv.getWidth() / 2));
    choLevel.setBounds (lv);
    experimental.setBounds (sd.removeFromTop (22));
    auto dev = sd.removeFromTop (24);
    sysexDevLabel.setBounds (dev.removeFromLeft (110));
    sysexDev.setBounds (dev.removeFromLeft (110));

    sd.removeFromTop (12);
    routeLabel.setBounds (sd.removeFromTop (20));
    auto rt = sd.removeFromTop (24);
    routeBox.setBounds (rt.removeFromLeft (rt.getWidth() / 2 - 3));
    rt.removeFromLeft (6);
    focusBox.setBounds (rt);
    sd.removeFromTop (4);
    clockBtn.setBounds (sd.removeFromTop (22));
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

void OutriggerEditor::timerCallback()
{
    meter.set (proc.meterPeak (0), proc.meterPeak (1), proc.meterRms (0), proc.meterRms (1), proc.takeClip());

    const auto act = proc.takeActivity();
    for (int i = 0; i < strips.size(); ++i) strips[i]->setActivity ((act >> i) & 1u);

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
    latencyLabel.setText (latText, juce::dontSendNotification);

    juce::String status;
    const auto mode = proc.getOutputMode();
    if (mode == OutputMode::Host) status = "Sending through the host's MIDI output";
    else if (proc.getDirectDeviceId().isEmpty()) status = "Pick a MIDI device for direct output";
    else status = proc.isDirectConnected() ? "Direct device connected" : "Device not available: rescan or reconnect";
    if (proc.syncPending()) status << "  |  sending settings...";
    deviceStatus.setText (status, juce::dontSendNotification);
    deviceStatus.setColour (juce::Label::textColourId, lnf.theme().textDim);
}
} // namespace outrigger
