/*
  ==============================================================================

    SphereField: see SphereField.h. The look and motion here are the same as the
    browser preview's (ui/src/Sphere.tsx).

  ==============================================================================
*/

#include "SphereField.h"

namespace sphere
{

namespace
{
struct RGB
{
    float r, g, b;
};

RGB mixRGB (RGB a, RGB b, float t)
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

constexpr int surfacePoints = 16000;
constexpr int hazePoints = 2200;
// Extra surface points for a big sphere: the dots stay nearly the same size in
// pixels, so without more of them a larger surface looks thinner and paler.
constexpr int firstExtraPoint = surfacePoints + hazePoints;
constexpr int extraPoints = 48000;
constexpr float extraStartRadiusPx = 200.0f;
constexpr float extraFadeCount = 2000.0f; // the newest extras fade in as the sphere grows
constexpr float extraEdgeFade = 0.2f;     // extras fade in over this much of the front half
constexpr float springUnset = 1.0e9f;     // an extra that wasn't drawn last frame
constexpr int numFrequencyColours = 24;
constexpr int tintSteps = 7;
constexpr int numTints = 1 + 2 * tintSteps; // none, then towards boost, then towards cut
constexpr int numRingColours = 24;
constexpr float poleDim = 0.82f;
constexpr float particleAlpha = 0.65f;
constexpr float particleSize = 0.78f;
// A soft light from the upper left, fixed to the view, so the sphere has form.
constexpr float lightX = -0.36f, lightY = 0.62f, lightZ = 0.7f;
constexpr float lightFloor = 0.74f, lightAmount = 0.46f;
constexpr float midLineWidth = 0.035f;

// A dot's colour is its frequency, on the log axis, until the EQ is changing
// real signal there. Orange and red only ever mean boost and cut.
struct ColourStop
{
    float t;
    RGB rgb;
};

constexpr std::array<ColourStop, 7> frequencyColours {{
    { 0.0f,  { 150,  96, 255 } }, // sub: violet
    { 0.17f, {  62, 120, 255 } }, // bass: blue
    { 0.33f, {  40, 205, 235 } }, // low mids: cyan
    { 0.5f,  {  70, 225, 120 } }, // mids: green
    { 0.67f, { 205, 235,  70 } }, // upper mids: lime
    { 0.83f, { 255, 225,  90 } }, // presence: yellow
    { 1.0f,  { 238, 240, 255 } }, // air: ice white
}};

constexpr RGB boostColour { 255, 120, 24 };
constexpr RGB cutColour   { 232, 52, 48 };

constexpr std::array<RGB, 3> boostRingRamp {{ { 255, 232, 176 }, { 255, 170, 60 }, { 255, 104, 14 } }};
constexpr std::array<RGB, 3> cutRingRamp   {{ { 255, 200, 186 }, { 235, 70, 62 }, { 186, 16, 22 } }};

RGB colourForFrequency (float t)
{
    size_t i = 0;
    while (i < frequencyColours.size() - 2 && t > frequencyColours[i + 1].t)
        ++i;

    const auto& a = frequencyColours[i];
    const auto& b = frequencyColours[i + 1];
    // Eased, so each range holds its own colour and changes over between ranges.
    const float along = std::clamp ((t - a.t) / (b.t - a.t), 0.0f, 1.0f);
    const auto c = mixRGB (a.rgb, b.rgb, along * along * (3.0f - 2.0f * along));
    const float y = std::sin ((t * 2.0f - 1.0f) * halfPi);
    const float k = 1.0f - (1.0f - poleDim) * y * y;
    return { c.r * k, c.g * k, c.b * k };
}

SpriteStyle makeStyle (RGB c, float profile)
{
    return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, profile };
}

int frequencyColourOf (float y)
{
    return (int) std::lround ((std::asin (std::clamp (y, -1.0f, 1.0f)) / pi + 0.5f) * (numFrequencyColours - 1));
}

float powerToLevel (float power)
{
    const float db = 10.0f * std::log10 (power + 1.0e-20f);
    return std::clamp ((db + 80.0f) / 60.0f, 0.0f, 1.0f);
}

// Spectrum smoothing (s): quick enough to follow drums, slow enough to average
// out the frame-to-frame noise of the analysis.
constexpr float spectrumAttack = 0.035f;
constexpr float spectrumRelease = 0.24f;
constexpr float hazeAttack = 0.25f, hazeRelease = 0.9f; // seconds
constexpr std::array<float, 5> blurTaps { 0.06f, 0.24f, 0.4f, 0.24f, 0.06f };

constexpr float springOmega = 2.0f * pi * 4.5f;
constexpr float springDamping = 0.42f;
constexpr float springStep = 1.0f / 120.0f;


// Audio, on a fixed scale in level units (60 dB = 1).
constexpr float levelRangeDb = 60.0f;
constexpr float loudFloor = 0.2f, loudWidth = 0.6f;  // -68 dB to -32 dB per band
constexpr float quietAlpha = 0.4f, loudAlpha = 1.5f, quietSize = 0.75f, loudSize = 1.75f;

// How much of a dot's colour the EQ gets: full at this many dB of change, on
// signal at least this loud coming in.
constexpr float tintFullDb = 12.0f;
constexpr float tintFloor = 0.25f, tintWidth = 0.3f;

constexpr int vortexRings = 6;
constexpr int beltRingsPerSide = 3;
constexpr int vortexPoints = 56;
constexpr std::array<float, 7> cutContoursDb { 1.5f, 3, 6, 12, 18, 24, 30 };
constexpr float cutFloorRingSpacing = 0.26f;
constexpr float cutFloorRingAlpha = 0.3f;
constexpr float cutRingPoleLimit = halfPi - 0.08f;

float randomFloat()
{
    static thread_local juce::Random random;
    return random.nextFloat();
}
}

SphereField::SphereField()
{
    vortexAmount.fill (-1.0f);
    buildParticles();
    buildStyles();

    for (int c = 0; c < lonCells; ++c)
    {
        const float lon = ((float) c / lonCells) * twoPi - pi;
        const float phi = pi / 4.0f + lon / 2.0f;
        const float cp = std::cos (phi), sp = std::sin (phi);
        cellLL[(size_t) c] = cp * cp;
        cellRR[(size_t) c] = sp * sp;
        cellLR[(size_t) c] = 2.0f * cp * sp;
    }
}

void SphereField::buildParticles()
{
    numParticles = firstExtraPoint + extraPoints;
    const auto n = (size_t) numParticles;

    for (auto* v : { &pX, &pY, &pZ, &pShell, &pSize, &pTwinkle, &pTwinkleRate, &pMid, &pFreq, &pCosLon, &pSinLon,
                     &specPos, &lonPos, &springOffset, &springVelocity })
        v->assign (n, 0.0f);

    pColour.assign (n, 0);
    drawOrder.assign (n, 0);
    std::fill (springOffset.begin() + firstExtraPoint, springOffset.end(), springUnset);

    const float goldenAngle = pi * (3.0f - std::sqrt (5.0f));
    const float sizeScale = std::sqrt (5000.0f / surfacePoints);
    // Fibonacci lattice, jittered so its spiral rows don't show as moire stripes.
    const float spacing = std::sqrt (4.0f * pi / surfacePoints);
    auto jitter = [spacing] { return (randomFloat() - 0.5f) * spacing * 0.7f; };

    for (int i = 0; i < numParticles; ++i)
    {
        const auto s = (size_t) i;
        float x, y, z, shell;

        if (i < surfacePoints || i >= firstExtraPoint)
        {
            float y0, lon0;

            if (i < surfacePoints)
            {
                y0 = 1.0f - (((float) i + 0.5f) / surfacePoints) * 2.0f;
                lon0 = goldenAngle * (float) i;
            }
            else
            {
                // A low-discrepancy sequence: however many are in use, they're evenly spread.
                const auto k = (double) (i - firstExtraPoint);
                y0 = 2.0f * (float) std::fmod (0.5 + k * 0.7548776662, 1.0) - 1.0f;
                lon0 = twoPi * (float) std::fmod (0.5 + k * 0.5698402910, 1.0);
            }

            const float lat = std::clamp (std::asin (y0) + jitter(), -halfPi, halfPi);
            const float r = std::cos (lat);
            const float theta = lon0 + jitter() / std::max (r, 0.05f);
            x = std::cos (theta) * r;
            y = std::sin (lat);
            z = std::sin (theta) * r;
            shell = 1.0f + (randomFloat() - 0.5f) * 0.02f;
            pSize[s] = (0.5f + randomFloat()) * sizeScale;
            pTwinkleRate[s] = 0.6f + randomFloat() * 1.4f;
        }
        else
        {
            // Sparse haze just inside / outside the shell.
            const float u = randomFloat() * 2.0f - 1.0f;
            const float theta = randomFloat() * twoPi;
            const float r = std::sqrt (1.0f - u * u);
            x = r * std::cos (theta);
            y = u;
            z = r * std::sin (theta);
            shell = 0.85f + randomFloat() * 0.3f;
            pSize[s] = (0.3f + randomFloat() * 0.6f) * sizeScale;
            pTwinkleRate[s] = 0.4f + randomFloat() * 0.8f;
        }

        pX[s] = x;
        pY[s] = y;
        pZ[s] = z;
        pShell[s] = shell;
        pColour[s] = frequencyColourOf (y);
        pTwinkle[s] = randomFloat() * twoPi;

        // Mid (centre pan) meridian: lon 0 is the x = 0, z > 0 half of the sphere.
        const float w = std::exp (-(x * x) / (midLineWidth * midLineWidth));
        pMid[s] = i < firstExtraPoint && z > 0 && shell > 0.99f && shell < 1.01f && w > 0.05f ? w : 0.0f;

        const float lat = std::asin (std::clamp (y, -1.0f, 1.0f));
        const float lon = std::atan2 (x, z);
        pFreq[s] = latToFreq (lat);
        pCosLon[s] = std::cos (lon);
        pSinLon[s] = std::sin (lon);

        const float t = (lat / halfPi + 1.0f) / 2.0f;
        specPos[s] = std::clamp (t * numSpectrumBands - 0.5f, 0.0f, (float) numSpectrumBands - 1.0f);
        lonPos[s] = ((lon + pi) / twoPi) * lonCells;
    }
}

void SphereField::buildStyles()
{
    for (int f = 0; f < numFrequencyColours; ++f)
    {
        const auto base = colourForFrequency ((float) f / (numFrequencyColours - 1));
        particleStyles.push_back (makeStyle (base, 0.0f));

        for (const auto target : { boostColour, cutColour })
            for (int i = 1; i <= tintSteps; ++i)
                particleStyles.push_back (makeStyle (mixRGB (base, target, (float) i / tintSteps), 0.0f));
    }

    midStyle = makeStyle ({ 255, 252, 244 }, 1.0f);

    auto rampColour = [] (const std::array<RGB, 3>& ramp, float t)
    {
        return t < 0.5f ? mixRGB (ramp[0], ramp[1], t * 2.0f) : mixRGB (ramp[1], ramp[2], t * 2.0f - 1.0f);
    };

    for (int i = 0; i < numRingColours; ++i)
    {
        const float t = (float) i / (numRingColours - 1);
        boostRingStyles.push_back (makeStyle (rampColour (boostRingRamp, t), 1.0f));
        cutRingStyles.push_back (makeStyle (rampColour (cutRingRamp, t), 1.0f));
    }
}

// The curve opens up the everyday range, so 3 dB and 9 dB look different as
// well as 12 dB and 30 dB.
const SpriteStyle& SphereField::ringStyle (float db) const
{
    const auto index = (size_t) std::lround (std::pow (std::min (1.0f, std::abs (db) / maxGainDb), 0.6f) * (numRingColours - 1));
    return db > 0 ? boostRingStyles[index] : cutRingStyles[index];
}

void SphereField::updateSpectrum (const StereoSpectrum::Result& spectrum, float dt)
{
    const float attack = 1.0f - std::exp (-dt / spectrumAttack);
    const float release = 1.0f - std::exp (-dt / spectrumRelease);
    float peak = 0;

    for (size_t b = 0; b < (size_t) numSpectrumBands; ++b)
    {
        // One coefficient for ll, rr and lr together, so lr can't outgrow them.
        const float k = spectrum.ll[b] + spectrum.rr[b] > heldLL[b] + heldRR[b] ? attack : release;
        heldLL[b] += (spectrum.ll[b] - heldLL[b]) * k;
        heldRR[b] += (spectrum.rr[b] - heldRR[b]) * k;
        heldLR[b] += (spectrum.lr[b] - heldLR[b]) * k;
        peak = std::max ({ peak, heldLL[b], heldRR[b] });
    }

    // Blur across frequency so single harmonics don't show up as rings on the sphere.
    auto blur = [] (const auto& src, auto& dst)
    {
        for (int b = 0; b < numSpectrumBands; ++b)
        {
            float sum = 0;
            for (int j = -2; j <= 2; ++j)
                sum += blurTaps[(size_t) (j + 2)] * src[(size_t) std::clamp (b + j, 0, numSpectrumBands - 1)];
            dst[(size_t) b] = sum;
        }
    };

    blur (heldLL, smoothLL);
    blur (heldRR, smoothRR);
    blur (heldLR, smoothLR);

    const float target = powerToLevel (peak) > 0.02f ? 1.0f : 0.0f;
    audioMix += (target - audioMix) * (1.0f - std::pow (0.94f, dt * 60.0f));

    // The backdrop's glow: the loudest band in each third of the spectrum, on
    // the same fixed scale as the dots, but slow enough to read as weather.
    for (size_t zone = 0; zone < hazeLevels.size(); ++zone)
    {
        float loudest = 0;

        for (int b = (int) zone * numSpectrumBands / 3; b < ((int) zone + 1) * numSpectrumBands / 3; ++b)
            loudest = std::max (loudest, (smoothLL[(size_t) b] + smoothRR[(size_t) b]) / 2.0f);

        const float level = std::clamp ((powerToLevel (loudest) - loudFloor) / loudWidth, 0.0f, 1.0f);
        const float time = level > hazeLevels[zone] ? hazeAttack : hazeRelease;
        hazeLevels[zone] += (level - hazeLevels[zone]) * (1.0f - std::exp (-dt / time));
    }
}

void SphereField::updateLevelTable()
{
    for (size_t b = 0; b < (size_t) numSpectrumBands; ++b)
        for (size_t c = 0; c < (size_t) lonCells; ++c)
            levelTable[b * lonCells + c] = powerToLevel (std::max (0.0f, cellLL[c] * smoothLL[b] + cellRR[c] * smoothRR[b] + cellLR[c] * smoothLR[b]));
}

// Particle's level in a band, interpolated around in longitude. Longitude -pi
// and +pi are both the back (Side), so the grid wraps.
float SphereField::levelInBand (int particle, int band) const
{
    const auto row = (size_t) std::clamp (band, 0, numSpectrumBands - 1) * lonCells;
    const float x = lonPos[(size_t) particle];
    const int whole = (int) x;
    const auto c0 = (size_t) (whole % lonCells);
    const auto c1 = (c0 + 1) % lonCells;
    return levelTable[row + c0] + (levelTable[row + c1] - levelTable[row + c0]) * (x - (float) whole);
}

float SphereField::audioLevelAt (int particle) const
{
    const float sp = specPos[(size_t) particle];
    const int b = (int) sp;
    const float t = sp - (float) b;
    const float p0 = levelInBand (particle, b - 1), p1 = levelInBand (particle, b);
    const float p2 = levelInBand (particle, b + 1), p3 = levelInBand (particle, b + 2);
    const float v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    return std::clamp (v, 0.0f, 1.0f);
}

void SphereField::addRingDot (Vec3 dir, const BandSet& bands, const BandDirections& dirs, const Camera& camera,
                              const SpriteStyle& style, float alpha, SpriteList& out) const
{
    const float r = 1.0f + surfaceDisplacement (bands, dirs, dir);
    const auto s = camera.toScreen (camera.toView ({ dir.x * r, dir.y * r, dir.z * r }));
    const float facing = 0.2f + 0.8f * std::clamp (s.z + 0.5f, 0.0f, 1.0f);
    const float size = camera.radiusPx() * 0.05f * 1.2f * (cameraDistance / (cameraDistance - s.z));
    out.add (style, s.x, s.y, size, facing * alpha);
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
    const float radiansPerOctave = pi / std::log2 (threedq::maxFreq / threedq::minFreq);
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
        const auto& style = ringStyle (gain * std::exp (-2.0f * (1.0f - std::cos (angle)) / (w * w)));
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

    for (int k = 0; k < vortexRings; ++k)
    {
        const float t = (float) (k + 1) / vortexRings;
        const float alpha = t * w * 1.5f; // angular radius of this ring
        const float sa = std::sin (alpha), ca = std::cos (alpha);
        const float phase = time * spin / (0.35f + t) + t * 2.4f; // inner rings turn faster; the offset makes a spiral
        const float ringFade = strength * (1.15f - t * 0.75f);
        const auto& style = ringStyle (gain * std::exp (-2.0f * (1.0f - ca) / (w * w))); // the bump's own dB at this ring

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

void SphereField::update (const BandSet& bands, const Camera& camera, const StereoSpectrum::Result& spectrum,
                          double timeSeconds, float dt, SpriteList& out)
{
    // The ring phases only need the time within a long cycle; keeping it small
    // keeps float precision for a session that runs for days.
    const float time = (float) std::fmod (timeSeconds, 3600.0);

    updateSpectrum (spectrum, dt);
    const bool reactive = audioMix > 0.001f;
    if (reactive)
        updateLevelTable();

    const int steps = (int) std::ceil (dt / springStep);
    const float stepDt = dt / (float) steps;
    const float kSpring = springOmega * springOmega;
    const float cSpring = 2.0f * springDamping * springOmega;

    // Bands, prepared once per frame. Muted bands drop out.
    std::array<float, maxBands> bX, bY, bZ, bAmp, bInvW2, cFc, cTwoN, cCos, cSin;
    std::array<float, maxBands> sFc, sA, sK, sCos, sSin, cBias, sBias;
    std::array<float, maxBands> rSin, rCos, rAmp, rInvW2;
    std::array<bool, maxBands> cLow, sLow;
    int nb = 0, nc = 0, ns = 0, nr = 0;

    for (const auto& b : bands)
    {
        if (b.muted)
            continue;

        if (b.isCut())
        {
            const auto i = (size_t) nc++;
            cLow[i] = b.type == FilterType::lowCut;
            cFc[i] = latToFreq (b.lat);
            cTwoN[i] = 2.0f * (float) b.slope / 6.0f;
            cCos[i] = b.stereo ? 0.0f : std::cos (b.lon);
            cSin[i] = b.stereo ? 0.0f : std::sin (b.lon);
            cBias[i] = b.stereo ? 2.0f : 1.0f; // makes the overlap below 1 everywhere
            continue;
        }

        if (b.isShelf())
        {
            const auto i = (size_t) ns++;
            sLow[i] = b.type == FilterType::lowShelf;
            sFc[i] = latToFreq (b.lat);
            sA[i] = shelfA (b.displayGain());
            sK[i] = shelfK (sA[i], b.q);
            sCos[i] = b.stereo ? 0.0f : std::cos (b.lon);
            sSin[i] = b.stereo ? 0.0f : std::sin (b.lon);
            sBias[i] = b.stereo ? 2.0f : 1.0f;
            continue;
        }

        if (b.stereo)
        {
            const auto i = (size_t) nr++;
            const float w = bandWidth (b);
            rSin[i] = std::sin (b.lat);
            rCos[i] = std::cos (b.lat);
            rAmp[i] = gainDisplacement (b.displayGain());
            rInvW2[i] = 1.0f / (w * w);
            continue;
        }

        const auto i = (size_t) nb++;
        const auto d = bandDirection (b);
        const float w = bandWidth (b);
        bX[i] = d.x;
        bY[i] = d.y;
        bZ[i] = d.z;
        bAmp[i] = gainDisplacement (b.displayGain());
        bInvW2[i] = 1.0f / (w * w);
    }

    const float cx = camera.centreX(), cy = camera.centreY(), rPx = camera.radiusPx();
    const float cosYaw = camera.cosYaw, sinYaw = camera.sinYaw, cosPitch = camera.cosPitch, sinPitch = camera.sinPitch;
    const float sizeBase = particleSize * rPx * 0.05f;

    auto towardsViewer = [&] (size_t s) { return pY[s] * sinPitch + (pZ[s] * cosYaw - pX[s] * sinYaw) * cosPitch; };

    int backCount = 0, frontStart = firstExtraPoint, drawCount = firstExtraPoint;

    for (int i = 0; i < firstExtraPoint; ++i)
        drawOrder[(size_t) (towardsViewer ((size_t) i) < 0 ? backCount++ : --frontStart)] = i;

    // Only the front half gets extras: that's the surface you see, at half the cost.
    const float wantedExtras = surfacePoints * (std::pow (rPx / extraStartRadiusPx, 1.6f) - 1.0f);

    for (int i = firstExtraPoint; i < numParticles; ++i)
    {
        if ((float) (i - firstExtraPoint) < wantedExtras && towardsViewer ((size_t) i) > 0)
            drawOrder[(size_t) drawCount++] = i;
        else
            springOffset[(size_t) i] = springUnset;
    }

    for (int k = 0; k < drawCount; ++k)
    {
        const int i = drawOrder[(size_t) k];
        const auto n = (size_t) i;
        const float dx = pX[n], dy = pY[n], dz = pZ[n];

        // EQ displacement: Gaussian bumps by squared chord distance.
        float disp = 0;

        for (size_t k = 0; k < (size_t) nb; ++k)
        {
            const float e = 2.0f * (1.0f - (dx * bX[k] + dy * bY[k] + dz * bZ[k])) * bInvW2[k];
            if (e < 12.0f)
                disp += bAmp[k] * std::exp (-e);
        }

        // Stereo peaks: the same bump, measured in latitude only.
        if (nr > 0)
        {
            const float cosLat = std::sqrt (std::max (0.0f, 1.0f - dy * dy));

            for (size_t k = 0; k < (size_t) nr; ++k)
            {
                const float e = 2.0f * (1.0f - (cosLat * rCos[k] + dy * rSin[k])) * rInvW2[k];
                if (e < 12.0f)
                    disp += rAmp[k] * std::exp (-e);
            }
        }

        // Cuts: Butterworth response at this frequency, weighted by stereo overlap.
        for (size_t k = 0; k < (size_t) nc; ++k)
        {
            const float ratio = cLow[k] ? cFc[k] / pFreq[n] : pFreq[n] / cFc[k];
            const float pw = std::pow (ratio, cTwoN[k]);
            if (pw < 1.0e-4f)
                continue;

            const float db = -10.0f * std::log10 (1.0f + pw);
            const float t = std::min (1.0f, -db / maxGainDb);
            const float overlap = (cBias[k] + pCosLon[n] * cCos[k] + pSinLon[n] * cSin[k]) / 2.0f;
            disp -= std::pow (t, 0.7f) * maxCutDisplacement * overlap;
        }

        // Shelves: their response at this frequency, weighted by stereo overlap.
        for (size_t k = 0; k < (size_t) ns; ++k)
        {
            const float ratio = pFreq[n] / sFc[k];
            const float overlap = (sBias[k] + pCosLon[n] * sCos[k] + pSinLon[n] * sSin[k]) / 2.0f;
            disp += gainDisplacement (shelfDbAt (sLow[k], sA[k], sK[k], ratio * ratio)) * overlap;
        }

        if (disp > maxBoostDisplacement || disp < -maxCutDisplacement)
            disp = limitDisplacement (disp);

        const float heardLevel = reactive ? audioLevelAt (i) : 0.0f;
        const float loud = std::clamp ((heardLevel - loudFloor) / loudWidth, 0.0f, 1.0f);

        // Boost or cut here in dB: the inverse of the shape curve. The analysis
        // hears the EQ's output, so its change is taken back out to find what
        // came in; a deep cut on a loud sound still counts.
        float tint = 0;

        if (reactive && disp != 0)
        {
            const float db = maxGainDb * std::pow (std::min (1.0f, std::abs (disp) / (disp > 0 ? maxBoostDisplacement : maxCutDisplacement)), 1.0f / 0.7f);
            const float incoming = std::clamp ((heardLevel + (disp > 0 ? -db : db) / levelRangeDb - tintFloor) / tintWidth, 0.0f, 1.0f);
            tint = audioMix * incoming * incoming * (3.0f - 2.0f * incoming) * std::pow (std::min (1.0f, db / tintFullDb), 0.6f);
        }

        // An extra comes in at the rim, already in place.
        if (springOffset[n] == springUnset)
        {
            springOffset[n] = disp;
            springVelocity[n] = 0;
        }

        float offset = springOffset[n], velocity = springVelocity[n];

        if (offset != disp || velocity != 0)
        {
            for (int s = 0; s < steps; ++s)
            {
                velocity += (kSpring * (disp - offset) - cSpring * velocity) * stepDt;
                offset += velocity * stepDt;
            }

            // Settle exactly once the motion is invisible.
            if (std::abs (offset - disp) < 1.0e-5f && std::abs (velocity) < 1.0e-4f)
            {
                offset = disp;
                velocity = 0;
            }

            springOffset[n] = offset;
            springVelocity[n] = velocity;
        }

        const float surface = surfaceRadius (pShell[n] + offset);

        // The floor stops a spring overshoot flipping a point through the centre.
        const float r = std::max (0.05f, surface);
        const float mx = dx * r, my = dy * r, mz = dz * r;
        const float x1 = mx * cosYaw + mz * sinYaw;
        const float z1 = -mx * sinYaw + mz * cosYaw;
        const float vy = my * cosPitch - z1 * sinPitch;
        const float z = my * sinPitch + z1 * cosPitch;
        const float persp = cameraDistance / (cameraDistance - z);
        const float sx = cx + x1 * rPx * persp;
        const float sy = cy - vy * rPx * persp;

        // The far side fades and shrinks so the front reads clearly through it.
        const float front = std::clamp ((z + 0.7f) / 1.4f, 0.0f, 1.0f);
        const float depth = front * front * (3.0f - 2.0f * front);
        const float facing = 0.1f + 0.9f * depth;
        const float lit = lightFloor + lightAmount * std::max (0.0f, (x1 * lightX + vy * lightY + z * lightZ) / r);
        const float twinkle = 0.88f + 0.12f * std::sin (pTwinkle[n] + time * pTwinkleRate[n]);
        const float audioGain = 1.0f + audioMix * (quietAlpha + (loudAlpha - quietAlpha) * loud - 1.0f);
        const float audioSize = 1.0f + audioMix * (quietSize + (loudSize - quietSize) * loud - 1.0f);
        const float size = sizeBase * pSize[n] * persp * audioSize * (0.75f + 0.25f * depth);
        const int step = (int) std::lround (tint * tintSteps);
        const int style = pColour[n] * numTints + (step == 0 ? 0 : (disp > 0 ? step : step + tintSteps));

        float extraFade = 1;

        if (i >= firstExtraPoint)
            extraFade = std::min (1.0f, (wantedExtras - (float) (i - firstExtraPoint)) / extraFadeCount)
                      * std::min (1.0f, towardsViewer (n) / extraEdgeFade);

        // A heavily cut dot has lost its audio but still needs to show its colour.
        out.add (particleStyles[(size_t) style], sx, sy, size,
                 extraFade * particleAlpha * facing * lit * twinkle * std::max (audioGain, 0.35f + 0.6f * tint));

        if (pMid[n] > 0)
            out.add (midStyle, sx, sy, size * 1.1f, facing * pMid[n] * 0.5f);
    }

    const auto dirs = directionsOf (bands);
    for (const auto& b : bands)
        addVortex (b, bands, dirs, camera, time, dt, out);
}

} // namespace sphere
