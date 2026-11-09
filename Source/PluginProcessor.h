/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SphereEq.h"

//==============================================================================
/**
*/
class _3DQAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    _3DQAudioProcessor();
    ~_3DQAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

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
    threedq::BandParams getBandParams (int slot) const;
    bool isBandMuted (int slot) const;

    // Plays only what this band works on; -1 = the normal EQ. Not a parameter, so
    // it's never automated or saved with the session.
    void setSoloBand (int slot) { soloBand.store (slot, std::memory_order_relaxed); }

    // Live dynamics readouts for the GUI: a band's current total gain and the
    // level its threshold is compared against.
    float getLiveGainDb (int slot) const  { return eq.getCurrentGainDb (slot); }
    float getLiveLevelDb (int slot) const { return eq.getCurrentLevelDb (slot); }

    juce::AudioProcessorValueTreeState state;

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
        std::atomic<float>* slope = nullptr; // choice index into threedq::cutSlopes
        std::atomic<float>* stereo = nullptr;
    };

    std::array<BandParamPointers, threedq::maxBands> bandParams;

    std::array<std::array<float, analysisRingSize>, 2> analysisRing {};
    std::atomic<int> analysisWritePos { 0 };
    std::atomic<int> soloBand { -1 };
    std::atomic<double> analysisSampleRate { 48000.0 };
    threedq::SphereEq eq;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3DQAudioProcessor)
};
