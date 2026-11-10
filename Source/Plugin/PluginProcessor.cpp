/*
  ==============================================================================

    ThreeDQProcessor: see PluginProcessor.h.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace threedq
{

ThreeDQProcessor::ThreeDQProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "ThreeDQ", createParameterLayout()) // XML tag names can't start with a digit
{
    for (int i = 0; i < maxBands; ++i)
    {
        const auto ids = getBandParamIDs (i);
        auto& p = bandParams[(size_t) i];
        p.enabled = state.getRawParameterValue (ids.enabled);
        p.freq    = state.getRawParameterValue (ids.freq);
        p.gain    = state.getRawParameterValue (ids.gain);
        p.q       = state.getRawParameterValue (ids.q);
        p.pos     = state.getRawParameterValue (ids.pos);
        p.mute    = state.getRawParameterValue (ids.mute);
        p.range   = state.getRawParameterValue (ids.range);
        p.threshold = state.getRawParameterValue (ids.threshold);
        p.type    = state.getRawParameterValue (ids.type);
        p.slope   = state.getRawParameterValue (ids.slope);
        p.stereo  = state.getRawParameterValue (ids.stereo);
    }
}

//==============================================================================
ThreeDQProcessor::BandParamIDs ThreeDQProcessor::getBandParamIDs (int slot)
{
    const auto prefix = "band" + juce::String (slot + 1) + "_";
    return { prefix + "on", prefix + "freq", prefix + "gain", prefix + "q", prefix + "pos", prefix + "mute",
             prefix + "range", prefix + "thresh", prefix + "type", prefix + "slope", prefix + "stereo" };
}

juce::AudioProcessorValueTreeState::ParameterLayout ThreeDQProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto logRange = [] (float lo, float hi)
    {
        return juce::NormalisableRange<float> { lo, hi,
            [] (float start, float end, float v) { return start * std::pow (end / start, v); },
            [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); } };
    };

    for (int i = 0; i < maxBands; ++i)
    {
        const auto ids = getBandParamIDs (i);
        const auto name = "Band " + juce::String (i + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (i + 1),
                                                                             "Band " + juce::String (i + 1), " | ");

        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids.enabled, 1 }, name + "On", false));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.freq, 1 }, name + "Freq",
                                                                      logRange (minFreq, maxFreq), 1000.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.gain, 1 }, name + "Gain",
                                                                      juce::NormalisableRange<float> { -maxGainDb, maxGainDb, 0.01f }, 0.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("dB")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.q, 1 }, name + "Q",
                                                                      logRange (minQ, maxQ), 1.0f));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.pos, 1 }, name + "Stereo Pos",
                                                                      juce::NormalisableRange<float> { -180.0f, 180.0f, 0.1f }, 0.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("deg")));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids.mute, 1 }, name + "Mute", false));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.range, 1 }, name + "Dyn Range",
                                                                      juce::NormalisableRange<float> { -maxGainDb, maxGainDb, 0.01f }, 0.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("dB")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.threshold, 1 }, name + "Dyn Threshold",
                                                                      juce::NormalisableRange<float> { minThresholdDb, maxThresholdDb, 0.1f }, -30.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("dB")));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.type, 1 }, name + "Type",
                                                                       juce::StringArray { "Peak", "Low Cut", "High Cut", "Low Shelf", "High Shelf" }, 0));
        juce::StringArray slopeNames;
        for (auto s : cutSlopes)
            slopeNames.add (juce::String (s) + " dB/oct");
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.slope, 1 }, name + "Slope",
                                                                       slopeNames, 3)); // 24 dB/oct
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids.stereo, 1 }, name + "Stereo", false));
        layout.add (std::move (group));
    }

    return layout;
}

BandParams ThreeDQProcessor::getBandParams (int slot) const
{
    const auto& p = bandParams[(size_t) slot];
    const auto type = (FilterType) std::clamp ((int) std::lround (p.type->load()), 0, 4);
    const auto slopeIndex = std::clamp ((int) std::lround (p.slope->load()), 0, (int) cutSlopes.size() - 1);
    return { p.enabled->load() >= 0.5f, p.freq->load(), p.gain->load(), p.q->load(), p.pos->load(),
             p.range->load(), p.threshold->load(), type, cutSlopes[(size_t) slopeIndex], p.stereo->load() >= 0.5f };
}

bool ThreeDQProcessor::isBandMuted (int slot) const
{
    return bandParams[(size_t) slot].mute->load() >= 0.5f;
}

//==============================================================================
void ThreeDQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    eq.prepare (sampleRate);
    analysisSampleRate = sampleRate;
}

void ThreeDQProcessor::releaseResources()
{
}

bool ThreeDQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 3DQ works on the stereo field, so stereo in, stereo out.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

void ThreeDQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2)
        return;

    for (int i = 0; i < maxBands; ++i)
    {
        auto p = getBandParams (i);
        p.enabled = p.enabled && ! isBandMuted (i); // muted bands fade out like removed ones
        eq.setBand (i, p);
    }

    if (const int solo = soloBand.load (std::memory_order_relaxed); solo >= 0 && getBandParams (solo).enabled)
        eq.setSolo (getBandParams (solo));
    else
        eq.clearSolo();

    eq.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());

    // Feed the GUI's analyser with the processed output.
    int pos = analysisWritePos.load (std::memory_order_relaxed);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* src = buffer.getReadPointer (ch);
        int p = pos;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            analysisRing[(size_t) ch][(size_t) p] = src[i];
            p = (p + 1) % analysisRingSize;
        }
    }
    analysisWritePos.store ((pos + buffer.getNumSamples()) % analysisRingSize, std::memory_order_release);
}

void ThreeDQProcessor::copyRecentOutput (float* left, float* right, int numSamples) const
{
    jassert (numSamples <= analysisRingSize);
    const int end = analysisWritePos.load (std::memory_order_acquire);
    int p = (end - numSamples + analysisRingSize) % analysisRingSize;
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = analysisRing[0][(size_t) p];
        right[i] = analysisRing[1][(size_t) p];
        p = (p + 1) % analysisRingSize;
    }
}

//==============================================================================
juce::AudioProcessorEditor* ThreeDQProcessor::createEditor()
{
    return new ThreeDQEditor (*this);
}

//==============================================================================
void ThreeDQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ThreeDQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace threedq

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new threedq::ThreeDQProcessor();
}
