/*
  ==============================================================================

    The view of the sphere: rotation, zoom, pan and perspective, picking, and
    how far the EQ'd shape reaches on screen.

  ==============================================================================
*/

#pragma once

#include "Bands.h"

namespace threedq
{

constexpr float cameraDistance       = 3.2f;
constexpr float sphereRadiusFraction = 0.32f; // of the window's smaller side, at zoom 1

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

} // namespace threedq
