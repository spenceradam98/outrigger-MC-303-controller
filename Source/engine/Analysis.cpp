#include "Analysis.h"

#include <algorithm>
#include <cmath>

namespace outrigger
{
void Meter::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    peakFall = (float) std::exp (-1.0 / (0.6 * sr));   // ~20 dB/s fall-off
    rmsCoeff = (float) std::exp (-1.0 / (0.3 * sr));   // 300 ms RMS window
    peaks[0] = peaks[1] = rmsVals[0] = rmsVals[1] = ms[0] = ms[1] = 0.0f;
    clip = false;
}

void Meter::process (const float* const* ch, int numChannels, int numSamples)
{
    for (int c = 0; c < 2; ++c)
    {
        const float* x = ch != nullptr && numChannels > 0 ? ch[std::min (c, numChannels - 1)] : nullptr;
        float p = peaks[c], m = ms[c];
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = x != nullptr ? x[i] : 0.0f;
            const float a = std::fabs (v);
            p = std::max (a, p * peakFall);
            m = rmsCoeff * m + (1.0f - rmsCoeff) * v * v;
            if (a >= 0.999f) clip = true;
        }
        peaks[c] = p;
        ms[c] = m;
        rmsVals[c] = std::sqrt (m);
    }
}

void LatencyProbe::start (int channel, int note, int pings)
{
    ch = channel & 15;
    nt = note & 127;
    pingsWanted = std::clamp (pings, 1, kMaxPings);
    pingsDone = numResults = 0;
    state = State::Waiting;
    phase = Phase::Quiet;
    noiseFloor = 0.0f;
    elapsed = 0;
    phaseEnd = (int64_t) (0.05 * sr);      // 50 ms of noise-floor measurement first
}

void LatencyProbe::finish()
{
    if (numResults == 0) { state = State::TimedOut; return; }
    std::sort (results, results + numResults);
    result = results[numResults / 2];
    spread = results[numResults - 1] - results[0];
    state = State::Done;
}

void LatencyProbe::process (const float* const* in, int numChannels, int numSamples, EventList& out)
{
    // Note-off for the last ping, wherever we are.
    if (noteOffAt >= 0 && elapsed >= noteOffAt) { out.add3 (0, (uint8_t) (0x80 | ch), (uint8_t) nt, 0); noteOffAt = -1; }

    if (state != State::Waiting) { elapsed += numSamples; return; }

    auto blockPeak = [&] (int from) {
        float a = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            for (int i = from; i < numSamples; ++i) a = std::max (a, std::fabs (in[c][i]));
        return a;
    };

    switch (phase)
    {
        case Phase::Quiet:
            noiseFloor = std::max (noiseFloor, blockPeak (0));
            if (elapsed + numSamples >= phaseEnd) phase = Phase::Armed;   // fire at the start of the next block
            break;

        case Phase::Armed:
        {
            out.add3 (0, (uint8_t) (0x90 | ch), (uint8_t) nt, 120);
            noteOffAt = elapsed + (int64_t) (0.1 * sr);
            phase = Phase::Listening;
            phaseEnd = elapsed + (int64_t) sr;     // 1 s timeout per ping
            const int64_t sentAt = elapsed;
            // Listen in this same block too.
            const float thr = std::max (threshold, noiseFloor * 4.0f);
            for (int i = 0; i < numSamples; ++i)
            {
                float a = 0.0f;
                for (int c = 0; c < numChannels; ++c) a = std::max (a, std::fabs (in[c][i]));
                if (a > thr) { results[numResults++] = (int) (elapsed + i - sentAt); phase = Phase::Gap; break; }
            }
            if (phase == Phase::Gap) { ++pingsDone; phaseEnd = elapsed + (int64_t) (0.4 * sr); }
            pingSentAt = sentAt;
            break;
        }

        case Phase::Listening:
        {
            const float thr = std::max (threshold, noiseFloor * 4.0f);
            for (int i = 0; i < numSamples; ++i)
            {
                float a = 0.0f;
                for (int c = 0; c < numChannels; ++c) a = std::max (a, std::fabs (in[c][i]));
                if (a > thr) { results[numResults++] = (int) (elapsed + i - pingSentAt); phase = Phase::Gap; break; }
            }
            if (phase == Phase::Gap) { ++pingsDone; phaseEnd = elapsed + (int64_t) (0.4 * sr); }
            else if (elapsed + numSamples >= phaseEnd) { ++pingsDone; phase = Phase::Gap; phaseEnd = elapsed + (int64_t) (0.1 * sr); }
            break;
        }

        case Phase::Gap:
        {
            // Let the hit ring out until the return is quiet again, then go again (or finish).
            const float thr = std::max (threshold, noiseFloor * 4.0f);
            if (blockPeak (0) > thr * 0.5f && elapsed < phaseEnd + (int64_t) (3.0 * sr))
                break;
            if (elapsed + numSamples >= phaseEnd)
            {
                if (pingsDone >= pingsWanted) finish();
                else phase = Phase::Armed;
            }
            break;
        }
    }
    elapsed += numSamples;
}
} // namespace outrigger
