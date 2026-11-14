// MIDI clock (24 PPQ) + start/stop/continue/song position from the host transport,
// so the unit's own sequencer and arpeggiator follow the DAW.
#pragma once

#include "Events.h"

#include <cstdint>

namespace outrigger
{
struct Transport
{
    bool valid = false, playing = false;
    double ppq = 0.0, bpm = 120.0;
};

class Clock
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { wasPlaying = false; expectedPpq = -1.0; }
    void process (const Transport& t, int numSamples, EventList& out);

private:
    double sr = 48000.0;
    bool wasPlaying = false;
    double expectedPpq = -1.0;
    int64_t lastTick = -1;
};
} // namespace outrigger
