#include "UnitInput.h"

#include <algorithm>

namespace outrigger
{
void UnitInput::setParts (const std::array<int, kNumParts>& channelOfPart, int numParts)
{
    channels = channelOfPart;
    count = numParts;
}

int UnitInput::partFor (int ch) const
{
    for (int p = 0; p < count; ++p)
        if (channels[(size_t) p] == ch) return p;
    return -1;
}

bool UnitInput::parse (const uint8_t* data, int size, ParamChange& c)
{
    if (size < 2 || data[0] < 0x80 || data[0] >= 0xF0) return false;
    const int type = data[0] & 0xF0, ch = data[0] & 15;
    auto& st = chState[(size_t) ch];
    const int part = partFor (ch);
    c = {};
    c.part = part;

    if (type == 0xC0)
    {
        if (part < 0) return false;
        c.field = Field::Program;
        c.value = data[1] & 127;
        c.bank = part == kRhythmPart ? -1 : st.bank;
        return true;
    }
    if (type != 0xB0 || size < 3) return false;

    const int num = data[1] & 127, val = data[2] & 127;
    auto set = [&] (Field f, int v) { c.field = f; c.value = v; return part >= 0; };

    switch (num)
    {
        case cc::BankMSB:    st.bank = val; return false;
        case cc::NrpnMSB:    st.nrpnMsb = val; st.rpnMsb = st.rpnLsb = -1; return false;
        case cc::NrpnLSB:    st.nrpnLsb = val; st.rpnMsb = st.rpnLsb = -1; return false;
        case cc::RpnMSB:     st.rpnMsb = val; st.nrpnMsb = st.nrpnLsb = -1; return false;
        case cc::RpnLSB:     st.rpnLsb = val; st.nrpnMsb = st.nrpnLsb = -1; return false;
        case cc::Volume:     return set (Field::Volume, val);
        case cc::Pan:        return set (Field::Pan, val);
        case cc::Expression: return set (Field::Expression, val);
        case cc::ReverbSend: return set (Field::ReverbSend, val);
        case cc::ChorusSend: return set (Field::ChorusSend, val);
        case cc::PortaTime:  return set (Field::PortaTime, val);
        case cc::Portamento: return set (Field::Portamento, val >= 64 ? 1 : 0);
        case cc::MonoOn:     return set (Field::Mono, 1);
        case cc::PolyOn:     return set (Field::Mono, 0);
        case cc::DataEntry:
        {
            if (st.nrpnMsb == nrpn::ToneMSB)
            {
                const int off = std::clamp (val - 64, -nrpn::OffsetRange, nrpn::OffsetRange);
                switch (st.nrpnLsb)
                {
                    case nrpn::Cutoff:    return set (Field::Cutoff, off);
                    case nrpn::Resonance: return set (Field::Resonance, off);
                    case nrpn::Attack:    return set (Field::Attack, off);
                    case nrpn::Decay:     return set (Field::Decay, off);
                    case nrpn::Release:   return set (Field::Release, off);
                    case nrpn::VibRate:   return set (Field::VibRate, off);
                    case nrpn::VibDepth:  return set (Field::VibDepth, off);
                    case nrpn::VibDelay:  return set (Field::VibDelay, off);
                    default: return false;
                }
            }
            if (st.rpnMsb == 0)
            {
                switch (st.rpnLsb)
                {
                    case rpn::BendRange:  return set (Field::BendRange, std::min (val, 24));
                    case rpn::CoarseTune: return set (Field::CoarseTune, std::clamp (val - 64, -24, 24));
                    case rpn::FineTune:   return set (Field::FineTune, val - 64);
                    default: return false;
                }
            }
            return false;
        }
        default: return false;
    }
}
} // namespace outrigger
