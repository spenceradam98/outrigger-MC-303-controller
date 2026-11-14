// Return-path analysis: level meters and round-trip latency measurement.
#pragma once

#include "Events.h"

#include <cstdint>

namespace outrigger
{
// Peak (with fall-off) and RMS per channel, fed one block at a time.
class Meter
{
public:
    void prepare (double sampleRate);
    void process (const float* const* ch, int numChannels, int numSamples);
    float peak (int c) const { return peaks[c & 1]; }
    float rms (int c) const { return rmsVals[c & 1]; }
    bool clipped() const { return clip; }
    void resetClip() { clip = false; }

private:
    double sr = 48000.0;
    float peaks[2] {}, rmsVals[2] {}, ms[2] {};
    float peakFall = 0.999f, rmsCoeff = 0.999f;
    bool clip = false;
};

// Sends a drum hit, then watches the return for its onset.
// Result = samples from the hit's scheduled MIDI time to the onset in the return audio.
class LatencyProbe
{
public:
    enum class State { Idle, Waiting, Done, TimedOut };

    void prepare (double sampleRate) { sr = sampleRate; state = State::Idle; }
    void start (int channel, int note) { ch = channel; nt = note; state = State::Waiting; sendPending = true; }

    // Call once per block. Emits the ping note at offset 0, its note-off later, and
    // analyses the return audio (input channels after trim).
    void process (const float* const* in, int numChannels, int numSamples, EventList& out);

    State getState() const { return state; }
    int resultSamples() const { return result; }
    void setThreshold (float linear) { threshold = linear; }

private:
    double sr = 48000.0;
    State state = State::Idle;
    int ch = 9, nt = 37;
    bool sendPending = false, noteOffPending = false;
    int64_t elapsed = 0, noteOffAt = 0;
    int result = 0;
    float threshold = 0.03f;     // about -30 dBFS
    float noiseFloor = 0.0f;
};
} // namespace outrigger
