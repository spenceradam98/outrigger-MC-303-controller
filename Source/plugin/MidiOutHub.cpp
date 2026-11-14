#include "MidiOutHub.h"

namespace outrigger
{
void MidiOutHub::Port::send (const uint8_t* data, int size)
{
    if (size <= 0 || output == nullptr) return;
    const juce::ScopedLock sl (sendLock);
    output->sendMessageNow (juce::MidiMessage (data, size));
}

std::shared_ptr<MidiOutHub::Port> MidiOutHub::acquire (const juce::String& identifier)
{
    if (identifier.isEmpty()) return {};
    const juce::ScopedLock sl (lock);
    if (auto existing = ports[identifier].lock()) return existing;

    auto out = juce::MidiOutput::openDevice (identifier);
    if (out == nullptr) return {};
    auto p = std::make_shared<Port>();
    p->identifier = identifier;
    p->name = out->getName();
    p->output = std::move (out);
    ports[identifier] = p;
    return p;
}

std::shared_ptr<MidiOutHub::InPort> MidiOutHub::acquireInput (const juce::String& identifier)
{
    if (identifier.isEmpty()) return {};
    const juce::ScopedLock sl (lock);
    if (auto existing = inPorts[identifier].lock()) return existing;

    auto p = std::make_shared<InPort>();
    p->input = juce::MidiInput::openDevice (identifier, p.get());
    if (p->input == nullptr) return {};
    p->identifier = identifier;
    p->name = p->input->getName();
    p->input->start();
    inPorts[identifier] = p;
    return p;
}

void MidiOutHub::InPort::addSink (const std::shared_ptr<UnitSink>& s)
{
    const juce::ScopedLock sl (sinkLock);
    sinks.push_back (s);
}

void MidiOutHub::InPort::removeSink (const std::shared_ptr<UnitSink>& s)
{
    const juce::ScopedLock sl (sinkLock);
    sinks.erase (std::remove_if (sinks.begin(), sinks.end(),
                                 [&] (const std::weak_ptr<UnitSink>& w) { auto x = w.lock(); return x == nullptr || x == s; }),
                 sinks.end());
}

void MidiOutHub::InPort::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // Channel messages only; clock, active sensing and SysEx from the unit are ignored.
    const int size = m.getRawDataSize();
    if (size < 1 || size > 3 || m.getRawData()[0] >= 0xF0) return;
    UnitSink::Msg msg;
    msg.size = (uint8_t) size;
    std::memcpy (msg.bytes, m.getRawData(), (size_t) size);
    const juce::ScopedLock sl (sinkLock);
    for (auto& w : sinks)
        if (auto s = w.lock()) { s->audio.push (msg); s->message.push (msg); }
}

MidiSender::MidiSender() : juce::Thread ("Outrigger MIDI out")
{
    pending.reserve (kSize);
    startThread (juce::Thread::Priority::highest);
}

MidiSender::~MidiSender()
{
    stopThread (1000);
}

void MidiSender::setPort (std::shared_ptr<MidiOutHub::Port> p)
{
    const juce::SpinLock::ScopedLockType sl (portLock);
    port = std::move (p);
    connected = port != nullptr;
}

std::shared_ptr<MidiOutHub::Port> MidiSender::getPort() const
{
    const juce::SpinLock::ScopedLockType sl (portLock);
    return port;
}

void MidiSender::push (const Event& e, double timeMs)
{
    if (! connected.load() || e.size == 0) return;
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) buffer[(size_t) scope.startIndex1] = { timeMs, e };
    else if (scope.blockSize2 > 0) buffer[(size_t) scope.startIndex2] = { timeMs, e };
}

void MidiSender::run()
{
    while (! threadShouldExit())
    {
        {
            const auto scope = fifo.read (fifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) pending.push_back (buffer[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) pending.push_back (buffer[(size_t) (scope.startIndex2 + i)]);
        }

        if (! pending.empty())
        {
            auto p = getPort();
            const double now = juce::Time::getMillisecondCounterHiRes();
            size_t sent = 0;
            // Events from one instance arrive in time order; send everything that is due.
            while (sent < pending.size() && pending[sent].timeMs <= now + 0.25)
            {
                if (p != nullptr) p->send (pending[sent].e.bytes, pending[sent].e.size);
                ++sent;
            }
            if (sent > 0) pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) sent);

            if (! pending.empty())
            {
                const double wait = pending.front().timeMs - juce::Time::getMillisecondCounterHiRes();
                if (wait > 1.5) juce::Thread::sleep (1);
                else juce::Thread::yield();
                continue;
            }
        }
        juce::Thread::sleep (1);
    }
}
} // namespace outrigger
