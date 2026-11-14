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

// Sends a few drum hits, watching the return for each onset.
// Result = median samples from a hit's scheduled MIDI time to its onset in the return audio.
class LatencyProbe
{
public:
    enum class State { Idle, Waiting, Done, TimedOut };
    static constexpr int kMaxPings = 8;

    void prepare (double sampleRate) { sr = sampleRate; state = State::Idle; }
    void start (int channel, int note, int pings = 3);

    // Call once per block. Emits the ping notes and analyses the return audio.
    void process (const float* const* in, int numChannels, int numSamples, EventList& out);

    State getState() const { return state; }
    int resultSamples() const { return result; }
    int successfulPings() const { return numResults; }
    int spreadSamples() const { return spread; }   // max - min of the successful pings
    void setThreshold (float linear) { threshold = linear; }

private:
    enum class Phase { Quiet, Armed, Listening, Gap };
    void finish();

    double sr = 48000.0;
    State state = State::Idle;
    Phase phase = Phase::Quiet;
    int ch = 9, nt = 37, pingsWanted = 3, pingsDone = 0;
    int64_t elapsed = 0, noteOffAt = -1, phaseEnd = 0, pingSentAt = 0;
    int results[kMaxPings] {};
    int numResults = 0, result = 0, spread = 0;
    float threshold = 0.03f;     // about -30 dBFS
    float noiseFloor = 0.0f;
};
} // namespace outrigger
