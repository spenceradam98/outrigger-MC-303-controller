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

void LatencyProbe::process (const float* const* in, int numChannels, int numSamples, EventList& out)
{
    if (state != State::Waiting)
    {
        if (noteOffPending && elapsed >= noteOffAt) { out.add3 (0, (uint8_t) (0x80 | ch), (uint8_t) nt, 0); noteOffPending = false; }
        elapsed += numSamples;
        return;
    }

    if (sendPending)
    {
        // Measure the noise floor in the block before the hit, then fire at offset 0 of the next one.
        float nf = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < numSamples; ++i) nf = std::max (nf, std::fabs (in[c][i]));
        noiseFloor = nf;
        sendPending = false;
        elapsed = -1;               // the hit goes out at the start of the next block
        return;
    }

    if (elapsed < 0)
    {
        out.add3 (0, (uint8_t) (0x90 | ch), (uint8_t) nt, 120);
        elapsed = 0;
        noteOffPending = true;
        noteOffAt = (int64_t) (0.1 * sr);
    }

    const float thr = std::max (threshold, noiseFloor * 4.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        float a = 0.0f;
        for (int c = 0; c < numChannels; ++c) a = std::max (a, std::fabs (in[c][i]));
        if (a > thr)
        {
            result = (int) (elapsed + i);
            state = State::Done;
            break;
        }
    }
    if (noteOffPending && elapsed >= noteOffAt) { out.add3 (0, (uint8_t) (0x80 | ch), (uint8_t) nt, 0); noteOffPending = false; }
    elapsed += numSamples;
    if (state == State::Waiting && elapsed > (int64_t) sr) state = State::TimedOut;
}
} // namespace outrigger
