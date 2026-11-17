/*
  ==============================================================================

    SphereField's band rings: the ripples that spread from a peak, the
    contour rings of cuts and shelves, the belts of stereo bands, and the
    dotted marker of how far a dynamic band can move.

  ==============================================================================
*/

#include "sphere_field.h"

namespace threedq
{

namespace
{
constexpr int vortexRings = 6;
constexpr int beltRingsPerSide = 3;
constexpr int vortexPoints = 56;
constexpr std::array<float, 7> cutContoursDb { 1.5f, 3, 6, 12, 18, 24, 30 };
constexpr float cutFloorRingSpacing = 0.26f;
constexpr float cutFloorRingAlpha = 0.3f;
constexpr float cutRingPoleLimit = halfPi - 0.08f;
}

void SphereField::addRingDot (Vec3 dir, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                              const SpriteStyle& style, float alpha, SpriteList& out) const
{
    const float r = 1.0f + surfaceDisplacement (bands, dirs, dir, [&] (int i, float dot) { return bellAt (bands.items[(size_t) i], dot); });
    const auto s = camera.toScreen (camera.toView ({ dir.x * r, dir.y * r, dir.z * r }));
    const float facing = 0.2f + 0.8f * std::clamp (s.z + 0.5f, 0.0f, 1.0f);
    const float size = camera.radiusPx() * 0.05f * 1.2f * (cameraDistance / (cameraDistance - s.z));
    out.add (style, s.x, s.y, size, facing * alpha);
}

// A dynamic band's full reach: a faint dotted ring where the surface would be
// with the whole range applied, above the bump for a dynamic boost and below
// it for a dynamic cut, so you can see how far the band can go as it moves.
// A peak gets a small ring round its centre; a stereo peak or a shelf, a ring
// round the sphere at its latitude (a shelf's, a little toward its pole).
void SphereField::addRangeMarker (const Band& b, BandSet& full, const BandDirections& dirs, const Camera& camera,
                                  float time, SpriteList& out) const
{
    if (b.muted || b.isCut() || b.range == 0.0f)
        return;

    // Draw with this band at its full range, then put it back.
    auto* reach = full.find (b.id);
    const Band before = *reach;
    reach->hasLiveGain = true;
    reach->liveGainDb = std::clamp (b.gainDb + b.range, -maxGainDb, maxGainDb);
    const auto restore = [&] { *reach = before; };

    const auto& style = ringStyle (b.range > 0 ? std::max (b.range, 6.0f) : std::min (b.range, -6.0f));
    constexpr float alpha = 0.5f;
    const float phase = time * 0.15f;

    if (! b.stereo && ! b.isShelf())
    {
        // Round the band's centre, at a third of its bump's width.
        const auto d = bandDirection (b);
        const Vec3 east { std::cos (b.lon), 0.0f, -std::sin (b.lon) };
        const Vec3 north { -std::sin (b.lat) * std::sin (b.lon), std::cos (b.lat), -std::sin (b.lat) * std::cos (b.lon) };
        const float rho = bandWidth (b) * 0.35f, cr = std::cos (rho), sr = std::sin (rho);
        constexpr int points = 28;

        for (int j = 0; j < points; ++j)
        {
            const float a = ((float) j / (float) points) * twoPi + phase;
            const float ce = std::cos (a) * sr, cn = std::sin (a) * sr;
            addRingDot ({ d.x * cr + east.x * ce + north.x * cn, d.y * cr + east.y * ce + north.y * cn, d.z * cr + east.z * ce + north.z * cn },
                        full, dirs, camera, style, alpha, out);
        }
        restore();
        return;
    }

    const float lat = b.isShelf() ? b.lat + (b.type == FilterType::lowShelf ? -0.25f : 0.25f) : b.lat;
    const float sinLat = std::sin (lat), cosLat = std::cos (lat);
    const int points = std::max (12, (int) std::lround (60.0f * cosLat));

    for (int j = 0; j < points; ++j)
    {
        const float lon = ((float) j / (float) points) * twoPi + phase;
        const float overlap = bandOverlap (b, lon);
        if (overlap > 0.02f)
            addRingDot ({ cosLat * std::sin (lon), sinLat, cosLat * std::cos (lon) }, full, dirs, camera, style, alpha * overlap, out);
    }
    restore();
}

// Particles round a circle of constant latitude, only where the band acts.
void SphereField::addLatitudeRing (const Band& b, float lat, float phase, const SpriteStyle& style, float fade, const BandSet& bands,
                                   const BandDirections& dirs, const Camera& camera, SpriteList& out) const
{
    const float sinLat = std::sin (lat), cosLat = std::cos (lat);
    const int points = std::max (12, (int) std::lround (76.0f * cosLat));

    for (int j = 0; j < points; ++j)
    {
        const float lon = ((float) j / (float) points) * twoPi + phase;
        const float overlap = bandOverlap (b, lon);
        if (overlap < 0.02f)
            continue;

        addRingDot ({ cosLat * std::sin (lon), sinLat, cosLat * std::cos (lon) }, bands, dirs, camera, style, fade * overlap, out);
    }
}

// A cut's vortex: swirling rings at constant latitude down the carved cap. The
// first ones are contours at fixed depths, so they bunch up on a steep slope;
// past the last contour evenly spaced rings carry on across the floor.
void SphereField::addCutRings (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                               float time, float amount, SpriteList& out)
{
    if (amount < 0.01f)
        return;

    const float towardPole = b.type == FilterType::lowCut ? -1.0f : 1.0f;
    const float radiansPerOctave = pi / std::log2 (maxFreq / minFreq);
    const int contours = (int) cutContoursDb.size();
    float lat = b.lat;

    for (int k = 0;; ++k)
    {
        if (k < contours)
        {
            // Butterworth inverted: octaves past the cutoff where it reaches this depth.
            const float octaves = std::log2 (std::pow (10.0f, cutContoursDb[(size_t) k] / 10.0f) - 1.0f) / (2.0f * ((float) b.slope / 6.0f));
            lat = b.lat + towardPole * octaves * radiansPerOctave;
        }
        else
        {
            lat += towardPole * cutFloorRingSpacing;
        }

        if (towardPole * lat > cutRingPoleLimit)
            break;
        if (towardPole * lat < -cutRingPoleLimit)
            continue;

        const float ringDb = cutResponseDb (b.type, latToFreq (b.lat), b.slope, latToFreq (lat));
        const auto& style = ringStyle (ringDb);
        const float shape = -gainShape (ringDb);
        const float ringFade = amount * (k < contours ? 0.35f + 0.55f * shape : cutFloorRingAlpha);
        const float t = (float) (k + 1) / vortexRings;
        const float phase = -time * 0.5f / (0.35f + t) + t * 2.4f;
        addLatitudeRing (b, lat, phase, style, ringFade, bands, dirs, camera, out);
    }
}

// A shelf's vortex: evenly spaced rings from the shelf frequency out to its
// pole, lying on the raised or lowered surface and fading with its height.
void SphereField::addShelfRings (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                                 float time, float amount, SpriteList& out)
{
    const float gain = b.displayGain();
    if (std::abs (gainShape (gain)) * amount < 0.01f)
        return;

    const float towardPole = b.type == FilterType::lowShelf ? -1.0f : 1.0f;
    const float spin = gain > 0 ? 0.5f : -0.5f;

    for (int k = 0;; ++k)
    {
        const float lat = b.lat + towardPole * (float) k * cutFloorRingSpacing;

        if (towardPole * lat > cutRingPoleLimit)
            break;
        if (towardPole * lat < -cutRingPoleLimit)
            continue;

        const float ringDb = shelfResponseDb (b, gain, latToFreq (lat));
        const auto& style = ringStyle (ringDb);
        const float ringFade = amount * std::min (1.0f, std::abs (gainShape (ringDb)) * 1.4f);
        const float t = (float) (k + 1) / vortexRings;
        const float phase = time * spin / (0.35f + t) + t * 2.4f;
        addLatitudeRing (b, lat, phase, style, ringFade, bands, dirs, camera, out);
    }
}

// A stereo peak's vortex: rings round the sphere on either side of its latitude.
void SphereField::addBeltRings (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                                float time, float strength, SpriteList& out)
{
    const float gain = b.displayGain();
    const float w = bandWidth (b);
    const float spin = gain > 0 ? 0.5f : -0.5f;

    for (int k = -beltRingsPerSide; k <= beltRingsPerSide; ++k)
    {
        const float offset = (float) k / (float) beltRingsPerSide;
        const float angle = offset * w * 1.5f;
        const float lat = b.lat + angle;
        if (std::abs (lat) > cutRingPoleLimit)
            continue;

        const float t = std::abs (offset);
        const float phase = time * spin / (0.35f + t) + offset * 2.4f;
        const float ringFade = strength * (1.15f - t * 0.75f);
        const auto& style = ringStyle (peakDbAtAngle (gain, b.q, std::abs (angle)));
        addLatitudeRing (b, lat, phase, style, ringFade, bands, dirs, camera, out);
    }
}

// A band's vortex: rings of particles lying on the bumped or dented surface
// around it, swirling slowly. The motion makes the depth read.
void SphereField::addVortex (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                             float time, float dt, SpriteList& out)
{
    const float goal = b.muted ? 0.0f : 1.0f;
    auto& slot = vortexAmount[(size_t) b.id];
    const float previous = slot < 0 ? goal : slot;
    const float amount = previous + (goal - previous) * (1.0f - std::exp (-dt / 0.12f));
    slot = amount;

    if (b.isCut())
    {
        addCutRings (b, bands, dirs, camera, time, amount, out);
        return;
    }

    if (b.isShelf())
    {
        addShelfRings (b, bands, dirs, camera, time, amount, out);
        return;
    }

    const float gain = b.displayGain();
    const float strength = std::abs (gainShape (gain)) * amount;
    if (strength < 0.01f)
        return;

    if (b.stereo)
    {
        addBeltRings (b, bands, dirs, camera, time, strength, out);
        return;
    }

    const auto c = bandDirection (b);
    const Vec3 ref = std::abs (c.y) < 0.9f ? Vec3 { 0, 1, 0 } : Vec3 { 1, 0, 0 };
    Vec3 e1 { ref.y * c.z - ref.z * c.y, ref.z * c.x - ref.x * c.z, ref.x * c.y - ref.y * c.x };
    const float len = std::sqrt (e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
    e1 = { e1.x / len, e1.y / len, e1.z / len };
    const Vec3 e2 { c.y * e1.z - c.z * e1.y, c.z * e1.x - c.x * e1.z, c.x * e1.y - c.y * e1.x };

    const float w = bandWidth (b);
    const float spin = gain > 0 ? 0.5f : -0.5f; // boosts and cuts turn opposite ways
    const float span = std::min (w * 1.5f, 0.85f * pi); // a very wide band's rings stop short of the far side

    for (int k = 0; k < vortexRings; ++k)
    {
        const float t = (float) (k + 1) / vortexRings;
        const float alpha = t * span; // angular radius of this ring
        const float sa = std::sin (alpha), ca = std::cos (alpha);
        const float phase = time * spin / (0.35f + t) + t * 2.4f; // inner rings turn faster; the offset makes a spiral
        const float ringFade = strength * (1.15f - t * 0.75f);
        const auto& style = ringStyle (peakDbAtAngle (gain, b.q, alpha)); // the bump's own dB at this ring

        for (int j = 0; j < vortexPoints; ++j)
        {
            const float beta = ((float) j / vortexPoints) * twoPi + phase;
            const float cb = std::cos (beta) * sa, sb = std::sin (beta) * sa;
            addRingDot ({ c.x * ca + e1.x * cb + e2.x * sb,
                          c.y * ca + e1.y * cb + e2.y * sb,
                          c.z * ca + e1.z * cb + e2.z * sb }, bands, dirs, camera, style, ringFade, out);
        }
    }
}

} // namespace threedq
