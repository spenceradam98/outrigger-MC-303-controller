// Parses MIDI coming back from the unit (its knobs, program buttons) into part-parameter
// changes, so the plugin's controls follow what was done on the hardware.
#pragma once

#include "Sync.h"

#include <array>

namespace outrigger
{
struct ParamChange
{
    int part = -1;          // part index
    Field field = Field::Count;
    int value = 0;          // in the field's own units (see kFields)
    int bank = -1;          // for Field::Program: the bank that came with it, or -1
};

class UnitInput
{
public:
    // channelOfPart: 0-based channel of each part (first numParts entries used).
    void setParts (const std::array<int, kNumParts>& channelOfPart, int numParts);

    // Returns true and fills `change` if the message maps to a part parameter.
    bool parse (const uint8_t* data, int size, ParamChange& change);

private:
    int partFor (int ch) const;
    std::array<int, kNumParts> channels {};
    int count = kNormalParts;
    struct ChannelState { int nrpnMsb = -1, nrpnLsb = -1, rpnMsb = -1, rpnLsb = -1, bank = 0; };
    std::array<ChannelState, 16> chState {};
};
} // namespace outrigger
