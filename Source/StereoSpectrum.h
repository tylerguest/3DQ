/*
  ==============================================================================

    StereoSpectrum: the stereo analysis that drives the sphere's audio response.

    For each of numBands log-spaced bands (20 Hz .. 20 kHz) it reports
        ll = |L|²,  rr = |R|²,  lr = Re(L · conj(R))
    integrated over the band, treating the FFT bins as samples of a continuous
    density joined by straight lines (so bands narrower than a bin, at the low
    end, still differ smoothly from their neighbours). The GUI turns these into the energy
    along each particle's stereo axis. Scaling matches Web Audio's AnalyserNode
    (Blackman window, magnitudes divided by the FFT size).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SphereEq.h"

class StereoSpectrum
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int numBands = 48; // must match NUM_SPECTRUM_BANDS in the GUI

    struct Result
    {
        std::array<float, numBands> ll {}, rr {}, lr {};
    };

    // left / right: the most recent fftSize samples.
    Result analyse (const float* left, const float* right, double sampleRate)
    {
        transform (left, bufferL);
        transform (right, bufferR);

        const int numBins = fftSize / 2;
        const double binHz = sampleRate / fftSize;
        const double scale = 1.0 / ((double) fftSize * (double) fftSize);

        Result result;

        for (int k = 0; k < numBins; ++k)
        {
            const double lRe = bufferL[(size_t) (2 * k)], lIm = bufferL[(size_t) (2 * k + 1)];
            const double rRe = bufferR[(size_t) (2 * k)], rIm = bufferR[(size_t) (2 * k + 1)];
            binLL[(size_t) k] = lRe * lRe + lIm * lIm;
            binRR[(size_t) k] = rRe * rRe + rIm * rIm;
            binLR[(size_t) k] = lRe * rRe + lIm * rIm;
        }

        for (int b = 0; b < numBands; ++b)
        {
            const double lo = std::min (bandEdge (b) / binHz, (double) (numBins - 1));
            const double hi = std::min (bandEdge (b + 1) / binHz, (double) (numBins - 1));
            result.ll[(size_t) b] = (float) (integrate (binLL, lo, hi) * scale);
            result.rr[(size_t) b] = (float) (integrate (binRR, lo, hi) * scale);
            result.lr[(size_t) b] = (float) (integrate (binLR, lo, hi) * scale);
        }

        return result;
    }

private:
    // Integral of per-bin values over [a, b] (in bins), joining the bins with
    // straight lines.
    static double integrate (const std::vector<double>& bins, double a, double b)
    {
        const int n = (int) bins.size();
        auto at = [&] (double x)
        {
            const int k = std::min ((int) std::floor (x), n - 2);
            const double f = x - k;
            return bins[(size_t) k] * (1.0 - f) + bins[(size_t) k + 1] * f;
        };

        double sum = 0;
        for (double x0 = a; x0 < b;)
        {
            const double x1 = std::min (b, std::floor (x0) + 1.0);
            sum += (x1 - x0) * 0.5 * (at (x0) + at (x1));
            x0 = x1;
        }
        return sum;
    }

    static double bandEdge (int i)
    {
        return threedq::minFreq * std::pow ((double) threedq::maxFreq / threedq::minFreq, (double) i / numBands);
    }

    void transform (const float* input, std::vector<float>& buffer)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        std::copy (input, input + fftSize, buffer.begin());
        window.multiplyWithWindowingTable (buffer.data(), (size_t) fftSize);
        fft.performRealOnlyForwardTransform (buffer.data(), true);
    }

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::blackman, false };
    std::vector<float> bufferL = std::vector<float> ((size_t) (2 * fftSize));
    std::vector<float> bufferR = std::vector<float> ((size_t) (2 * fftSize));
    std::vector<double> binLL = std::vector<double> ((size_t) (fftSize / 2));
    std::vector<double> binRR = std::vector<double> ((size_t) (fftSize / 2));
    std::vector<double> binLR = std::vector<double> ((size_t) (fftSize / 2));
};
