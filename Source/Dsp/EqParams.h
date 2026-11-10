/*
  ==============================================================================

    The EQ's limits and per-band settings, shared by the DSP, the plugin's
    parameters and the sphere. Plain C++ (no JUCE).

  ==============================================================================
*/

#pragma once

#include <array>

namespace threedq
{

constexpr int   maxBands  = 12;
constexpr float minFreq   = 20.0f;
constexpr float maxFreq   = 20000.0f;
constexpr float maxGainDb = 30.0f;
constexpr float minQ      = 0.3f;
constexpr float maxQ      = 10.0f;
constexpr float maxShelfQ = 2.0f; // higher resonates far past the shelf's own gain
constexpr float minThresholdDb = -60.0f;
constexpr float maxThresholdDb = 0.0f;

// The values are the plugin's choice parameter, so new types go at the end.
enum class FilterType { peak = 0, lowCut = 1, highCut = 2, lowShelf = 3, highShelf = 4 };

constexpr bool isCutType (FilterType t)     { return t == FilterType::lowCut || t == FilterType::highCut; }
constexpr bool isShelfType (FilterType t)   { return t == FilterType::lowShelf || t == FilterType::highShelf; }

// Cut slopes in dB/oct (order = slope / 6). Index = the plugin's choice parameter.
constexpr std::array<int, 6> cutSlopes { 6, 12, 18, 24, 36, 48 };

struct BandParams
{
    bool  enabled = false;
    float freq    = 1000.0f;
    float gainDb  = 0.0f;
    float q       = 1.0f;
    float posDeg  = 0.0f; // stereo position (longitude), -180..180
    float rangeDb = 0.0f; // dynamic range; 0 = static band
    float thresholdDb = -30.0f; // dBFS, of the band's own (band-passed) signal
    FilterType type = FilterType::peak;
    int slopeDbPerOct = 24; // cuts only
    bool  stereo = false; // filter L and R alike instead of one stereo axis
};

} // namespace threedq
