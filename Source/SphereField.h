/*
  ==============================================================================

    SphereField: the sphere's particles. Each frame it advances the springs,
    the audio response and the energy map, and writes every glowing dot to a
    SpriteList. It knows nothing about the graphics API, so another backend can
    draw the same list by replacing SpriteRenderer alone.

  ==============================================================================
*/

#pragma once

#include <vector>
#include "SphereModel.h"
#include "StereoSpectrum.h"

namespace sphere
{

// One glowing dot, in device pixels. The layout is what the GPU receives.
struct Sprite
{
    float x, y, size;
    float r, g, b;
    float alpha;
    float profile; // 0 = particle (crisp core + halo), 1 = tint (soft falloff)
};

struct SpriteStyle
{
    float r, g, b, profile;
};

class SpriteList
{
public:
    void clear()                     { count = 0; }
    const Sprite* data() const       { return sprites.data(); }
    int size() const                 { return count; }

    void add (const SpriteStyle& s, float x, float y, float size, float alpha)
    {
        if (alpha <= 0.003f)
            return;

        if ((size_t) count == sprites.size())
            sprites.resize (std::max<size_t> (32768, sprites.size() * 2));

        sprites[(size_t) count++] = { x, y, size, s.r, s.g, s.b, std::min (alpha, 1.0f), s.profile };
    }

private:
    std::vector<Sprite> sprites;
    int count = 0;
};

class SphereField
{
public:
    SphereField();

    void update (const BandSet& bands, const Camera& camera, const StereoSpectrum::Result& spectrum,
                 double timeSeconds, float dt, SpriteList& out);

private:
    static constexpr int numSpectrumBands = StereoSpectrum::numBands;
    static constexpr int lonCells = 64;

    void buildParticles();
    void buildStyles();
    void updateSpectrum (const StereoSpectrum::Result& spectrum, float dt);
    void updateLevelTable();
    float levelInBand (int particle, int band) const;
    float audioLevelAt (int particle) const;
    void addVortex (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                    float time, float dt, SpriteList& out);
    void addCutRings (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                      float time, float amount, SpriteList& out);
    void addShelfRings (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                        float time, float amount, SpriteList& out);
    void addBeltRings (const Band& b, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                       float time, float strength, SpriteList& out);
    void addLatitudeRing (const Band& b, float lat, float phase, const SpriteStyle& style, float fade, const BandSet& bands,
                          const BandDirections& dirs, const Camera& camera, SpriteList& out) const;
    void addRingDot (Vec3 dir, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                     const SpriteStyle& style, float alpha, SpriteList& out) const;
    const SpriteStyle& ringStyle (float db) const;

    int numParticles = 0;
    std::vector<float> pX, pY, pZ, pShell, pSize, pTwinkle, pTwinkleRate, pMid, pFreq, pCosLon, pSinLon, specPos, lonPos;
    std::vector<int> pBucket;
    std::vector<float> springOffset, springVelocity, audioOffset, audioVelocity, energy;

    std::vector<SpriteStyle> particleStyles, boostRingStyles, cutRingStyles;
    SpriteStyle midStyle {};

    std::array<float, numSpectrumBands> heldLL {}, heldRR {}, heldLR {};
    std::array<float, numSpectrumBands> smoothLL {}, smoothRR {}, smoothLR {};
    std::array<float, numSpectrumBands * lonCells> levelTable {};
    std::array<float, lonCells> cellLL {}, cellRR {}, cellLR {};

    float audioMix = 0;
    float bassAverage = 0, bassOnset = 0;
    float breath = 0, breathVelocity = 0;
    float audioPeakRef = 0, audioMeanLift = 0;
    float energyAge = 0, energyMapAmount = 0, energyPeak = 0, energyMeanHeat = 0;

    std::array<float, maxBands> vortexAmount;
};

} // namespace sphere
