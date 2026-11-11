/*
  ==============================================================================

    SphereField: see sphere_field.h.

  ==============================================================================
*/

#include "sphere_field.h"

namespace threedq
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

// The affected-audio layer: how far it leaves the surface at full strength (in
// sphere radii), how quickly it follows the music (s), and how bright it is.
constexpr float layerTravelOut = 0.24f, layerTravelIn = 0.36f; // a cut sinks further, to show through the dent
constexpr float layerAttack = 0.06f, layerRelease = 0.5f;
constexpr float layerAlpha = 1.1f;
constexpr float layerShown = 0.015f; // weaker than this, it isn't drawn

// The sidechain's ghost shell: how far outside the surface it floats (sphere
// radii; louder floats further), how faint it is alone, and how bright where
// it meets loud audio on this track.
constexpr float ghostLift = 0.07f, ghostTravel = 0.05f;
constexpr float ghostAlpha = 0.22f, maskAlpha = 0.95f;
constexpr float maskShown = 0.3f; // clash past this draws in the mask colour

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
                     &specPos, &lonPos, &springOffset, &springVelocity, &layerLift })
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
    // Cool and pale, so the ghost never reads as the track's own spectrum, or as boost or cut.
    ghostStyle = makeStyle ({ 160, 182, 255 }, 0.0f);
    maskStyle = makeStyle ({ 236, 242, 255 }, 0.0f);

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

float SphereField::holdSpectrum (const StereoSpectrum::Result& spectrum, float dt, HeldSpectrum& held) const
{
    const float attack = 1.0f - std::exp (-dt / spectrumAttack);
    const float release = 1.0f - std::exp (-dt / spectrumRelease);
    float peak = 0;
    auto& heldLL = held.heldLL;
    auto& heldRR = held.heldRR;
    auto& heldLR = held.heldLR;

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

    blur (heldLL, held.smoothLL);
    blur (heldRR, held.smoothRR);
    blur (heldLR, held.smoothLR);
    return peak;
}

void SphereField::updateSpectrum (const StereoSpectrum::Result& spectrum, float dt)
{
    const float peak = holdSpectrum (spectrum, dt, output);
    const auto& smoothLL = output.smoothLL;
    const auto& smoothRR = output.smoothRR;

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

void SphereField::updateLevelTable (HeldSpectrum& held) const
{
    for (size_t b = 0; b < (size_t) numSpectrumBands; ++b)
        for (size_t c = 0; c < (size_t) lonCells; ++c)
            held.levels[b * lonCells + c] = powerToLevel (std::max (0.0f, cellLL[c] * held.smoothLL[b] + cellRR[c] * held.smoothRR[b]
                                                                          + cellLR[c] * held.smoothLR[b]));
}

// Particle's level in a band, interpolated around in longitude. Longitude -pi
// and +pi are both the back (Side), so the grid wraps.
float SphereField::levelInBand (const HeldSpectrum& held, int particle, int band) const
{
    const auto& levelTable = held.levels;
    const auto row = (size_t) std::clamp (band, 0, numSpectrumBands - 1) * lonCells;
    const float x = lonPos[(size_t) particle];
    const int whole = (int) x;
    const auto c0 = (size_t) (whole % lonCells);
    const auto c1 = (c0 + 1) % lonCells;
    return levelTable[row + c0] + (levelTable[row + c1] - levelTable[row + c0]) * (x - (float) whole);
}

float SphereField::audioLevelAt (const HeldSpectrum& held, int particle) const
{
    const float sp = specPos[(size_t) particle];
    const int b = (int) sp;
    const float t = sp - (float) b;
    const float p0 = levelInBand (held, particle, b - 1), p1 = levelInBand (held, particle, b);
    const float p2 = levelInBand (held, particle, b + 1), p3 = levelInBand (held, particle, b + 2);
    const float v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    return std::clamp (v, 0.0f, 1.0f);
}

void SphereField::update (const BandSet& bands, const Camera& camera, const StereoSpectrum::Result& spectrum,
                          const StereoSpectrum::Result* sidechainSpectrum, double timeSeconds, float dt, SpriteList& out)
{
    // The ring phases only need the time within a long cycle; keeping it small
    // keeps float precision for a session that runs for days.
    const float time = (float) std::fmod (timeSeconds, 3600.0);

    updateSpectrum (spectrum, dt);
    const bool reactive = audioMix > 0.001f;
    if (reactive)
        updateLevelTable (output);

    // The sidechain's ghost: with none connected it lets go as silence would.
    static const StereoSpectrum::Result silence {};
    const float sidechainPeak = holdSpectrum (sidechainSpectrum != nullptr ? *sidechainSpectrum : silence, dt, sidechain);
    const float sidechainTarget = sidechainSpectrum != nullptr && powerToLevel (sidechainPeak) > 0.02f ? 1.0f : 0.0f;
    sidechainMix += (sidechainTarget - sidechainMix) * (1.0f - std::pow (0.94f, dt * 60.0f));
    const bool ghosting = sidechainMix > 0.001f;
    if (ghosting)
        updateLevelTable (sidechain);

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

    struct Projected { float x, y, z, persp, sx, sy; };
    auto project = [&] (float dx, float dy, float dz, float r)
    {
        const float mx = dx * r, my = dy * r, mz = dz * r;
        const float x1 = mx * cosYaw + mz * sinYaw;
        const float z1 = -mx * sinYaw + mz * cosYaw;
        const float vy = my * cosPitch - z1 * sinPitch;
        const float z = my * sinPitch + z1 * cosPitch;
        const float persp = cameraDistance / (cameraDistance - z);
        return Projected { x1, vy, z, persp, cx + x1 * rPx * persp, cy - vy * rPx * persp };
    };

    const float layerAttackK = 1.0f - std::exp (-dt / layerAttack);
    const float layerReleaseK = 1.0f - std::exp (-dt / layerRelease);

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

        const float heardLevel = reactive ? audioLevelAt (output, i) : 0.0f;
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
            layerLift[n] = 0;
        }

        // The layer follows the changed audio: out fast with a hit, back slowly.
        const float layerTarget = disp > 0 ? tint : -tint;
        float lift = layerLift[n];
        if (layerTarget != 0 || lift != 0)
        {
            lift += (layerTarget - lift) * (std::abs (layerTarget) > std::abs (lift) ? layerAttackK : layerReleaseK);
            if (layerTarget == 0 && std::abs (lift) < 1.0e-3f)
                lift = 0;
            layerLift[n] = lift;
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
        const auto view = project (dx, dy, dz, r);
        const float x1 = view.x, vy = view.y, z = view.z, persp = view.persp, sx = view.sx, sy = view.sy;

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

        // The sidechain's ghost shell, over the surface lattice only: faint on
        // its own, bright where this track is loud at the same frequency and
        // stereo position, which is where the two mask each other.
        if (ghosting && i < surfacePoints)
        {
            const float sidechainLoud = std::clamp ((audioLevelAt (sidechain, i) - loudFloor) / loudWidth, 0.0f, 1.0f);

            if (sidechainLoud > 0)
            {
                const float clash = std::min (sidechainLoud, loud);
                const auto gv = project (dx, dy, dz, r + ghostLift + ghostTravel * sidechainLoud);
                const float gFront = std::clamp ((gv.z + 0.7f) / 1.4f, 0.0f, 1.0f);
                const float gDepth = gFront * gFront * (3.0f - 2.0f * gFront);
                out.add (clash > maskShown ? maskStyle : ghostStyle, gv.sx, gv.sy,
                         sizeBase * pSize[n] * gv.persp * (0.6f + 0.7f * clash) * (0.75f + 0.25f * gDepth),
                         sidechainMix * twinkle * (ghostAlpha * sidechainLoud + maskAlpha * clash) * (0.1f + 0.9f * gDepth));
            }
        }

        // The affected audio, as a layer leaving the surface: out where it's
        // boosted, in where it's cut, in pure boost or cut colour. A fainter dot
        // halfway gives the layer depth as it separates.
        if (std::abs (lift) > layerShown)
        {
            const float strength = std::abs (lift);
            const auto& layerStyle = particleStyles[(size_t) (pColour[n] * numTints + (lift > 0 ? tintSteps : 2 * tintSteps))];
            const float baseAlpha = extraFade * particleAlpha * layerAlpha * twinkle * std::min (1.0f, strength * 1.6f);

            for (const auto [along, alpha, scale] : { std::tuple { 1.0f, 1.0f, 0.95f }, std::tuple { 0.5f, 0.35f, 0.8f } })
            {
                const float rl = std::max (0.05f, r + lift * (lift > 0 ? layerTravelOut : layerTravelIn) * along);
                const auto lv = project (dx, dy, dz, rl);
                const float lFront = std::clamp ((lv.z + 0.7f) / 1.4f, 0.0f, 1.0f);
                const float lDepth = lFront * lFront * (3.0f - 2.0f * lFront);
                // Inside the sphere it's seen through the surface, so dimmer.
                const float inside = lift < 0 ? 0.7f : 1.0f;
                out.add (layerStyle, lv.sx, lv.sy, sizeBase * pSize[n] * lv.persp * audioSize * scale * (0.75f + 0.25f * lDepth),
                         baseAlpha * alpha * inside * (0.1f + 0.9f * lDepth));
            }
        }
    }

    const auto dirs = directionsOf (bands);
    for (const auto& b : bands)
    {
        addVortex (b, bands, dirs, camera, time, dt, out);
        addRangeMarker (b, bands, dirs, camera, time, out);
    }
}

} // namespace threedq
