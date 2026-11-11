/*
  ==============================================================================

    The sphere's band model and the surface shape the EQ gives it, with no
    drawing or GUI code in it.

      latitude  -> frequency (log scale, 20 Hz at the bottom pole, 20 kHz at the top)
      longitude -> stereo position (0 = front = Mid, +90 deg = R, -90 deg = L, back = Side)
      gain      -> bump out of (boost) or dent into (cut) the surface
      q         -> how wide the bump is

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

// Angular radius of a band's bump: narrower as Q goes up.
inline float bandWidth (const Band& b)    { return 0.55f / std::sqrt (b.q); }

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

// How much a band on axis lonBand affects a point at longitude lon: 1 on its
// own axis, 0 on the opposite one (a Mid cut never touches Side).
inline float axisOverlap (float lonBand, float lon)    { return (1.0f + std::cos (lon - lonBand)) / 2.0f; }

inline float bandOverlap (const Band& b, float lon)    { return b.stereo ? 1.0f : axisOverlap (b.lon, lon); }

inline float surfaceDisplacement (const BandSet& bands, const BandDirections& dirs, Vec3 d)
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
        const float w = bandWidth (b);
        r += gainDisplacement (b.displayGain()) * std::exp (-2.0f * (1.0f - dot) / (w * w));
    }

    return limitDisplacement (r);
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
