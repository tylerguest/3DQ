/*
  ==============================================================================

    3DQ editor: shows the native sphere (SphereView) and keeps its bands in sync
    with the processor's band parameters.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SphereView.h"
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
    // sphere -> processor
    void applyBandsFromSphere (const sphere::BandSet& bands);
    // processor -> sphere
    sphere::BandSet getBandsFromProcessor() const;

    // Runs on the sphere's render thread.
    void readLiveAudio (SphereView::LiveAudio& live);

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    static constexpr int timerHz = 30, minimumWidth = 440, minimumHeight = 340;

    _3DQAudioProcessor& audioProcessor;
    int hostWindowTicks = 0;

    // Set while we write parameters on the sphere's behalf, so those changes
    // aren't echoed back to it.
    std::atomic<bool> applyingSphereEdit { false };
    std::atomic<bool> hostChangedBands { false };

    StereoSpectrum spectrum;
    std::vector<float> recentLeft  = std::vector<float> ((size_t) StereoSpectrum::fftSize);
    std::vector<float> recentRight = std::vector<float> ((size_t) StereoSpectrum::fftSize);

    // Last, so its render thread stops before anything it reads is destroyed.
    SphereView sphereView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3DQAudioProcessorEditor)
};
