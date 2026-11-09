/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
_3DQAudioProcessor::_3DQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
      state (*this, nullptr, "ThreeDQ", createParameterLayout()) // XML tag names can't start with a digit
{
    for (int i = 0; i < threedq::maxBands; ++i)
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
_3DQAudioProcessor::BandParamIDs _3DQAudioProcessor::getBandParamIDs (int slot)
{
    const auto prefix = "band" + juce::String (slot + 1) + "_";
    return { prefix + "on", prefix + "freq", prefix + "gain", prefix + "q", prefix + "pos", prefix + "mute",
             prefix + "range", prefix + "thresh", prefix + "type", prefix + "slope", prefix + "stereo" };
}

juce::AudioProcessorValueTreeState::ParameterLayout _3DQAudioProcessor::createParameterLayout()
{
    using namespace threedq;
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

threedq::BandParams _3DQAudioProcessor::getBandParams (int slot) const
{
    const auto& p = bandParams[(size_t) slot];
    const auto type = (threedq::FilterType) std::clamp ((int) std::lround (p.type->load()), 0, 4);
    const auto slopeIndex = std::clamp ((int) std::lround (p.slope->load()), 0, (int) threedq::cutSlopes.size() - 1);
    return { p.enabled->load() >= 0.5f, p.freq->load(), p.gain->load(), p.q->load(), p.pos->load(),
             p.range->load(), p.threshold->load(), type, threedq::cutSlopes[(size_t) slopeIndex], p.stereo->load() >= 0.5f };
}

bool _3DQAudioProcessor::isBandMuted (int slot) const
{
    return bandParams[(size_t) slot].mute->load() >= 0.5f;
}

_3DQAudioProcessor::~_3DQAudioProcessor()
{
}

//==============================================================================
const juce::String _3DQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool _3DQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool _3DQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool _3DQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double _3DQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int _3DQAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int _3DQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void _3DQAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String _3DQAudioProcessor::getProgramName (int index)
{
    return {};
}

void _3DQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void _3DQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    eq.prepare (sampleRate);
    analysisSampleRate = sampleRate;
}

void _3DQAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool _3DQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // 3DQ works on the stereo field, so stereo in/out only.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void _3DQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2)
        return;

    for (int i = 0; i < threedq::maxBands; ++i)
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

void _3DQAudioProcessor::copyRecentOutput (float* left, float* right, int numSamples) const
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
bool _3DQAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* _3DQAudioProcessor::createEditor()
{
    return new _3DQAudioProcessorEditor (*this);
}

//==============================================================================
void _3DQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void _3DQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new _3DQAudioProcessor();
}
