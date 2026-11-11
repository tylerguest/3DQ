/*
  ==============================================================================

    SphereField: the sphere's particles. The surface's shape is the EQ alone;
    a dot's size and brightness are the audio at its frequency and stereo
    position, and it turns orange or red where the EQ is changing real signal.
    That changed audio also shows as a layer of its own: it lifts off the
    surface where the EQ boosts and sinks into the sphere where it cuts, moving
    with the music.
    With a sidechain coming in, its spectrum is a faint ghost shell just
    outside the sphere, laid out the same way (frequency up, stereo around). It
    glows where it meets loud audio on this track: where the two mask each
    other, and where a sidechained band has something to duck.
    Each frame it advances the springs and writes every glowing dot to a
    SpriteList. It knows nothing about the graphics API, so another backend can
    draw the same list by replacing SpriteRenderer alone. The band rings are in
    band_rings.cpp.

  ==============================================================================
*/

#pragma once

#include "../model/camera.h"
#include "../dsp/stereo_spectrum.h"
#include "sprites.h"

namespace threedq
{

class SphereField
{
public:
    SphereField();

    // sidechain: the sidechain input's spectrum, or null when none is connected.
    void update (const BandSet& bands, const Camera& camera, const StereoSpectrum::Result& spectrum,
                 const StereoSpectrum::Result* sidechain, double timeSeconds, float dt, SpriteList& out);

    // How strongly the lows, mids and highs glow behind the sphere: 0..1, slow.
    const std::array<float, 3>& getHazeLevels() const    { return hazeLevels; }

private:
    static constexpr int numSpectrumBands = StereoSpectrum::numBands;
    static constexpr int lonCells = 64;

    // A spectrum held and smoothed for display, and its level at every
    // frequency and stereo position (0..1, 60 dB).
    struct HeldSpectrum
    {
        std::array<float, numSpectrumBands> heldLL {}, heldRR {}, heldLR {};
        std::array<float, numSpectrumBands> smoothLL {}, smoothRR {}, smoothLR {};
        std::array<float, numSpectrumBands * lonCells> levels {};
    };

    void buildParticles();
    void buildStyles();
    void updateSpectrum (const StereoSpectrum::Result& spectrum, float dt);
    float holdSpectrum (const StereoSpectrum::Result& spectrum, float dt, HeldSpectrum& held) const; // returns the peak
    void updateLevelTable (HeldSpectrum& held) const;
    float levelInBand (const HeldSpectrum& held, int particle, int band) const;
    float audioLevelAt (const HeldSpectrum& held, int particle) const;
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
    // reach: a copy of the bands to work in; the band is put back as it was.
    void addRangeMarker (const Band& b, BandSet& reach, const BandDirections& dirs, const Camera& camera,
                         float time, SpriteList& out) const;
    void addRingDot (Vec3 dir, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                     const SpriteStyle& style, float alpha, SpriteList& out) const;
    const SpriteStyle& ringStyle (float db) const;

    int numParticles = 0;
    std::vector<float> pX, pY, pZ, pShell, pSize, pTwinkle, pTwinkleRate, pMid, pFreq, pCosLon, pSinLon, specPos, lonPos;
    std::vector<int> pColour, drawOrder; // drawOrder: the far half first, so nearer balls cover it
    std::vector<float> springOffset, springVelocity;
    std::vector<float> layerLift; // the affected-audio layer: + lifting off (boost), - sinking in (cut)

    std::vector<SpriteStyle> particleStyles, boostRingStyles, cutRingStyles;
    SpriteStyle midStyle {}, ghostStyle {}, maskStyle {};

    HeldSpectrum output, sidechain;
    std::array<float, lonCells> cellLL {}, cellRR {}, cellLR {};

    float audioMix = 0;
    float sidechainMix = 0; // eases in while a sidechain is coming in
    std::array<float, 3> hazeLevels {};

    std::array<float, maxBands> vortexAmount;
};

} // namespace threedq
