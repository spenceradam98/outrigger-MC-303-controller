// Engine tests: run with ctest or ./outrigger_tests
#include "Analysis.h"
#include "Clock.h"
#include "Router.h"
#include "Sync.h"
#include "UnitInput.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace outrigger;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool has3 (const EventList& l, uint8_t a, uint8_t b, uint8_t c)
{
    for (auto& e : l) if (e.size == 3 && e.bytes[0] == a && e.bytes[1] == b && e.bytes[2] == c) return true;
    return false;
}

static int countStatus (const EventList& l, uint8_t st)
{
    int n = 0;
    for (auto& e : l) if (e.size > 0 && e.bytes[0] == st) ++n;
    return n;
}

static std::array<PartState, kNumParts> defaultParts()
{
    std::array<PartState, kNumParts> p;
    for (int i = 0; i < kNumParts; ++i) p[(size_t) i].channel = defaultChannelForPart (i);
    return p;
}

int main()
{
    // 1. Roland checksum: the two worked examples in the MC-303 Supplemental Notes.
    {
        const uint8_t a[5] { 0x00, 0x40, 0x01, 0x30, 0x06 };
        CHECK (rolandChecksum (a, 5) == 0x09);
        const uint8_t b[5] { 0x00, 0x40, 0x01, 0x38, 0x05 };
        CHECK (rolandChecksum (b, 5) == 0x02);

        EventList l;
        Sync::buildSysex (l, 0x10, sysex::ReverbMacro, 6);
        const uint8_t expect[13] { 0xF0, 0x41, 0x10, 0x00, 0x03, 0x12, 0x00, 0x40, 0x01, 0x30, 0x06, 0x09, 0xF7 };
        CHECK (l.size() == 1 && l[0].size == 13);
        bool same = true; for (int i = 0; i < 13; ++i) same = same && l[0].bytes[i] == expect[i];
        CHECK (same);
    }

    // 2. NRPN cutoff on part 3 (channel 3): CC99 1, CC98 32, CC6 64+20.
    {
        EventList l;
        Sync::buildNrpn (l, 2, nrpn::ToneMSB, nrpn::Cutoff, 84);
        CHECK (has3 (l, 0xB2, 99, 1) && has3 (l, 0xB2, 98, 32) && has3 (l, 0xB2, 6, 84));
        CHECK (has3 (l, 0xB2, 101, 127) && has3 (l, 0xB2, 100, 127));
    }

    // 3. Full sync sends programs for every part, rhythm set as PC only on ch 10, then stays quiet.
    {
        Sync s; s.prepare (48000.0, 1000000, 1000000);
        auto parts = defaultParts();
        parts[1].bank = 64; parts[1].v[(size_t) Field::Program] = 10;
        parts[0].v[(size_t) Field::Program] = 41;
        GlobalState g;
        EventList l;
        s.process (parts, g, 512, l);
        CHECK (has3 (l, 0xB0, 0, 64));
        CHECK (countStatus (l, 0xC0) == 1 && countStatus (l, 0xC9) == 1);
        bool rhythmBank = false; for (auto& e : l) if (e.bytes[0] == 0xB9 && e.bytes[1] == 0) rhythmBank = true;
        CHECK (! rhythmBank);
        CHECK (has3 (l, 0xB9, 7, 100));                 // volume on rhythm part
        CHECK (countStatus (l, 0xF0) == 2);             // reverb + chorus macros
        l.clear();
        s.process (parts, g, 512, l);
        CHECK (l.size() == 0);

        // 4. A single change sends only that change.
        parts[3].v[(size_t) Field::Pan] = 20;
        s.process (parts, g, 512, l);
        CHECK (l.size() == 1 && has3 (l, 0xB2, 10, 20));

        // 5. Channel change re-sends the part on the new channel.
        l.clear();
        parts[3].channel = 11;
        s.process (parts, g, 512, l);
        CHECK (has3 (l, 0xBB, 10, 20) && countStatus (l, 0xCB) == 1);

        // 6. Drum note edit on the rhythm part: NRPN MSB 0x1C (pan), LSB = note.
        l.clear();
        g.drum[38][2] = 30;
        s.process (parts, g, 512, l);
        CHECK (has3 (l, 0xB9, 99, 0x1C) && has3 (l, 0xB9, 98, 38) && has3 (l, 0xB9, 6, 30));

        // 7. Experimental SysEx stays off unless enabled.
        l.clear();
        g.reverbLevel = 100;
        s.process (parts, g, 512, l);
        CHECK (l.size() == 0);
        g.experimentalSysex = true;
        s.process (parts, g, 512, l);
        CHECK (countStatus (l, 0xF0) == 2);
    }

    // 8. Bandwidth budget: a full resend at 2000 B/s is spread over several blocks.
    {
        Sync s; s.prepare (48000.0, 2000, 384);
        auto parts = defaultParts();
        for (auto& p : parts) p.v[(size_t) Field::Cutoff] = 10;
        GlobalState g;
        int blocks = 0, total = 0;
        EventList l;
        do { l.clear(); total += s.process (parts, g, 480, l); ++blocks; } while (s.pending() && blocks < 1000);
        CHECK (blocks > 1 && blocks < 1000);
        CHECK (total > 384);
    }

    // 9. Router: passthrough keeps channels, focus remaps, mute drops note-ons, routing change releases notes.
    {
        Router r;
        Router::Config c;
        EventList l;
        r.configure (c, l);
        const uint8_t on1[3] { 0x92, 60, 100 };
        r.route (on1, 3, 5, l);
        CHECK (l.size() == 1 && l[0].bytes[0] == 0x92 && l[0].offset == 5);
        CHECK ((r.takeActivity() & (1u << 3)) != 0);

        l.clear();
        c.mode = RouteMode::FocusPart; c.focusPart = 5;
        r.configure (c, l);
        CHECK (has3 (l, 0x82, 60, 0));                  // held note released on the old channel
        CHECK (r.heldCount() == 0);

        l.clear();
        r.route (on1, 3, 0, l);
        CHECK (l.size() == 1 && l[0].bytes[0] == 0x94);
        const uint8_t off1[3] { 0x82, 60, 0 };
        r.route (off1, 3, 0, l);
        CHECK (has3 (l, 0x84, 60, 0));

        l.clear();
        c.enabled[5] = false;
        r.configure (c, l);
        r.route (on1, 3, 0, l);
        CHECK (l.size() == 0);

        l.clear();
        r.panic (l);
        CHECK (has3 (l, 0xB9, 123, 0) && has3 (l, 0xB0, 123, 0));
    }

    // 10. Clock: 120 BPM, 1 s at 48 kHz = 2 beats = 48 ticks, start at the top.
    {
        Clock clk; clk.prepare (48000.0);
        Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.ppq = 0.0;
        int ticks = 0, starts = 0;
        for (int b = 0; b < 100; ++b)
        {
            EventList l;
            clk.process (t, 480, l);
            ticks += countStatus (l, 0xF8);
            starts += countStatus (l, 0xFA);
            t.ppq += 480.0 * 2.0 / 48000.0;
        }
        CHECK (starts == 1);
        if (ticks != 48) std::printf ("ticks=%d\n", ticks);
        CHECK (ticks == 48);
        EventList l;
        t.playing = false;
        clk.process (t, 480, l);
        CHECK (countStatus (l, 0xFC) == 1);
    }

    // 11. Latency probe: simulated hardware answers each hit 300 samples later with a decaying burst.
    {
        LatencyProbe p; p.prepare (48000.0);
        p.start (9, 37, 3);
        std::vector<float> buf (256);
        std::vector<int64_t> hits;
        int64_t pos = 0;
        int noteOns = 0;
        for (int b = 0; b < 2000 && p.getState() == LatencyProbe::State::Waiting; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                float v = 0.001f;
                for (auto h : hits)
                    if (pos + i >= h + 300 && pos + i < h + 300 + 4000) v += 0.6f * std::exp (-(float) (pos + i - h - 300) / 600.0f);
                buf[(size_t) i] = v;
            }
            const float* chans[1] { buf.data() };
            EventList l;
            p.process (chans, 1, 256, l);
            for (auto& e : l) if (e.size == 3 && e.bytes[0] == 0x99 && e.bytes[2] > 0) { ++noteOns; hits.push_back (pos + e.offset); }
            pos += 256;
        }
        CHECK (noteOns == 3);
        CHECK (p.getState() == LatencyProbe::State::Done);
        CHECK (p.successfulPings() == 3);
        CHECK (p.resultSamples() == 300);
        CHECK (p.spreadSamples() == 0);
    }

    // 11b. Latency probe with no return signal times out.
    {
        LatencyProbe p; p.prepare (48000.0);
        p.start (9, 37, 2);
        std::vector<float> buf (256, 0.0f);
        const float* chans[1] { buf.data() };
        for (int b = 0; b < 2000 && p.getState() == LatencyProbe::State::Waiting; ++b) { EventList l; p.process (chans, 1, 256, l); }
        CHECK (p.getState() == LatencyProbe::State::TimedOut);
    }

    // 12. Meter.
    {
        Meter m; m.prepare (48000.0);
        std::vector<float> s (4800, 0.5f);
        const float* chans[1] { s.data() };
        m.process (chans, 1, 4800);
        CHECK (std::fabs (m.peak (0) - 0.5f) < 0.01f && std::fabs (m.peak (1) - 0.5f) < 0.01f);
        CHECK (m.rms (0) > 0.05f && ! m.clipped());
    }

    // 13. Sound Module mode: 16 parts, part 16 on channel 16, part 11 on channel 11.
    {
        CHECK (defaultChannelForPart (0) == 9 && defaultChannelForPart (9) == 8 && defaultChannelForPart (10) == 10 && defaultChannelForPart (15) == 15);
        CHECK (displayNumberForPart (15) == 16 && displayNumberForPart (0) == 10);
        Sync s; s.prepare (48000.0, 1000000, 1000000);
        auto parts = defaultParts();
        GlobalState g;
        EventList l;
        s.process (parts, g, 512, l, kNormalParts);
        CHECK (countStatus (l, 0xCF) == 0);
        l.clear();
        s.process (parts, g, 512, l, kNumParts);
        CHECK (countStatus (l, 0xCF) == 1 && countStatus (l, 0xCA) == 1);

        Router r; Router::Config c; c.numParts = kNumParts;
        l.clear();
        r.configure (c, l);
        const uint8_t on[3] { 0x9F, 60, 90 };
        r.route (on, 3, 0, l);
        CHECK ((r.takeActivity() & (1u << 15)) != 0);
    }

    // 14. Unit input: CC, NRPN, RPN and program changes map back to part parameters.
    {
        UnitInput u;
        std::array<int, kNumParts> ch {};
        for (int p = 0; p < kNumParts; ++p) ch[(size_t) p] = defaultChannelForPart (p);
        u.setParts (ch, kNormalParts);
        ParamChange c;
        const uint8_t vol[3] { 0xB2, 7, 90 };
        CHECK (u.parse (vol, 3, c) && c.part == 3 && c.field == Field::Volume && c.value == 90);
        const uint8_t n1[3] { 0xB0, 99, 1 }, n2[3] { 0xB0, 98, 0x20 }, n3[3] { 0xB0, 6, 100 };
        CHECK (! u.parse (n1, 3, c) && ! u.parse (n2, 3, c));
        CHECK (u.parse (n3, 3, c) && c.part == 1 && c.field == Field::Cutoff && c.value == 36);
        const uint8_t r1[3] { 0xB0, 101, 0 }, r2[3] { 0xB0, 100, 0 }, r3[3] { 0xB0, 6, 12 };
        u.parse (r1, 3, c); u.parse (r2, 3, c);
        CHECK (u.parse (r3, 3, c) && c.field == Field::BendRange && c.value == 12);
        const uint8_t bk[3] { 0xB1, 0, 65 }, pc[2] { 0xC1, 57 };
        u.parse (bk, 3, c);
        CHECK (u.parse (pc, 2, c) && c.part == 2 && c.field == Field::Program && c.value == 57 && c.bank == 65);
        const uint8_t rpc[2] { 0xC9, 40 };
        CHECK (u.parse (rpc, 2, c) && c.part == 0 && c.bank == -1);
        const uint8_t other[3] { 0xBC, 7, 1 };     // channel 13 is not a part in normal mode
        CHECK (! u.parse (other, 3, c));
    }

    if (failures == 0) std::printf ("All engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
