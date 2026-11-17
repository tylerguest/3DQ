/*
  ==============================================================================

    The sphere's band model: the frequency scale, the surface shape each band
    type gives, and that the drawn shape agrees with what the EQ does.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/model/bands.h"

namespace threedq::test
{

namespace
{
Band bandAt (float freq, float gainDb, FilterType type = FilterType::peak, float lonDeg = 0.0f)
{
    Band b;
    b.lat = freqToLat (freq);
    b.lon = lonDeg * (float) pi / 180.0f;
    b.gainDb = gainDb;
    b.type = type;
    return b;
}

Vec3 direction (float freq, float lonDeg)
{
    Band b = bandAt (freq, 0.0f, FilterType::peak, lonDeg);
    return bandDirection (b);
}

float displacementAt (const BandSet& bands, float freq, float lonDeg)
{
    return surfaceDisplacement (bands, directionsOf (bands), direction (freq, lonDeg));
}

BandSet just (const Band& b)
{
    BandSet set;
    set.add (b);
    return set;
}
}

class BandModelTests final : public juce::UnitTest
{
public:
    BandModelTests() : juce::UnitTest ("Band model", "model") {}

    void runTest() override
    {
        beginTest ("Latitude and frequency: poles are 20 Hz and 20 kHz, and they round-trip");
        expectWithinAbsoluteError (latToFreq (-halfPi), minFreq, 1.0e-3f);
        expectWithinAbsoluteError (latToFreq (halfPi), maxFreq, 0.5f);
        expectWithinAbsoluteError (latToFreq (0.0f), std::sqrt (minFreq * maxFreq), 0.05f, "the equator is the geometric middle");
        for (float freq : { 20.0f, 50.0f, 440.0f, 1000.0f, 7000.0f, 20000.0f })
            expectWithinAbsoluteError (latToFreq (freqToLat (freq)) / freq, 1.0f, 1.0e-4f);

        beginTest ("Gain shape: odd, monotonic, full at the limit");
        {
            float previous = -2.0f;
            for (float db = -maxGainDb; db <= maxGainDb; db += 0.5f)
            {
                const float s = gainShape (db);
                expectGreaterThan (s, previous);
                expectWithinAbsoluteError (gainShape (-db), -s, 1.0e-6f);
                previous = s;
            }
            expectEquals (gainShape (maxGainDb), 1.0f);
            expectEquals (gainShape (2.0f * maxGainDb), 1.0f, "past the limit it stays full");
            expectEquals (gainShape (0.0f), 0.0f);
        }

        beginTest ("Displacement limit: untouched up to full scale, then eased into a little room");
        {
            expectEquals (limitDisplacement (0.3f), 0.3f);
            expectEquals (limitDisplacement (-0.6f), -0.6f);
            float previous = 0;
            for (float d = 0; d < 5.0f; d += 0.05f)
            {
                const float up = limitDisplacement (d), down = limitDisplacement (-d);
                expectGreaterOrEqual (up, previous);
                expectLessOrEqual (up, maxBoostDisplacement + stackRoom);
                expectGreaterOrEqual (down, -(maxCutDisplacement + stackRoom));
                previous = up;
            }
            expectGreaterThan (1.0f - (maxCutDisplacement + stackRoom), 0.0f, "a dent never reaches the centre");
        }

        beginTest ("Cut shape: -3 dB at its cutoff, the right slope, 0 dB in the pass band");
        for (int slope : cutSlopes)
        {
            expectWithinAbsoluteError (cutResponseDb (FilterType::lowCut, 1000.0f, slope, 1000.0f), -3.0103f, 1.0e-3f);
            expectWithinAbsoluteError (cutResponseDb (FilterType::highCut, 1000.0f, slope, 1000.0f), -3.0103f, 1.0e-3f);
            expectWithinAbsoluteError (cutResponseDb (FilterType::lowCut, 1000.0f, slope, 62.5f) - cutResponseDb (FilterType::lowCut, 1000.0f, slope, 125.0f),
                                       -6.0206f * (float) slope / 6.0f, 0.06f, "slope " + juce::String (slope));
            expectWithinAbsoluteError (cutResponseDb (FilterType::highCut, 1000.0f, slope, 50.0f), 0.0f, 0.05f);
        }

        beginTest ("Shelf shape: half its gain at its frequency, full gain on its side");
        for (float gain : { -12.0f, 6.0f, 18.0f })
        {
            const auto low = bandAt (1000.0f, gain, FilterType::lowShelf);
            const auto high = bandAt (1000.0f, gain, FilterType::highShelf);
            expectWithinAbsoluteError (shelfResponseDb (low, gain, 1000.0f), gain / 2.0f, 0.05f);
            expectWithinAbsoluteError (shelfResponseDb (low, gain, 20.0f), gain, 0.05f);
            expectWithinAbsoluteError (shelfResponseDb (high, gain, 20000.0f), gain, 0.05f);
            expectWithinAbsoluteError (shelfResponseDb (high, gain, 20.0f), 0.0f, 0.05f);
        }

        beginTest ("The drawn shelves and cuts match what the EQ does");
        {
            constexpr double sr = 96000.0; // little warping below a few kHz
            for (bool lowShelf : { true, false })
                for (float q : { 0.5f, 0.707f, 1.5f, 6.0f })
                {
                    auto b = bandAt (1000.0f, 9.0f, lowShelf ? FilterType::lowShelf : FilterType::highShelf);
                    b.q = q;
                    dsp::Biquad f;
                    dsp::designShelf (f, lowShelf, 1000.0, q, 9.0, sr);
                    for (float freq : { 50.0f, 300.0f, 1000.0f, 2500.0f, 5000.0f })
                        expectWithinAbsoluteError ((double) shelfResponseDb (b, 9.0f, freq), responseDb (f, freq, sr), 0.25,
                                                   juce::String (lowShelf ? "low" : "high") + " shelf, Q " + juce::String (q) + ", " + juce::String (freq) + " Hz");
                }

            for (bool lowCut : { true, false })
                for (int slope : cutSlopes)
                {
                    std::array<dsp::Biquad, dsp::maxSections> sections {};
                    const int used = dsp::designCut (sections, lowCut, slope / 6, 1000.0, sr);
                    for (float freq : { 300.0f, 700.0f, 1000.0f, 1500.0f, 3000.0f })
                    {
                        double digital = 0;
                        for (int k = 0; k < used; ++k)
                            digital += responseDb (sections[(size_t) k], freq, sr);
                        expectWithinAbsoluteError ((double) cutResponseDb (lowCut ? FilterType::lowCut : FilterType::highCut, 1000.0f, slope, freq),
                                                   digital, 0.35, juce::String (slope) + " dB/oct at " + juce::String (freq) + " Hz");
                    }
                }
        }

        beginTest ("Bell shape: the analogue peak, symmetric in log frequency");
        for (float gain : { -12.0f, 6.0f, 18.0f })
            for (float q : { 0.5f, 1.0f, 4.0f })
            {
                expectWithinAbsoluteError (peakResponseDb (gain, q, 1.0f), gain, 1.0e-4f);
                expectWithinAbsoluteError (peakResponseDb (gain, q, 3.0f), peakResponseDb (gain, q, 1.0f / 3.0f), 1.0e-4f);
                expectLessThan (std::abs (peakResponseDb (gain, q, 1000.0f)), 0.01f);
            }

        beginTest ("The drawn bell matches what the EQ does");
        {
            constexpr double sr = 96000.0;
            for (float q : { 0.5f, 1.0f, 4.0f })
                for (float gain : { -12.0f, 9.0f })
                {
                    dsp::Biquad f;
                    dsp::designPeak (f, 1000.0, q, gain, sr);
                    for (float freq : { 200.0f, 700.0f, 1000.0f, 1400.0f, 5000.0f })
                        expectWithinAbsoluteError ((double) peakResponseDb (gain, q, freq / 1000.0f), responseDb (f, freq, sr), 0.25,
                                                   "Q " + juce::String (q) + ", " + juce::String (gain) + " dB, " + juce::String (freq) + " Hz");
                }
        }

        beginTest ("Surface: a bell's bump along frequency is its response");
        for (float q : { 0.5f, 1.0f, 4.0f })
            for (float gain : { -12.0f, 6.0f, 18.0f })
            {
                auto b = bandAt (1000.0f, gain);
                b.q = q;
                const auto bands = just (b);
                for (float freq : { 250.0f, 700.0f, 1000.0f, 1190.0f, 1414.0f, 4000.0f })
                    expectWithinAbsoluteError (displacementAt (bands, freq, 0.0f), gainDisplacement (peakResponseDb (gain, q, freq / 1000.0f)), 1.0e-4f,
                                               "Q " + juce::String (q) + ", " + juce::String (gain) + " dB, " + juce::String (freq) + " Hz");
            }

        beginTest ("Bell curve: the quick form agrees with the response everywhere, and is flat only where the skirt is");
        for (float q : { minQ, 0.5f, 1.0f, 4.0f, maxQ })
            for (float gain : { -30.0f, -0.2f, 0.0f, 3.0f, 30.0f })
            {
                Band b;
                b.q = q;
                b.gainDb = gain;
                const auto curve = bellCurve (b);
                for (float angle = 0; angle <= pi; angle += 0.005f)
                    expectWithinAbsoluteError (bellDisplacement (curve, std::cos (angle)), gainDisplacement (peakDbAtAngle (gain, q, angle)), 3.0e-4f,
                                               "Q " + juce::String (q) + ", " + juce::String (gain) + " dB, " + juce::String (angle) + " rad");
            }

        beginTest ("Surface: a Q 4 bell half an octave away is down to its real height, not a Gaussian's");
        {
            auto b = bandAt (1000.0f, 12.0f);
            b.q = 4.0f;
            const float ratio = displacementAt (just (b), 1414.2f, 0.0f) / gainDisplacement (12.0f);
            const float real = gainShape (peakResponseDb (12.0f, 4.0f, 1.4142f)) / gainShape (12.0f);
            expectWithinAbsoluteError (ratio, real, 0.01f);
            expectLessThan (ratio, 0.35f);
        }

        beginTest ("Surface: a bell's bump is round, as wide in stereo as in frequency");
        {
            auto b = bandAt (std::sqrt (minFreq * maxFreq), 12.0f);
            b.q = 1.0f;
            const auto bands = just (b);
            const float angle = 0.2f;
            const float up = displacementAt (bands, latToFreq (angle), 0.0f);
            const float across = displacementAt (bands, std::sqrt (minFreq * maxFreq), angle * 180.0f / (float) pi);
            expectWithinAbsoluteError (across, up, 1.0e-4f);
        }

        beginTest ("Drawn Q: exact up to the sharpest drawn, then still narrowing, more slowly");
        {
            for (float q : { minQ, 0.5f, 1.0f, sharpestDrawnQ })
                expectEquals (drawnQ (q), q);
            float previous = drawnQ (sharpestDrawnQ);
            for (float q = sharpestDrawnQ + 0.5f; q <= maxQ; q += 0.5f)
            {
                expectGreaterThan (drawnQ (q), previous);
                expectLessThan (drawnQ (q), q);
                previous = drawnQ (q);
            }
            expectWithinAbsoluteError (drawnQ (maxQ), 8.0f, 0.1f);
        }

        beginTest ("Band width: narrower as Q goes up, and where the bump is about a third high");
        {
            float previous = 10.0f;
            for (float q : { minQ, 0.5f, 1.0f, 4.0f, maxQ })
            {
                Band b;
                b.q = q;
                const float w = bandWidth (b);
                expectLessThan (w, previous);
                expectGreaterThan (w, 0.0f);
                previous = w;
            }
            Band b;
            const float third = gainShape (peakDbAtAngle (12.0f, 1.0f, bandWidth (b))) / gainShape (12.0f);
            expectWithinAbsoluteError (third, std::exp (-1.0f), 0.01f);
        }

        beginTest ("Axis overlap: full on its own axis, half at right angles, none opposite");
        expectWithinAbsoluteError (axisOverlap (0.0f, 0.0f), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (axisOverlap (0.0f, halfPi), 0.5f, 1.0e-6f);
        expectWithinAbsoluteError (axisOverlap (0.0f, pi), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (axisOverlap (halfPi, -halfPi), 0.0f, 1.0e-6f);

        beginTest ("Surface: a peak bumps out at its centre and nowhere else");
        {
            const auto bands = just (bandAt (1000.0f, 12.0f));
            expectWithinAbsoluteError (displacementAt (bands, 1000.0f, 0.0f), gainDisplacement (12.0f), 1.0e-5f);
            expectLessThan (std::abs (displacementAt (bands, 1000.0f, 180.0f)), 1.0e-4f, "the far side");
            expectLessThan (std::abs (displacementAt (bands, 20.0f, 0.0f)), 0.01f * gainDisplacement (12.0f), "far down the scale");
            expectLessThan (displacementAt (just (bandAt (1000.0f, -12.0f)), 1000.0f, 0.0f), 0.0f, "a cut dents in");
        }

        beginTest ("Surface: a muted band leaves it round");
        {
            auto b = bandAt (1000.0f, 12.0f);
            b.muted = true;
            expectEquals (displacementAt (just (b), 1000.0f, 0.0f), 0.0f);
        }

        beginTest ("Surface: a Mid cut takes out Mid lows and never touches Side");
        {
            const auto bands = just (bandAt (1000.0f, 0.0f, FilterType::lowCut));
            expectLessThan (displacementAt (bands, 100.0f, 0.0f), -0.5f);
            expectLessThan (std::abs (displacementAt (bands, 100.0f, 180.0f)), 1.0e-5f);
            expectLessThan (std::abs (displacementAt (bands, 15000.0f, 0.0f)), 1.0e-3f, "pass band");
        }

        beginTest ("Surface: a stereo peak is a belt all the way round");
        {
            auto b = bandAt (1000.0f, 12.0f, FilterType::peak, 40.0f);
            b.stereo = true;
            const auto bands = just (b);
            for (float lon : { -180.0f, -90.0f, 0.0f, 40.0f, 135.0f })
                expectWithinAbsoluteError (displacementAt (bands, 1000.0f, lon), gainDisplacement (12.0f), 1.0e-4f);
        }

        beginTest ("Surface: live gain overrides the set gain");
        {
            auto b = bandAt (1000.0f, 12.0f);
            b.hasLiveGain = true;
            b.liveGainDb = -6.0f;
            expectWithinAbsoluteError (displacementAt (just (b), 1000.0f, 0.0f), gainDisplacement (-6.0f), 1.0e-5f);
        }

        beginTest ("Band directions: frequency is up, Mid is the front, R is to the right");
        {
            const auto front = direction (std::sqrt (minFreq * maxFreq), 0.0f);
            expectWithinAbsoluteError (front.z, 1.0f, 1.0e-5f);
            expectGreaterThan (direction (std::sqrt (minFreq * maxFreq), 90.0f).x, 0.99f);
            expectGreaterThan (direction (19000.0f, 0.0f).y, 0.99f);

            Band b;
            setLatLon (b, direction (440.0f, -60.0f));
            expectWithinAbsoluteError (latToFreq (b.lat), 440.0f, 0.05f);
            expectWithinAbsoluteError (b.lon, -60.0f * (float) pi / 180.0f, 1.0e-5f);
        }

        beginTest ("BandSet: add, find, remove and free slots");
        {
            BandSet set;
            expectEquals (set.freeSlot(), 0);
            for (int i = 0; i < maxBands; ++i)
            {
                Band b;
                b.id = set.freeSlot();
                b.gainDb = (float) i;
                set.add (b);
            }
            expectEquals (set.count, maxBands);
            expectEquals (set.freeSlot(), -1, "full");
            set.add (Band {});
            expectEquals (set.count, maxBands, "adding to a full set does nothing");

            set.remove (4);
            expectEquals (set.count, maxBands - 1);
            expect (set.find (4) == nullptr);
            expectEquals (set.freeSlot(), 4, "the freed slot is reused");
            expectEquals (set.find (5)->gainDb, 5.0f, "the others keep their settings");
            set.remove (99);
            expectEquals (set.count, maxBands - 1, "removing a missing id does nothing");
        }
    }
};

static BandModelTests bandModelTests;

} // namespace threedq::test
