/*
  ==============================================================================

    3DQ editor: shows the native sphere (SphereView) and keeps its bands in sync
    with the processor's band parameters.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "plugin_processor.h"
#include "../ui/sphere_view.h"
#include "../dsp/stereo_spectrum.h"

namespace threedq
{

class ThreeDQEditor  : public juce::AudioProcessorEditor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::Timer
{
public:
    explicit ThreeDQEditor (ThreeDQProcessor&);
    ~ThreeDQEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // sphere -> processor
    void applyBandsFromSphere (const BandSet& bands);
    // processor -> sphere
    BandSet getBandsFromProcessor() const;

    void handlePresetCommand (PresetCommand, int preset);
    void bandsEditedOnSphere (const BandSet& bands);
    void showPresetBankBands();  // after the bank's current setup changed
    void updatePresetBar();
    juce::String sessionPresetName() const;
    void askToSaveAs();
    void saveUnderName (const juce::String& name);
    void showPresetDialog (const juce::String& title, const juce::String& message, const juce::String& initialText,
                           const juce::String& confirmLabel, std::function<void (const juce::String&)> onConfirm);
    void showPresetError (const juce::String& message);

    // Runs on the sphere's render thread.
    void readLiveAudio (SphereView::LiveAudio& live);

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    static constexpr int timerHz = 30, minimumWidth = 440, minimumHeight = 340;

    ThreeDQProcessor& audioProcessor;
    int hostWindowTicks = 0;

    // Set while we write parameters on the sphere's behalf, so those changes
    // aren't echoed back to it.
    std::atomic<bool> applyingSphereEdit { false };
    std::atomic<bool> hostChangedBands { false };

    StereoSpectrum spectrum, sidechainSpectrum;
    std::vector<float> recentLeft  = std::vector<float> ((size_t) StereoSpectrum::fftSize);
    std::vector<float> recentRight = std::vector<float> ((size_t) StereoSpectrum::fftSize);

    struct DialogLook : juce::LookAndFeel_V4
    {
        DialogLook();
    };

    DialogLook dialogLook;
    std::unique_ptr<juce::AlertWindow> presetDialog; // after dialogLook, which it uses

    // Last, so its render thread stops before anything it reads is destroyed.
    SphereView sphereView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreeDQEditor)
};

} // namespace threedq
