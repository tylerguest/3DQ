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
#include "SphereUi.h"

namespace
{
constexpr float degreesPerRadian = 180.0f / sphere::pi;
const juce::Identifier presetNameProperty ("presetName");

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
    sphereView.onBandsEdited = [this] (const sphere::BandSet& bands) { bandsEditedOnSphere (bands); };
    sphereView.onPresetCommand = [this] (sphere::PresetCommand command, int preset) { handlePresetCommand (command, preset); };
    sphereView.readLiveAudio = [this] (SphereView::LiveAudio& live) { readLiveAudio (live); };
    sphereView.onSoloChanged = [this] (int band) { audioProcessor.setSoloBand (band); };
    sphereView.setBands (getBandsFromProcessor());
    audioProcessor.presets.syncFromHost (getBandsFromProcessor(), sessionPresetName());
    updatePresetBar();
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
    // Solo is only for while you're looking at it: the track must not stay filtered.
    audioProcessor.setSoloBand (-1);

    for (int i = 0; i < threedq::maxBands; ++i)
    {
        const auto ids = _3DQAudioProcessor::getBandParamIDs (i);
        for (const auto& id : { ids.enabled, ids.freq, ids.gain, ids.q, ids.pos, ids.mute, ids.range, ids.threshold, ids.type, ids.slope, ids.stereo })
            audioProcessor.state.removeParameterListener (id, this);
    }
}

//==============================================================================
_3DQAudioProcessorEditor::DialogLook::DialogLook()
{
    using namespace sphere;
    const juce::Colour field (0xff1a1a18), line (0xff34332f);

    setColour (juce::AlertWindow::backgroundColourId, graphite);
    setColour (juce::AlertWindow::textColourId, textColour);
    setColour (juce::AlertWindow::outlineColourId, warmGrey.withAlpha (0.45f));
    setColour (juce::TextEditor::backgroundColourId, field);
    setColour (juce::TextEditor::textColourId, textColour);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, warmGrey);
    setColour (juce::TextEditor::highlightColourId, warmGrey.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, textColour);
    setColour (juce::TextButton::buttonColourId, field);
    setColour (juce::TextButton::buttonOnColourId, line);
    setColour (juce::TextButton::textColourOffId, textColour);
    setColour (juce::TextButton::textColourOnId, textColour);
    setColour (juce::ComboBox::outlineColourId, line);
}

void _3DQAudioProcessorEditor::bandsEditedOnSphere (const sphere::BandSet& bands)
{
    applyBandsFromSphere (bands);
    audioProcessor.presets.edited (bands);
    updatePresetBar();
}

juce::String _3DQAudioProcessorEditor::sessionPresetName() const
{
    return audioProcessor.state.state.getProperty (presetNameProperty).toString();
}

void _3DQAudioProcessorEditor::updatePresetBar()
{
    audioProcessor.state.state.setProperty (presetNameProperty, audioProcessor.presets.name(), nullptr);
    sphereView.setPresetInfo (audioProcessor.presets.info());
}

void _3DQAudioProcessorEditor::showPresetBankBands()
{
    applyBandsFromSphere (audioProcessor.presets.bands());
    sphereView.showNewBands (audioProcessor.presets.bands());
    updatePresetBar();
}

void _3DQAudioProcessorEditor::handlePresetCommand (sphere::PresetCommand command, int preset)
{
    using sphere::PresetCommand;
    auto& bank = audioProcessor.presets;
    const auto loaded = [this] (bool ok)
    {
        if (ok)
            showPresetBankBands();
        else
            showPresetError ("That preset couldn't be read.");
    };

    switch (command)
    {
        case PresetCommand::undo:      if (bank.undo()) showPresetBankBands(); break;
        case PresetCommand::redo:      if (bank.redo()) showPresetBankBands(); break;
        case PresetCommand::toggleAB:  bank.toggleAB(); showPresetBankBands(); break;
        case PresetCommand::copyAB:    bank.copyToOtherSlot(); updatePresetBar(); break;
        case PresetCommand::previous:  loaded (bank.step (-1)); break;
        case PresetCommand::next:      loaded (bank.step (1)); break;
        case PresetCommand::refresh:   bank.rescan(); updatePresetBar(); break;
        case PresetCommand::load:      loaded (bank.load (preset)); break;
        case PresetCommand::saveAs:    askToSaveAs(); break;
        case PresetCommand::save:
            if (! bank.save())
                showPresetError ("That preset couldn't be saved.");
            updatePresetBar();
            break;
        case PresetCommand::remove:
            if (! bank.remove (preset))
                showPresetError ("That preset couldn't be moved to the trash.");
            updatePresetBar();
            break;
    }
}

void _3DQAudioProcessorEditor::askToSaveAs()
{
    const auto current = audioProcessor.presets.name();
    const auto suggestion = sphere::PresetBank::isReservedName (current) ? juce::String ("New preset") : current;

    showPresetDialog ("Save preset", {}, suggestion, "Save", [this] (const juce::String& entered)
    {
        const auto name = sphere::PresetBank::legalName (entered);

        if (name.isEmpty())
            return;

        if (sphere::PresetBank::isReservedName (name))
        {
            showPresetError ("\"" + name + "\" is the built-in preset. Choose another name.");
            return;
        }

        if (audioProcessor.presets.exists (name))
            showPresetDialog ("Replace preset?", "\"" + name + "\" already exists.", {}, "Replace", [this, name] (const juce::String&) { saveUnderName (name); });
        else
            saveUnderName (name);
    });
}

void _3DQAudioProcessorEditor::saveUnderName (const juce::String& name)
{
    if (! audioProcessor.presets.saveAs (name))
        showPresetError ("That preset couldn't be saved.");

    updatePresetBar();
}

void _3DQAudioProcessorEditor::showPresetDialog (const juce::String& title, const juce::String& message, const juce::String& initialText,
                                                 const juce::String& confirmLabel, std::function<void (const juce::String&)> onConfirm)
{
    presetDialog = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon, this);
    presetDialog->setLookAndFeel (&dialogLook);
    presetDialog->setAlwaysOnTop (true);

    const bool asksForText = initialText.isNotEmpty();

    if (asksForText)
    {
        presetDialog->addTextEditor ("name", initialText);
        presetDialog->getTextEditor ("name")->selectAll();
    }

    presetDialog->addButton (confirmLabel, 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis = juce::Component::SafePointer (this), asksForText, onConfirm = std::move (onConfirm)] (int result)
        {
            if (safeThis == nullptr)
                return;

            const auto text = asksForText ? safeThis->presetDialog->getTextEditorContents ("name") : juce::String();
            safeThis->presetDialog.reset();

            if (result == 1)
                onConfirm (text);
        }), false);
}

void _3DQAudioProcessorEditor::showPresetError (const juce::String& message)
{
    showPresetDialog ("3DQ", message, {}, "OK", [] (const juce::String&) {});
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
    auto& bank = audioProcessor.presets;
    const auto sessionName = sessionPresetName();

    if (hostChangedBands.exchange (false) || (sessionName.isNotEmpty() && sessionName != bank.name()))
    {
        const auto bands = getBandsFromProcessor();
        sphereView.setBands (bands);
        bank.syncFromHost (bands, sessionName);
        updatePresetBar();
    }

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
