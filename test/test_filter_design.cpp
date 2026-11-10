/*
  ==============================================================================

    Filter designs: each one's response matches what its formula promises,
    and every design stays stable across the whole parameter range.

  ==============================================================================
*/

#include "test_helpers.h"

namespace threedq::test
{

class FilterDesignTests final : public juce::UnitTest
{
public:
    FilterDesignTests() : juce::UnitTest ("Filter design", "dsp") {}

    void runTest() override
    {
        using namespace dsp;
        constexpr double sr = 48000.0;

        beginTest ("Peak: full gain at its frequency, flat at DC and Nyquist");
        for (double freq : { 100.0, 1000.0, 10000.0 })
            for (double gain : { -24.0, -6.0, 6.0, 24.0 })
                for (double q : { 0.3, 1.0, 10.0 })
                {
                    Biquad f;
                    designPeak (f, freq, q, gain, sr);
                    const auto where = " (" + juce::String (freq) + " Hz, " + juce::String (gain) + " dB, Q " + juce::String (q) + ")";
                    expectWithinAbsoluteError (responseDb (f, freq, sr), gain, 0.01, "centre" + where);
                    expectWithinAbsoluteError (responseDb (f, 0.0, sr), 0.0, 1.0e-6, "DC" + where);
                    expectWithinAbsoluteError (responseDb (f, sr / 2, sr), 0.0, 1.0e-6, "Nyquist" + where);
                }

        beginTest ("Peak: 0 dB is a straight wire");
        {
            Biquad f;
            designPeak (f, 1000.0, 1.0, 0.0, sr);
            for (double freq : { 20.0, 500.0, 1000.0, 5000.0, 20000.0 })
                expectWithinAbsoluteError (responseDb (f, freq, sr), 0.0, 1.0e-9);
        }

        beginTest ("Peak: a wider Q reaches further");
        {
            Biquad narrow, wide;
            designPeak (narrow, 1000.0, 4.0, 12.0, sr);
            designPeak (wide, 1000.0, 0.5, 12.0, sr);
            expectGreaterThan (responseDb (wide, 2000.0, sr), responseDb (narrow, 2000.0, sr) + 3.0);
        }

        beginTest ("Shelves: full gain on their side, half gain at their frequency");
        for (bool low : { true, false })
            for (double gain : { -18.0, -6.0, 6.0, 18.0 })
            {
                Biquad f;
                designShelf (f, low, 1000.0, 0.707, gain, sr);
                const auto where = juce::String (low ? " (low shelf, " : " (high shelf, ") + juce::String (gain) + " dB)";
                expectWithinAbsoluteError (responseDb (f, 1000.0, sr), gain / 2.0, 0.05, "frequency" + where);
                expectWithinAbsoluteError (responseDb (f, low ? 0.0 : sr / 2, sr), gain, 1.0e-6, "shelf side" + where);
                expectWithinAbsoluteError (responseDb (f, low ? sr / 2 : 0.0, sr), 0.0, 1.0e-6, "other side" + where);
            }

        beginTest ("Shelves: Q is capped so they can't ring far past their gain");
        {
            Biquad capped, atCap;
            designShelf (capped, true, 1000.0, 10.0, 12.0, sr);
            designShelf (atCap, true, 1000.0, maxShelfQ, 12.0, sr);
            for (double freq : { 300.0, 1000.0, 3000.0 })
                expectWithinAbsoluteError (responseDb (capped, freq, sr), responseDb (atCap, freq, sr), 1.0e-9);
        }

        beginTest ("Cuts: -3 dB at the cutoff for every slope");
        for (bool highPass : { true, false })
            for (int order = 1; order <= 2 * maxSections; ++order)
            {
                std::array<Biquad, maxSections> sections {};
                const int used = designCut (sections, highPass, order, 1000.0, sr);
                expectEquals (used, (order + 1) / 2, "sections for order " + juce::String (order));

                double atCutoff = 0;
                for (int k = 0; k < used; ++k)
                    atCutoff += responseDb (sections[(size_t) k], 1000.0, sr);

                expectWithinAbsoluteError (atCutoff, -3.0103, 0.01, "order " + juce::String (order));
            }

        beginTest ("Cuts: about 6 dB per octave per order in the stop band, flat in the pass band");
        for (int order = 1; order <= 2 * maxSections; ++order)
        {
            std::array<Biquad, maxSections> sections {};
            const int used = designCut (sections, true, order, 4000.0, sr); // high-pass: lows are the stop band
            auto total = [&] (double freq)
            {
                double db = 0;
                for (int k = 0; k < used; ++k)
                    db += responseDb (sections[(size_t) k], freq, sr);
                return db;
            };

            expectWithinAbsoluteError (total (250.0) - total (125.0), 6.02 * order, 0.3, "slope, order " + juce::String (order));
            expectWithinAbsoluteError (total (16000.0), 0.0, 0.2, "pass band, order " + juce::String (order));
        }

        beginTest ("Listen filters: a peak's band-pass is 0 dB at its frequency");
        for (double q : { 0.3, 1.0, 10.0 })
        {
            Biquad f;
            designListenFilter (f, FilterType::peak, 2000.0, q, sr);
            expectWithinAbsoluteError (responseDb (f, 2000.0, sr), 0.0, 0.01);
            expectLessThan (responseDb (f, 200.0, sr), -6.0);
        }

        beginTest ("Listen filters: shelves and cuts listen on their own side");
        {
            Biquad low, high;
            designListenFilter (low, FilterType::lowShelf, 1000.0, 1.0, sr);
            designListenFilter (high, FilterType::highCut, 1000.0, 1.0, sr);
            expectWithinAbsoluteError (responseDb (low, 50.0, sr), 0.0, 0.1);
            expectLessThan (responseDb (low, 10000.0, sr), -30.0);
            expectWithinAbsoluteError (responseDb (high, 15000.0, sr), 0.0, 0.2);
            expectLessThan (responseDb (high, 100.0, sr), -30.0);
        }

        beginTest ("Every design is stable across the parameter range");
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double freq : { 20.0, 200.0, 2000.0, 15000.0, 0.49 * rate })
                for (double q : { (double) minQ, 1.0, (double) maxQ })
                    for (double gain : { (double) -maxGainDb, 0.0, (double) maxGainDb })
                    {
                        const auto where = juce::String (rate) + " Hz rate, " + juce::String (freq) + " Hz, Q " + juce::String (q) + ", " + juce::String (gain) + " dB";
                        Biquad f;
                        designPeak (f, freq, q, gain, rate);
                        expect (isStable (f), "peak " + where);
                        designShelf (f, true, freq, q, gain, rate);
                        expect (isStable (f), "low shelf " + where);
                        designShelf (f, false, freq, q, gain, rate);
                        expect (isStable (f), "high shelf " + where);
                        designListenFilter (f, FilterType::peak, freq, q, rate);
                        expect (isStable (f), "listen " + where);

                        std::array<Biquad, maxSections> sections {};
                        const int used = designCut (sections, freq < 1000.0, 8, freq, rate);
                        for (int k = 0; k < used; ++k)
                            expect (isStable (sections[(size_t) k]), "cut " + where);
                    }
    }
};

static FilterDesignTests filterDesignTests;

} // namespace threedq::test
