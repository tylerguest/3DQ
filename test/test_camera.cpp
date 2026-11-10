/*
  ==============================================================================

    The camera: rotations invert, the projection is centred, picking finds
    the point under the mouse.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/model/camera.h"

namespace threedq::test
{

namespace
{
float distance (Vec3 a, Vec3 b)
{
    return std::sqrt ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

float length (Vec3 a)    { return distance (a, {}); }
}

class CameraTests final : public juce::UnitTest
{
public:
    CameraTests() : juce::UnitTest ("Camera", "model") {}

    void runTest() override
    {
        juce::Random random (3);
        auto randomCamera = [&]
        {
            return Camera::make (300 + 900 * random.nextFloat(), 300 + 900 * random.nextFloat(),
                                 (random.nextFloat() * 2 - 1) * 3.0f, (random.nextFloat() * 2 - 1) * 1.5f,
                                 0.5f + 2.0f * random.nextFloat(), (random.nextFloat() * 2 - 1) * 100.0f, (random.nextFloat() * 2 - 1) * 100.0f);
        };

        beginTest ("toView and fromView undo each other and keep lengths");
        for (int i = 0; i < 200; ++i)
        {
            const auto camera = randomCamera();
            const Vec3 p { random.nextFloat() * 2 - 1, random.nextFloat() * 2 - 1, random.nextFloat() * 2 - 1 };
            expectLessThan (distance (camera.fromView (camera.toView (p)), p), 1.0e-5f);
            expectLessThan (distance (camera.toView (camera.fromView (p)), p), 1.0e-5f);
            expectWithinAbsoluteError (length (camera.toView (p)), length (p), 1.0e-5f);
        }

        beginTest ("The default view looks at Mid, with high frequencies up");
        {
            const auto camera = Camera::make (800, 600, 0, 0, 1, 0, 0);
            expectLessThan (distance (camera.toView ({ 0, 0, 1 }), { 0, 0, 1 }), 1.0e-6f);
            const auto top = camera.toScreen (camera.toView ({ 0, 1, 0 }));
            expectLessThan (top.y, camera.centreY(), "up on screen");
        }

        beginTest ("The origin lands on the centre, panned; the sphere's size follows zoom");
        {
            const auto camera = Camera::make (800, 600, 0.7f, -0.3f, 2.0f, 40.0f, -25.0f);
            const auto origin = camera.toScreen (camera.toView ({}));
            expectWithinAbsoluteError (origin.x, 440.0f, 1.0e-3f);
            expectWithinAbsoluteError (origin.y, 275.0f, 1.0e-3f);
            expectWithinAbsoluteError (camera.radiusPx(), 600.0f * sphereRadiusFraction * 2.0f, 1.0e-3f);
        }

        beginTest ("Picking the centre of the sphere hits its front");
        {
            const auto camera = Camera::make (800, 600, 0, 0, 1, 0, 0);
            Vec3 hit;
            expect (camera.pickSphere (400, 300, false, 1.0f, hit));
            expectLessThan (distance (hit, { 0, 0, 1 }), 1.0e-5f);
        }

        beginTest ("Picking a projected point finds that point again");
        for (int i = 0; i < 200; ++i)
        {
            const auto camera = randomCamera();
            // A random point on the visible half of the sphere.
            Vec3 view { random.nextFloat() * 1.4f - 0.7f, random.nextFloat() * 1.4f - 0.7f, 0 };
            view.z = std::sqrt (std::max (0.0f, 1.0f - view.x * view.x - view.y * view.y));
            if (view.z < 0.3f)
                continue;
            const auto screen = camera.toScreen (view);
            Vec3 hit;
            expect (camera.pickSphere (screen.x, screen.y, false, 1.0f, hit));
            expectLessThan (distance (hit, camera.fromView (view)), 1.0e-3f);
        }

        beginTest ("Off the sphere: no hit, unless clamped to the edge");
        {
            const auto camera = Camera::make (800, 600, 0.4f, 0.2f, 1, 0, 0);
            Vec3 hit;
            expect (! camera.pickSphere (5, 5, false, 1.0f, hit));
            expect (camera.pickSphere (5, 5, true, 1.0f, hit));
            expectWithinAbsoluteError (length (hit), 1.0f, 1.0e-5f);
            expectGreaterThan (camera.toView (hit).z, 0.0f, "on the visible half");

            // Just outside the silhouette, the clamped point is the sphere's edge,
            // where the view ray grazes it (view z = 1 / camera distance).
            const float edgeZ = 1.0f / cameraDistance;
            const auto edge = camera.toScreen ({ std::sqrt (1.0f - edgeZ * edgeZ), 0, edgeZ });
            const float justOutside = camera.centreX() + (edge.x - camera.centreX()) * 1.01f;
            expect (! camera.pickSphere (justOutside, camera.centreY(), false, 1.0f, hit));
            expect (camera.pickSphere (justOutside, camera.centreY(), true, 1.0f, hit));
            expectWithinAbsoluteError (camera.toView (hit).z, edgeZ, 0.02f, "on the silhouette");
        }

        beginTest ("A flat sphere's extent is one radius in each direction, give or take perspective");
        {
            const auto camera = Camera::make (800, 600, 0.3f, 0.2f, 1, 0, 0);
            const auto extent = surfaceExtent ({}, camera);
            expectWithinAbsoluteError (extent.top, 1.0f, 0.1f);
            expectWithinAbsoluteError (extent.bottom, -1.0f, 0.1f);
            expectWithinAbsoluteError (extent.halfWidth, 1.0f, 0.1f);

            BandSet boosted;
            Band b;
            b.lat = halfPi * 0.9f;
            b.gainDb = 24.0f;
            boosted.add (b);
            expectGreaterThan (surfaceExtent (boosted, camera).top, extent.top + 0.2f, "a boost at the top reaches higher");
        }
    }
};

static CameraTests cameraTests;

} // namespace threedq::test
