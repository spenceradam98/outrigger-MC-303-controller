#include "Router.h"

#include <algorithm>

namespace outrigger
{
int Router::partForChannel (int ch) const
{
    for (int p = 0; p < cfg.numParts; ++p)
        if (cfg.channel[(size_t) p] == ch) return p;
    return -1;
}

int Router::heldCount() const
{
    int n = 0;
    for (auto& ch : held) for (auto c : ch) n += c;
    return n;
}

void Router::configure (const Config& c, EventList& out)
{
    bool changed = c.mode != cfg.mode || c.focusPart != cfg.focusPart || c.numParts != cfg.numParts;
    for (int p = 0; p < kNumParts; ++p)
        changed = changed || c.channel[(size_t) p] != cfg.channel[(size_t) p] || c.enabled[(size_t) p] != cfg.enabled[(size_t) p];
    if (changed) releaseAll (out, 0);
    cfg = c;
}

void Router::route (const uint8_t* data, int size, int offset, EventList& out)
{
    if (size < 1 || data[0] < 0x80 || data[0] >= 0xF0) return;

    const int type = data[0] & 0xF0;
    const int inCh = data[0] & 0x0F;
    int part = cfg.mode == RouteMode::FocusPart ? std::min (cfg.focusPart, cfg.numParts - 1) : partForChannel (inCh);
    int outCh = inCh;

    if (part >= 0)
    {
        if (! cfg.enabled[(size_t) part])
        {
            // Let note-offs through for notes started before the part was muted.
            if (! (type == 0x80 || (type == 0x90 && size > 2 && data[2] == 0))) return;
        }
        outCh = cfg.channel[(size_t) part];
    }

    uint8_t m[3] { (uint8_t) (type | outCh), size > 1 ? data[1] : (uint8_t) 0, size > 2 ? data[2] : (uint8_t) 0 };
    const int n = (type == 0xC0 || type == 0xD0) ? 2 : 3;

    if (type == 0x90 && m[2] > 0)
    {
        auto& h = held[(size_t) outCh][(size_t) (m[1] & 127)];
        if (h < 255) ++h;
        if (part >= 0) activity |= 1u << part;
    }
    else if (type == 0x80 || (type == 0x90 && m[2] == 0))
    {
        auto& h = held[(size_t) outCh][(size_t) (m[1] & 127)];
        if (h == 0) return;   // already released by a routing change
        --h;
    }
    out.add (offset, m, n);
}

void Router::releaseAll (EventList& out, int offset)
{
    for (int ch = 0; ch < 16; ++ch)
        for (int note = 0; note < 128; ++note)
            while (held[(size_t) ch][(size_t) note] > 0)
            {
                out.add3 (offset, (uint8_t) (0x80 | ch), (uint8_t) note, 0);
                --held[(size_t) ch][(size_t) note];
            }
}

void Router::panic (EventList& out, int offset)
{
    releaseAll (out, offset);
    for (int p = 0; p < cfg.numParts; ++p)
    {
        const auto st = (uint8_t) (0xB0 | cfg.channel[(size_t) p]);
        out.add3 (offset, st, cc::Hold, 0);
        out.add3 (offset, st, cc::AllNotesOff, 0);
        out.add3 (offset, st, cc::AllSoundOff, 0);
    }
}
} // namespace outrigger
