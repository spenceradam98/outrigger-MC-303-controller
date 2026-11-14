#pragma once

#include "Analysis.h"
#include "Clock.h"
#include "MidiOutHub.h"
#include "Router.h"
#include "Sync.h"
#include "UnitInput.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace outrigger
{
enum class OutputMode { Host = 0, Direct, Both };

juce::String partName (int part);     // "R", "1".."7"
juce::String paramId (int part, const char* suffix);

class OutriggerProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    OutriggerProcessor();
    ~OutriggerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    static constexpr bool isEffectVariant() { return OUTRIGGER_FX != 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- message-thread controls ----
    void requestSendAll() { sendAllFlag = true; }
    void requestPanic() { panicFlag = true; }
    void startLatencyMeasure (int note) { pingNote = note; pingFlag = true; probeState = (int) LatencyProbe::State::Waiting; }
    int lastMeasureSpread() const { return probeSpread.load(); }
    int lastMeasurePings() const { return probePings.load(); }
    LatencyProbe::State latencyState() const { return (LatencyProbe::State) probeState.load(); }
    int lastMeasuredLatency() const { return probeResult.load(); }
    void setHardwareLatency (int samples);     // reported to the host for compensation
    int getHardwareLatency() const { return hardwareLatency.load(); }

    void setOutputMode (OutputMode m) { outputMode = (int) m; }
    OutputMode getOutputMode() const { return (OutputMode) outputMode.load(); }
    bool setDirectDevice (const juce::String& identifier);   // empty = none
    juce::String getDirectDeviceId() const { return directDeviceId; }
    bool isDirectConnected() const { return sender.hasPort(); }

    // MIDI coming back from the unit.
    bool setUnitInputDevice (const juce::String& identifier);   // empty = none
    juce::String getUnitInputId() const { return unitInputId; }
    bool isUnitInputConnected() const { return inPort != nullptr; }
    uint32_t takeUnitInputActivity() { return unitInActivity.exchange (0); }

    // What the host gives us (shown in the UI so setup problems are obvious).
    bool hostGivesAudioInput() const { return hostInput.load(); }
    bool hostGivesTransport() const { return hostTransport.load(); }
    bool processingRuns() const { return juce::Time::getMillisecondCounter() - lastProcessMs.load() < 1000; }
    int activeParts() const { return moduleMode->load() > 0.5f ? kNumParts : kNormalParts; }

    // Whole-setup presets (part settings, effects, drum edits; not device or return settings).
    juce::ValueTree createSetup() const;
    void applySetup (const juce::ValueTree& setup);

    void setDeviceId (int id) { sysexDeviceId = juce::jlimit (0, 31, id); }
    int getDeviceId() const { return sysexDeviceId.load(); }
    void setExperimentalSysex (bool b) { experimentalSysex = b; }
    bool getExperimentalSysex() const { return experimentalSysex.load(); }

    int getThemeIndex() const { return themeIndex.load(); }
    void setThemeIndex (int t) { themeIndex = t; }

    // Drum-note edits on the rhythm part (-1 = untouched).
    int getDrum (int note, int k) const { return drum[(size_t) note][(size_t) k].load(); }
    void setDrum (int note, int k, int v) { drum[(size_t) note][(size_t) k] = v; }
    void clearDrumEdits();

    // Meters and activity for the UI.
    const Meter& meter() const { return meterIn; }
    float meterPeak (int c) const { return peaks[(size_t) (c & 1)].load(); }
    float meterRms (int c) const { return rmsVals[(size_t) (c & 1)].load(); }
    bool takeClip() { return clipFlag.exchange (false); }
    uint32_t takeActivity() { return activityBits.exchange (0); }
    bool syncPending() const { return syncBusy.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;      // message thread: applies parameter moves made on the unit
    void readParameters();

    // parameter pointers
    struct PartParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* channel = nullptr;
        std::atomic<float>* bank = nullptr;
        std::array<std::atomic<float>*, kNumFields> fields {};
    };
    std::array<PartParams, kNumParts> pp;
    std::atomic<float>* revType = nullptr; std::atomic<float>* choType = nullptr;
    std::atomic<float>* revLevel = nullptr; std::atomic<float>* choLevel = nullptr;
    std::atomic<float>* trimDb = nullptr; std::atomic<float>* outDb = nullptr;
    std::atomic<float>* monitor = nullptr; std::atomic<float>* monoSum = nullptr;
    std::atomic<float>* routeMode = nullptr; std::atomic<float>* focusPart = nullptr;
    std::atomic<float>* clockOn = nullptr;
    std::atomic<float>* moduleMode = nullptr; std::atomic<float>* unitThru = nullptr; std::atomic<float>* unitFollow = nullptr;

    // engine (audio thread)
    Sync sync;
    Router router;
    Clock clock;
    LatencyProbe probe;
    Meter meterIn;
    EventList events, hostOnly;
    std::array<PartState, kNumParts> parts;
    GlobalState global;
    bool clockWasOn = false;
    juce::SmoothedValue<float> trimGain, outGain;

    // cross-thread
    std::atomic<bool> sendAllFlag { true }, panicFlag { false }, pingFlag { false };
    std::atomic<int> pingNote { 37 };
    std::atomic<int> probeState { 0 }, probeResult { 0 }, probeSpread { 0 }, probePings { 0 }, hardwareLatency { 0 };
    std::atomic<bool> hostInput { true }, hostTransport { false };
    std::atomic<uint32_t> lastProcessMs { 0 }, unitInActivity { 0 };
    std::atomic<int> outputMode { (int) OutputMode::Direct };
    std::atomic<int> sysexDeviceId { 0x10 };
    std::atomic<bool> experimentalSysex { false };
    std::atomic<int> themeIndex { 0 };
    std::array<std::array<std::atomic<int>, kDrumParams>, 128> drum;
    std::array<std::atomic<float>, 2> peaks {}, rmsVals {};
    std::atomic<bool> clipFlag { false }, syncBusy { false };
    std::atomic<uint32_t> activityBits { 0 };

    juce::SharedResourcePointer<MidiOutHub> hub;
    MidiSender sender;
    juce::String directDeviceId, unitInputId;
    juce::SpinLock pendingLock;
    juce::String pendingDirectId, pendingInputId;
    std::atomic<bool> pendingDevices { false };
    std::shared_ptr<MidiOutHub::InPort> inPort;
    std::shared_ptr<UnitSink> unitSink = std::make_shared<UnitSink>();
    UnitInput unitParser;     // message thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutriggerProcessor)
};
} // namespace outrigger
