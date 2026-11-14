// Direct-to-device MIDI output, shared between every Outrigger instance in the process
// (one hardware port can only be opened once on Windows). Each instance has its own
// sender thread that delivers time-stamped events from a lock-free queue.
#pragma once

#include "Events.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <map>
#include <memory>

namespace outrigger
{
// Short messages from the unit, fanned out to the audio thread and the message thread.
struct UnitSink
{
    struct Msg { uint8_t size = 0; uint8_t bytes[3] {}; };
    struct Fifo
    {
        static constexpr int kSize = 1024;
        juce::AbstractFifo fifo { kSize };
        std::array<Msg, kSize> buf {};
        bool push (const Msg& m)
        {
            const auto s = fifo.write (1);
            if (s.blockSize1 > 0) { buf[(size_t) s.startIndex1] = m; return true; }
            if (s.blockSize2 > 0) { buf[(size_t) s.startIndex2] = m; return true; }
            return false;
        }
        bool pop (Msg& m)
        {
            const auto s = fifo.read (1);
            if (s.blockSize1 > 0) { m = buf[(size_t) s.startIndex1]; return true; }
            if (s.blockSize2 > 0) { m = buf[(size_t) s.startIndex2]; return true; }
            return false;
        }
    };
    Fifo audio, message;
};

class MidiOutHub
{
public:
    struct Port
    {
        juce::String identifier, name;
        std::unique_ptr<juce::MidiOutput> output;
        juce::CriticalSection sendLock;
        void send (const uint8_t* data, int size);
    };

    // MIDI input from the unit, shared between instances like the output.
    struct InPort : public juce::MidiInputCallback
    {
        ~InPort() override { if (input != nullptr) input->stop(); }
        void addSink (const std::shared_ptr<UnitSink>& s);
        void removeSink (const std::shared_ptr<UnitSink>& s);
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
        juce::String identifier, name;
        std::unique_ptr<juce::MidiInput> input;
        juce::CriticalSection sinkLock;
        std::vector<std::weak_ptr<UnitSink>> sinks;
    };

    // Opens the device on first use; shared afterwards. Returns null if it can't be opened.
    std::shared_ptr<Port> acquire (const juce::String& identifier);
    std::shared_ptr<InPort> acquireInput (const juce::String& identifier);

private:
    juce::CriticalSection lock;
    std::map<juce::String, std::weak_ptr<Port>> ports;
    std::map<juce::String, std::weak_ptr<InPort>> inPorts;
};

class MidiSender : private juce::Thread
{
public:
    MidiSender();
    ~MidiSender() override;

    void setPort (std::shared_ptr<MidiOutHub::Port> p);   // message thread
    std::shared_ptr<MidiOutHub::Port> getPort() const;
    bool hasPort() const { return connected.load(); }

    // Audio thread: queue an event to go out at a Time::getMillisecondCounterHiRes() time.
    void push (const Event& e, double timeMs);

private:
    void run() override;

    struct Timed { double timeMs; Event e; };
    static constexpr int kSize = 8192;
    juce::AbstractFifo fifo { kSize };
    std::vector<Timed> buffer = std::vector<Timed> (kSize);
    std::vector<Timed> pending;

    mutable juce::SpinLock portLock;
    std::shared_ptr<MidiOutHub::Port> port;
    std::atomic<bool> connected { false };
};
} // namespace outrigger
