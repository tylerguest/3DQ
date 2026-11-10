/*
  ==============================================================================

    The 3DQ processor: the band parameters, the EQ, the presets, and the recent
    output the editor's analyser reads.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../Dsp/SphereEq.h"
#include "../Model/PresetBank.h"

namespace threedq
{

class ThreeDQProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    ThreeDQProcessor();
    ~ThreeDQProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    //==============================================================================
    const juce::String getName() const override         { return JucePlugin_Name; }

    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    //==============================================================================
    // 3DQ's presets live in its own menu, so hosts see a single program.
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Band parameters: one fixed slot per possible band, so hosts can automate them.
    struct BandParamIDs
    {
        juce::String enabled, freq, gain, q, pos, mute, range, threshold, type, slope, stereo;
    };

    static BandParamIDs getBandParamIDs (int slot);

    // enabled = the slot holds a band (it's on the sphere), regardless of mute.
    BandParams getBandParams (int slot) const;
    bool isBandMuted (int slot) const;

    // Plays only what this band works on; -1 = the normal EQ. Not a parameter, so
    // it's never automated or saved with the session.
    void setSoloBand (int slot) { soloBand.store (slot, std::memory_order_relaxed); }

    // Live dynamics readouts for the GUI: a band's current total gain and the
    // level its threshold is compared against.
    float getLiveGainDb (int slot) const  { return eq.getCurrentGainDb (slot); }
    float getLiveLevelDb (int slot) const { return eq.getCurrentLevelDb (slot); }

    juce::AudioProcessorValueTreeState state;

    // The preset menu, A/B and undo; message thread only. The preset's name is
    // kept with the session in state's "presetName" property.
    PresetBank presets;

    //==============================================================================
    // Recent output audio for the GUI's analyser. The audio thread writes into a
    // ring buffer; the GUI copies the latest samples (a torn read only costs a
    // slightly glitchy frame of visualisation, never audio).
    static constexpr int analysisRingSize = 16384;

    // Copies the most recent numSamples (<= analysisRingSize) of each channel.
    void copyRecentOutput (float* left, float* right, int numSamples) const;
    double getAnalysisSampleRate() const { return analysisSampleRate.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct BandParamPointers
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* pos = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* range = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* type = nullptr;  // choice index: Peak, Low cut, High cut, Low shelf, High shelf
        std::atomic<float>* slope = nullptr; // choice index into cutSlopes
        std::atomic<float>* stereo = nullptr;
    };

    std::array<BandParamPointers, maxBands> bandParams;

    std::array<std::array<float, analysisRingSize>, 2> analysisRing {};
    std::atomic<int> analysisWritePos { 0 };
    std::atomic<int> soloBand { -1 };
    std::atomic<double> analysisSampleRate { 48000.0 };
    dsp::SphereEq eq;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreeDQProcessor)
};

} // namespace threedq
