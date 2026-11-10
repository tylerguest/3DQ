/*
  ==============================================================================

    The sphere's band model and camera, with no drawing or GUI code in it.

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
#include "SphereEq.h"

namespace sphere
{

using threedq::FilterType;

constexpr float pi     = 3.14159265358979323846f;
constexpr float halfPi = pi / 2.0f;
constexpr float twoPi  = pi * 2.0f;

constexpr int   maxBands = threedq::maxBands;
constexpr float maxGainDb = threedq::maxGainDb;
constexpr int   defaultCutSlope = 24;
constexpr float defaultThresholdDb = -30.0f;

constexpr float maxBoostDisplacement = 0.55f;
constexpr float maxCutDisplacement   = 0.82f;
constexpr float stackRoom            = 0.1f;
constexpr float cameraDistance       = 3.2f;
constexpr float sphereRadiusFraction = 0.32f; // of the window's smaller side, at zoom 1

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

    // The gain the band has right now, including dynamic movement.
    bool hasLiveGain = false;
    float liveGainDb = 0;

    bool isCut() const           { return threedq::isCutType (type); }
    bool isShelf() const         { return threedq::isShelfType (type); }
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
    return threedq::minFreq * std::pow (threedq::maxFreq / threedq::minFreq, t);
}

inline float freqToLat (float freq)
{
    const float t = std::log (freq / threedq::minFreq) / std::log (threedq::maxFreq / threedq::minFreq);
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
    const float limited = std::min (q, threedq::maxShelfQ);
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

// View maths. All positions are in device pixels; +z faces the viewer.
struct Camera
{
    float width = 1, height = 1, zoom = 1;
    float cosYaw = 1, sinYaw = 0, cosPitch = 1, sinPitch = 0;
    float panX = 0, panY = 0;

    static Camera make (float width, float height, float yaw, float pitch, float zoom, float panX, float panY)
    {
        return { width, height, zoom, std::cos (yaw), std::sin (yaw), std::cos (pitch), std::sin (pitch), panX, panY };
    }

    float centreX() const     { return width / 2.0f + panX; }
    float centreY() const     { return height / 2.0f + panY; }

    float radiusPx() const    { return std::min (width, height) * sphereRadiusFraction * zoom; }

    Vec3 toView (Vec3 p) const
    {
        const float x1 = p.x * cosYaw + p.z * sinYaw;
        const float z1 = -p.x * sinYaw + p.z * cosYaw;
        return { x1, p.y * cosPitch - z1 * sinPitch, p.y * sinPitch + z1 * cosPitch };
    }

    Vec3 fromView (Vec3 p) const
    {
        const float y  = p.y * cosPitch + p.z * sinPitch;
        const float z1 = -p.y * sinPitch + p.z * cosPitch;
        return { p.x * cosYaw - z1 * sinYaw, y, p.x * sinYaw + z1 * cosYaw };
    }

    // View space -> pixels (perspective); z is passed through as depth.
    Vec3 toScreen (Vec3 p) const
    {
        const float scale = cameraDistance / (cameraDistance - p.z);
        const float r = radiusPx();
        return { centreX() + p.x * r * scale, centreY() - p.y * r * scale, p.z };
    }

    // Pixel -> unit direction (model space) of the point on a sphere of the
    // given radius under it. With clampToEdge, positions outside the silhouette
    // snap to the sphere's edge; otherwise they return false.
    bool pickSphere (float px, float py, bool clampToEdge, float radius, Vec3& out) const
    {
        const float r = radiusPx();
        const float u = (px - centreX()) / r;
        const float v = -(py - centreY()) / r;
        const float D = cameraDistance;
        // Ray from (0,0,D) through (u,v,0) at t=1: P(t) = (t u, t v, D - t D).
        const float a = u * u + v * v + D * D;
        const float b = -2.0f * D * D;
        const float c = D * D - radius * radius;
        const float disc = b * b - 4.0f * a * c;

        if (disc >= 0)
        {
            const float t = (-b - std::sqrt (disc)) / (2.0f * a);
            out = fromView ({ t * u / radius, t * v / radius, (D - t * D) / radius });
            return true;
        }

        if (! clampToEdge)
            return false;

        const float t = D * D / a; // closest approach to the centre
        const Vec3 q { t * u, t * v, D - t * D };
        const float len = std::sqrt (q.x * q.x + q.y * q.y + q.z * q.z);
        out = fromView ({ q.x / len, q.y / len, q.z / len });
        return true;
    }
};

// How far above and below the centre of view the EQ'd surface reaches on screen,
// in sphere radii (up is positive). The EQ can make the shape very lopsided.
struct SurfaceExtent
{
    float top = 1, bottom = -1, halfWidth = 1;
};

inline SurfaceExtent surfaceExtent (const BandSet& bands, const Camera& camera, int rows = 36, int columns = 72)
{
    const auto dirs = directionsOf (bands);
    SurfaceExtent extent { 0, 0, 0 };

    for (int row = 0; row < rows; ++row)
    {
        const float lat = -halfPi + pi * (float) row / (float) (rows - 1);
        const float cosLat = std::cos (lat), sinLat = std::sin (lat);

        for (int column = 0; column < columns; ++column)
        {
            const float lon = twoPi * (float) column / (float) columns;
            const Vec3 d { cosLat * std::sin (lon), sinLat, cosLat * std::cos (lon) };
            const float r = surfaceRadius (1.0f + surfaceDisplacement (bands, dirs, d));
            const auto p = camera.toView ({ d.x * r, d.y * r, d.z * r });
            const float perspective = cameraDistance / (cameraDistance - p.z);
            const float y = p.y * perspective;
            extent.halfWidth = std::max (extent.halfWidth, std::abs (p.x * perspective));
            extent.top = std::max (extent.top, y);
            extent.bottom = std::min (extent.bottom, y);
        }
    }

    return extent;
}

inline void setLatLon (Band& b, Vec3 d)
{
    b.lat = std::asin (std::clamp (d.y, -1.0f, 1.0f));
    b.lon = std::atan2 (d.x, d.z);
}

} // namespace sphere
