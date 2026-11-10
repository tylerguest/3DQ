/*
  ==============================================================================

    The processor: its parameters (which saved sessions depend on), session
    state, bus layouts, and audio through the whole plugin.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/plugin/plugin_processor.h"

namespace threedq::test
{

namespace
{
constexpr double sr = 48000.0;
constexpr int blockSize = 512;

void set (ThreeDQProcessor& p, const juce::String& id, float value)
{
    auto* param = p.state.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float get (ThreeDQProcessor& p, const juce::String& id)
{
    return p.state.getRawParameterValue (id)->load();
}

// Runs a stereo signal through the processor in host-sized blocks.
void run (ThreeDQProcessor& p, std::vector<float>& left, std::vector<float>& right)
{
    juce::MidiBuffer midi;
    for (size_t start = 0; start < left.size(); start += blockSize)
    {
        const int n = (int) std::min<size_t> (blockSize, left.size() - start);
        float* channels[] = { left.data() + start, right.data() + start };
        juce::AudioBuffer<float> buffer (channels, 2, n);
        p.processBlock (buffer, midi);
    }
}

std::unique_ptr<ThreeDQProcessor> makeProcessor()
{
    auto p = std::make_unique<ThreeDQProcessor>();
    p->prepareToPlay (sr, blockSize);
    return p;
}

void addPeak (ThreeDQProcessor& p, int slot, float freq, float gain)
{
    const auto ids = ThreeDQProcessor::getBandParamIDs (slot);
    set (p, ids.freq, freq);
    set (p, ids.gain, gain);
    set (p, ids.enabled, 1.0f);
}

double gainThrough (ThreeDQProcessor& p, double freq)
{
    const auto in = sine (freq, sr, 48000);
    auto left = in, right = in;
    run (p, left, right);
    return gainDb (in, left);
}
}

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("Processor", "plugin") {}

    void runTest() override
    {
        beginTest ("Parameter IDs, in order: changing them breaks saved sessions and automation");
        {
            ThreeDQProcessor p;
            juce::StringArray expected;
            for (int band = 1; band <= maxBands; ++band)
                for (auto name : { "on", "freq", "gain", "q", "pos", "mute", "range", "thresh", "type", "slope", "stereo" })
                    expected.add ("band" + juce::String (band) + "_" + name);

            juce::StringArray actual;
            bool allVersion1 = true;
            for (auto* param : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                {
                    actual.add (withId->paramID);
                    allVersion1 = allVersion1 && withId->getVersionHint() == 1;
                }

            expectEquals (actual.size(), 132);
            expect (actual == expected, "IDs or their order changed");
            expect (allVersion1, "every parameter has version hint 1");
        }

        beginTest ("Parameter defaults");
        {
            ThreeDQProcessor p;
            for (int slot = 0; slot < maxBands; ++slot)
            {
                const auto ids = ThreeDQProcessor::getBandParamIDs (slot);
                expectEquals (get (p, ids.enabled), 0.0f);
                expectWithinAbsoluteError (get (p, ids.freq), 1000.0f, 0.01f);
                expectEquals (get (p, ids.gain), 0.0f);
                expectWithinAbsoluteError (get (p, ids.q), 1.0f, 1.0e-4f);
                expectEquals (get (p, ids.pos), 0.0f);
                expectEquals (get (p, ids.mute), 0.0f);
                expectEquals (get (p, ids.range), 0.0f);
                expectEquals (get (p, ids.threshold), -30.0f);
                expectEquals (get (p, ids.type), 0.0f);
                expectEquals (get (p, ids.slope), 3.0f);
                expectEquals (get (p, ids.stereo), 0.0f);

                const auto bp = p.getBandParams (slot);
                expect (! bp.enabled && bp.type == FilterType::peak && bp.slopeDbPerOct == 24);
            }
        }

        beginTest ("Parameter ranges match the EQ's limits");
        {
            ThreeDQProcessor p;
            const auto ids = ThreeDQProcessor::getBandParamIDs (0);
            auto range = [&] (const juce::String& id) { return p.state.getParameterRange (id); };
            expectWithinAbsoluteError (range (ids.freq).start, minFreq, 1.0e-3f);
            expectWithinAbsoluteError (range (ids.freq).end, maxFreq, 1.0e-3f);
            expectEquals (range (ids.gain).start, -maxGainDb);
            expectEquals (range (ids.gain).end, maxGainDb);
            expectWithinAbsoluteError (range (ids.q).start, minQ, 1.0e-5f);
            expectWithinAbsoluteError (range (ids.q).end, maxQ, 1.0e-5f);
            expectEquals (range (ids.threshold).start, minThresholdDb);
            expectEquals (range (ids.threshold).end, maxThresholdDb);
            expectWithinAbsoluteError (range (ids.freq).convertFrom0to1 (0.5f), std::sqrt (minFreq * maxFreq), 0.1f, "frequency is log-scaled");
        }

        beginTest ("Band parameters reach getBandParams");
        {
            ThreeDQProcessor p;
            const auto ids = ThreeDQProcessor::getBandParamIDs (7);
            set (p, ids.enabled, 1.0f);
            set (p, ids.freq, 250.0f);
            set (p, ids.gain, -9.0f);
            set (p, ids.q, 2.5f);
            set (p, ids.pos, 45.0f);
            set (p, ids.range, 6.0f);
            set (p, ids.threshold, -20.0f);
            set (p, ids.type, 2.0f);
            set (p, ids.slope, 5.0f);
            set (p, ids.stereo, 1.0f);
            set (p, ids.mute, 1.0f);

            const auto bp = p.getBandParams (7);
            expect (bp.enabled, "a muted band is still on the sphere");
            expect (p.isBandMuted (7));
            expectWithinAbsoluteError (bp.freq, 250.0f, 0.1f);
            expectWithinAbsoluteError (bp.gainDb, -9.0f, 0.01f);
            expectWithinAbsoluteError (bp.q, 2.5f, 0.01f);
            expectWithinAbsoluteError (bp.posDeg, 45.0f, 0.1f);
            expectWithinAbsoluteError (bp.rangeDb, 6.0f, 0.01f);
            expectWithinAbsoluteError (bp.thresholdDb, -20.0f, 0.1f);
            expect (bp.type == FilterType::highCut);
            expectEquals (bp.slopeDbPerOct, 48);
            expect (bp.stereo);
        }

        beginTest ("State round-trips through the host, preset name included");
        {
            ThreeDQProcessor a;
            addPeak (a, 3, 640.0f, 7.5f);
            set (a, ThreeDQProcessor::getBandParamIDs (3).pos, -120.0f);
            set (a, ThreeDQProcessor::getBandParamIDs (11).type, 4.0f);
            a.state.state.setProperty ("presetName", "Crisp", nullptr);

            juce::MemoryBlock saved;
            a.getStateInformation (saved);

            ThreeDQProcessor b;
            b.setStateInformation (saved.getData(), (int) saved.getSize());
            for (auto* param : a.getParameters())
            {
                auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);
                expectWithinAbsoluteError (get (b, withId->paramID), get (a, withId->paramID), 1.0e-4f, withId->paramID);
            }
            expectEquals (b.state.state.getProperty ("presetName").toString(), juce::String ("Crisp"));

            juce::MemoryBlock again;
            b.getStateInformation (again);
            expect (again == saved, "saving the restored state gives the same bytes");
        }

        beginTest ("Garbage or foreign state is ignored");
        {
            ThreeDQProcessor p;
            addPeak (p, 0, 500.0f, 3.0f);
            const char garbage[] = "definitely not a plugin state";
            p.setStateInformation (garbage, (int) sizeof (garbage));
            p.setStateInformation (nullptr, 0);

            juce::MemoryBlock foreign;
            juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("SomeOtherPlugin"), foreign);
            p.setStateInformation (foreign.getData(), (int) foreign.getSize());

            expectEquals (get (p, ThreeDQProcessor::getBandParamIDs (0).enabled), 1.0f);
            expectWithinAbsoluteError (get (p, ThreeDQProcessor::getBandParamIDs (0).gain), 3.0f, 0.01f);
        }

        beginTest ("Bus layouts: stereo in and out only");
        {
            ThreeDQProcessor p;
            auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out)
            {
                juce::AudioProcessor::BusesLayout l;
                l.inputBuses.add (in);
                l.outputBuses.add (out);
                return l;
            };
            using Set = juce::AudioChannelSet;
            expect (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())));
            expect (! p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono())));
            expect (! p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
            expect (! p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())));
            expectEquals (p.getTotalNumInputChannels(), 2);
            expectEquals (p.getTotalNumOutputChannels(), 2);
        }

        beginTest ("With no bands, audio passes through untouched");
        {
            auto p = makeProcessor();
            const auto inL = noise (48000, 0.5f, 11), inR = noise (48000, 0.5f, 12);
            auto left = inL, right = inR;
            run (*p, left, right);
            expect (left == inL && right == inR);
        }

        beginTest ("A band changes the sound; muting it takes it out again");
        {
            auto p = makeProcessor();
            addPeak (*p, 0, 1000.0f, 12.0f);
            expectWithinAbsoluteError (gainThrough (*p, 1000.0), 12.0, 0.1);

            set (*p, ThreeDQProcessor::getBandParamIDs (0).mute, 1.0f);
            gainThrough (*p, 1000.0); // fade out
            const auto inL = noise (4800, 0.5f, 13);
            auto left = inL, right = inL;
            run (*p, left, right);
            expect (left == inL && right == inL, "muted: untouched");

            set (*p, ThreeDQProcessor::getBandParamIDs (0).mute, 0.0f);
            expectWithinAbsoluteError (gainThrough (*p, 1000.0), 12.0, 0.1, "unmuted: back");
        }

        beginTest ("Solo plays only the band's region, and ends cleanly");
        {
            auto p = makeProcessor();
            addPeak (*p, 2, 1000.0f, 6.0f);
            p->setSoloBand (2);
            expectWithinAbsoluteError (gainThrough (*p, 1000.0), 0.0, 0.2);
            expectLessThan (gainThrough (*p, 100.0), -15.0);

            p->setSoloBand (5); // a slot with no band: the normal EQ
            expectWithinAbsoluteError (gainThrough (*p, 1000.0), 6.0, 0.1);
            p->setSoloBand (-1);
            expectWithinAbsoluteError (gainThrough (*p, 1000.0), 6.0, 0.1);
        }

        beginTest ("The analyser gets the latest output");
        {
            auto p = makeProcessor();
            addPeak (*p, 0, 1000.0f, 6.0f);
            auto left = noise (20000, 0.5f, 21), right = noise (20000, 0.5f, 22);
            run (*p, left, right);

            constexpr int n = 4096;
            std::vector<float> gotL (n), gotR (n);
            p->copyRecentOutput (gotL.data(), gotR.data(), n);
            expect (std::equal (gotL.begin(), gotL.end(), left.end() - n));
            expect (std::equal (gotR.begin(), gotR.end(), right.end() - n));
            expectEquals (p->getAnalysisSampleRate(), sr);
        }

        beginTest ("Reports itself as an effect with one program");
        {
            ThreeDQProcessor p;
            expectEquals (p.getName(), juce::String ("3DQ"));
            expect (! p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect() && p.hasEditor());
            expectEquals (p.getNumPrograms(), 1);
        }
    }
};

static ProcessorTests processorTests;

} // namespace threedq::test
