/*
  ==============================================================================

    Biquad sections and the coefficient designs the EQ uses: peaks, shelves,
    Butterworth cuts, and the band-passes that bands listen through for
    dynamics and solo. Plain C++ (no JUCE).

    Peaks, shelves, high cuts and the listen filters are matched to their
    analog prototypes (after Vicanek, "Matched Second Order Digital Filters"):
    the poles are the analog poles mapped exactly (z = e^sT), and the zeros
    are fitted so the magnitude equals the analog one at DC, at Nyquist and at
    the band's frequency. Unlike the bilinear transform, this doesn't squeeze
    the response towards Nyquist, so a 15 kHz bell keeps its shape at 44.1 kHz.
    Low cuts keep the bilinear design, which is already accurate where they
    are used.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "eq_params.h"

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

namespace detail
{
    // Sets a section from analog poles s^2 + 2 zeta wp s + wp^2 (wp in radians
    // per sample) and the analog |H|^2 at DC, at Nyquist and at wm.
    inline void designMatched (Biquad& f, double zeta, double wp, double dcSquared, double nyquistSquared, double wm, double atWmSquared)
    {
        const double r = std::exp (-zeta * wp);
        f.a2 = r * r;
        f.a1 = zeta <= 1.0 ? -2.0 * r * std::cos (wp * std::sqrt (1.0 - zeta * zeta))
                           : -2.0 * r * std::cosh (wp * std::sqrt (zeta * zeta - 1.0));

        // |H|^2 = (B0 p0 + B1 p1 + B2 p2) / (A0 p0 + A1 p1 + A2 p2), with
        // p1 = sin^2 (w / 2), p0 = 1 - p1, p2 = 4 p0 p1.
        const double A0 = (1.0 + f.a1 + f.a2) * (1.0 + f.a1 + f.a2);
        const double A1 = (1.0 - f.a1 + f.a2) * (1.0 - f.a1 + f.a2);
        const double A2 = -4.0 * f.a2;
        const double p1 = std::pow (std::sin (wm / 2.0), 2.0), p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
        const double B0 = A0 * dcSquared, B1 = A1 * nyquistSquared;
        const double B2 = (atWmSquared * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;

        // Back to b0..b2, taking the root that keeps the zeros inside the unit circle.
        const double sum = std::sqrt (B0), alternating = std::sqrt (B1);
        const double w = (sum + alternating) / 2.0;
        f.b0 = (w + std::sqrt (std::max (0.0, w * w + B2))) / 2.0;
        f.b1 = (sum - alternating) / 2.0;
        f.b2 = f.b0 != 0.0 ? -B2 / (4.0 * f.b0) : 0.0;
    }

    // 1 / H: swaps poles and zeros. The section's zeros must be inside the unit circle.
    inline void invert (Biquad& f)
    {
        const double b0 = f.b0, b1 = f.b1, b2 = f.b2;
        f.b0 = 1.0 / b0;
        f.b1 = f.a1 / b0;
        f.b2 = f.a2 / b0;
        f.a1 = b1 / b0;
        f.a2 = b2 / b0;
    }

    inline void makeWire (Biquad& f)
    {
        f.b0 = 1.0;
        f.b1 = f.b2 = f.a1 = f.a2 = 0.0;
    }

    // The band's frequency in radians per sample, kept just under Nyquist.
    inline double radians (double freq, double sampleRate)    { return std::min (2.0 * pi * freq / sampleRate, 0.98 * pi); }

    // Analog |H|^2 at x = w / w0.
    inline double peakSquared (double x, double q, double A)
    {
        const double real = 1.0 - x * x;
        return (real * real + std::pow (x * A / q, 2.0)) / (real * real + std::pow (x / (A * q), 2.0));
    }

    inline double shelfSquared (bool low, double x, double q, double A)
    {
        const double r = x * x, damping = A / (q * q) * r;
        const double lowSide = (A - r) * (A - r) + damping, highSide = (1.0 - A * r) * (1.0 - A * r) + damping;
        return A * A * (low ? lowSide / highSide : highSide / lowSide);
    }

    inline double lowPassSquared (double x, double q)
    {
        const double real = 1.0 - x * x;
        return 1.0 / (real * real + std::pow (x / q, 2.0));
    }

    inline double bandPassSquared (double x, double q)    { return std::pow (x / q, 2.0) * lowPassSquared (x, q); }

    // A matched second-order low-pass at w0 with Q q.
    inline void designLowPass (Biquad& f, double w0, double q)
    {
        detail::designMatched (f, 1.0 / (2.0 * q), w0, 1.0, lowPassSquared (pi / w0, q), w0, q * q);
    }
}

// What a band works on, for its detector and for solo: a band-pass (0 dB peak
// gain) at its frequency and Q; for a shelf or cut, a Butterworth low- or
// high-pass covering what the shelf moves or the cut removes.
inline void designListenFilter (Biquad& f, FilterType type, double freq, double q, double sampleRate)
{
    const double w0 = detail::radians (freq, sampleRate);

    if (type == FilterType::lowShelf || type == FilterType::lowCut)
    {
        detail::designLowPass (f, w0, 1.0 / std::sqrt (2.0));
    }
    else if (isShelfType (type) || isCutType (type))
    {
        // The bilinear high-pass, as the low cut uses.
        const double cosw0 = std::cos (w0), alpha = std::sin (w0) / std::sqrt (2.0), s0 = 1.0 + alpha;
        f.b0 = (1.0 + cosw0) / 2.0 / s0;
        f.b1 = -(1.0 + cosw0) / s0;
        f.b2 = f.b0;
        f.a1 = (-2.0 * cosw0) / s0;
        f.a2 = (1.0 - alpha) / s0;
    }
    else
    {
        detail::designMatched (f, 1.0 / (2.0 * q), w0, 0.0, detail::bandPassSquared (pi / w0, q), w0, 1.0);
    }
}

// A peaking EQ: the RBJ analog bell, matched. A cut is the boost inverted, as
// the analog cut is exactly 1 / the boost; that keeps its notch where it belongs.
inline void designPeak (Biquad& f, double freq, double q, double gainDb, double sampleRate)
{
    if (gainDb == 0.0)
        return detail::makeWire (f);

    const double w0 = detail::radians (freq, sampleRate);
    const double A  = std::pow (10.0, std::abs (gainDb) / 40.0);
    detail::designMatched (f, 1.0 / (2.0 * A * q), w0, 1.0, detail::peakSquared (pi / w0, q, A), w0, A * A * A * A);

    if (gainDb < 0.0)
        detail::invert (f);
}

// Shelves: the RBJ analog shelf, matched. The band's frequency is the
// half-gain point. Each is designed on the side whose poles sit lowest (a low
// shelf's boost, a high shelf's cut), where they can't pass Nyquist, and
// inverted for the other side.
inline void designShelf (Biquad& f, bool low, double freq, double q, double gainDb, double sampleRate)
{
    if (gainDb == 0.0)
        return detail::makeWire (f);

    const double w0 = detail::radians (freq, sampleRate);
    q = std::min (q, (double) maxShelfQ);
    const double designedDb = low ? std::abs (gainDb) : -std::abs (gainDb);
    const double A = std::pow (10.0, designedDb / 40.0);
    const double wp = low ? w0 / std::sqrt (A) : w0 * std::sqrt (A);
    detail::designMatched (f, 1.0 / (2.0 * q), wp, detail::shelfSquared (low, 0.0, q, A), detail::shelfSquared (low, pi / w0, q, A), w0, A * A);

    if (designedDb * gainDb < 0.0)
        detail::invert (f);
}

// Butterworth cut of order n: n/2 second-order sections with the Butterworth
// Qs, plus one first-order section when n is odd. A high cut (low-pass) is
// matched; a low cut (high-pass) uses the RBJ bilinear sections. Returns how
// many sections it used.
inline int designCut (std::array<Biquad, maxSections>& sections, bool highPass, int n, double freq, double sampleRate)
{
    const double w0    = highPass ? 2.0 * pi * freq / sampleRate : detail::radians (freq, sampleRate);
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    int k = 0;

    for (int i = 0; i < n / 2; ++i)
    {
        const double theta = n % 2 == 0 ? (2.0 * i + 1.0) * pi / (2.0 * n) : (i + 1.0) * pi / n;
        const double sq = 1.0 / (2.0 * std::cos (theta));
        auto& f = sections[(size_t) k++];

        if (! highPass)
        {
            detail::designLowPass (f, w0, sq);
            continue;
        }

        const double alpha = sinw0 / (2.0 * sq);
        const double a0 = 1.0 + alpha;
        f.b0 = (1.0 + cosw0) / 2.0 / a0;
        f.b1 = -(1.0 + cosw0) / a0;
        f.b2 = f.b0;
        f.a1 = (-2.0 * cosw0) / a0;
        f.a2 = (1.0 - alpha) / a0;
    }

    if (n % 2 == 1)
    {
        auto& f = sections[(size_t) k++];
        f.b2 = 0.0;
        f.a2 = 0.0;

        if (highPass)
        {
            // First order, bilinear transform with prewarping.
            const double K = std::tan (pi * freq / sampleRate);
            f.b0 = 1.0 / (1.0 + K);
            f.b1 = -f.b0;
            f.a1 = (K - 1.0) / (K + 1.0);
        }
        else
        {
            // Matched: the analog pole, and the analog gain at DC and Nyquist.
            f.a1 = -std::exp (-w0);
            const double x = pi / w0, nyquist = 1.0 / std::sqrt (1.0 + x * x);
            f.b0 = ((1.0 + f.a1) + nyquist * (1.0 - f.a1)) / 2.0;
            f.b1 = ((1.0 + f.a1) - nyquist * (1.0 - f.a1)) / 2.0;
        }
    }

    return k;
}

} // namespace threedq::dsp
