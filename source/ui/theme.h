/*
  ==============================================================================

    Colours, fonts and text formats shared by everything drawn over the sphere.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace threedq
{

inline const juce::Colour warmGrey (138, 134, 124);
inline const juce::Colour boostColour (255, 138, 42);
inline const juce::Colour cutColour (229, 72, 77);
inline const juce::Colour graphite (17, 17, 16);
inline const juce::Colour textColour (232, 228, 218);
inline const juce::Colour pale (255, 252, 244);
// Freq, Q and what is chosen or on: a warm gold.
inline const juce::Colour accentColour (242, 193, 78);
// The input's level on the meter, a cool slate against the output's pale.
inline const juce::Colour inputMeterColour (98, 158, 182);

// The window side at which the panel and the on-screen buttons are drawn 1:1.
constexpr float uiReferenceSize = 520.0f;

// The bottom of the preset bar along the top of the window, in UI points
// (multiplied by uiScale). Views fitted under it keep clear of it.
constexpr float presetBarBottom = 40.0f;

// The bar along the bottom of the window (output gain; the ?
// button sits in the corner beside it), in UI points. The band panel floats above it.
constexpr float bottomBarHeight = 22.0f;

// How much the panel and the on-screen buttons grow with the window. It follows
// the window's smaller side, but more slowly than the window, so they don't
// dominate a large one. Width and height are in component pixels.
inline float uiScale (float width, float height)
{
    return std::max (1.0f, std::sqrt (std::min (width, height) / uiReferenceSize));
}

// Attack and release times: "2.0 ms", "90 ms", "1.20 s".
inline juce::String formatMs (float ms)
{
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 2) + " s";
    return juce::String (ms, ms < 10.0f ? 1 : 0) + " ms";
}

inline juce::String formatRatio (float ratio)    { return juce::String (ratio, ratio < 10.0f ? 1 : 0) + ":1"; }

inline juce::String formatFreq (float freq)
{
    if (freq >= 1000.0f)
        return juce::String (freq / 1000.0f, freq >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String ((int) std::lround (freq)) + " Hz";
}

// One decimal, or two when needed (wheel steps are 0.25 dB).
inline juce::String formatDb (float db)
{
    auto s = juce::String (db, 2);
    return s.endsWithChar ('0') ? s.dropLastCharacters (1) : s;
}

// Enough decimals to tell Q 0.025 from 0.03, without clutter at 40.
inline juce::String formatQ (float q)
{
    return juce::String (q, q < 0.1f ? 3 : q < 10.0f ? 2 : 1);
}

inline juce::String formatGain (float db)    { return (db >= 0 ? "+" : "") + formatDb (db) + " dB"; }

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions{}.withPointHeight (size).withStyle (bold ? "Bold" : "Regular"));
}

inline juce::Path arc (float cx, float cy, float radius, float from, float to)
{
    juce::Path p;
    p.addCentredArc (cx, cy, radius, radius, 0.0f, from, to, true);
    return p;
}

} // namespace threedq
