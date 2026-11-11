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
constexpr float maxTrimGainDb = 24.0f; // the input and output gains: up to this either way
constexpr float minQ      = 0.025f; // a gentle tilt across the whole spectrum
constexpr float maxQ      = 40.0f;  // a notch for a single resonance
constexpr float maxShelfQ = 2.0f; // higher resonates far past the shelf's own gain
constexpr float minThresholdDb = -60.0f;
constexpr float maxThresholdDb = 0.0f;

// Dynamics timing and ratio. The defaults are what every band had before they
// could be set, so older sessions sound the same.
constexpr float minAttackMs = 0.1f, maxAttackMs = 200.0f, defaultAttackMs = 2.0f;
constexpr float minReleaseMs = 5.0f, maxReleaseMs = 3000.0f, defaultReleaseMs = 90.0f;
constexpr float minRatio = 1.0f, maxRatio = 20.0f, defaultRatio = 2.0f;

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
    bool  externalSidechain = false; // dynamics listen to the sidechain input, not the band's own signal
    float attackMs = defaultAttackMs;   // how fast the detector rises
    float releaseMs = defaultReleaseMs; // how fast it falls back
    float ratio = defaultRatio;         // above threshold: dB in per dB of gain change, as on a compressor
};

} // namespace threedq
