/*
  ==============================================================================

    The EQ engine: stereo axes, band types, dynamics, solo, fades and staying
    well behaved under anything a host can throw at it.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/dsp/sphere_eq.h"

namespace threedq::test
{

namespace
{
constexpr double sr = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
};

// Runs a stereo signal through the EQ in blocks of blockSize.
Stereo run (dsp::SphereEq& eq, Stereo in, int blockSize = 512)
{
    const int n = (int) in.left.size();
    for (int start = 0; start < n; start += blockSize)
        eq.process (in.left.data() + start, in.right.data() + start, std::min (blockSize, n - start));
    return in;
}

// The same, with the detector of sidechained bands hearing `sidechain`.
Stereo runWithSidechain (dsp::SphereEq& eq, Stereo in, const Stereo& sidechain, int blockSize = 512)
{
    const int n = (int) in.left.size();
    for (int start = 0; start < n; start += blockSize)
        eq.process (in.left.data() + start, in.right.data() + start, std::min (blockSize, n - start),
                    sidechain.left.data() + start, sidechain.right.data() + start);
    return in;
}

Stereo mid (const std::vector<float>& x)      { return { x, x }; }

Stereo side (const std::vector<float>& x)
{
    Stereo s { x, x };
    for (auto& v : s.right)
        v = -v;
    return s;
}

BandParams peak (float freq, float gain, float pos = 0.0f, float q = 1.0f)
{
    BandParams p;
    p.enabled = true;
    p.freq = freq;
    p.gainDb = gain;
    p.q = q;
    p.posDeg = pos;
    return p;
}

BandParams ofType (FilterType type, float freq, float gain = 0.0f, int slope = 24)
{
    auto p = peak (freq, gain);
    p.type = type;
    p.slopeDbPerOct = slope;
    return p;
}

std::unique_ptr<dsp::SphereEq> makeEq (double rate = sr)
{
    auto eq = std::make_unique<dsp::SphereEq>();
    eq->prepare (rate);
    return eq;
}
}

class SphereEqTests final : public juce::UnitTest
{
public:
    SphereEqTests() : juce::UnitTest ("SphereEq", "dsp") {}

    void runTest() override
    {
        constexpr int n = 48000;

        beginTest ("No bands: the signal passes through untouched");
        {
            auto eq = makeEq();
            const Stereo in { noise (n, 0.5f, 1), noise (n, 0.5f, 2) };
            const auto out = run (*eq, in);
            expect (out.left == in.left && out.right == in.right);
        }

        beginTest ("A Mid peak boosts Mid by its gain");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, 12.0f));
            const auto in = mid (sine (1000.0, sr, n));
            const auto out = run (*eq, in);
            expectWithinAbsoluteError (gainDb (in.left, out.left), 12.0, 0.1);
            expectWithinAbsoluteError (gainDb (in.right, out.right), 12.0, 0.1);
        }

        beginTest ("A Mid peak leaves Side alone");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, 12.0f));
            const auto in = side (sine (1000.0, sr, n));
            const auto out = run (*eq, in);
            expectLessThan (maxDifference (in.left, out.left), 1.0e-6);
            expectLessThan (maxDifference (in.right, out.right), 1.0e-6);
        }

        beginTest ("A Side peak boosts Side and leaves Mid alone");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, -9.0f, 180.0f));
            const auto sideIn = side (sine (1000.0, sr, n));
            expectWithinAbsoluteError (gainDb (sideIn.left, run (*eq, sideIn).left), -9.0, 0.1);

            auto eq2 = makeEq();
            eq2->setBand (0, peak (1000.0f, -9.0f, 180.0f));
            const auto midIn = mid (sine (1000.0, sr, n));
            expectLessThan (maxDifference (midIn.left, run (*eq2, midIn).left), 1.0e-6);
        }

        beginTest ("A band hard left changes only the left channel");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, 12.0f, -90.0f));
            const Stereo in { sine (1000.0, sr, n), sine (1000.0, sr, n) };
            const auto out = run (*eq, in);
            expectWithinAbsoluteError (gainDb (in.left, out.left), 12.0, 0.1);
            expectLessThan (maxDifference (in.right, out.right), 1.0e-6);
        }

        beginTest ("A stereo band filters both channels alike, whatever its position");
        for (float pos : { -90.0f, 0.0f, 120.0f })
        {
            auto eq = makeEq();
            auto p = peak (1000.0f, 6.0f, pos);
            p.stereo = true;
            eq->setBand (0, p);
            const Stereo in { sine (1000.0, sr, n), std::vector<float> ((size_t) n, 0.0f) };
            const auto out = run (*eq, in);
            expectWithinAbsoluteError (gainDb (in.left, out.left), 6.0, 0.1);
            expectLessThan (rms (out.right), 1.0e-9, "nothing leaks into the silent channel");
        }

        beginTest ("Bands in series add up");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, 6.0f));
            eq->setBand (5, peak (1000.0f, 4.0f));
            const auto in = mid (sine (1000.0, sr, n));
            expectWithinAbsoluteError (gainDb (in.left, run (*eq, in).left), 10.0, 0.1);
        }

        beginTest ("A Mid low cut removes Mid lows and keeps Side lows");
        {
            auto eq = makeEq();
            eq->setBand (0, ofType (FilterType::lowCut, 1000.0f));
            const auto lowMid = mid (sine (100.0, sr, n));
            expectLessThan (gainDb (lowMid.left, run (*eq, lowMid).left), -60.0);

            auto eq2 = makeEq();
            eq2->setBand (0, ofType (FilterType::lowCut, 1000.0f));
            const auto lowSide = side (sine (100.0, sr, n));
            expectLessThan (maxDifference (lowSide.left, run (*eq2, lowSide).left), 1.0e-6);
        }

        beginTest ("A high cut keeps what's below it");
        {
            auto eq = makeEq();
            eq->setBand (0, ofType (FilterType::highCut, 2000.0f, 0.0f, 48));
            const auto low = mid (sine (200.0, sr, n));
            expectWithinAbsoluteError (gainDb (low.left, run (*eq, low).left), 0.0, 0.05);
        }

        beginTest ("A low shelf lifts the lows by its gain");
        {
            auto eq = makeEq();
            eq->setBand (0, ofType (FilterType::lowShelf, 1000.0f, 8.0f));
            const auto lows = mid (sine (40.0, sr, n));
            expectWithinAbsoluteError (gainDb (lows.left, run (*eq, lows).left), 8.0, 0.1);
        }

        beginTest ("A removed band fades out, then the signal is untouched again");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, 12.0f));
            run (*eq, mid (noise (n)));
            eq->setBand (0, {});
            run (*eq, mid (noise (n)));
            const auto in = mid (noise (n, 0.5f, 7));
            const auto out = run (*eq, in);
            expect (out.left == in.left && out.right == in.right);
        }

        beginTest ("Gain changes glide instead of clicking");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (100.0f, 0.0f, 0.0f, 0.5f));
            const auto settle = mid (sine (100.0, sr, n));
            run (*eq, settle);
            eq->setBand (0, peak (100.0f, 12.0f, 0.0f, 0.5f));
            const auto in = mid (sine (100.0, sr, 4800, 0.25f));
            const auto out = run (*eq, in);
            double biggestStep = 0;
            for (size_t i = 1; i < out.left.size(); ++i)
                biggestStep = std::max (biggestStep, (double) std::abs (out.left[i] - out.left[i - 1]));
            // A +12 dB, 100 Hz sine moves at most 2 pi f A / sr = 0.013 per sample.
            expectLessThan (biggestStep, 0.02);
        }

        beginTest ("A Q 40 notch takes out its own frequency and leaves its neighbours");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, -24.0f, 0.0f, maxQ));
            const auto at = mid (sine (1000.0, sr, n));
            expectWithinAbsoluteError (gainDb (at.left, run (*eq, at).left), -24.0, 0.3);
            const auto beside = mid (sine (1200.0, sr, n));
            expectGreaterThan (gainDb (beside.left, run (*eq, beside).left), -1.0);
        }

        beginTest ("Sweeping Q between its extremes stays finite and bounded");
        for (double rate : { 44100.0, 192000.0 })
        {
            auto eq = makeEq (rate);
            const auto in = mid (sine (1000.0, rate, (int) rate, 0.25f));
            auto out = in;
            float loudest = 0;
            bool finite = true;
            for (int start = 0, block = 0; start < (int) rate; start += 480, ++block)
            {
                eq->setBand (0, peak (block % 4 < 2 ? 1000.0f : 40.0f, -24.0f + 48.0f * (float) (block % 2), 0.0f, block % 3 == 0 ? minQ : maxQ));
                const int count = std::min (480, (int) rate - start);
                eq->process (out.left.data() + start, out.right.data() + start, count);
                for (int i = start; i < start + count; ++i)
                {
                    finite = finite && std::isfinite (out.left[(size_t) i]);
                    loudest = std::max (loudest, std::abs (out.left[(size_t) i]));
                }
            }
            expect (finite, "finite at " + juce::String (rate));
            expectLessThan (loudest, 0.25f * 31.7f * 2.0f, "never far past +30 dB at " + juce::String (rate));
        }

        beginTest ("A dynamic cut pulls a loud band down, within its range");
        {
            auto eq = makeEq();
            auto p = peak (1000.0f, 0.0f);
            p.rangeDb = -12.0f;
            p.thresholdDb = -30.0f;
            eq->setBand (0, p);
            run (*eq, mid (sine (1000.0, sr, n, 0.5f)));
            const float loudGain = eq->getCurrentGainDb (0);
            expectLessThan (loudGain, -5.0f);
            expectGreaterOrEqual (loudGain, -12.01f);
            expectGreaterThan (eq->getCurrentLevelDb (0), -30.0f);

            run (*eq, mid (sine (1000.0, sr, n, 0.0005f)));
            expectWithinAbsoluteError (eq->getCurrentGainDb (0), 0.0f, 0.05f, "quiet signal: back to static");
        }

        beginTest ("A dynamic boost lifts a loud band");
        {
            auto eq = makeEq();
            auto p = peak (1000.0f, 0.0f);
            p.rangeDb = 6.0f;
            p.thresholdDb = -40.0f;
            eq->setBand (0, p);
            run (*eq, mid (sine (1000.0, sr, n, 0.5f)));
            expectWithinAbsoluteError (eq->getCurrentGainDb (0), 6.0f, 0.01f);
        }

        beginTest ("The ratio sets how hard a band moves above its threshold");
        {
            // 0.1 amplitude at 1 kHz: about 23 dB over a -40 dB threshold, past the knee.
            auto movementAt = [&] (float ratio)
            {
                auto eq = makeEq();
                auto p = peak (1000.0f, 0.0f);
                p.rangeDb = -30.0f;
                p.thresholdDb = -40.0f;
                p.ratio = ratio;
                eq->setBand (0, p);
                run (*eq, mid (sine (1000.0, sr, n, 0.1f)));
                return eq->getCurrentGainDb (0);
            };

            // The detector follows the peak of the band's own axis: Mid is (L + R) / sqrt 2, 3 dB over each channel.
            const float over = 20.0f * std::log10 (0.1f * std::sqrt (2.0f)) + 40.0f;
            expectWithinAbsoluteError (movementAt (2.0f), -over * 0.5f, 1.0f, "2:1 takes back half");
            expectWithinAbsoluteError (movementAt (4.0f), -over * 0.75f, 1.0f, "4:1 takes back three quarters");
            expectWithinAbsoluteError (movementAt (1.0f), 0.0f, 0.01f, "1:1 does nothing");
            expectGreaterOrEqual (movementAt (20.0f), -30.01f, "never past the range");
        }

        beginTest ("A slow attack takes longer to pull a band down");
        {
            auto movementAfter = [&] (float attackMs, int samples)
            {
                auto eq = makeEq();
                auto p = peak (1000.0f, 0.0f);
                p.rangeDb = -12.0f;
                p.thresholdDb = -30.0f;
                p.attackMs = attackMs;
                eq->setBand (0, p);
                run (*eq, mid (sine (1000.0, sr, samples, 0.5f)));
                return eq->getCurrentGainDb (0);
            };

            const int tenMs = (int) (0.01 * sr);
            expectLessThan (movementAfter (1.0f, tenMs), -5.0f, "1 ms: there in 10 ms");
            expectGreaterThan (movementAfter (200.0f, tenMs), -2.0f, "200 ms: barely started after 10 ms");
            expectLessThan (movementAfter (200.0f, n), -5.0f, "200 ms: there in a second");
        }

        beginTest ("A slow release holds a band down for longer");
        {
            auto movementAfterStopping = [&] (float releaseMs)
            {
                auto eq = makeEq();
                auto p = peak (1000.0f, 0.0f);
                p.rangeDb = -12.0f;
                p.thresholdDb = -30.0f;
                p.releaseMs = releaseMs;
                eq->setBand (0, p);
                run (*eq, mid (sine (1000.0, sr, n, 0.5f)));
                run (*eq, mid (std::vector<float> ((size_t) (0.1 * sr), 0.0f))); // 100 ms of silence
                return eq->getCurrentGainDb (0);
            };

            expectGreaterThan (movementAfterStopping (10.0f), -0.5f, "10 ms: back in 100 ms");
            expectLessThan (movementAfterStopping (2000.0f), -5.0f, "2 s: still held down after 100 ms");
        }

        beginTest ("A sidechained band listens to the sidechain, not its own signal");
        {
            auto eq = makeEq();
            auto p = peak (1000.0f, 0.0f);
            p.rangeDb = -12.0f;
            p.thresholdDb = -30.0f;
            p.externalSidechain = true;
            eq->setBand (0, p);

            const auto loud = mid (sine (1000.0, sr, n, 0.5f)), quiet = mid (sine (1000.0, sr, n, 0.0005f));
            runWithSidechain (*eq, loud, quiet);
            expectWithinAbsoluteError (eq->getCurrentGainDb (0), 0.0f, 0.05f, "quiet sidechain: a loud track stays put");

            runWithSidechain (*eq, quiet, loud);
            expectLessThan (eq->getCurrentGainDb (0), -5.0f, "loud sidechain: a quiet track ducks");
            expectGreaterOrEqual (eq->getCurrentGainDb (0), -12.01f);
            expectGreaterThan (eq->getCurrentLevelDb (0), -30.0f, "the level is the sidechain's");

            run (*eq, loud);
            expectWithinAbsoluteError (eq->getCurrentGainDb (0), 0.0f, 0.05f, "no sidechain at all: back to static");
        }

        beginTest ("A sidechained band hears the sidechain on its own axis only");
        {
            auto eq = makeEq();
            auto p = peak (1000.0f, 0.0f); // Mid
            p.rangeDb = -12.0f;
            p.thresholdDb = -30.0f;
            p.externalSidechain = true;
            eq->setBand (0, p);
            const auto quiet = mid (sine (1000.0, sr, n, 0.0005f));

            runWithSidechain (*eq, quiet, side (sine (1000.0, sr, n, 0.5f)));
            expectWithinAbsoluteError (eq->getCurrentGainDb (0), 0.0f, 0.05f, "a Side-only sidechain doesn't reach a Mid band");

            p.stereo = true;
            eq->setBand (0, p);
            runWithSidechain (*eq, quiet, side (sine (1000.0, sr, n, 0.5f)));
            expectLessThan (eq->getCurrentGainDb (0), -5.0f, "a stereo band hears either channel");
        }

        beginTest ("A mono sidechain reaches a band at any position, Side included");
        for (float pos : { 0.0f, 180.0f, 90.0f })
        {
            auto eq = makeEq();
            auto p = peak (1000.0f, 0.0f, pos);
            p.rangeDb = -12.0f;
            p.thresholdDb = -30.0f;
            p.externalSidechain = true;
            eq->setBand (0, p);

            auto quiet = mid (sine (1000.0, sr, n, 0.0005f));
            const auto key = sine (1000.0, sr, n, 0.5f);
            for (int start = 0; start < n; start += 512)
                eq->process (quiet.left.data() + start, quiet.right.data() + start, std::min (512, n - start),
                             key.data() + start, key.data() + start); // one channel feeds both sides
            expectLessThan (eq->getCurrentGainDb (0), -5.0f, "position " + juce::String (pos));
        }

        beginTest ("A new sample rate keeps the band's attack and release times");
        {
            auto p = peak (1000.0f, 0.0f);
            p.rangeDb = -12.0f;
            p.thresholdDb = -30.0f;
            p.attackMs = 50.0f;
            const double rate = 96000.0;
            const auto burst = mid (sine (1000.0, rate, (int) (0.02 * rate), 0.5f));

            auto fresh = makeEq (rate);
            fresh->setBand (0, p);
            run (*fresh, burst);

            auto moved = makeEq (sr);
            moved->setBand (0, p);
            moved->prepare (rate); // the host changes rate; the band isn't set again
            run (*moved, burst);

            expectWithinAbsoluteError (moved->getCurrentGainDb (0), fresh->getCurrentGainDb (0), 1.0e-4f);
            expectLessThan (fresh->getCurrentGainDb (0), -0.5f, "the burst does move the band");
        }

        beginTest ("Solo plays what the band works on, from the EQ's input");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, 12.0f));
            eq->setSolo (peak (1000.0f, 12.0f, 0.0f, 2.0f));
            const auto atBand = mid (sine (1000.0, sr, n));
            expectWithinAbsoluteError (gainDb (atBand.left, run (*eq, atBand).left), 0.0, 0.2, "at the band: its own level, not boosted");

            const auto away = mid (sine (100.0, sr, n));
            expectLessThan (gainDb (away.left, run (*eq, away).left), -15.0, "away from the band");

            const auto sideIn = side (sine (1000.0, sr, n));
            expectLessThan (gainDb (sideIn.left, run (*eq, sideIn).left), -80.0, "a Mid band's solo has no Side");
        }

        beginTest ("Ending solo goes back to the EQ");
        {
            auto eq = makeEq();
            eq->setBand (0, peak (1000.0f, 6.0f));
            eq->setSolo (peak (1000.0f, 6.0f));
            run (*eq, mid (noise (n)));
            eq->clearSolo();
            run (*eq, mid (noise (n)));
            const auto in = mid (sine (1000.0, sr, n));
            expectWithinAbsoluteError (gainDb (in.left, run (*eq, in).left), 6.0, 0.1);
        }

        beginTest ("Stays finite and bounded under random automation at any rate");
        for (double rate : { 44100.0, 96000.0, 192000.0 })
        {
            auto eq = makeEq (rate);
            juce::Random random (42);
            float peakLevel = 0;
            bool finite = true;

            for (int block = 0; block < 400; ++block)
            {
                for (int slot = 0; slot < maxBands; ++slot)
                {
                    BandParams p;
                    p.enabled = random.nextFloat() < 0.8f;
                    p.freq = minFreq * std::pow (maxFreq / minFreq, random.nextFloat());
                    p.gainDb = (random.nextFloat() * 2.0f - 1.0f) * maxGainDb / 4.0f;
                    p.q = minQ + random.nextFloat() * (maxQ - minQ);
                    p.posDeg = random.nextFloat() * 360.0f - 180.0f;
                    p.rangeDb = random.nextFloat() < 0.3f ? (random.nextFloat() * 2.0f - 1.0f) * 12.0f : 0.0f;
                    p.type = (FilterType) random.nextInt (5);
                    p.slopeDbPerOct = cutSlopes[(size_t) random.nextInt ((int) cutSlopes.size())];
                    p.stereo = random.nextBool();
                    eq->setBand (slot, p);
                }

                if (block % 50 == 25)
                    eq->setSolo (peak (5000.0f, 0.0f));
                else if (block % 50 == 40)
                    eq->clearSolo();

                Stereo in { noise (256, 0.25f, block), noise (256, 0.25f, block + 1000) };
                const auto out = run (*eq, in, 1 + random.nextInt (256));

                for (size_t i = 0; i < out.left.size(); ++i)
                {
                    finite = finite && std::isfinite (out.left[i]) && std::isfinite (out.right[i]);
                    peakLevel = std::max ({ peakLevel, std::abs (out.left[i]), std::abs (out.right[i]) });
                }
            }

            expect (finite, "finite at " + juce::String (rate));
            expectLessThan (peakLevel, 100.0f, "bounded at " + juce::String (rate));
        }

        beginTest ("Bands near Nyquist at 44.1 kHz don't blow up");
        {
            auto eq = makeEq (44100.0);
            eq->setBand (0, peak (maxFreq, maxGainDb, 0.0f, maxQ));
            eq->setBand (1, ofType (FilterType::highShelf, maxFreq, maxGainDb));
            eq->setBand (2, ofType (FilterType::highCut, maxFreq, 0.0f, 48));
            const auto out = run (*eq, mid (noise (n)));
            bool finite = true;
            for (auto v : out.left)
                finite = finite && std::isfinite (v);
            expect (finite);
        }
    }
};

static SphereEqTests sphereEqTests;

} // namespace threedq::test
