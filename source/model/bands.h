/*
  ==============================================================================

    The sphere's band model and the surface shape the EQ gives it, with no
    drawing or GUI code in it.

      latitude  -> frequency (log scale, 20 Hz at the bottom pole, 20 kHz at the top)
      longitude -> stereo position (0 = front = Mid, +90 deg = R, -90 deg = L, back = Side)
      gain      -> bump out of (boost) or dent into (cut) the surface
      q         -> how wide the bump is (a bell's bump is its real response)

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "../dsp/eq_params.h"

namespace threedq
{

constexpr float pi     = 3.14159265358979323846f;
constexpr float halfPi = pi / 2.0f;
constexpr float twoPi  = pi * 2.0f;

constexpr int   defaultCutSlope = 24;
constexpr float defaultThresholdDb = -30.0f;

constexpr float maxBoostDisplacement = 0.55f;
constexpr float maxCutDisplacement   = 0.82f;
constexpr float stackRoom            = 0.1f;

struct Vec3
{
    float x = 0, y = 0, z = 0;
};

struct Band
{
    int id = 0;          // parameter slot in the plugin
    float lat = 0, lon = 0; // radians
    float gainDb = 0, q = 1;
    bool muted = false;
    float range = 0;     // dynamic range; 0 = static band
    float threshold = defaultThresholdDb;
    FilterType type = FilterType::peak;
    int slope = defaultCutSlope;
    bool stereo = false; // filters L and R alike: a full belt round the sphere
    bool externalSidechain = false; // dynamics follow the sidechain input
    float attackMs = defaultAttackMs, releaseMs = defaultReleaseMs, ratio = defaultRatio;

    // The gain the band has right now, including dynamic movement.
    bool hasLiveGain = false;
    float liveGainDb = 0;
    float peakMovementDb = 0; // the largest recent move from the static gain, held for reading

    bool isCut() const           { return isCutType (type); }
    bool isShelf() const         { return isShelfType (type); }
    float displayGain() const    { return hasLiveGain ? liveGainDb : gainDb; }
};

// A fixed-capacity list, so per-frame copies never allocate.
struct BandSet
{
    std::array<Band, maxBands> items;
    int count = 0;

    Band* begin()               { return items.data(); }
    Band* end()                 { return items.data() + count; }
    const Band* begin() const   { return items.data(); }
    const Band* end() const     { return items.data() + count; }

    Band* find (int id)
    {
        for (auto& b : *this)
            if (b.id == id)
                return &b;
        return nullptr;
    }

    const Band* find (int id) const
    {
        for (const auto& b : *this)
            if (b.id == id)
                return &b;
        return nullptr;
    }

    void add (const Band& b)
    {
        if (count < maxBands)
            items[(size_t) count++] = b;
    }

    void remove (int id)
    {
        const auto last = std::remove_if (begin(), end(), [id] (const Band& b) { return b.id == id; });
        count = (int) (last - begin());
    }

    // Band ids are plugin parameter slots: the lowest free one, or -1.
    int freeSlot()
    {
        for (int id = 0; id < maxBands; ++id)
            if (find (id) == nullptr)
                return id;
        return -1;
    }
};

using BandDirections = std::array<Vec3, maxBands>;

inline float latToFreq (float lat)
{
    const float t = (lat / halfPi + 1.0f) / 2.0f;
    return minFreq * std::pow (maxFreq / minFreq, t);
}

inline float freqToLat (float freq)
{
    const float t = std::log (freq / minFreq) / std::log (maxFreq / minFreq);
    return (t * 2.0f - 1.0f) * halfPi;
}

inline Vec3 bandDirection (const Band& b)
{
    const float c = std::cos (b.lat);
    return { c * std::sin (b.lon), std::sin (b.lat), c * std::cos (b.lon) };
}

inline BandDirections directionsOf (const BandSet& bands)
{
    BandDirections dirs;
    for (int i = 0; i < bands.count; ++i)
        dirs[(size_t) i] = bandDirection (bands.items[(size_t) i]);
    return dirs;
}


// How much of the full bump a gain shows (-1..1). A gentle curve rather than
// linear, so everyday gains stay visible while +-30 dB still fits on the sphere.
inline float gainShape (float gainDb)
{
    const float shape = std::pow (std::min (1.0f, std::abs (gainDb) / maxGainDb), 0.7f);
    return gainDb < 0 ? -shape : shape;
}

inline float gainDisplacement (float gainDb)
{
    return gainShape (gainDb) * (gainDb > 0 ? maxBoostDisplacement : maxCutDisplacement);
}

// Stacked bands can add up past full scale: ease them into a little extra room
// so the surface never passes through the centre of the sphere.
inline float limitDisplacement (float d)
{
    const float max = d > 0 ? maxBoostDisplacement : maxCutDisplacement;
    const float x = std::abs (d);
    if (x <= max)
        return d;
    const float limited = max + stackRoom * (1.0f - std::exp (-(x - max) / stackRoom));
    return d < 0 ? -limited : limited;
}

// A cut's magnitude (dB, <= 0) at frequency f: ideal Butterworth.
inline float cutResponseDb (FilterType type, float cutoff, int slope, float f)
{
    const float n = (float) slope / 6.0f;
    const float ratio = type == FilterType::lowCut ? cutoff / f : f / cutoff;
    return -10.0f * std::log10 (1.0f + std::pow (ratio, 2.0f * n));
}

// A shelf's magnitude (dB) at (frequency / shelf frequency)^2: the analogue RBJ
// shelf. shelfA is 10^(gain / 40), shelfK is A / Q^2 (the per-frame parts).
inline float shelfDbAt (bool low, float shelfA, float shelfK, float ratioSquared)
{
    const float damping = shelfK * ratioSquared;
    const float a = shelfA - ratioSquared, c = 1.0f - shelfA * ratioSquared;
    const float lowSide = a * a + damping, highSide = c * c + damping;
    return 10.0f * std::log10 (shelfA * shelfA * (low ? lowSide / highSide : highSide / lowSide));
}

inline float shelfA (float gainDb)           { return std::pow (10.0f, gainDb / 40.0f); }
inline float shelfK (float a, float q)
{
    const float limited = std::min (q, maxShelfQ);
    return a / (limited * limited);
}

inline float shelfResponseDb (const Band& b, float gainDb, float f)
{
    const float a = shelfA (gainDb);
    const float ratio = f / latToFreq (b.lat);
    return shelfDbAt (b.type == FilterType::lowShelf, a, shelfK (a, b.q), ratio * ratio);
}

// A bell's magnitude (dB) at x = f / f0: the analogue RBJ peak, the shape the
// EQ's matched bell follows.
inline float peakResponseDb (float gainDb, float q, float x)
{
    const float a = std::pow (10.0f, gainDb / 40.0f);
    const float x2 = x * x, u = (1.0f - x2) * (1.0f - x2);
    return 10.0f * std::log10 ((u + x2 * a * a / (q * q)) / (u + x2 / (a * a * q * q)));
}

// Past this Q a bell is narrower than the particles are apart, so it would
// flicker in and out as it moves. Above it the drawn width keeps narrowing,
// but more slowly: Q 40 draws as Q 8.
constexpr float sharpestDrawnQ = 4.0f;

inline float drawnQ (float q)
{
    return q <= sharpestDrawnQ ? q : sharpestDrawnQ * std::pow (q / sharpestDrawnQ, 0.3f);
}

// A bell's dB at an angle from its centre on the sphere. The angle reads as a
// distance in frequency, as latitude does, so along its meridian the bump is
// the filter's response, and round its centre it is the same in every direction.
inline float logRatioPerRadian()    { return std::log (maxFreq / minFreq) / pi; }

inline float peakDbAtAngle (float gainDb, float q, float angle)
{
    return peakResponseDb (gainDb, drawnQ (q), std::exp (angle * logRatioPerRadian()));
}

// A bell's drawn shape, worked out once so it is cheap to evaluate in many
// directions: the same response as peakDbAtAngle, flat past flatDot.
struct BellCurve
{
    float a2 = 1, q2 = 1;
    float flatDot = 2; // cosine of the angle past which the skirt is under 1e-4 dB
};

inline BellCurve bellCurve (const Band& b)
{
    BellCurve c;
    c.a2 = std::pow (10.0f, b.displayGain() / 20.0f);
    const float q = drawnQ (b.q);
    c.q2 = q * q;
    // Far out a skirt is about 4.34 (a^2 - 1/a^2) / (q^2 x^2) dB, with x the frequency ratio.
    const float spread = std::abs (c.a2 - 1.0f / c.a2) / c.q2;
    if (spread > 0)
    {
        const float flatX2 = std::max (1.0f, 4.343f * spread / 1.0e-4f);
        c.flatDot = std::cos (std::min (pi, 0.5f * std::log (flatX2) / logRatioPerRadian()));
    }
    return c;
}

inline float bellDisplacement (const BellCurve& c, float dot)
{
    if (dot < c.flatDot)
        return 0.0f;

    const float x2 = std::exp (2.0f * std::acos (std::min (dot, 1.0f)) * logRatioPerRadian());
    const float u = (1.0f - x2) * (1.0f - x2);
    return gainDisplacement (10.0f * std::log10 ((u + x2 * c.a2 / c.q2) / (u + x2 / (c.a2 * c.q2))));
}

// The angle at which a bell's bump has fallen to about a third of its height
// (as a Gaussian of this width would, at its width): for sizing the rings
// round it. It narrows as Q goes up.
inline float bandWidth (const Band& b)
{
    constexpr float referenceDb = 12.0f;
    const float edgeDb = referenceDb * std::exp (-1.0f / 0.7f); // the gain shape's curve, undone
    float lo = 0.0f, hi = pi;
    for (int i = 0; i < 24; ++i)
    {
        const float mid = (lo + hi) / 2.0f;
        (peakDbAtAngle (referenceDb, b.q, mid) > edgeDb ? lo : hi) = mid;
    }
    return std::max (lo, 0.01f);
}

// How much a band on axis lonBand affects a point at longitude lon: 1 on its
// own axis, 0 on the opposite one (a Mid cut never touches Side).
inline float axisOverlap (float lonBand, float lon)    { return (1.0f + std::cos (lon - lonBand)) / 2.0f; }

inline float bandOverlap (const Band& b, float lon)    { return b.stereo ? 1.0f : axisOverlap (b.lon, lon); }

// bell (i, dot): bell i's displacement at cosine dot from its centre, so a
// renderer can look it up rather than work out the response.
template <typename BellDisplacement>
inline float surfaceDisplacement (const BandSet& bands, const BandDirections& dirs, Vec3 d, BellDisplacement&& bell)
{
    float r = 0;

    for (int i = 0; i < bands.count; ++i)
    {
        const auto& b = bands.items[(size_t) i];
        if (b.muted)
            continue;

        if (b.isCut())
        {
            const float lat = std::asin (std::clamp (d.y, -1.0f, 1.0f));
            const float lon = std::atan2 (d.x, d.z);
            const float db = cutResponseDb (b.type, latToFreq (b.lat), b.slope, latToFreq (lat));
            r += gainShape (db) * maxCutDisplacement * bandOverlap (b, lon);
            continue;
        }

        if (b.isShelf())
        {
            const float lat = std::asin (std::clamp (d.y, -1.0f, 1.0f));
            const float lon = std::atan2 (d.x, d.z);
            r += gainDisplacement (shelfResponseDb (b, b.displayGain(), latToFreq (lat))) * bandOverlap (b, lon);
            continue;
        }

        const auto& bd = dirs[(size_t) i];
        // A stereo peak is a belt: only the distance in latitude counts.
        const float dot = b.stereo ? std::clamp (std::sqrt (std::max (0.0f, 1.0f - d.y * d.y)) * std::cos (b.lat) + d.y * std::sin (b.lat), -1.0f, 1.0f)
                                   : std::clamp (d.x * bd.x + d.y * bd.y + d.z * bd.z, -1.0f, 1.0f);
        r += bell (i, dot);
    }

    return limitDisplacement (r);
}

inline float surfaceDisplacement (const BandSet& bands, const BandDirections& dirs, Vec3 d)
{
    return surfaceDisplacement (bands, dirs, d, [&] (int i, float dot) { return bellDisplacement (bellCurve (bands.items[(size_t) i]), dot); });
}

// The radius a surface is drawn at: past the knee it eases towards a limit, so
// peaks stay in the window without a crease.
inline float surfaceRadius (float r)
{
    constexpr float knee = 1.3f, room = 0.3f;
    return r > knee ? knee + room * (1.0f - std::exp ((knee - r) / room)) : r;
}

inline void setLatLon (Band& b, Vec3 d)
{
    b.lat = std::asin (std::clamp (d.y, -1.0f, 1.0f));
    b.lon = std::atan2 (d.x, d.z);
}

} // namespace threedq
