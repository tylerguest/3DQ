/*
  ==============================================================================

    Biquad sections and the coefficient designs the EQ uses: RBJ cookbook
    peaks and shelves, Butterworth cuts, and the band-passes that bands listen
    through for dynamics and solo. Plain C++ (no JUCE).

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "EqParams.h"

namespace threedq::dsp
{

constexpr double pi = 3.14159265358979323846;
constexpr int maxSections = 4; // biquads per band: order 8 = 4 sections

// One transposed direct form II section. z1 / z2 are a band's own state;
// zr1 / zr2 the right channel's when it filters L and R alike.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0, zr1 = 0, zr2 = 0;

    void clear()    { z1 = z2 = zr1 = zr2 = 0.0; }
};

inline double runSection (const Biquad& f, double& z1, double& z2, double in)
{
    const double out = f.b0 * in + z1;
    z1 = f.b1 * in - f.a1 * out + z2;
    z2 = f.b2 * in - f.a2 * out;
    return out;
}

// What a band works on, for its detector and for solo: a band-pass (RBJ, 0 dB
// peak gain) at its frequency and Q; for a shelf or cut, a Butterworth low- or
// high-pass covering what the shelf moves or the cut removes.
inline void designListenFilter (Biquad& f, FilterType type, double freq, double q, double sampleRate)
{
    const double w0    = 2.0 * pi * freq / sampleRate;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);

    if (isShelfType (type) || isCutType (type))
    {
        const double alpha = sinw0 / std::sqrt (2.0);
        const double s0 = 1.0 + alpha;
        const double sign = type == FilterType::lowShelf || type == FilterType::lowCut ? 1.0 : -1.0;
        f.b0 = (1.0 - sign * cosw0) / 2.0 / s0;
        f.b1 = sign * (1.0 - sign * cosw0) / s0;
        f.b2 = f.b0;
        f.a1 = (-2.0 * cosw0) / s0;
        f.a2 = (1.0 - alpha) / s0;
    }
    else
    {
        const double alpha = sinw0 / (2.0 * q);
        const double s0 = 1.0 + alpha;
        f.b0 = alpha / s0;
        f.b1 = 0.0;
        f.b2 = -alpha / s0;
        f.a1 = (-2.0 * cosw0) / s0;
        f.a2 = (1.0 - alpha) / s0;
    }
}

// RBJ cookbook peaking EQ (same as Web Audio's "peaking" BiquadFilterNode).
inline void designPeak (Biquad& f, double freq, double q, double gainDb, double sampleRate)
{
    const double w0    = 2.0 * pi * freq / sampleRate;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double alpha = sinw0 / (2.0 * q);
    const double a0    = 1.0 + alpha / A;
    f.b0 = (1.0 + alpha * A) / a0;
    f.b1 = (-2.0 * cosw0) / a0;
    f.b2 = (1.0 - alpha * A) / a0;
    f.a1 = (-2.0 * cosw0) / a0;
    f.a2 = (1.0 - alpha / A) / a0;
}

// RBJ cookbook shelves; the high shelf is the low shelf with cos(w0) negated
// and b1 / a1 flipped. The band's frequency is the half-gain point.
inline void designShelf (Biquad& f, bool low, double freq, double q, double gainDb, double sampleRate)
{
    const double w0    = 2.0 * pi * freq / sampleRate;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double alpha = sinw0 / (2.0 * std::min (q, (double) maxShelfQ));
    const double root  = 2.0 * std::sqrt (A) * alpha;
    const double sign  = low ? 1.0 : -1.0;
    const double cs    = sign * cosw0;
    const double a0    = (A + 1.0) + (A - 1.0) * cs + root;
    f.b0 = A * ((A + 1.0) - (A - 1.0) * cs + root) / a0;
    f.b1 = sign * 2.0 * A * ((A - 1.0) - (A + 1.0) * cs) / a0;
    f.b2 = A * ((A + 1.0) - (A - 1.0) * cs - root) / a0;
    f.a1 = -sign * 2.0 * ((A - 1.0) + (A + 1.0) * cs) / a0;
    f.a2 = ((A + 1.0) + (A - 1.0) * cs - root) / a0;
}

// Butterworth cut of order n: n/2 RBJ second-order sections with the
// Butterworth Qs, plus one first-order section when n is odd. Returns how many
// sections it used.
inline int designCut (std::array<Biquad, maxSections>& sections, bool highPass, int n, double freq, double sampleRate)
{
    const double w0    = 2.0 * pi * freq / sampleRate;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    int k = 0;

    for (int i = 0; i < n / 2; ++i)
    {
        const double theta = n % 2 == 0 ? (2.0 * i + 1.0) * pi / (2.0 * n) : (i + 1.0) * pi / n;
        const double sq = 1.0 / (2.0 * std::cos (theta));
        const double alpha = sinw0 / (2.0 * sq);
        const double a0 = 1.0 + alpha;
        auto& f = sections[(size_t) k++];

        if (highPass)
        {
            f.b0 = (1.0 + cosw0) / 2.0 / a0;
            f.b1 = -(1.0 + cosw0) / a0;
            f.b2 = f.b0;
        }
        else
        {
            f.b0 = (1.0 - cosw0) / 2.0 / a0;
            f.b1 = (1.0 - cosw0) / a0;
            f.b2 = f.b0;
        }

        f.a1 = (-2.0 * cosw0) / a0;
        f.a2 = (1.0 - alpha) / a0;
    }

    if (n % 2 == 1)
    {
        // First order, bilinear transform with prewarping.
        const double K = std::tan (pi * freq / sampleRate);
        auto& f = sections[(size_t) k++];
        f.b0 = highPass ? 1.0 / (1.0 + K) : K / (1.0 + K);
        f.b1 = highPass ? -f.b0 : f.b0;
        f.b2 = 0.0;
        f.a1 = (K - 1.0) / (K + 1.0);
        f.a2 = 0.0;
    }

    return k;
}

} // namespace threedq::dsp
