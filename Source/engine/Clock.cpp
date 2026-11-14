#include "Clock.h"

#include <algorithm>
#include <cmath>

namespace outrigger
{
static void songPosition (EventList& out, int offset, double ppq)
{
    const int sixteenths = (int) std::floor (std::max (0.0, ppq) * 4.0 + 1e-6) & 0x3FFF;
    out.add3 (offset, 0xF2, (uint8_t) (sixteenths & 0x7F), (uint8_t) (sixteenths >> 7));
}

void Clock::process (const Transport& t, int numSamples, EventList& out)
{
    if (! t.valid || t.bpm <= 0.0)
        return;

    if (! t.playing)
    {
        if (wasPlaying) out.add1 (0, 0xFC);
        wasPlaying = false;
        expectedPpq = -1.0;
        return;
    }

    const double beatsPerSample = t.bpm / 60.0 / sr;
    double ppq = t.ppq;

    bool restart = ! wasPlaying;
    if (! wasPlaying)
    {
        if (ppq < 1.0 / 96.0) out.add1 (0, 0xFA);
        else { songPosition (out, 0, ppq); out.add1 (0, 0xFB); }
    }
    else if (std::abs (ppq - expectedPpq) > 1.0 / 48.0)   // loop or jump
    {
        out.add1 (0, 0xFC);
        songPosition (out, 0, ppq);
        out.add1 (0, 0xFB);
        restart = true;
    }
    wasPlaying = true;
    if (restart) lastTick = (int64_t) std::ceil (ppq * 24.0 - 1e-6) - 1;

    // Song position is in 16ths; after continue, clocks resume from the next 16th boundary.
    const double end = ppq + beatsPerSample * numSamples;
    for (int64_t k = lastTick + 1; (double) k / 24.0 < end - 1e-9; ++k)
    {
        const int offset = std::clamp ((int) std::floor (((double) k / 24.0 - ppq) / beatsPerSample), 0, numSamples - 1);
        out.add1 (offset, 0xF8);
        lastTick = k;
    }
    expectedPpq = end;
}
} // namespace outrigger
