#include "Sync.h"

#include <algorithm>

namespace outrigger
{
void Sync::prepare (double sampleRate, int bytesPerSecond, int burstBytes)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    rate = bytesPerSecond;
    burst = burstBytes;
    tokens = burst;
    invalidateAll();
}

void Sync::invalidateAll()
{
    for (int p = 0; p < kNumParts; ++p) invalidatePart (p);
    sentReverbType = sentChorusType = sentReverbLevel = sentChorusLevel = kUnknown;
    for (auto& n : sentDrum) n.fill (kUnknown);
}

void Sync::invalidatePart (int part)
{
    sent[(size_t) part].fill (kUnknown);
    sentChannel[(size_t) part] = kUnknown;
    sentBank[(size_t) part] = kUnknown;
    if (part == kRhythmPart)
        for (auto& n : sentDrum) n.fill (kUnknown);
}

int Sync::buildNrpn (EventList& out, int ch, uint8_t msb, uint8_t lsb, uint8_t value)
{
    const uint8_t st = (uint8_t) (0xB0 | (ch & 15));
    out.add3 (0, st, cc::NrpnMSB, msb);
    out.add3 (0, st, cc::NrpnLSB, lsb);
    out.add3 (0, st, cc::DataEntry, value);
    // Null RPN afterwards so a stray CC6 from elsewhere can't change this parameter.
    out.add3 (0, st, cc::RpnMSB, 127);
    out.add3 (0, st, cc::RpnLSB, 127);
    return 15;
}

int Sync::buildRpn (EventList& out, int ch, uint8_t lsb, uint8_t value)
{
    const uint8_t st = (uint8_t) (0xB0 | (ch & 15));
    out.add3 (0, st, cc::RpnMSB, 0);
    out.add3 (0, st, cc::RpnLSB, lsb);
    out.add3 (0, st, cc::DataEntry, value);
    out.add3 (0, st, cc::RpnMSB, 127);
    out.add3 (0, st, cc::RpnLSB, 127);
    return 15;
}

int Sync::buildSysex (EventList& out, uint8_t deviceId, const uint8_t address[4], uint8_t data)
{
    uint8_t body[5] { address[0], address[1], address[2], address[3], (uint8_t) (data & 0x7F) };
    const uint8_t m[13] { 0xF0, sysex::Roland, (uint8_t) (deviceId & 0x7F), sysex::ModelHi, sysex::ModelLo, sysex::DataSet,
                          body[0], body[1], body[2], body[3], body[4], rolandChecksum (body, 5), 0xF7 };
    out.add (0, m, 13);
    return 13;
}

int Sync::costOf (Field f) const
{
    switch (f)
    {
        case Field::Program: return 3 + 3 + 2;
        case Field::Cutoff: case Field::Resonance: case Field::Attack: case Field::Decay: case Field::Release:
        case Field::VibRate: case Field::VibDepth: case Field::VibDelay:
        case Field::BendRange: case Field::CoarseTune: case Field::FineTune: return 15;
        default: return 3;
    }
}

int Sync::emitField (EventList& out, int ch, Field f, int value, int bank, bool rhythm)
{
    const uint8_t st = (uint8_t) (0xB0 | (ch & 15));
    auto clamp7 = [] (int x) { return (uint8_t) std::clamp (x, 0, 127); };
    auto nrpnValue = [] (int offset) { return (uint8_t) (64 + std::clamp (offset, -nrpn::OffsetRange, nrpn::OffsetRange)); };

    switch (f)
    {
        case Field::Program:
            if (rhythm)   // rhythm sets are program change only
            {
                out.add2 (0, (uint8_t) (0xC0 | (ch & 15)), clamp7 (value));
                return 2;
            }
            out.add3 (0, st, cc::BankMSB, clamp7 (bank));
            out.add3 (0, st, cc::BankLSB, 0);
            out.add2 (0, (uint8_t) (0xC0 | (ch & 15)), clamp7 (value));
            return 8;
        case Field::Volume:     out.add3 (0, st, cc::Volume, clamp7 (value)); return 3;
        case Field::Pan:        out.add3 (0, st, cc::Pan, clamp7 (value)); return 3;
        case Field::Expression: out.add3 (0, st, cc::Expression, clamp7 (value)); return 3;
        case Field::ReverbSend: out.add3 (0, st, cc::ReverbSend, clamp7 (value)); return 3;
        case Field::ChorusSend: out.add3 (0, st, cc::ChorusSend, clamp7 (value)); return 3;
        case Field::Cutoff:     return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::Cutoff, nrpnValue (value));
        case Field::Resonance:  return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::Resonance, nrpnValue (value));
        case Field::Attack:     return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::Attack, nrpnValue (value));
        case Field::Decay:      return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::Decay, nrpnValue (value));
        case Field::Release:    return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::Release, nrpnValue (value));
        case Field::VibRate:    return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::VibRate, nrpnValue (value));
        case Field::VibDepth:   return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::VibDepth, nrpnValue (value));
        case Field::VibDelay:   return buildNrpn (out, ch, nrpn::ToneMSB, nrpn::VibDelay, nrpnValue (value));
        case Field::Portamento: out.add3 (0, st, cc::Portamento, value ? 127 : 0); return 3;
        case Field::PortaTime:  out.add3 (0, st, cc::PortaTime, clamp7 (value)); return 3;
        case Field::Mono:       out.add3 (0, st, value ? cc::MonoOn : cc::PolyOn, value ? 1 : 0); return 3;
        case Field::BendRange:  return buildRpn (out, ch, rpn::BendRange, clamp7 (value));
        case Field::CoarseTune: return buildRpn (out, ch, rpn::CoarseTune, clamp7 (64 + value));
        case Field::FineTune:   return buildRpn (out, ch, rpn::FineTune, clamp7 (64 + value));
        case Field::Count: break;
    }
    return 0;
}

int Sync::process (const std::array<PartState, kNumParts>& parts, const GlobalState& g, int numSamples, EventList& out,
                   int numActiveParts)
{
    tokens = std::min (burst, tokens + rate * numSamples / sr);
    int emitted = 0;
    lastWasPartial = false;

    auto afford = [&] (int cost) {
        if (tokens < cost || out.size() + 8 > EventList::kCapacity) { lastWasPartial = true; return false; }
        return true;
    };
    auto spend = [&] (int bytes) { tokens -= bytes; emitted += bytes; };

    // Global effects first (they colour every part).
    if (g.reverbType != sentReverbType && afford (13)) { spend (buildSysex (out, g.deviceId, sysex::ReverbMacro, (uint8_t) g.reverbType)); sentReverbType = g.reverbType; }
    if (g.chorusType != sentChorusType && afford (13)) { spend (buildSysex (out, g.deviceId, sysex::ChorusMacro, (uint8_t) g.chorusType)); sentChorusType = g.chorusType; }
    if (g.experimentalSysex)
    {
        if (g.reverbLevel != sentReverbLevel && afford (13)) { spend (buildSysex (out, g.deviceId, sysex::ReverbLevel, (uint8_t) g.reverbLevel)); sentReverbLevel = g.reverbLevel; }
        if (g.chorusLevel != sentChorusLevel && afford (13)) { spend (buildSysex (out, g.deviceId, sysex::ChorusLevel, (uint8_t) g.chorusLevel)); sentChorusLevel = g.chorusLevel; }
    }

    // Programs for all parts before anything else, then the rest field by field, so a
    // full resend gets every part playing the right sound as early as possible.
    for (int fi = 0; fi < kNumFields; ++fi)
    {
        const auto f = (Field) fi;
        for (int p = 0; p < std::clamp (numActiveParts, 1, kNumParts); ++p)
        {
            const auto& ps = parts[(size_t) p];
            if (! ps.enabled) continue;
            const bool rhythm = p == kRhythmPart;

            if (sentChannel[(size_t) p] != ps.channel)
            {
                sent[(size_t) p].fill (kUnknown);
                sentBank[(size_t) p] = kUnknown;
                sentChannel[(size_t) p] = ps.channel;
                if (rhythm) for (auto& n : sentDrum) n.fill (kUnknown);
            }

            const int want = ps.v[(size_t) fi];
            bool changed = sent[(size_t) p][(size_t) fi] != want;
            if (f == Field::Program && ! rhythm) changed = changed || sentBank[(size_t) p] != ps.bank;
            if (! changed) continue;

            const int cost = rhythm && f == Field::Program ? 2 : costOf (f);
            if (! afford (cost)) return emitted;
            spend (emitField (out, ps.channel, f, want, ps.bank, rhythm));
            sent[(size_t) p][(size_t) fi] = want;
            if (f == Field::Program) sentBank[(size_t) p] = ps.bank;
        }
    }

    // Drum-note edits on the rhythm part.
    const auto& rp = parts[(size_t) kRhythmPart];
    if (rp.enabled)
        for (int note = 0; note < 128; ++note)
            for (int k = 0; k < kDrumParams; ++k)
            {
                const int want = g.drum[(size_t) note][(size_t) k];
                if (want < 0 || sentDrum[(size_t) note][(size_t) k] == want) continue;
                if (! afford (15)) return emitted;
                spend (buildNrpn (out, rp.channel, kDrumNrpn[k], (uint8_t) note, (uint8_t) std::clamp (want, 0, 127)));
                sentDrum[(size_t) note][(size_t) k] = want;
            }

    return emitted;
}
} // namespace outrigger
