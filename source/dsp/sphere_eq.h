/*
  ==============================================================================

    SphereEq: the 3DQ equaliser DSP. Plain C++ (no JUCE), so it can be tested
    on its own.

    Each band is an RBJ peaking filter that acts on one axis of the stereo
    signal. The band's stereo position (longitude on the sphere, in degrees)
    picks the axis u = (cos φ, sin φ) in the (L, R) plane, φ = 45° + pos/2:

        pos    0°  -> Mid   (L+R)/√2
        pos   90°  -> R
        pos  180°  -> Side  (R-L)/√2
        pos  -90°  -> L

    Per band:  c = u·x,  x' = x + u · (H(c) - c).  Bands run in series.

    Dynamic bands (range != 0): the band listens to its own component c through
    a band-pass at its frequency and Q. When that level goes over the threshold,
    the band's gain moves from its static gain toward static + range (soft knee,
    then the band's ratio above threshold, never more than the range). So a
    negative range is a dynamic cut (e.g. de-essing), a positive range a dynamic
    boost. The detector rises and falls with the band's attack and release.

    A band can instead listen to the sidechain input (externalSidechain): the
    same band-pass, on the same axis, of another track's audio, so e.g. a band
    dips the mix's vocal range only while the vocal plays. With no sidechain
    signal it hears silence and stays at its static gain.

    Cut bands (low cut = high-pass, high cut = low-pass) replace the peaking
    filter with a Butterworth cascade of slope 6..48 dB/oct on the same axis,
    so e.g. a low cut at Side removes lows from the side signal only. Gain,
    Q and dynamics don't apply to cuts.

    Shelf bands are RBJ shelving filters on the same axis, with the band's
    frequency at the half-gain point. They take gain, Q and dynamics like a
    peak; their detector listens to the shelf's side of the spectrum.

    A band normally acts on its one stereo axis. A stereo band ignores its
    position and filters L and R alike, as a conventional EQ does; its detector
    follows the louder channel, so both channels always get the same gain.

    Solo replaces the output with what one band works on, taken from the EQ's
    input so other bands and the band's own gain don't colour it: the band-pass
    at its frequency and Q (a shelf or cut: what it moves or removes), on its
    axis only, or on L and R alike for a stereo band.

  ==============================================================================
*/

#pragma once

#include <atomic>
#include "eq_params.h"
#include "filter_design.h"

namespace threedq::dsp
{

// Axis angle φ in radians. u and -u are the same axis, so φ is only meaningful mod π.
inline double stereoAxisAngle (double posDeg)
{
    return pi / 4.0 + (posDeg * pi / 180.0) / 2.0;
}

class SphereEq
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        // One-pole smoothing applied once per sub-block, ~15 ms time constant.
        smoothingCoeff = 1.0 - std::exp (-(double) subBlockSize / (0.015 * sampleRate));
        for (auto& b : bands)
        {
            b.snapToTarget = true;
            setTiming (b, b.attackMs, b.releaseMs); // the same times at the new rate
        }
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            b.clearState();
            b.envelope = 0.0;
            b.dynamicDb = 0.0;
        }

        solo.mix = solo.mixPrevious = 0.0;
    }

    // Live readouts for the GUI (safe to call from any thread).
    // Current total gain of a band, including its dynamic movement.
    float getCurrentGainDb (int index) const  { return currentGainDb[(size_t) index].load (std::memory_order_relaxed); }
    // Current level of a band's own signal (dBFS), what the threshold compares against.
    float getCurrentLevelDb (int index) const { return currentLevelDb[(size_t) index].load (std::memory_order_relaxed); }

    void setBand (int index, const BandParams& p)
    {
        auto& b = bands[(size_t) index];
        b.targetLogFreq = std::log (std::clamp ((double) p.freq, (double) minFreq, (double) maxFreq));
        b.targetGainDb  = p.enabled ? std::clamp ((double) p.gainDb, (double) -maxGainDb, (double) maxGainDb) : 0.0;
        b.targetLogQ    = std::log (std::clamp ((double) p.q, (double) minQ, (double) maxQ));
        b.targetPhi     = stereoAxisAngle (p.posDeg);
        b.enabled       = p.enabled;
        b.rangeDb       = std::clamp ((double) p.rangeDb, (double) -maxGainDb, (double) maxGainDb);
        b.thresholdDb   = std::clamp ((double) p.thresholdDb, (double) minThresholdDb, (double) maxThresholdDb);
        b.externalSidechain = p.externalSidechain;
        b.ratioSlope = 1.0 - 1.0 / std::clamp ((double) p.ratio, (double) minRatio, (double) maxRatio);

        const double attackMs  = std::clamp ((double) p.attackMs, (double) minAttackMs, (double) maxAttackMs);
        const double releaseMs = std::clamp ((double) p.releaseMs, (double) minReleaseMs, (double) maxReleaseMs);
        if (attackMs != b.attackMs || releaseMs != b.releaseMs)
            setTiming (b, attackMs, releaseMs);

        const int order = ! isCutType (p.type) ? 0 : std::clamp (p.slopeDbPerOct / 6, 1, 2 * maxSections);
        if (p.type != b.type || order != b.order || p.stereo != b.stereo)
        {
            // New filter structure: start it from silence (its old state is
            // meaningless), fading the band back in.
            b.type = p.type;
            b.order = order;
            b.stereo = p.stereo;
            b.clearState();
            b.mix = 0.0;
        }
    }

    // Plays only what this band works on (see the top of this file), or goes back to the EQ.
    void setSolo (const BandParams& p)
    {
        solo.on = true;
        solo.targetLogFreq = std::log (std::clamp ((double) p.freq, (double) minFreq, (double) maxFreq));
        solo.targetLogQ    = std::log (std::clamp ((double) p.q, (double) minQ, (double) maxQ));
        solo.targetPhi     = stereoAxisAngle (p.posDeg);
        solo.type = p.type;
        solo.stereo = p.stereo;
    }

    void clearSolo()    { solo.on = false; }

    // In-place stereo processing. sidechainLeft / Right: the sidechain input for
    // the same samples, or null when there is none (a mono sidechain passes its
    // one channel as both).
    // A mono sidechain passes the same pointer for both sides.
    void process (float* left, float* right, int numSamples,
                  const float* sidechainLeft = nullptr, const float* sidechainRight = nullptr)
    {
        // A mono key has no stereo position, so every band hears all of it;
        // projected onto the Side axis it would cancel to nothing.
        const bool monoKey = sidechainLeft != nullptr && sidechainLeft == sidechainRight;

        for (int start = 0; start < numSamples; start += subBlockSize)
        {
            const int n = std::min (subBlockSize, numSamples - start);
            const bool soloing = solo.on || solo.mix > 0.0;

            if (soloing)
            {
                std::copy_n (left + start, n, solo.inputLeft.begin());
                std::copy_n (right + start, n, solo.inputRight.begin());
            }

            for (size_t bi = 0; bi < bands.size(); ++bi)
            {
                auto& b = bands[bi];

                if (! advance (b))
                {
                    currentGainDb[bi].store (0.0f, std::memory_order_relaxed);
                    currentLevelDb[bi].store (-120.0f, std::memory_order_relaxed);
                    continue;
                }

                const double uL = std::cos (b.phi);
                const double uR = std::sin (b.phi);
                // The detector reads the level per channel, like a stereo band's
                // max(|L|, |R|): a sound on the band's axis reads its louder
                // channel's level, at any position (on Mid, not (L+R)/sqrt2's +3 dB).
                const double perChannel = std::max (std::abs (uL), std::abs (uR));
                const bool dynamic = ! isCutType (b.type) && b.rangeDb != 0.0;
                const int numSections = b.numSections;
                const double mixStart = b.mixPrevious, mixStep = (b.mix - b.mixPrevious) / n;

                for (int i = start; i < start + n; ++i)
                {
                    // mix ramps the band in / out (cuts; peaks fade their gain instead).
                    const double wetAmount = mixStart + mixStep * (i - start + 1);

                    // What the detector hears: the band's own input, or the sidechain.
                    double detectL = left[i], detectR = right[i];
                    if (b.externalSidechain)
                    {
                        detectL = sidechainLeft != nullptr ? (double) sidechainLeft[i] : 0.0;
                        detectR = sidechainRight != nullptr ? (double) sidechainRight[i] : 0.0;
                    }

                    if (b.stereo)
                    {
                        const double inL = left[i], inR = right[i];

                        if (dynamic)
                        {
                            const double scL = runSection (b.detector, b.detector.z1, b.detector.z2, detectL);
                            const double scR = runSection (b.detector, b.detector.zr1, b.detector.zr2, detectR);
                            followEnvelope (b, std::max (std::abs (scL), std::abs (scR)));
                        }

                        double yL = inL, yR = inR;
                        for (int k = 0; k < numSections; ++k)
                        {
                            auto& f = b.sections[(size_t) k];
                            yL = runSection (f, f.z1, f.z2, yL);
                            yR = runSection (f, f.zr1, f.zr2, yR);
                        }

                        left[i]  = (float) (inL + (yL - inL) * wetAmount);
                        right[i] = (float) (inR + (yR - inR) * wetAmount);
                        continue;
                    }

                    const double c = uL * left[i] + uR * right[i];

                    if (dynamic)
                    {
                        const double heard = b.externalSidechain && monoKey ? detectL : (uL * detectL + uR * detectR) * perChannel;
                        followEnvelope (b, std::abs (runSection (b.detector, b.detector.z1, b.detector.z2, heard)));
                    }

                    double y = c;
                    for (int k = 0; k < numSections; ++k)
                    {
                        auto& f = b.sections[(size_t) k];
                        y = runSection (f, f.z1, f.z2, y);
                    }

                    const double d = (y - c) * wetAmount;
                    left[i]  = (float) (left[i]  + uL * d);
                    right[i] = (float) (right[i] + uR * d);
                }

                const double levelDb = 20.0 * std::log10 (b.envelope + 1.0e-9);
                b.mixPrevious = b.mix;
                currentGainDb[bi].store ((float) (b.gainDb + b.dynamicDb), std::memory_order_relaxed);
                currentLevelDb[bi].store ((float) levelDb, std::memory_order_relaxed);
            }

            if (soloing)
                applySolo (left + start, right + start, n);
        }
    }

private:
    static constexpr int subBlockSize = 16;
    static constexpr double kneeDb = 6.0;   // soft knee width around the threshold

    // Gain movement (dB) for a detector level: 0 below threshold, then
    // towards the range with a soft knee, never past it. slope: dB of gain
    // change per dB over the threshold (1 - 1 / ratio).
    static double dynamicGainFor (double levelDb, double thresholdDb, double rangeDb, double slope)
    {
        const double over = levelDb - thresholdDb;
        double soft;
        if (over <= -kneeDb / 2)      soft = 0.0;
        else if (over >= kneeDb / 2)  soft = over;
        else                          soft = (over + kneeDb / 2) * (over + kneeDb / 2) / (2.0 * kneeDb);
        const double amount = std::min (std::abs (rangeDb), soft * slope);
        return rangeDb < 0 ? -amount : amount;
    }

    struct Band
    {
        bool enabled = false;
        bool snapToTarget = true;
        double targetLogFreq = std::log (1000.0), targetGainDb = 0.0, targetLogQ = 0.0, targetPhi = pi / 4.0;
        double logFreq = targetLogFreq, gainDb = 0.0, logQ = 0.0, phi = targetPhi;
        FilterType type = FilterType::peak;
        int order = 0; // cuts: Butterworth order (slope / 6)
        bool stereo = false;
        bool externalSidechain = false;

        std::array<Biquad, maxSections> sections {};
        int numSections = 1;
        double mix = 0.0, mixPrevious = 0.0; // 0..1 wet amount (ramped per sample)

        Biquad detector; // band-pass the dynamics listen through

        void clearState()
        {
            for (auto& f : sections) f.clear();
            detector.clear();
        }

        // Dynamics
        double rangeDb = 0.0, thresholdDb = -30.0;
        double ratioSlope = 0.5; // dB of gain change per dB over threshold, from the ratio (2:1)
        double attackMs = defaultAttackMs, releaseMs = defaultReleaseMs; // what the coefficients were made for
        double attackCoeff = 1.0, releaseCoeff = 1.0;
        double envelope = 0.0;  // side-chain peak level (linear)
        double dynamicDb = 0.0; // current dynamic gain movement
    };

    struct Solo
    {
        bool on = false, stereo = false;
        FilterType type = FilterType::peak;
        double targetLogFreq = 0.0, targetLogQ = 0.0, targetPhi = 0.0;
        double logFreq = 0.0, logQ = 0.0, phi = 0.0;
        double mix = 0.0, mixPrevious = 0.0; // 0 = the EQ's output, 1 = the solo
        Biquad filter;
        std::array<float, subBlockSize> inputLeft {}, inputRight {}; // the EQ's input, this sub-block
    };

    // Per-sample one-pole coefficients for the detector's attack and release.
    void setTiming (Band& b, double attackMs, double releaseMs) const
    {
        b.attackMs = attackMs;
        b.releaseMs = releaseMs;
        b.attackCoeff  = 1.0 - std::exp (-1.0 / (attackMs * 0.001 * sampleRate));
        b.releaseCoeff = 1.0 - std::exp (-1.0 / (releaseMs * 0.001 * sampleRate));
    }

    void followEnvelope (Band& b, double level) const
    {
        b.envelope += (level - b.envelope) * (level > b.envelope ? b.attackCoeff : b.releaseCoeff);
    }

    // Moves a band's smoothed parameters one sub-block toward their targets and
    // refreshes its coefficients. Returns false if the band is silent (skip it).
    bool advance (Band& b)
    {
        if (b.snapToTarget)
        {
            b.logFreq = b.targetLogFreq;
            b.logQ    = b.targetLogQ;
            b.phi     = b.targetPhi;
            b.gainDb  = 0.0; // fade in from flat to avoid a click
            b.mix = b.mixPrevious = isCutType (b.type) ? 0.0 : 1.0; // cuts fade in by mix
            b.snapToTarget = false;
        }

        // Removed bands fade out (peaks and shelves: gain to 0 dB, cuts: mix to 0) before going idle.
        const bool faded = isCutType (b.type)
                             ? b.mix < 1.0e-3
                             : std::abs (b.gainDb) < 1.0e-3 && std::abs (b.dynamicDb) < 1.0e-3;
        if (! b.enabled && faded)
        {
            b.gainDb = 0.0;
            b.dynamicDb = 0.0;
            b.envelope = 0.0;
            b.mix = b.mixPrevious = 0.0;
            b.clearState();
            b.snapToTarget = true; // a re-enabled band starts where it's placed
            return false;
        }

        // While a band fades out, hold its shape and only ramp the gain.
        if (b.enabled)
        {
            b.logFreq += (b.targetLogFreq - b.logFreq) * smoothingCoeff;
            b.logQ    += (b.targetLogQ    - b.logQ)    * smoothingCoeff;

            // Shortest way round, treating φ and φ+π as the same axis.
            double dPhi = std::remainder (b.targetPhi - b.phi, pi);
            b.phi += dPhi * smoothingCoeff;
        }

        b.gainDb += (b.targetGainDb - b.gainDb) * smoothingCoeff;
        const double mixTarget = isCutType (b.type) && ! b.enabled ? 0.0 : 1.0;
        b.mix += (mixTarget - b.mix) * smoothingCoeff;

        // Dynamic movement from the detector (it has its own attack/release);
        // a disabled band, or one whose range is removed, eases back to static.
        const double dynamicTarget = (b.enabled && b.rangeDb != 0.0)
                                        ? dynamicGainFor (20.0 * std::log10 (b.envelope + 1.0e-9), b.thresholdDb, b.rangeDb, b.ratioSlope)
                                        : 0.0;
        if (b.rangeDb == 0.0)
            b.envelope = 0.0;
        b.dynamicDb += (dynamicTarget - b.dynamicDb) * (b.enabled && b.rangeDb != 0.0 ? 1.0 : smoothingCoeff);

        updateCoefficients (b);
        return true;
    }

    // Crossfades this sub-block toward the solo, or back to the EQ's output.
    void applySolo (float* left, float* right, int n)
    {
        if (solo.mix <= 0.0)
        {
            // Starting from silence: no glide, and nothing left over from last time.
            solo.logFreq = solo.targetLogFreq;
            solo.logQ    = solo.targetLogQ;
            solo.phi     = solo.targetPhi;
            solo.filter.clear();
        }

        solo.logFreq += (solo.targetLogFreq - solo.logFreq) * smoothingCoeff;
        solo.logQ    += (solo.targetLogQ    - solo.logQ)    * smoothingCoeff;
        solo.phi     += std::remainder (solo.targetPhi - solo.phi, pi) * smoothingCoeff;
        solo.mix     += ((solo.on ? 1.0 : 0.0) - solo.mix) * smoothingCoeff;

        if (! solo.on && solo.mix < 1.0e-3)
            solo.mix = 0.0;

        auto& f = solo.filter;
        designListenFilter (f, solo.type, std::min (std::exp (solo.logFreq), 0.49 * sampleRate), std::exp (solo.logQ), sampleRate);

        const double uL = std::cos (solo.phi), uR = std::sin (solo.phi);
        const double mixStep = (solo.mix - solo.mixPrevious) / n;

        for (int i = 0; i < n; ++i)
        {
            const double inL = solo.inputLeft[(size_t) i], inR = solo.inputRight[(size_t) i];
            double outL, outR;

            if (solo.stereo)
            {
                outL = runSection (f, f.z1, f.z2, inL);
                outR = runSection (f, f.zr1, f.zr2, inR);
            }
            else
            {
                const double y = runSection (f, f.z1, f.z2, uL * inL + uR * inR);
                outL = uL * y;
                outR = uR * y;
            }

            const double wet = solo.mixPrevious + mixStep * (i + 1);
            left[i]  = (float) (left[i]  + (outL - left[i])  * wet);
            right[i] = (float) (right[i] + (outR - right[i]) * wet);
        }

        solo.mixPrevious = solo.mix;
    }

    // Filter coefficients for the band's current (smoothed) settings.
    void updateCoefficients (Band& b) const
    {
        const double freq = std::min (std::exp (b.logFreq), 0.49 * sampleRate);
        const double q    = std::exp (b.logQ);

        designListenFilter (b.detector, b.type, freq, q, sampleRate);

        if (isCutType (b.type))
        {
            b.numSections = designCut (b.sections, b.type == FilterType::lowCut, b.order, freq, sampleRate); // a low cut is a high-pass
            return;
        }

        const double totalDb = std::clamp (b.gainDb + b.dynamicDb, (double) -maxGainDb, (double) maxGainDb);

        if (b.type == FilterType::peak)
            designPeak (b.sections[0], freq, q, totalDb, sampleRate);
        else
            designShelf (b.sections[0], b.type == FilterType::lowShelf, freq, q, totalDb, sampleRate);

        b.numSections = 1;
    }

    double sampleRate = 48000.0;
    double smoothingCoeff = 1.0;
    std::array<Band, maxBands> bands;
    Solo solo;
    std::array<std::atomic<float>, maxBands> currentGainDb {};
    std::array<std::atomic<float>, maxBands> currentLevelDb {};
};

} // namespace threedq::dsp
