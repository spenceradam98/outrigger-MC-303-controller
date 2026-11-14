// Direct-to-device MIDI output, shared between every Outrigger instance in the process
// (one hardware port can only be opened once on Windows). Each instance has its own
// sender thread that delivers time-stamped events from a lock-free queue.
#pragma once

#include "Events.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <map>
#include <memory>

namespace outrigger
{
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

    // Opens the device on first use; shared afterwards. Returns null if it can't be opened.
    std::shared_ptr<Port> acquire (const juce::String& identifier);

private:
    juce::CriticalSection lock;
    std::map<juce::String, std::weak_ptr<Port>> ports;
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
