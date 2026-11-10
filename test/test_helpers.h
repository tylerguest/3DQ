/*
  ==============================================================================

    Shared test helpers: test signals, levels and filter responses.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>
#include "../source/dsp/filter_design.h"

namespace threedq::test
{

constexpr double pi = 3.14159265358979323846;

// One channel of a sine wave.
inline std::vector<float> sine (double freq, double sampleRate, int numSamples, float amplitude = 0.25f)
{
    std::vector<float> out ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
        out[(size_t) i] = amplitude * (float) std::sin (2.0 * pi * freq * i / sampleRate);
    return out;
}

// Repeatable white noise in -amplitude..amplitude.
inline std::vector<float> noise (int numSamples, float amplitude = 0.25f, juce::int64 seed = 1)
{
    juce::Random random (seed);
    std::vector<float> out ((size_t) numSamples);
    for (auto& x : out)
        x = amplitude * (random.nextFloat() * 2.0f - 1.0f);
    return out;
}

// RMS of samples [start, end).
inline double rms (const std::vector<float>& x, size_t start = 0, size_t end = 0)
{
    end = end == 0 ? x.size() : end;
    double sum = 0;
    for (size_t i = start; i < end; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) std::max<size_t> (1, end - start));
}

inline double toDb (double gain)    { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

// Level change in dB from `in` to `out`, over the second half (once filters settle).
inline double gainDb (const std::vector<float>& in, const std::vector<float>& out)
{
    const auto half = in.size() / 2;
    return toDb (rms (out, half) / std::max (rms (in, half), 1.0e-12));
}

inline double maxDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        worst = std::max (worst, (double) std::abs (a[i] - b[i]));
    return worst;
}

// |H| of a biquad at freq, in dB.
inline double responseDb (const dsp::Biquad& f, double freq, double sampleRate)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * pi * freq / sampleRate);
    const auto z2 = z1 * z1;
    const auto h = (f.b0 + f.b1 * z1 + f.b2 * z2) / (1.0 + f.a1 * z1 + f.a2 * z2);
    return toDb (std::abs (h));
}

// A biquad is stable when both poles are inside the unit circle.
inline bool isStable (const dsp::Biquad& f)
{
    return std::abs (f.a2) < 1.0 && std::abs (f.a1) < 1.0 + f.a2;
}

} // namespace threedq::test
