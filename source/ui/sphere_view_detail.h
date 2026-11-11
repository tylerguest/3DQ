/*
  ==============================================================================

    Gesture constants and small helpers shared by SphereView's source files.
    Private to SphereView.

  ==============================================================================
*/

#pragma once

#include "sphere_view.h"
#include "theme.h"

namespace threedq::view
{

constexpr float nodeHitRadius = 16.0f;
constexpr float dragThreshold = 4.0f;
// A press this short counts as a click even if the hand drifted past dragThreshold.
constexpr float clickMaxTravel = 10.0f;
constexpr double clickMaxSeconds = 0.35;
constexpr float rotateSpeed = 0.008f;     // radians per pixel
constexpr float gainDbPerPixel = 0.1f;
constexpr float autoRotate = 0.0035f;     // radians per 1/60 s
constexpr double autoResumeDelay = 1.5;   // seconds
constexpr float minZoom = 0.25f, maxZoom = 2.5f;
constexpr float helpButtonRadius = 9.0f;
constexpr float helpButtonInset = 24.0f;  // from the window's right edge to the button's centre; it sits mid-bar
constexpr float bottomBarSideInset = 44.0f; // the bar's lower corners, in from the window's sides, leaving the ? its corner
constexpr float bottomBarSlant = 13.0f;     // how far its ends lean in towards the top
constexpr float outputFieldWidth = 96.0f, outputFieldHeight = 18.0f;
constexpr float outputDbPerPixel = 0.1f;
constexpr float knobPixelsFullRange = 200.0f;
constexpr double peakHoldSeconds = 1.0;  // the GR meter holds a peak this long,
constexpr float peakFallSeconds = 0.6f;  // then eases back over about this long;
constexpr float meterGlideSeconds = 0.04f; // it glides to each new value rather than jumping
constexpr float knobFineFactor = 0.2f; // while Shift is held

// The bottom bar's buttons, in the same pixels as width and height; unit is one
// scaled UI point in them.
inline juce::Point<float> helpButtonCentre (float width, float height, float unit)
{
    return { width - helpButtonInset * unit, height - bottomBarHeight / 2.0f * unit };
}

inline juce::Rectangle<float> outputFieldArea (float width, float height, float unit)
{
    const float right = width - (bottomBarSideInset + bottomBarSlant + 6.0f) * unit;
    return { right - outputFieldWidth * unit, height - (bottomBarHeight + outputFieldHeight) / 2.0f * unit,
             outputFieldWidth * unit, outputFieldHeight * unit };
}

inline double nowSeconds()    { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

inline const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0xb7);

inline juce::String panLabel (float lon)
{
    const int pan = (int) std::lround (std::sin (lon) * 100.0f);
    if (pan == 0)
        return "Pan C";
    return juce::String ("Pan ") + (pan < 0 ? "L " : "R ") + juce::String (std::abs (pan)) + "%";
}

inline juce::String midSideLabel (float lon)
{
    const int mid = (int) std::lround ((1.0f + std::cos (lon)) / 2.0f * 100.0f);
    return "Mid " + juce::String (mid) + "% " + dot + " Side " + juce::String (100 - mid) + "%";
}

inline bool commandHeld (const juce::ModifierKeys& mods)    { return mods.isCtrlDown() || mods.isCommandDown(); }

// Depth comes from the undisplaced direction: a node at the bottom of a deep
// dent is near the centre, but it's still on the side it faces.
inline Vec3 screenPosition (const BandSet& bands, const BandDirections& dirs, const Band& b, const Camera& camera)
{
    const auto d = bandDirection (b);
    const float r = 1.0f + surfaceDisplacement (bands, dirs, d);
    auto s = camera.toScreen (camera.toView ({ d.x * r, d.y * r, d.z * r }));
    s.z = camera.toView (d).z;
    return s;
}

} // namespace threedq::view
