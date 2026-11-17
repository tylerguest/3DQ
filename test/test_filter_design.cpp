/*
  ==============================================================================

    Filter designs: each one's response matches what its formula promises,
    and every design stays stable across the whole parameter range.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/model/bands.h"

namespace threedq::test
{

namespace
{
// The analog RBJ bell's gain (dB) at freq: the response the digital one matches.
double analogPeakDb (double freq, double centre, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const std::complex<double> s (0.0, freq / centre);
    return 20.0 * std::log10 (std::abs ((s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0)));
}

// The largest gap (dB) between a digital response and an analog one, from 20 Hz
// to 20 kHz (or just under Nyquist), where the analog one is above floorDb.
template <typename Digital, typename Analog>
double worstGapDb (double sampleRate, Digital digital, Analog analog, double floorDb = -200.0)
{
    double worst = 0.0;
    for (double freq = 20.0; freq <= std::min (20000.0, 0.499 * sampleRate); freq *= 1.02)
        if (const double a = analog (freq); a > floorDb)
            worst = std::max (worst, std::abs (digital (freq) - a));
    return worst;
}
}

class FilterDesignTests final : public juce::UnitTest
{
public:
    FilterDesignTests() : juce::UnitTest ("Filter design", "dsp") {}

    void runTest() override
    {
        using namespace dsp;
        constexpr double sr = 48000.0;

        beginTest ("Peak: full gain at its frequency, flat at DC, and the analog bell's gain at Nyquist");
        for (double freq : { 100.0, 1000.0, 10000.0 })
            for (double gain : { -24.0, -6.0, 6.0, 24.0 })
                for (double q : { 0.3, 1.0, 10.0 })
                {
                    Biquad f;
                    designPeak (f, freq, q, gain, sr);
                    const auto where = " (" + juce::String (freq) + " Hz, " + juce::String (gain) + " dB, Q " + juce::String (q) + ")";
                    expectWithinAbsoluteError (responseDb (f, freq, sr), gain, 0.01, "centre" + where);
                    expectWithinAbsoluteError (responseDb (f, 0.0, sr), 0.0, 1.0e-6, "DC" + where);
                    expectWithinAbsoluteError (responseDb (f, sr / 2, sr), analogPeakDb (sr / 2, freq, q, gain), 1.0e-6, "Nyquist" + where);
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

        beginTest ("Peak: Q 40 is a narrow notch, Q 0.025 a tilt across the spectrum");
        {
            Biquad notch, tilt;
            designPeak (notch, 1000.0, maxQ, -24.0, sr);
            designPeak (tilt, 1000.0, minQ, 12.0, sr);
            expectWithinAbsoluteError (responseDb (notch, 1000.0, sr), -24.0, 0.01);
            expectGreaterThan (responseDb (notch, 1100.0, sr), -2.0, "a tenth of an octave away it's nearly gone");
            expectGreaterThan (responseDb (notch, 900.0, sr), -2.0);
            expectGreaterThan (responseDb (tilt, 100.0, sr), 6.0, "three octaves down it's still most of its gain");
            expectGreaterThan (responseDb (tilt, 10000.0, sr), 6.0);
        }

        beginTest ("Shelves: full gain on their side, half gain at their frequency");
        for (bool low : { true, false })
            for (double gain : { -18.0, -6.0, 6.0, 18.0 })
            {
                Biquad f;
                designShelf (f, low, 1000.0, 0.707, gain, sr);
                const auto where = juce::String (low ? " (low shelf, " : " (high shelf, ") + juce::String (gain) + " dB)";
                expectWithinAbsoluteError (responseDb (f, 1000.0, sr), gain / 2.0, 0.05, "frequency" + where);
                expectWithinAbsoluteError (responseDb (f, low ? 0.0 : sr / 2, sr), gain, 0.01, "shelf side" + where);
                expectWithinAbsoluteError (responseDb (f, low ? sr / 2 : 0.0, sr), 0.0, 0.01, "other side" + where);
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

        beginTest ("Bells keep the analog shape up to 20 kHz, at every sample rate");
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (double freq : { 30.0, 300.0, 3000.0, 8000.0, 12000.0, 15000.0 })
                for (double q : { 0.3, 1.0, 4.0, 10.0 })
                    for (double gain : { -12.0, -3.0, 3.0, 12.0 })
                    {
                        Biquad f;
                        designPeak (f, freq, q, gain, rate);
                        const double gap = worstGapDb (rate, [&] (double x) { return responseDb (f, x, rate); },
                                                       [&] (double x) { return analogPeakDb (x, freq, q, gain); });
                        expectLessThan (gap, freq <= 3000.0 ? 0.6 : 1.5, juce::String (rate) + " Hz rate, " + juce::String (freq) + " Hz, Q "
                                                                            + juce::String (q) + ", " + juce::String (gain) + " dB");
                    }

        beginTest ("A +12 dB bell at 15 kHz keeps its top end at 44.1 kHz");
        {
            Biquad f;
            designPeak (f, 15000.0, 1.0, 12.0, 44100.0);
            for (double freq : { 18000.0, 20000.0 })
                expectWithinAbsoluteError (responseDb (f, freq, 44100.0), analogPeakDb (freq, 15000.0, 1.0, 12.0), 1.0, juce::String (freq) + " Hz");
        }

        beginTest ("Shelves keep the analog shape up to 20 kHz, at every sample rate");
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (bool low : { true, false })
                for (float freq : { 30.0f, 300.0f, 3000.0f, 8000.0f, 12000.0f, 15000.0f })
                    for (float q : { 0.5f, 0.707f, 2.0f })
                        for (float gain : { -12.0f, -3.0f, 3.0f, 12.0f })
                        {
                            threedq::Band b;
                            b.lat = threedq::freqToLat (freq);
                            b.q = q;
                            b.type = low ? FilterType::lowShelf : FilterType::highShelf;
                            Biquad f;
                            designShelf (f, low, freq, q, gain, rate);
                            const double gap = worstGapDb (rate, [&] (double x) { return responseDb (f, x, rate); },
                                                           [&] (double x) { return (double) threedq::shelfResponseDb (b, gain, (float) x); });
                            expectLessThan (gap, freq <= 3000.0f ? 0.6 : 1.6, juce::String (rate) + " Hz rate, " + (low ? "low" : "high") + " shelf, "
                                                                                + juce::String (freq) + " Hz, Q " + juce::String (q) + ", " + juce::String (gain) + " dB");
                        }

        beginTest ("High cuts keep the analog slope up to 20 kHz, at every sample rate");
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (float freq : { 100.0f, 1000.0f, 5000.0f, 10000.0f, 15000.0f })
                for (int slope : cutSlopes)
                {
                    std::array<Biquad, maxSections> sections {};
                    const int used = designCut (sections, false, slope / 6, freq, rate);
                    auto digital = [&] (double x)
                    {
                        double db = 0;
                        for (int k = 0; k < used; ++k)
                            db += responseDb (sections[(size_t) k], x, rate);
                        return db;
                    };
                    const double gap = worstGapDb (rate, digital, [&] (double x) { return (double) threedq::cutResponseDb (FilterType::highCut, freq, slope, (float) x); }, -40.0);
                    expectLessThan (gap, freq <= 5000.0f ? 2.0 : 4.0, juce::String (rate) + " Hz rate, " + juce::String (freq) + " Hz, " + juce::String (slope) + " dB/oct");
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
                        designListenFilter (f, FilterType::lowShelf, freq, q, rate);
                        expect (isStable (f), "low listen " + where);

                        // A cut's zeros become its poles, so the boost's must be inside too.
                        for (bool low : { true, false })
                        {
                            designShelf (f, low, freq, q, -gain, rate);
                            expect (isStable (f), "inverted shelf " + where);
                        }

                        std::array<Biquad, maxSections> sections {};
                        for (int order : { 1, 3, 8 })
                            for (bool highPass : { true, false })
                            {
                                const int used = designCut (sections, highPass, order, freq, rate);
                                for (int k = 0; k < used; ++k)
                                    expect (isStable (sections[(size_t) k]), "cut " + where);
                            }
                    }
    }
};

static FilterDesignTests filterDesignTests;

} // namespace threedq::test
