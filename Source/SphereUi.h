/*
  ==============================================================================

    Colours, fonts and text formats shared by everything drawn over the sphere.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace sphere
{

inline const juce::Colour warmGrey (138, 134, 124);
inline const juce::Colour boostColour (255, 138, 42);
inline const juce::Colour cutColour (229, 72, 77);
inline const juce::Colour graphite (17, 17, 16);
inline const juce::Colour textColour (232, 228, 218);
inline const juce::Colour pale (255, 252, 244);

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

} // namespace sphere
