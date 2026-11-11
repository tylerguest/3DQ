/*
  ==============================================================================

    3DQ editor: shows the native sphere and keeps its bands in sync with the
    processor's band parameters.

    Band sync:
      - The sphere reports every edit through onBandsEdited; a band's id is its
        parameter slot (0..maxBands-1).
      - When the host changes band parameters (automation, preset load, undo),
        the editor pushes the processor's bands back to the sphere.

  ==============================================================================
*/

#include "plugin_processor.h"
#include "plugin_editor.h"
#include "host_window.h"
#include "../ui/theme.h"

namespace threedq
{

namespace
{
constexpr float degreesPerRadian = 180.0f / pi;
const juce::Identifier presetNameProperty ("presetName");

int slopeIndexFromDb (int slope)
{
    // Nearest available slope.
    int best = 0;
    for (size_t i = 0; i < cutSlopes.size(); ++i)
        if (std::abs (cutSlopes[i] - slope) < std::abs (cutSlopes[(size_t) best] - slope))
            best = (int) i;
    return best;
}
}

//==============================================================================
ThreeDQEditor::ThreeDQEditor (ThreeDQProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    sphereView.onBandsEdited = [this] (const BandSet& bands) { bandsEditedOnSphere (bands); };
    sphereView.onPresetCommand = [this] (PresetCommand command, int preset) { handlePresetCommand (command, preset); };
    sphereView.readLiveAudio = [this] (SphereView::LiveAudio& live) { readLiveAudio (live); };
    sphereView.onSoloChanged = [this] (int band) { audioProcessor.setSoloBand (band); };
    sphereView.setBands (getBandsFromProcessor());
    audioProcessor.presets.syncFromHost (getBandsFromProcessor(), sessionPresetName());
    updatePresetBar();
    addAndMakeVisible (sphereView);

    for (int i = 0; i < maxBands; ++i)
    {
        const auto ids = ThreeDQProcessor::getBandParamIDs (i);
        for (const auto& id : ids.all())
            audioProcessor.state.addParameterListener (id, this);
    }

    startTimerHz (timerHz);

    setResizable (true, true);
    setResizeLimits (minimumWidth, minimumHeight, 8192, 8192); // the band panel needs this much room
    setSize (520, 520);
}

ThreeDQEditor::~ThreeDQEditor()
{
    // Solo is only for while you're looking at it: the track must not stay filtered.
    audioProcessor.setSoloBand (-1);

    for (int i = 0; i < maxBands; ++i)
    {
        const auto ids = ThreeDQProcessor::getBandParamIDs (i);
        for (const auto& id : ids.all())
            audioProcessor.state.removeParameterListener (id, this);
    }
}

//==============================================================================
ThreeDQEditor::DialogLook::DialogLook()
{
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

void ThreeDQEditor::bandsEditedOnSphere (const BandSet& bands)
{
    applyBandsFromSphere (bands);
    audioProcessor.presets.edited (bands);
    updatePresetBar();
}

juce::String ThreeDQEditor::sessionPresetName() const
{
    return audioProcessor.state.state.getProperty (presetNameProperty).toString();
}

void ThreeDQEditor::updatePresetBar()
{
    audioProcessor.state.state.setProperty (presetNameProperty, audioProcessor.presets.name(), nullptr);
    sphereView.setPresetInfo (audioProcessor.presets.info());
}

void ThreeDQEditor::showPresetBankBands()
{
    applyBandsFromSphere (audioProcessor.presets.bands());
    sphereView.showNewBands (audioProcessor.presets.bands());
    updatePresetBar();
}

void ThreeDQEditor::handlePresetCommand (PresetCommand command, int preset)
{
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

void ThreeDQEditor::askToSaveAs()
{
    const auto current = audioProcessor.presets.name();
    const auto suggestion = PresetBank::isReservedName (current) ? juce::String ("New preset") : current;

    showPresetDialog ("Save preset", {}, suggestion, "Save", [this] (const juce::String& entered)
    {
        const auto name = PresetBank::legalName (entered);

        if (name.isEmpty())
            return;

        if (PresetBank::isReservedName (name))
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

void ThreeDQEditor::saveUnderName (const juce::String& name)
{
    if (! audioProcessor.presets.saveAs (name))
        showPresetError ("That preset couldn't be saved.");

    updatePresetBar();
}

void ThreeDQEditor::showPresetDialog (const juce::String& title, const juce::String& message, const juce::String& initialText,
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

void ThreeDQEditor::showPresetError (const juce::String& message)
{
    showPresetDialog ("3DQ", message, {}, "OK", [] (const juce::String&) {});
}

//==============================================================================
void ThreeDQEditor::applyBandsFromSphere (const BandSet& bands)
{
    applyingSphereEdit = true;
    const juce::ScopeGuard resetFlag { [this] { applyingSphereEdit = false; } };

    std::array<const Band*, maxBands> bySlot {};
    for (const auto& b : bands)
        if (b.id >= 0 && b.id < maxBands)
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

    for (int slot = 0; slot < maxBands; ++slot)
    {
        const auto ids = ThreeDQProcessor::getBandParamIDs (slot);

        if (const auto* b = bySlot[(size_t) slot])
        {
            set (ids.freq, latToFreq (b->lat));
            set (ids.gain, b->gainDb);
            set (ids.q, b->q);
            set (ids.pos, b->lon * degreesPerRadian);
            set (ids.mute, b->muted ? 1.0f : 0.0f);
            set (ids.range, b->range);
            set (ids.threshold, b->threshold);
            set (ids.type, (float) (int) b->type);
            set (ids.slope, (float) slopeIndexFromDb (b->slope));
            set (ids.stereo, b->stereo ? 1.0f : 0.0f);
            set (ids.sidechain, b->externalSidechain ? 1.0f : 0.0f);
            set (ids.attack, b->attackMs);
            set (ids.release, b->releaseMs);
            set (ids.ratio, b->ratio);
            set (ids.enabled, 1.0f);
        }
        else
        {
            set (ids.enabled, 0.0f);
            set (ids.mute, 0.0f);
            set (ids.range, 0.0f);
            set (ids.type, 0.0f);
            set (ids.sidechain, 0.0f);
            set (ids.attack, defaultAttackMs);
            set (ids.release, defaultReleaseMs);
            set (ids.ratio, defaultRatio);
        }
    }
}

BandSet ThreeDQEditor::getBandsFromProcessor() const
{
    BandSet result;

    for (int slot = 0; slot < maxBands; ++slot)
    {
        const auto p = audioProcessor.getBandParams (slot);
        if (! p.enabled)
            continue;

        Band b;
        b.id = slot;
        b.lat = freqToLat (p.freq);
        b.lon = p.posDeg / degreesPerRadian;
        b.gainDb = p.gainDb;
        b.q = p.q;
        b.muted = audioProcessor.isBandMuted (slot);
        b.range = p.rangeDb;
        b.threshold = p.thresholdDb;
        b.type = p.type;
        b.slope = p.slopeDbPerOct;
        b.stereo = p.stereo;
        b.externalSidechain = p.externalSidechain;
        b.attackMs = p.attackMs;
        b.releaseMs = p.releaseMs;
        b.ratio = p.ratio;
        result.add (b);
    }

    return result;
}

void ThreeDQEditor::readLiveAudio (SphereView::LiveAudio& live)
{
    audioProcessor.copyRecentOutput (recentLeft.data(), recentRight.data(), StereoSpectrum::fftSize);
    live.spectrum = spectrum.analyse (recentLeft.data(), recentRight.data(), audioProcessor.getAnalysisSampleRate());

    live.hasSidechain = audioProcessor.hasSidechainInput();
    if (live.hasSidechain)
    {
        audioProcessor.copyRecentSidechain (recentLeft.data(), recentRight.data(), StereoSpectrum::fftSize);
        live.sidechain = sidechainSpectrum.analyse (recentLeft.data(), recentRight.data(), audioProcessor.getAnalysisSampleRate());
    }

    for (int i = 0; i < maxBands; ++i)
    {
        live.gainDb[(size_t) i]  = audioProcessor.getLiveGainDb (i);
        live.levelDb[(size_t) i] = audioProcessor.getLiveLevelDb (i);
    }
}

void ThreeDQEditor::parameterChanged (const juce::String&, float)
{
    // May arrive on the audio thread (host automation); just flag it.
    if (! applyingSphereEdit)
        hostChangedBands = true;
}

void ThreeDQEditor::timerCallback()
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
            keepHostWindowLargeEnough (peer->getNativeHandle(), juce::roundToInt (minimumWidth * scale), juce::roundToInt (minimumHeight * scale));
        }
    }
}

//==============================================================================
void ThreeDQEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111110));
}

void ThreeDQEditor::resized()
{
    sphereView.setBounds (getLocalBounds());
}

} // namespace threedq
