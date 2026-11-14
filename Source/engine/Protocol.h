// Outrigger engine: plain C++ (no JUCE). MIDI protocol of the target groovebox.
//
// Every message the plugin can send is listed here with how sure we are that the
// unit responds to it. Sources and the full table are in docs/DESIGN.md.
//   Confirmed  = in the manufacturer's MC-303 Supplemental Notes (1997) or owner's manual.
//   Likely     = standard GS behaviour the unit is built on, not printed in the sources we read.
//   Experimental = GS SysEx address not confirmed for this unit; off unless the user enables it.
#pragma once

#include <cstdint>

namespace outrigger
{
enum class Confidence { Confirmed, Likely, Experimental };

namespace cc
{
    constexpr uint8_t BankMSB = 0, Modulation = 1, PortaTime = 5, DataEntry = 6, Volume = 7, Pan = 10,
                      Expression = 11, BankLSB = 32, Hold = 64, Portamento = 65, ReverbSend = 91,
                      ChorusSend = 93, NrpnLSB = 98, NrpnMSB = 99, RpnLSB = 100, RpnMSB = 101,
                      AllSoundOff = 120, ResetAll = 121, AllNotesOff = 123, MonoOn = 126, PolyOn = 127;
}

// NRPN (CC99 = 01, CC98 = below, CC6 = value 14..114, 64 = no change). Confirmed.
namespace nrpn
{
    constexpr uint8_t ToneMSB = 0x01;
    constexpr uint8_t VibRate = 0x08, VibDepth = 0x09, VibDelay = 0x0A, Cutoff = 0x20, Resonance = 0x21,
                      Attack = 0x63, Decay = 0x64, Release = 0x66;
    // Rhythm part, per drum note (CC99 = below, CC98 = note number). Confirmed.
    constexpr uint8_t DrumPitch = 0x18, DrumLevel = 0x1A, DrumPan = 0x1C, DrumReverb = 0x1D, DrumChorus = 0x1E;
    constexpr int OffsetRange = 50;   // value sent = 64 + offset, offset in -50..+50
}

// RPN (CC101 = 00, CC100 = below, CC6 = value). Confirmed.
namespace rpn
{
    constexpr uint8_t BendRange = 0x00, FineTune = 0x01, CoarseTune = 0x02;
}

// SysEx: F0 41 <dev> 00 03 12 <a1 a2 a3 a4> <data> <sum> F7. Header and checksum confirmed.
namespace sysex
{
    constexpr uint8_t Roland = 0x41, ModelHi = 0x00, ModelLo = 0x03, DataSet = 0x12;
    constexpr uint8_t DefaultDeviceId = 0x10;
    // Confirmed: effect macro selection.
    constexpr uint8_t ReverbMacro[4] = { 0x00, 0x40, 0x01, 0x30 };   // 0..7 Room1-3, Hall1-2, Plate, Delay, Pan Delay
    constexpr uint8_t ChorusMacro[4] = { 0x00, 0x40, 0x01, 0x38 };   // 0..7 Chorus1-4, FB Chorus, Flanger, Short Delay, Short Delay FB
    // Experimental (GS addresses, not confirmed for this unit).
    constexpr uint8_t ReverbLevel[4] = { 0x00, 0x40, 0x01, 0x33 };
    constexpr uint8_t ChorusLevel[4] = { 0x00, 0x40, 0x01, 0x3A };
}

constexpr int kNumParts = 8;          // index 0 = rhythm part R, 1..7 = parts 1..7
constexpr int kRhythmPart = 0;
constexpr int defaultChannelForPart (int part) { return part == kRhythmPart ? 9 : part - 1; } // 0-based channel

constexpr const char* kReverbTypes[8] = { "Room 1", "Room 2", "Room 3", "Hall 1", "Hall 2", "Plate", "Delay", "Pan Delay" };
constexpr const char* kChorusTypes[8] = { "Chorus 1", "Chorus 2", "Chorus 3", "Chorus 4", "FB Chorus", "Flanger", "Short Delay", "Short Delay FB" };

constexpr uint8_t rolandChecksum (const uint8_t* bytes, int n)
{
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += bytes[i];
    return (uint8_t) ((128 - (sum % 128)) % 128);
}
} // namespace outrigger
