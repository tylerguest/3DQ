/*
  ==============================================================================

    3DQ editor: shows the native sphere and keeps its bands in sync with the
    processor's band parameters.

    Band sync:
      - The sphere reports every edit through onBandsEdited; a band's id is its
        parameter slot (0..maxBands-1).
      - When the host changes band parameters (automation, preset load, undo),
        the editor pushes the processor's bands back to the sphere.

    The browser preview in ui/ is separate: it isn't part of the plugin.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostWindow.h"

namespace
{
constexpr float degreesPerRadian = 180.0f / sphere::pi;

int slopeIndexFromDb (int slope)
{
    // Nearest available slope.
    int best = 0;
    for (size_t i = 0; i < threedq::cutSlopes.size(); ++i)
        if (std::abs (threedq::cutSlopes[i] - slope) < std::abs (threedq::cutSlopes[(size_t) best] - slope))
            best = (int) i;
    return best;
}
}

//==============================================================================
_3DQAudioProcessorEditor::_3DQAudioProcessorEditor (_3DQAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    sphereView.onBandsEdited = [this] (const sphere::BandSet& bands) { applyBandsFromSphere (bands); };
    sphereView.readLiveAudio = [this] (SphereView::LiveAudio& live) { readLiveAudio (live); };
    sphereView.setBands (getBandsFromProcessor());
    addAndMakeVisible (sphereView);

    for (int i = 0; i < threedq::maxBands; ++i)
    {
        const auto ids = _3DQAudioProcessor::getBandParamIDs (i);
        for (const auto& id : { ids.enabled, ids.freq, ids.gain, ids.q, ids.pos, ids.mute, ids.range, ids.threshold, ids.type, ids.slope, ids.stereo })
            audioProcessor.state.addParameterListener (id, this);
    }

    startTimerHz (timerHz);

    setResizable (true, true);
    setResizeLimits (minimumWidth, minimumHeight, 8192, 8192); // the band panel needs this much room
    setSize (520, 520);
}

_3DQAudioProcessorEditor::~_3DQAudioProcessorEditor()
{
    for (int i = 0; i < threedq::maxBands; ++i)
    {
        const auto ids = _3DQAudioProcessor::getBandParamIDs (i);
        for (const auto& id : { ids.enabled, ids.freq, ids.gain, ids.q, ids.pos, ids.mute, ids.range, ids.threshold, ids.type, ids.slope, ids.stereo })
            audioProcessor.state.removeParameterListener (id, this);
    }
}

//==============================================================================
void _3DQAudioProcessorEditor::applyBandsFromSphere (const sphere::BandSet& bands)
{
    applyingSphereEdit = true;
    const juce::ScopeGuard resetFlag { [this] { applyingSphereEdit = false; } };

    std::array<const sphere::Band*, threedq::maxBands> bySlot {};
    for (const auto& b : bands)
        if (b.id >= 0 && b.id < threedq::maxBands)
            bySlot[(size_t) b.id] = &b;

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = audioProcessor.state.getParameter (id))
        {
            const auto normalised = param->convertTo0to1 (value);
            if (std::abs (param->getValue() - normalised) > 1.0e-6f)
                param->setValueNotifyingHost (normalised);
        }
    };

    for (int slot = 0; slot < threedq::maxBands; ++slot)
    {
        const auto ids = _3DQAudioProcessor::getBandParamIDs (slot);

        if (const auto* b = bySlot[(size_t) slot])
        {
            set (ids.freq, sphere::latToFreq (b->lat));
            set (ids.gain, b->gainDb);
            set (ids.q, b->q);
            set (ids.pos, b->lon * degreesPerRadian);
            set (ids.mute, b->muted ? 1.0f : 0.0f);
            set (ids.range, b->range);
            set (ids.threshold, b->threshold);
            set (ids.type, (float) (int) b->type);
            set (ids.slope, (float) slopeIndexFromDb (b->slope));
            set (ids.stereo, b->stereo ? 1.0f : 0.0f);
            set (ids.enabled, 1.0f);
        }
        else
        {
            set (ids.enabled, 0.0f);
            set (ids.mute, 0.0f);
            set (ids.range, 0.0f);
            set (ids.type, 0.0f);
        }
    }
}

sphere::BandSet _3DQAudioProcessorEditor::getBandsFromProcessor() const
{
    sphere::BandSet result;

    for (int slot = 0; slot < threedq::maxBands; ++slot)
    {
        const auto p = audioProcessor.getBandParams (slot);
        if (! p.enabled)
            continue;

        sphere::Band b;
        b.id = slot;
        b.lat = sphere::freqToLat (p.freq);
        b.lon = p.posDeg / degreesPerRadian;
        b.gainDb = p.gainDb;
        b.q = p.q;
        b.muted = audioProcessor.isBandMuted (slot);
        b.range = p.rangeDb;
        b.threshold = p.thresholdDb;
        b.type = p.type;
        b.slope = p.slopeDbPerOct;
        b.stereo = p.stereo;
        result.add (b);
    }

    return result;
}

void _3DQAudioProcessorEditor::readLiveAudio (SphereView::LiveAudio& live)
{
    audioProcessor.copyRecentOutput (recentLeft.data(), recentRight.data(), StereoSpectrum::fftSize);
    live.spectrum = spectrum.analyse (recentLeft.data(), recentRight.data(), audioProcessor.getAnalysisSampleRate());

    for (int i = 0; i < threedq::maxBands; ++i)
    {
        live.gainDb[(size_t) i]  = audioProcessor.getLiveGainDb (i);
        live.levelDb[(size_t) i] = audioProcessor.getLiveLevelDb (i);
    }
}

void _3DQAudioProcessorEditor::parameterChanged (const juce::String&, float)
{
    // May arrive on the audio thread (host automation); just flag it.
    if (! applyingSphereEdit)
        hostChangedBands = true;
}

void _3DQAudioProcessorEditor::timerCallback()
{
    if (hostChangedBands.exchange (false))
        sphereView.setBands (getBandsFromProcessor());

    // A few times a second, so the window comes back promptly if it's dragged too small.
    if (++hostWindowTicks >= timerHz / 3)
    {
        hostWindowTicks = 0;

        if (auto* peer = getPeer(); peer != nullptr && juce::PluginHostType().isReaper())
        {
            const auto scale = peer->getPlatformScaleFactor();
            threedq::keepHostWindowLargeEnough (peer->getNativeHandle(), juce::roundToInt (minimumWidth * scale), juce::roundToInt (minimumHeight * scale));
        }
    }
}

//==============================================================================
void _3DQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111110));
}

void _3DQAudioProcessorEditor::resized()
{
    sphereView.setBounds (getLocalBounds());
}
