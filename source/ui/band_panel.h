/*
  ==============================================================================

    The band control panel, floating above the bottom bar whenever there is a band: filter type,
    stereo / mono and its position, solo, mute, remove, and the Freq, Gain and Q knobs.
    The ring round Gain holds the band's dynamics: drag it for the range, the
    inner ring for the threshold, and the gear opens ratio, attack,
    release and the sidechain switch (SC: the dynamics follow the sidechain).

    Layout, hit-testing and value mapping are here, with drawing. SphereView
    owns the gestures and the shared state.
    The panel is laid out at its default-window size and scaled up as a whole
    with the window; BandPanelLayout's rectangles are in those unscaled units.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../model/camera.h"

namespace threedq
{

constexpr int numFilterTypes = 5; // indexed by FilterType

// What a control sets. Gain is the big knob. Round it are two rings: the outer
// one is the dynamic range, with the live gain change (the meter) moving along
// it; the inner one is the threshold, with the level the band hears. Ratio, attack, release and the GR
// meter are in the dynamics menu, opened from the gear.
enum PanelKnob { freqKnob, gainKnob, qKnob, posKnob, rangeKnob, thresholdKnob, ratioKnob, attackKnob, releaseKnob, liveGainMeter };

// A menu that opens over the panel: the filter types, or the dynamics settings.
enum class PanelPopup { none, types, dynamics };

struct PanelTarget
{
    enum class Kind { none, background, previous, next, typeMenu, type, dynamicsMenu, sidechain, stereo, solo, mute, remove, knob };

    Kind kind = Kind::none;
    int index = 0;        // which type or PanelKnob
    bool inPopup = false; // in the open menu, so clicking it keeps the menu open

    bool operator== (const PanelTarget& other) const    { return kind == other.kind && index == other.index && inPopup == other.inPopup; }
};

struct BandPanelLayout
{
    juce::Rectangle<float> panel, previous, title, next, typePill, stereo, positionPill, solo, mute, remove, gear;
    juce::Point<float> freqCentre, gainCentre, qCentre;

    // The open menu, if any: its area and what's in it.
    PanelPopup popup = PanelPopup::none;
    juce::Rectangle<float> popupArea, sidechain, grMeter;
    std::array<juce::Rectangle<float>, numFilterTypes> typeItems;
    std::array<juce::Rectangle<float>, 3> dynamicsKnobs; // ratio, attack, release

    float scale = 1;

    // Window size in component pixels. openAmount: 0 = away below the window, 1 = fully up.
    static BandPanelLayout make (float width, float height, float openAmount, PanelPopup popup);

    // position is in component pixels. band: the selected band, or null. Controls
    // the band doesn't show (a stereo band's position, a cut's gear) never hit.
    PanelTarget hit (juce::Point<float> position, bool open, const Band* band) const;
};

// How the view moves as the panel opens, so the sphere starts clear of it. It's
// worked out once, when the panel opens: zooming or panning after that is
// applied on top, and closing the panel puts the view back.
struct PanelViewFit
{
    float zoomScale = 1, panDeltaY = 0;
};

PanelViewFit fitViewAbovePanel (float width, float height, float zoom, float panY, SurfaceExtent extent);

// Knob positions run 0..1.
float knobPosition (const Band&, int knob);
void setKnobPosition (Band&, int knob, float position);
void resetKnob (Band&, int knob);
bool knobEnabled (const Band&, int knob);
bool knobIsStepped (const Band&, int knob);

// sidechainConnected: whether the host is feeding the sidechain input.
// levelDb: what the band's detector hears now, shown against its threshold.
void drawBandPanel (juce::Graphics&, const BandPanelLayout&, float dpr, float openAmount,
                    const Band* band, bool soloed, PanelTarget hover, int activeKnob, bool sidechainConnected,
                    float levelDb);

} // namespace threedq
