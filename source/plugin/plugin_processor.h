/*
  ==============================================================================

    The 3DQ processor: the band parameters, the EQ, the presets, and the recent
    output (and sidechain input) the editor's analyser reads.

    Buses: stereo in and out, plus an optional sidechain input (mono or
    stereo, off until the host routes something to it). Bands switched to the
    sidechain take their dynamics from it.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../dsp/sphere_eq.h"
#include "../model/preset_bank.h"

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
        juce::String enabled, freq, gain, q, pos, mute, range, threshold, type, slope, stereo, sidechain,
                     attack, release, ratio;

        // Every ID, for listening to them all.
        std::array<juce::String, 15> all() const
        {
            return { enabled, freq, gain, q, pos, mute, range, threshold, type, slope, stereo, sidechain, attack, release, ratio };
        }
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
    // Recent audio for the GUI's analyser. The audio thread writes into ring
    // buffers; the GUI copies the latest samples (a torn read only costs a
    // slightly glitchy frame of visualisation, never audio).
    static constexpr int analysisRingSize = 16384;

    // Copy the most recent numSamples (<= analysisRingSize) of each channel.
    void copyRecentOutput (float* left, float* right, int numSamples) const      { outputRing.copyRecent (left, right, numSamples); }
    void copyRecentSidechain (float* left, float* right, int numSamples) const   { sidechainRing.copyRecent (left, right, numSamples); }
    double getAnalysisSampleRate() const { return analysisSampleRate.load(); }

    // Whether sound has reached the sidechain input in the last moment: false
    // with the bus off, with nothing routed to it, or once the host stops
    // processing (so the GUI never shows a frozen sidechain).
    bool hasSidechainInput() const;

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
        std::atomic<float>* sidechain = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* ratio = nullptr;
    };

    struct AnalysisRing
    {
        void push (const float* left, const float* right, int numSamples);
        void copyRecent (float* left, float* right, int numSamples) const;

        std::array<std::array<float, analysisRingSize>, 2> samples {};
        std::atomic<int> writePos { 0 };
    };

    std::array<BandParamPointers, maxBands> bandParams;

    AnalysisRing outputRing, sidechainRing;
    std::atomic<juce::uint32> lastSidechainSoundMs { 0 }; // Time::getMillisecondCounter() when it was last heard
    std::atomic<bool> sidechainHeard { false };
    std::atomic<int> soloBand { -1 };
    std::atomic<double> analysisSampleRate { 48000.0 };
    dsp::SphereEq eq;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreeDQProcessor)
};

} // namespace threedq
