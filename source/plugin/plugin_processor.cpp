/*
  ==============================================================================

    ThreeDQProcessor: see plugin_processor.h.

  ==============================================================================
*/

#include <algorithm>
#include "plugin_processor.h"
#include "plugin_editor.h"

namespace threedq
{

ThreeDQProcessor::ThreeDQProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state (*this, nullptr, "ThreeDQ", createParameterLayout()) // XML tag names can't start with a digit
{
    inputGain = state.getRawParameterValue (inputGainID);
    outputGain = state.getRawParameterValue (outputGainID);
    bypass = state.getRawParameterValue (bypassID);

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
        p.sidechain = state.getRawParameterValue (ids.sidechain);
        p.attack  = state.getRawParameterValue (ids.attack);
        p.release = state.getRawParameterValue (ids.release);
        p.ratio   = state.getRawParameterValue (ids.ratio);
    }
}

//==============================================================================
ThreeDQProcessor::BandParamIDs ThreeDQProcessor::getBandParamIDs (int slot)
{
    const auto prefix = "band" + juce::String (slot + 1) + "_";
    return { prefix + "on", prefix + "freq", prefix + "gain", prefix + "q", prefix + "pos", prefix + "mute",
             prefix + "range", prefix + "thresh", prefix + "type", prefix + "slope", prefix + "stereo",
             prefix + "sc", prefix + "atk", prefix + "rel", prefix + "ratio" };
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
                                                                       juce::StringArray { "Peak", "High Pass", "Low Pass", "Low Shelf", "High Shelf" }, 0));
        juce::StringArray slopeNames;
        for (auto s : cutSlopes)
            slopeNames.add (juce::String (s) + " dB/oct");
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.slope, 1 }, name + "Slope",
                                                                       slopeNames, 3)); // 24 dB/oct
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids.stereo, 1 }, name + "Stereo", false));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids.sidechain, 1 }, name + "Dyn Sidechain", false));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.attack, 1 }, name + "Dyn Attack",
                                                                      logRange (minAttackMs, maxAttackMs), defaultAttackMs,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("ms")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.release, 1 }, name + "Dyn Release",
                                                                      logRange (minReleaseMs, maxReleaseMs), defaultReleaseMs,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("ms")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.ratio, 1 }, name + "Dyn Ratio",
                                                                      logRange (minRatio, maxRatio), defaultRatio,
                                                                      juce::AudioParameterFloatAttributes().withLabel (":1")));
        layout.add (std::move (group));
    }

    for (const auto& [id, name] : { std::pair { inputGainID, "Input" }, std::pair { outputGainID, "Output" } })
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                                 juce::NormalisableRange<float> (-maxTrimGainDb, maxTrimGainDb), 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bypassID, 1 }, "Bypass", false));
    return layout;
}

BandParams ThreeDQProcessor::getBandParams (int slot) const
{
    const auto& p = bandParams[(size_t) slot];
    const auto type = (FilterType) std::clamp ((int) std::lround (p.type->load()), 0, 4);
    const auto slopeIndex = std::clamp ((int) std::lround (p.slope->load()), 0, (int) cutSlopes.size() - 1);
    return { p.enabled->load() >= 0.5f, p.freq->load(), p.gain->load(), p.q->load(), p.pos->load(),
             p.range->load(), p.threshold->load(), type, cutSlopes[(size_t) slopeIndex], p.stereo->load() >= 0.5f,
             p.sidechain->load() >= 0.5f, p.attack->load(), p.release->load(), p.ratio->load() };
}

bool ThreeDQProcessor::isBandMuted (int slot) const
{
    return bandParams[(size_t) slot].mute->load() >= 0.5f;
}

//==============================================================================
void ThreeDQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    eq.prepare (sampleRate);
    dryBuffer.setSize (2, std::max (samplesPerBlock, 1));
    bypassMix.reset (sampleRate, bypassFadeSeconds);
    bypassMix.setCurrentAndTargetValue (bypass->load() >= 0.5f ? 1.0f : 0.0f);
    for (auto [level, db] : { std::pair { &inputLevel, inputGain }, std::pair { &outputLevel, outputGain } })
    {
        level->reset (sampleRate, 0.03);
        level->setCurrentAndTargetValue (juce::Decibels::decibelsToGain (db->load()));
    }
    analysisSampleRate = sampleRate;
}

void ThreeDQProcessor::releaseResources()
{
}

bool ThreeDQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 3DQ works on the stereo field, so stereo in, stereo out. The sidechain
    // can be off, mono or stereo.
    const auto sidechain = layouts.inputBuses.size() > 1 ? layouts.getChannelSet (true, 1) : juce::AudioChannelSet::disabled();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo());
}

void ThreeDQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2)
        return;

    // The sidechain, if the host has turned it on; a mono one feeds both sides.
    const float* sidechainLeft = nullptr;
    const float* sidechainRight = nullptr;

    if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0)
    {
        const auto sidechain = getBusBuffer (buffer, true, 1);
        sidechainLeft = sidechain.getReadPointer (0);
        sidechainRight = sidechain.getReadPointer (sidechain.getNumChannels() > 1 ? 1 : 0);
    }

    if (sidechainLeft != nullptr)
    {
        // Anything above about -100 dBFS counts as sound arriving.
        constexpr float audible = 1.0e-5f;
        const auto n = (size_t) buffer.getNumSamples();
        const bool sound = std::any_of (sidechainLeft, sidechainLeft + n, [] (float x) { return std::abs (x) > audible; })
                        || std::any_of (sidechainRight, sidechainRight + n, [] (float x) { return std::abs (x) > audible; });

        if (sound)
        {
            lastSidechainSoundMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
            sidechainHeard.store (true, std::memory_order_relaxed);
        }
    }

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

    // Read before the EQ writes the main channels, in case the host shares buffers.
    if (sidechainLeft != nullptr)
        sidechainRing.push (sidechainLeft, sidechainRight, buffer.getNumSamples());

    // Bypassed or fading: keep the input as it came, to fade to. The EQ keeps
    // running underneath, so coming back is as smooth as going.
    const int numSamples = buffer.getNumSamples();
    bypassMix.setTargetValue (bypass->load (std::memory_order_relaxed) >= 0.5f ? 1.0f : 0.0f);
    const bool bypassing = bypassMix.isSmoothing() || bypassMix.getTargetValue() > 0.0f;

    if (bypassing)
    {
        if (dryBuffer.getNumSamples() < numSamples) // a host sent a bigger block than it promised
            dryBuffer.setSize (2, numSamples, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // The input gain, then the input level for the meter.
    applyGain (inputLevel, inputGain->load (std::memory_order_relaxed), buffer);
    holdPeaks (buffer, inputPeak);

    eq.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(), sidechainLeft, sidechainRight);
    applyGain (outputLevel, outputGain->load (std::memory_order_relaxed), buffer);

    if (bypassing && ! bypassMix.isSmoothing())
    {
        for (int ch = 0; ch < 2; ++ch) // fully bypassed: the input exactly
            buffer.copyFrom (ch, 0, dryBuffer, ch, 0, numSamples);
    }
    else if (bypassing)
    {
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const auto* dryLeft = dryBuffer.getReadPointer (0);
        const auto* dryRight = dryBuffer.getReadPointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const float mix = bypassMix.getNextValue();
            left[i] += (dryLeft[i] - left[i]) * mix;
            right[i] += (dryRight[i] - right[i]) * mix;
        }
    }

    holdPeaks (buffer, outputPeak);

    // Feed the GUI's analyser with the processed output.
    outputRing.push (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());
    lastBlockMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    processedAny.store (true, std::memory_order_relaxed);
}

// Raises each channel's held peak to this block's, if it's louder.
void ThreeDQProcessor::holdPeaks (const juce::AudioBuffer<float>& buffer, HeldPeaks& peaks)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto range = buffer.findMinMax (ch, 0, buffer.getNumSamples());
        const float peak = std::max (-range.getStart(), range.getEnd());
        auto& held = peaks[(size_t) ch];
        if (peak > held.load (std::memory_order_relaxed))
            held.store (peak, std::memory_order_relaxed); // a read racing this only loses one block's peak
    }
}

// Scales both channels by a gain in dB, gliding to a new one so a change never clicks.
void ThreeDQProcessor::applyGain (GainGlide& level, float db, juce::AudioBuffer<float>& buffer)
{
    level.setTargetValue (juce::Decibels::decibelsToGain (db));
    if (! level.isSmoothing() && level.getTargetValue() == 1.0f)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float gain = level.getNextValue();
        left[i] *= gain;
        right[i] *= gain;
    }
}

bool ThreeDQProcessor::isProcessing() const
{
    constexpr juce::uint32 holdMs = 250;
    return processedAny.load (std::memory_order_relaxed)
        && juce::Time::getMillisecondCounter() - lastBlockMs.load (std::memory_order_relaxed) < holdMs;
}

bool ThreeDQProcessor::hasSidechainInput() const
{
    // Held briefly, so a gap between notes doesn't flicker it off.
    constexpr juce::uint32 holdMs = 500;
    return sidechainHeard.load (std::memory_order_relaxed)
        && juce::Time::getMillisecondCounter() - lastSidechainSoundMs.load (std::memory_order_relaxed) < holdMs;
}

void ThreeDQProcessor::AnalysisRing::push (const float* left, const float* right, int numSamples)
{
    const int pos = writePos.load (std::memory_order_relaxed);

    for (const auto [ch, src] : { std::pair { 0, left }, std::pair { 1, right } })
    {
        int p = pos;
        for (int i = 0; i < numSamples; ++i)
        {
            samples[(size_t) ch][(size_t) p] = src[i];
            p = (p + 1) % analysisRingSize;
        }
    }

    writePos.store ((pos + numSamples) % analysisRingSize, std::memory_order_release);
}

void ThreeDQProcessor::AnalysisRing::copyRecent (float* left, float* right, int numSamples) const
{
    jassert (numSamples <= analysisRingSize);
    const int end = writePos.load (std::memory_order_acquire);
    int p = (end - numSamples + analysisRingSize) % analysisRingSize;
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = samples[0][(size_t) p];
        right[i] = samples[1][(size_t) p];
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
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    const auto restored = juce::ValueTree::fromXml (*xml);
    state.replaceState (restored);

    // A state saved before a parameter existed doesn't mention it, and JUCE
    // would leave it at whatever it was. It belongs at its default instead.
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (! restored.getChildWithProperty ("id", ranged->getParameterID()).isValid())
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

} // namespace threedq

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new threedq::ThreeDQProcessor();
}
