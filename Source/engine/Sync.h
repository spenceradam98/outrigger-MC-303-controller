// Turns the desired state of the 8 parts (plus global effects and drum-note edits) into
// MIDI messages, sending only what changed and staying inside a MIDI bandwidth budget.
#pragma once

#include "Events.h"
#include "Protocol.h"

#include <array>
#include <climits>

namespace outrigger
{
enum class Field : int
{
    Program,          // bank MSB + program change, sent together
    Volume, Pan, Expression, ReverbSend, ChorusSend,
    Cutoff, Resonance, Attack, Decay, Release, VibRate, VibDepth, VibDelay,
    Portamento, PortaTime, Mono, BendRange, CoarseTune, FineTune,
    Count
};
constexpr int kNumFields = (int) Field::Count;

struct FieldInfo
{
    const char* id;          // parameter id suffix
    const char* name;
    int minValue, maxValue, defaultValue;
    Confidence confidence;
};

// Value meaning: CC fields 0..127; NRPN fields are offsets -50..+50; tune in semitones / steps.
constexpr FieldInfo kFields[kNumFields] = {
    { "prog",     "Program",       0, 127,   0, Confidence::Confirmed },
    { "vol",      "Volume",        0, 127, 100, Confidence::Confirmed },
    { "pan",      "Pan",           0, 127,  64, Confidence::Confirmed },
    { "expr",     "Expression",    0, 127, 127, Confidence::Likely },
    { "rev",      "Reverb",        0, 127,  40, Confidence::Likely },
    { "cho",      "Chorus",        0, 127,   0, Confidence::Likely },
    { "cutoff",   "Cutoff",      -50,  50,   0, Confidence::Confirmed },
    { "reso",     "Resonance",   -50,  50,   0, Confidence::Confirmed },
    { "atk",      "Attack",      -50,  50,   0, Confidence::Confirmed },
    { "dec",      "Decay",       -50,  50,   0, Confidence::Confirmed },
    { "rel",      "Release",     -50,  50,   0, Confidence::Confirmed },
    { "vrate",    "Vib Rate",    -50,  50,   0, Confidence::Confirmed },
    { "vdepth",   "Vib Depth",   -50,  50,   0, Confidence::Confirmed },
    { "vdelay",   "Vib Delay",   -50,  50,   0, Confidence::Confirmed },
    { "porta",    "Portamento",    0,   1,   0, Confidence::Confirmed },
    { "ptime",    "Porta Time",    0, 127,   0, Confidence::Confirmed },
    { "mono",     "Mono",          0,   1,   0, Confidence::Confirmed },
    { "bend",     "Bend Range",    0,  24,   2, Confidence::Confirmed },
    { "coarse",   "Coarse Tune", -24,  24,   0, Confidence::Confirmed },
    { "fine",     "Fine Tune",   -64,  63,   0, Confidence::Confirmed },
};

struct PartState
{
    bool enabled = true;
    int channel = 0;                 // 0-based
    int bank = 0;                    // CC0 tone bank (ignored on the rhythm part)
    std::array<int, kNumFields> v {};
    PartState() { for (int i = 0; i < kNumFields; ++i) v[(size_t) i] = kFields[i].defaultValue; }
};

constexpr int kDrumParams = 5;   // pitch, level, pan, reverb, chorus
constexpr const char* kDrumParamNames[kDrumParams] = { "Pitch", "Level", "Pan", "Reverb", "Chorus" };
constexpr uint8_t kDrumNrpn[kDrumParams] = { nrpn::DrumPitch, nrpn::DrumLevel, nrpn::DrumPan, nrpn::DrumReverb, nrpn::DrumChorus };

struct GlobalState
{
    int reverbType = 4;              // Hall 2
    int chorusType = 2;              // Chorus 3
    int reverbLevel = 64, chorusLevel = 64;
    bool experimentalSysex = false;
    uint8_t deviceId = sysex::DefaultDeviceId;
    // Drum note edits for the rhythm part: -1 = untouched (never sent), else 0..127.
    std::array<std::array<int, kDrumParams>, 128> drum {};
    GlobalState() { for (auto& n : drum) n.fill (-1); }
};

class Sync
{
public:
    // bytesPerSecond: share of the 3125 B/s DIN link we allow for controller data.
    void prepare (double sampleRate, int bytesPerSecond = 2000, int burstBytes = 384);

    // Forget what the unit has: everything is resent (Send All, new device, state load).
    void invalidateAll();
    void invalidatePart (int part);

    // Desired state in, messages out at offset 0. Notes are not budgeted (router handles them).
    // Returns the number of bytes emitted.
    int process (const std::array<PartState, kNumParts>& parts, const GlobalState& g, int numSamples, EventList& out,
                 int numActiveParts = kNormalParts);

    bool pending() const { return lastWasPartial; }
    double budget() const { return tokens; }

    // Message builders (public for tests).
    static int buildNrpn (EventList& out, int ch, uint8_t msb, uint8_t lsb, uint8_t value);
    static int buildRpn  (EventList& out, int ch, uint8_t lsb, uint8_t value);
    static int buildSysex (EventList& out, uint8_t deviceId, const uint8_t address[4], uint8_t data);

private:
    static constexpr int kUnknown = INT_MIN;
    int costOf (Field f) const;
    int emitField (EventList& out, int ch, Field f, int value, int bank, bool rhythm);

    double sr = 48000.0, rate = 2000.0, burst = 384.0, tokens = 384.0;
    std::array<std::array<int, kNumFields>, kNumParts> sent {};
    std::array<int, kNumParts> sentChannel {}, sentBank {};
    int sentReverbType = kUnknown, sentChorusType = kUnknown, sentReverbLevel = kUnknown, sentChorusLevel = kUnknown;
    std::array<std::array<int, kDrumParams>, 128> sentDrum {};
    bool lastWasPartial = false;
};
} // namespace outrigger
