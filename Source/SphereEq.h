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
    This matches the browser preview in ui/src/audioPreview.ts.

    Dynamic bands (range != 0): the band listens to its own component c through
    a band-pass at its frequency and Q. When that level goes over the threshold,
    the band's gain moves from its static gain toward static + range (soft knee,
    ~2:1 slope above threshold, never more than the range). So a negative range
    is a dynamic cut (e.g. de-essing), a positive range a dynamic boost.

    Cut bands (low cut = high-pass, high cut = low-pass) replace the peaking
    filter with a Butterworth cascade of slope 6..48 dB/oct on the same axis,
    so e.g. a low cut at Side removes lows from the side signal only. Gain,
    Q and dynamics don't apply to cuts.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace threedq
{

constexpr int   maxBands  = 12;
constexpr float minFreq   = 20.0f;
constexpr float maxFreq   = 20000.0f;
constexpr float maxGainDb = 30.0f; // must match MAX_GAIN_DB in ui/src/bands.ts
constexpr float minQ      = 0.3f;
constexpr float maxQ      = 10.0f;
constexpr float minThresholdDb = -60.0f;
constexpr float maxThresholdDb = 0.0f;

enum class FilterType { peak = 0, lowCut = 1, highCut = 2 };

// Cut slopes in dB/oct (order = slope / 6). Index = the plugin's choice parameter.
constexpr std::array<int, 6> cutSlopes { 6, 12, 18, 24, 36, 48 };
constexpr int maxSections = 4; // biquads per band: order 8 = 4 sections

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
};

constexpr double pi = 3.14159265358979323846;

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
        // Detector: fast attack, slower release (per sample).
        attackCoeff  = 1.0 - std::exp (-1.0 / (0.002 * sampleRate));
        releaseCoeff = 1.0 - std::exp (-1.0 / (0.090 * sampleRate));
        for (auto& b : bands)
            b.snapToTarget = true;
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

        const int order = p.type == FilterType::peak ? 0 : std::clamp (p.slopeDbPerOct / 6, 1, 2 * maxSections);
        if (p.type != b.type || order != b.order)
        {
            // New filter structure: start it from silence (its old state is
            // meaningless), fading the band back in.
            b.type = p.type;
            b.order = order;
            b.clearState();
            b.mix = 0.0;
        }
    }

    // In-place stereo processing.
    void process (float* left, float* right, int numSamples)
    {
        for (int start = 0; start < numSamples; start += subBlockSize)
        {
            const int n = std::min (subBlockSize, numSamples - start);

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
                const bool dynamic = b.type == FilterType::peak && b.rangeDb != 0.0;
                const int numSections = b.numSections;
                const double mixStart = b.mixPrevious, mixStep = (b.mix - b.mixPrevious) / n;

                for (int i = start; i < start + n; ++i)
                {
                    const double c = uL * left[i] + uR * right[i];

                    if (dynamic)
                    {
                        // Side-chain: band-pass of this band's component, peak envelope.
                        const double sc = b.sb0 * c + b.sz1;
                        b.sz1 = -b.a1s * sc + b.sz2;
                        b.sz2 = b.sb2 * c - b.a2s * sc;
                        const double level = std::abs (sc);
                        b.envelope += (level - b.envelope) * (level > b.envelope ? attackCoeff : releaseCoeff);
                    }

                    // Cascade of transposed direct form II sections.
                    double y = c;
                    for (int k = 0; k < numSections; ++k)
                    {
                        auto& f = b.sections[(size_t) k];
                        const double out = f.b0 * y + f.z1;
                        f.z1 = f.b1 * y - f.a1 * out + f.z2;
                        f.z2 = f.b2 * y - f.a2 * out;
                        y = out;
                    }

                    // mix ramps the band in / out (cuts; peaks fade their gain instead).
                    const double d = (y - c) * (mixStart + mixStep * (i - start + 1));
                    left[i]  = (float) (left[i]  + uL * d);
                    right[i] = (float) (right[i] + uR * d);
                }

                const double levelDb = 20.0 * std::log10 (b.envelope + 1.0e-9);
                b.mixPrevious = b.mix;
                currentGainDb[bi].store ((float) (b.gainDb + b.dynamicDb), std::memory_order_relaxed);
                currentLevelDb[bi].store ((float) levelDb, std::memory_order_relaxed);
            }
        }
    }

private:
    static constexpr int subBlockSize = 16;
    static constexpr double kneeDb = 6.0;   // soft knee width around the threshold
    static constexpr double slope  = 0.5;   // dB of gain change per dB over threshold (~2:1)

    // Gain movement (dB) for a detector level: 0 below threshold, then
    // towards the range with a soft knee, never past it.
    static double dynamicGainFor (double levelDb, double thresholdDb, double rangeDb)
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

        struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
        std::array<Section, maxSections> sections {};
        int numSections = 1;
        double mix = 0.0, mixPrevious = 0.0; // 0..1 wet amount (ramped per sample)

        void clearState()
        {
            for (auto& f : sections) f.z1 = f.z2 = 0.0;
            sz1 = sz2 = 0.0;
        }

        // Dynamics
        double rangeDb = 0.0, thresholdDb = -30.0;
        double envelope = 0.0;  // side-chain peak level (linear)
        double dynamicDb = 0.0; // current dynamic gain movement
        double sb0 = 0.0, sb2 = 0.0, a1s = 0.0, a2s = 0.0; // side-chain band-pass (b1 = 0)
        double sz1 = 0.0, sz2 = 0.0;
    };

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
            b.mix = b.mixPrevious = b.type == FilterType::peak ? 1.0 : 0.0; // cuts fade in by mix
            b.snapToTarget = false;
        }

        // Removed bands fade out (peaks: gain to 0 dB, cuts: mix to 0) before going idle.
        const bool faded = b.type == FilterType::peak
                             ? std::abs (b.gainDb) < 1.0e-3 && std::abs (b.dynamicDb) < 1.0e-3
                             : b.mix < 1.0e-3;
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
        const double mixTarget = b.type == FilterType::peak ? 1.0 : (b.enabled ? 1.0 : 0.0);
        b.mix += (mixTarget - b.mix) * smoothingCoeff;

        // Dynamic movement from the detector (it has its own attack/release);
        // a disabled band, or one whose range is removed, eases back to static.
        const double dynamicTarget = (b.enabled && b.rangeDb != 0.0)
                                        ? dynamicGainFor (20.0 * std::log10 (b.envelope + 1.0e-9), b.thresholdDb, b.rangeDb)
                                        : 0.0;
        if (b.rangeDb == 0.0)
            b.envelope = 0.0;
        b.dynamicDb += (dynamicTarget - b.dynamicDb) * (b.enabled && b.rangeDb != 0.0 ? 1.0 : smoothingCoeff);

        updateCoefficients (b);
        return true;
    }

    // Filter coefficients for the band's current (smoothed) settings.
    void updateCoefficients (Band& b) const
    {
        const double freq  = std::min (std::exp (b.logFreq), 0.49 * sampleRate);
        const double q     = std::exp (b.logQ);
        const double w0    = 2.0 * pi * freq / sampleRate;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);

        // Side-chain band-pass (RBJ, 0 dB peak gain) at the band's frequency / Q.
        {
            const double alpha = sinw0 / (2.0 * q);
            const double s0 = 1.0 + alpha;
            b.sb0 = alpha / s0;
            b.sb2 = -alpha / s0;
            b.a1s = (-2.0 * cosw0) / s0;
            b.a2s = (1.0 - alpha) / s0;
        }

        if (b.type == FilterType::peak)
        {
            // RBJ cookbook peaking EQ (same as Web Audio's "peaking" BiquadFilterNode).
            const double totalDb = std::clamp (b.gainDb + b.dynamicDb, (double) -maxGainDb, (double) maxGainDb);
            const double A     = std::pow (10.0, totalDb / 40.0);
            const double alpha = sinw0 / (2.0 * q);
            const double a0    = 1.0 + alpha / A;
            auto& f = b.sections[0];
            f.b0 = (1.0 + alpha * A) / a0;
            f.b1 = (-2.0 * cosw0) / a0;
            f.b2 = (1.0 - alpha * A) / a0;
            f.a1 = (-2.0 * cosw0) / a0;
            f.a2 = (1.0 - alpha / A) / a0;
            b.numSections = 1;
            return;
        }

        // Butterworth cut of order n: n/2 RBJ second-order sections with the
        // Butterworth Qs, plus one first-order section when n is odd.
        const bool high = b.type == FilterType::lowCut; // a low cut is a high-pass
        const int n = b.order;
        int k = 0;
        for (int i = 0; i < n / 2; ++i)
        {
            const double theta = n % 2 == 0 ? (2.0 * i + 1.0) * pi / (2.0 * n) : (i + 1.0) * pi / n;
            const double sq = 1.0 / (2.0 * std::cos (theta));
            const double alpha = sinw0 / (2.0 * sq);
            const double a0 = 1.0 + alpha;
            auto& f = b.sections[(size_t) k++];
            if (high)
            {
                f.b0 = (1.0 + cosw0) / 2.0 / a0;
                f.b1 = -(1.0 + cosw0) / a0;
                f.b2 = f.b0;
            }
            else
            {
                f.b0 = (1.0 - cosw0) / 2.0 / a0;
                f.b1 = (1.0 - cosw0) / a0;
                f.b2 = f.b0;
            }
            f.a1 = (-2.0 * cosw0) / a0;
            f.a2 = (1.0 - alpha) / a0;
        }
        if (n % 2 == 1)
        {
            // First order, bilinear transform with prewarping.
            const double K = std::tan (pi * freq / sampleRate);
            auto& f = b.sections[(size_t) k++];
            f.b0 = high ? 1.0 / (1.0 + K) : K / (1.0 + K);
            f.b1 = high ? -f.b0 : f.b0;
            f.b2 = 0.0;
            f.a1 = (K - 1.0) / (K + 1.0);
            f.a2 = 0.0;
        }
        b.numSections = k;
    }

    double sampleRate = 48000.0;
    double smoothingCoeff = 1.0;
    double attackCoeff = 1.0, releaseCoeff = 1.0;
    std::array<Band, maxBands> bands;
    std::array<std::atomic<float>, maxBands> currentGainDb {};
    std::array<std::atomic<float>, maxBands> currentLevelDb {};
};

} // namespace threedq
