/*
  ==============================================================================

    The stereo analyser: energy lands in the right band, and the cross term
    tells in-phase, anti-phase and one-sided signals apart.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/dsp/stereo_spectrum.h"

namespace threedq::test
{

class StereoSpectrumTests final : public juce::UnitTest
{
public:
    StereoSpectrumTests() : juce::UnitTest ("Stereo spectrum", "dsp") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;
        constexpr int n = StereoSpectrum::fftSize;
        StereoSpectrum analyser;

        const auto tone = sine (1000.0, sr, n);
        auto inverted = tone;
        for (auto& v : inverted)
            v = -v;
        const std::vector<float> silence ((size_t) n, 0.0f);

        // Band holding 1 kHz.
        const int toneBand = (int) (std::log (1000.0 / minFreq) / std::log ((double) maxFreq / minFreq) * StereoSpectrum::numBands);

        beginTest ("A tone lands in its own band");
        {
            const auto r = analyser.analyse (tone.data(), tone.data(), sr);
            int loudest = 0;
            for (int b = 0; b < StereoSpectrum::numBands; ++b)
                if (r.ll[(size_t) b] > r.ll[(size_t) loudest])
                    loudest = b;
            expectEquals (loudest, toneBand);
            expectLessThan (r.ll[2], r.ll[(size_t) toneBand] * 1.0e-6f, "nothing far below it");
            expectLessThan (r.ll[44], r.ll[(size_t) toneBand] * 1.0e-6f, "nothing far above it");
        }

        beginTest ("Same signal on both sides: ll = rr = lr");
        {
            const auto r = analyser.analyse (tone.data(), tone.data(), sr);
            const auto b = (size_t) toneBand;
            expectGreaterThan (r.ll[b], 0.0f);
            expectWithinAbsoluteError (r.rr[b] / r.ll[b], 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (r.lr[b] / r.ll[b], 1.0f, 1.0e-5f);
        }

        beginTest ("Anti-phase: lr = -ll");
        {
            const auto r = analyser.analyse (tone.data(), inverted.data(), sr);
            const auto b = (size_t) toneBand;
            expectWithinAbsoluteError (r.lr[b] / r.ll[b], -1.0f, 1.0e-5f);
        }

        beginTest ("Left only: nothing on the right, no cross term");
        {
            const auto r = analyser.analyse (tone.data(), silence.data(), sr);
            for (int b = 0; b < StereoSpectrum::numBands; ++b)
            {
                expectEquals (r.rr[(size_t) b], 0.0f);
                expectEquals (r.lr[(size_t) b], 0.0f);
            }
            expectGreaterThan (r.ll[(size_t) toneBand], 0.0f);
        }

        beginTest ("Level scales with the square of amplitude");
        {
            const auto quiet = sine (1000.0, sr, n, 0.125f);
            const auto loud = analyser.analyse (tone.data(), tone.data(), sr);
            const auto soft = analyser.analyse (quiet.data(), quiet.data(), sr);
            const auto b = (size_t) toneBand;
            expectWithinAbsoluteError (loud.ll[b] / soft.ll[b], 4.0f, 1.0e-3f);
        }

        beginTest ("Silence is all zeros, and every value is finite on noise");
        {
            const auto r = analyser.analyse (silence.data(), silence.data(), sr);
            for (int b = 0; b < StereoSpectrum::numBands; ++b)
                expect (r.ll[(size_t) b] == 0.0f && r.rr[(size_t) b] == 0.0f && r.lr[(size_t) b] == 0.0f);

            const auto left = noise (n, 1.0f, 5), right = noise (n, 1.0f, 6);
            for (double rate : { 44100.0, 192000.0 })
            {
                const auto rn = analyser.analyse (left.data(), right.data(), rate);
                bool fine = true;
                for (int b = 0; b < StereoSpectrum::numBands; ++b)
                    fine = fine && std::isfinite (rn.ll[(size_t) b]) && std::isfinite (rn.lr[(size_t) b])
                           && rn.ll[(size_t) b] >= 0 && rn.rr[(size_t) b] >= 0
                           && std::abs (rn.lr[(size_t) b]) <= std::sqrt (rn.ll[(size_t) b] * rn.rr[(size_t) b]) * 1.0001f + 1.0e-12f;
                expect (fine, "at " + juce::String (rate) + " Hz: finite, non-negative, |lr| <= sqrt(ll rr)");
            }
        }
    }
};

static StereoSpectrumTests stereoSpectrumTests;

} // namespace threedq::test
