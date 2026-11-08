/*
  ==============================================================================

    3DQ editor: hosts the web GUI (ui/) inside a juce::WebBrowserComponent and
    keeps the GUI's bands in sync with the processor's band parameters.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "StereoSpectrum.h"

//==============================================================================
class _3DQAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::Timer
{
public:
    _3DQAudioProcessorEditor (_3DQAudioProcessor&);
    ~_3DQAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    // GUI -> processor
    void applyBandsFromGui (const juce::var& bands);
    // processor -> GUI
    juce::var getBandsForGui() const;

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    _3DQAudioProcessor& audioProcessor;

    // Set while we write parameters on the GUI's behalf, so those changes
    // aren't echoed back to the GUI.
    std::atomic<bool> applyingGuiChange { false };
    std::atomic<bool> hostChangedBands { false };

    // Audio analysis sent to the GUI as a "spectrum" event every timer tick.
    void sendSpectrum();
    StereoSpectrum spectrum;
    std::vector<float> recentLeft  = std::vector<float> ((size_t) StereoSpectrum::fftSize);
    std::vector<float> recentRight = std::vector<float> ((size_t) StereoSpectrum::fftSize);

    juce::WebBrowserComponent webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3DQAudioProcessorEditor)
};
