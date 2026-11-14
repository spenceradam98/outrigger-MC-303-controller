#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace outrigger
{
juce::String partName (int part) { return part == kRhythmPart ? juce::String ("R") : juce::String (displayNumberForPart (part)); }
juce::String paramId (int part, const char* suffix) { return "p" + juce::String (part) + "_" + suffix; }

static juce::String offsetText (float v, int) { const int i = juce::roundToInt (v); return i > 0 ? "+" + juce::String (i) : juce::String (i); }

juce::AudioProcessorValueTreeState::ParameterLayout OutriggerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int p = 0; p < kNumParts; ++p)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("part" + String (p), "Part " + partName (p), " | ");
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { paramId (p, "on"), 1 }, "Part " + partName (p) + " On", true));
        group->addChild (std::make_unique<AudioParameterInt> (ParameterID { paramId (p, "ch"), 1 }, "Part " + partName (p) + " Channel", 1, 16, defaultChannelForPart (p) + 1));
        group->addChild (std::make_unique<AudioParameterInt> (ParameterID { paramId (p, "bank"), 1 }, "Part " + partName (p) + " Bank", 0, 127, 0));
        for (int f = 0; f < kNumFields; ++f)
        {
            const auto& fi = kFields[f];
            const auto name = "Part " + partName (p) + " " + fi.name;
            const ParameterID id { paramId (p, fi.id), 1 };
            if (fi.maxValue == 1)
                group->addChild (std::make_unique<AudioParameterBool> (id, name, fi.defaultValue != 0));
            else if (fi.minValue < 0)
                group->addChild (std::make_unique<AudioParameterInt> (id, name, fi.minValue, fi.maxValue, fi.defaultValue,
                                                                     AudioParameterIntAttributes().withStringFromValueFunction (offsetText)));
            else
                group->addChild (std::make_unique<AudioParameterInt> (id, name, fi.minValue, fi.maxValue, fi.defaultValue));
        }
        layout.add (std::move (group));
    }

    StringArray rev, cho;
    for (auto* s : kReverbTypes) rev.add (s);
    for (auto* s : kChorusTypes) cho.add (s);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rev_type", 1 }, "Reverb Type", rev, 4));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "cho_type", 1 }, "Chorus Type", cho, 2));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "rev_level", 1 }, "Reverb Level", 0, 127, 64));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "cho_level", 1 }, "Chorus Level", 0, 127, 64));

    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "trim", 1 }, "Return Trim", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output", NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "monitor", 1 }, "Monitor Return", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mono", 1 }, "Mono Sum", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "route", 1 }, "MIDI Routing", StringArray { "Passthrough", "Focus Part" }, 0));
    StringArray partNames;
    for (int p = 0; p < kNumParts; ++p) partNames.add ("Part " + partName (p));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "focus", 1 }, "Focus Part", partNames, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "clock", 1 }, "Send MIDI Clock", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "module_mode", 1 }, "Sound Module Mode (16 parts)", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "unit_follow", 1 }, "Follow Unit Knobs", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "unit_thru", 1 }, "Unit MIDI To Host", false));
    return layout;
}

OutriggerProcessor::OutriggerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Return", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Outrigger", createLayout())
{
    for (int p = 0; p < kNumParts; ++p)
    {
        auto& x = pp[(size_t) p];
        x.on = apvts.getRawParameterValue (paramId (p, "on"));
        x.channel = apvts.getRawParameterValue (paramId (p, "ch"));
        x.bank = apvts.getRawParameterValue (paramId (p, "bank"));
        for (int f = 0; f < kNumFields; ++f) x.fields[(size_t) f] = apvts.getRawParameterValue (paramId (p, kFields[f].id));
    }
    revType = apvts.getRawParameterValue ("rev_type");
    choType = apvts.getRawParameterValue ("cho_type");
    revLevel = apvts.getRawParameterValue ("rev_level");
    choLevel = apvts.getRawParameterValue ("cho_level");
    trimDb = apvts.getRawParameterValue ("trim");
    outDb = apvts.getRawParameterValue ("out");
    monitor = apvts.getRawParameterValue ("monitor");
    monoSum = apvts.getRawParameterValue ("mono");
    routeMode = apvts.getRawParameterValue ("route");
    focusPart = apvts.getRawParameterValue ("focus");
    clockOn = apvts.getRawParameterValue ("clock");
    moduleMode = apvts.getRawParameterValue ("module_mode");
    unitFollow = apvts.getRawParameterValue ("unit_follow");
    unitThru = apvts.getRawParameterValue ("unit_thru");
    clearDrumEdits();
    startTimerHz (30);
}

void OutriggerProcessor::clearDrumEdits()
{
    for (auto& n : drum) for (auto& v : n) v = -1;
}

bool OutriggerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void OutriggerProcessor::prepareToPlay (double sampleRate, int)
{
    sync.prepare (sampleRate);
    clock.prepare (sampleRate);
    probe.prepare (sampleRate);
    meterIn.prepare (sampleRate);
    trimGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    trimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimDb->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outDb->load(), -60.0f));
    sendAllFlag = true;
    setLatencySamples (hardwareLatency.load());
}

void OutriggerProcessor::readParameters()
{
    for (int p = 0; p < kNumParts; ++p)
    {
        auto& ps = parts[(size_t) p];
        const auto& x = pp[(size_t) p];
        ps.enabled = x.on->load() > 0.5f;
        ps.channel = juce::jlimit (0, 15, (int) x.channel->load() - 1);
        ps.bank = (int) x.bank->load();
        for (int f = 0; f < kNumFields; ++f) ps.v[(size_t) f] = juce::roundToInt (x.fields[(size_t) f]->load());
    }
    global.reverbType = (int) revType->load();
    global.chorusType = (int) choType->load();
    global.reverbLevel = (int) revLevel->load();
    global.chorusLevel = (int) choLevel->load();
    global.experimentalSysex = experimentalSysex.load();
    global.deviceId = (uint8_t) sysexDeviceId.load();
    for (int n = 0; n < 128; ++n)
        for (int k = 0; k < kDrumParams; ++k) global.drum[(size_t) n][(size_t) k] = drum[(size_t) n][(size_t) k].load();
}

void OutriggerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    const double blockStartMs = juce::Time::getMillisecondCounterHiRes();

    readParameters();
    events.clear();

    Router::Config rc;
    rc.mode = routeMode->load() > 0.5f ? RouteMode::FocusPart : RouteMode::Passthrough;
    rc.focusPart = (int) focusPart->load();
    rc.numParts = activeParts();
    for (int p = 0; p < kNumParts; ++p) { rc.channel[(size_t) p] = parts[(size_t) p].channel; rc.enabled[(size_t) p] = parts[(size_t) p].enabled; }
    router.configure (rc, events);

    if (panicFlag.exchange (false)) router.panic (events);
    if (sendAllFlag.exchange (false)) sync.invalidateAll();

    lastProcessMs = juce::Time::getMillisecondCounter();
    hostInput = getTotalNumInputChannels() > 0;

    for (const auto meta : midi)
        router.route (meta.data, meta.numBytes, meta.samplePosition, events);

    // MIDI from the unit, forwarded to the host (so it can be recorded) when enabled.
    UnitSink::Msg um;
    const bool thru = unitThru->load() > 0.5f;
    while (unitSink->audio.pop (um))
        if (thru) hostOnly.add (0, um.bytes, um.size);

    // Transport -> MIDI clock
    if (clockOn->load() > 0.5f)
    {
        Transport t;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                hostTransport = pos->getBpm().hasValue();
                t.valid = pos->getPpqPosition().hasValue() && pos->getBpm().hasValue();
                t.playing = pos->getIsPlaying();
                t.ppq = pos->getPpqPosition().orFallback (0.0);
                t.bpm = pos->getBpm().orFallback (120.0);
            }
        clock.process (t, n, events);
        clockWasOn = true;
    }
    else if (clockWasOn)
    {
        events.add1 (0, 0xFC);
        clock.reset();
        clockWasOn = false;
    }

    sync.process (parts, global, n, events, activeParts());
    syncBusy = sync.pending();

    // ---- return audio ----
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    trimGain.setTargetValue (juce::Decibels::decibelsToGain (trimDb->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (outDb->load(), -60.0f));

    if (numIn == 0) buffer.clear();
    for (int c = numIn; c < numOut; ++c)   // mono return feeding a stereo output
        if (numIn > 0) buffer.copyFrom (c, 0, buffer, 0, 0, n); else buffer.clear (c, 0, n);
    const int chans = juce::jmin (2, numOut);

    if (monoSum->load() > 0.5f && chans == 2)
    {
        buffer.addFrom (0, 0, buffer, 1, 0, n);
        buffer.applyGain (0, 0, n, 0.5f);
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    }
    for (int i = 0; i < n; ++i)
    {
        const float g = trimGain.getNextValue();
        for (int c = 0; c < chans; ++c) buffer.getWritePointer (c)[i] *= g;
    }

    meterIn.process (buffer.getArrayOfReadPointers(), chans, n);
    for (int c = 0; c < 2; ++c) { peaks[(size_t) c] = meterIn.peak (c); rmsVals[(size_t) c] = meterIn.rms (c); }
    if (meterIn.clipped()) { clipFlag = true; meterIn.resetClip(); }

    if (pingFlag.exchange (false)) probe.start (parts[(size_t) kRhythmPart].channel, pingNote.load(), 3);
    probe.process (buffer.getArrayOfReadPointers(), chans, n, events);
    if (probe.getState() == LatencyProbe::State::Done)
    {
        probeResult = probe.resultSamples();
        probeSpread = probe.spreadSamples();
        probePings = probe.successfulPings();
    }
    probeState = (int) probe.getState();

    if (monitor->load() > 0.5f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            for (int c = 0; c < chans; ++c) buffer.getWritePointer (c)[i] *= g;
        }
    }
    else
    {
        outGain.skip (n);
        buffer.clear();
    }

    activityBits |= router.takeActivity();

    // ---- MIDI out ----
    events.sortByOffset();
    midi.clear();
    const auto mode = getOutputMode();
    if (mode != OutputMode::Direct)
        for (const auto& e : events) midi.addEvent (e.bytes, e.size, e.offset);
    for (const auto& e : hostOnly) midi.addEvent (e.bytes, e.size, e.offset);
    hostOnly.clear();
    if (mode != OutputMode::Host && ! isNonRealtime())
    {
        // Schedule one block ahead so offsets inside the block keep their spacing.
        const double base = blockStartMs + 1000.0 * n / sr;
        for (const auto& e : events) sender.push (e, base + 1000.0 * e.offset / sr);
    }
}

void OutriggerProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    events.clear();
    router.releaseAll (events);
    midi.clear();
    for (const auto& e : events)
    {
        if (getOutputMode() != OutputMode::Direct) midi.addEvent (e.bytes, e.size, e.offset);
        if (getOutputMode() != OutputMode::Host) sender.push (e, juce::Time::getMillisecondCounterHiRes());
    }
    buffer.clear();
}

void OutriggerProcessor::setHardwareLatency (int samples)
{
    hardwareLatency = juce::jmax (0, samples);
    setLatencySamples (hardwareLatency.load());
}

bool OutriggerProcessor::setDirectDevice (const juce::String& identifier)
{
    directDeviceId = identifier;
    auto port = hub->acquire (identifier);
    sender.setPort (port);
    sendAllFlag = true;      // a newly connected unit knows nothing yet
    return port != nullptr || identifier.isEmpty();
}

bool OutriggerProcessor::setUnitInputDevice (const juce::String& identifier)
{
    if (inPort != nullptr) inPort->removeSink (unitSink);
    unitInputId = identifier;
    inPort = hub->acquireInput (identifier);
    if (inPort != nullptr) inPort->addSink (unitSink);
    return inPort != nullptr || identifier.isEmpty();
}

OutriggerProcessor::~OutriggerProcessor()
{
    stopTimer();
    if (inPort != nullptr) inPort->removeSink (unitSink);
}

void OutriggerProcessor::timerCallback()
{
    if (pendingDevices.exchange (false))
    {
        juce::String out, in;
        {
            const juce::SpinLock::ScopedLockType sl (pendingLock);
            out = pendingDirectId;
            in = pendingInputId;
        }
        setDirectDevice (out);
        setUnitInputDevice (in);
    }

    // Knob moves and program changes made on the unit itself.
    std::array<int, kNumParts> channels {};
    for (int p = 0; p < kNumParts; ++p) channels[(size_t) p] = juce::jlimit (0, 15, (int) pp[(size_t) p].channel->load() - 1);
    unitParser.setParts (channels, activeParts());
    const bool follow = unitFollow->load() > 0.5f;
    UnitSink::Msg m;
    while (unitSink->message.pop (m))
    {
        ParamChange c;
        if (! unitParser.parse (m.bytes, m.size, c) || c.part < 0) continue;
        unitInActivity |= 1u << c.part;
        if (! follow) continue;
        auto setParam = [this] (const juce::String& id, int value) {
            if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) value));
        };
        setParam (paramId (c.part, kFields[(int) c.field].id), c.value);
        if (c.field == Field::Program && c.bank >= 0) setParam (paramId (c.part, "bank"), c.bank);
    }
}

// ---- setups ----
static bool isSetupParam (const juce::String& id)
{
    return (id.startsWithChar ('p') && id.length() > 1 && juce::CharacterFunctions::isDigit (id[1]))
        || id == "rev_type" || id == "cho_type" || id == "rev_level" || id == "cho_level" || id == "module_mode";
}

juce::ValueTree OutriggerProcessor::createSetup() const
{
    juce::ValueTree setup ("OutriggerSetup");
    setup.setProperty ("version", 1, nullptr);
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && isSetupParam (r->getParameterID()))
        {
            juce::ValueTree v ("Param");
            v.setProperty ("id", r->getParameterID(), nullptr);
            v.setProperty ("value", r->convertFrom0to1 (r->getValue()), nullptr);
            setup.appendChild (v, nullptr);
        }
    juce::String drumText;
    for (int note = 0; note < 128; ++note)
        for (int k = 0; k < kDrumParams; ++k)
            if (const int v = drum[(size_t) note][(size_t) k].load(); v >= 0)
                drumText << note << ":" << k << ":" << v << " ";
    setup.setProperty ("drum", drumText.trim(), nullptr);
    return setup;
}

static void parseDrumText (OutriggerProcessor& p, const juce::String& text)
{
    p.clearDrumEdits();
    for (auto& tok : juce::StringArray::fromTokens (text, " ", ""))
    {
        auto f = juce::StringArray::fromTokens (tok, ":", "");
        if (f.size() == 3)
            p.setDrum (juce::jlimit (0, 127, f[0].getIntValue()), juce::jlimit (0, kDrumParams - 1, f[1].getIntValue()),
                       juce::jlimit (0, 127, f[2].getIntValue()));
    }
}

void OutriggerProcessor::applySetup (const juce::ValueTree& setup)
{
    if (! setup.hasType ("OutriggerSetup")) return;
    for (auto v : setup)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (v.getProperty ("id").toString())))
            if (isSetupParam (r->getParameterID()))
                r->setValueNotifyingHost (r->convertTo0to1 ((float) (double) v.getProperty ("value")));
    parseDrumText (*this, setup.getProperty ("drum").toString());
    sendAllFlag = true;
}

// ---- host state ----
static juce::String findDevice (const juce::Array<juce::MidiDeviceInfo>& devices, juce::String id, const juce::String& name)
{
    for (auto& d : devices) if (d.identifier == id) return id;
    if (name.isNotEmpty())
        for (auto& d : devices) if (d.name == name) return d.identifier;   // the OS renumbered the port
    return id;    // kept even if absent, so it reconnects when the device returns
}

void OutriggerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree settings ("Settings");
    settings.setProperty ("outputMode", outputMode.load(), nullptr);
    settings.setProperty ("deviceId", directDeviceId, nullptr);
    if (auto p = sender.getPort()) settings.setProperty ("deviceName", p->name, nullptr);
    settings.setProperty ("inputId", unitInputId, nullptr);
    if (inPort != nullptr) settings.setProperty ("inputName", inPort->name, nullptr);
    settings.setProperty ("sysexDevice", sysexDeviceId.load(), nullptr);
    settings.setProperty ("experimental", experimentalSysex.load(), nullptr);
    settings.setProperty ("theme", themeIndex.load(), nullptr);
    settings.setProperty ("latency", hardwareLatency.load(), nullptr);
    settings.setProperty ("drum", createSetup().getProperty ("drum"), nullptr);

    state.removeChild (state.getChildWithName ("Settings"), nullptr);
    state.appendChild (settings, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void OutriggerProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto settings = state.getChildWithName ("Settings");
    apvts.replaceState (state);

    if (settings.isValid())
    {
        outputMode = (int) settings.getProperty ("outputMode", (int) OutputMode::Direct);
        sysexDeviceId = (int) settings.getProperty ("sysexDevice", 0x10);
        experimentalSysex = (bool) settings.getProperty ("experimental", false);
        themeIndex = (int) settings.getProperty ("theme", 0);
        parseDrumText (*this, settings.getProperty ("drum").toString());
        setHardwareLatency ((int) settings.getProperty ("latency", 0));

        const auto out = findDevice (juce::MidiOutput::getAvailableDevices(), settings.getProperty ("deviceId").toString(),
                                     settings.getProperty ("deviceName").toString());
        const auto in = findDevice (juce::MidiInput::getAvailableDevices(), settings.getProperty ("inputId").toString(),
                                    settings.getProperty ("inputName").toString());
        if (juce::MessageManager::existsAndIsCurrentThread())
        {
            setDirectDevice (out);
            setUnitInputDevice (in);
        }
        else
        {
            // Some hosts restore state off the message thread: open the ports from the timer instead.
            const juce::SpinLock::ScopedLockType sl (pendingLock);
            pendingDirectId = out;
            pendingInputId = in;
            pendingDevices = true;
        }
    }
    sendAllFlag = true;
}

juce::AudioProcessorEditor* OutriggerProcessor::createEditor() { return new OutriggerEditor (*this); }
} // namespace outrigger

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new outrigger::OutriggerProcessor(); }
