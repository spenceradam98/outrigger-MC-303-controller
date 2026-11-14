// Routes the host's incoming MIDI to the unit's part channels, tracks held notes so
// nothing hangs when routing changes, and records per-part note activity for the UI.
#pragma once

#include "Events.h"
#include "Protocol.h"

#include <array>

namespace outrigger
{
enum class RouteMode
{
    Passthrough,   // keep the host's channel (send part R on ch 10, parts on 1-7 from the DAW)
    FocusPart      // everything goes to the selected part's channel (one instance per DAW track)
};

class Router
{
public:
    struct Config
    {
        RouteMode mode = RouteMode::Passthrough;
        int focusPart = 1;
        int numParts = kNormalParts;               // 8 normal, 16 in Sound Module mode
        std::array<int, kNumParts> channel {};     // 0-based
        std::array<bool, kNumParts> enabled {};
        Config() { for (int p = 0; p < kNumParts; ++p) { channel[(size_t) p] = defaultChannelForPart (p); enabled[(size_t) p] = true; } }
    };

    // Call once per block before routing; emits note-offs if routing changed under held notes.
    void configure (const Config& c, EventList& out);

    // One incoming short message (1..3 bytes). SysEx and system messages from the host are dropped.
    void route (const uint8_t* data, int size, int offset, EventList& out);

    // Note-off for every held note plus All Notes Off on all part channels.
    void panic (EventList& out, int offset = 0);
    // Note-off for held notes only (transport stop, bypass).
    void releaseAll (EventList& out, int offset = 0);

    // Part index for a 0-based channel, or -1.
    int partForChannel (int ch) const;

    // Bit p set if part p received a note-on since the last call.
    uint32_t takeActivity() { auto a = activity; activity = 0; return a; }
    int numParts() const { return cfg.numParts; }
    int heldCount() const;

private:
    Config cfg;
    std::array<std::array<uint8_t, 128>, 16> held {};   // velocity-free count of held notes per out channel
    uint32_t activity = 0;
};
} // namespace outrigger
