/*
  ==============================================================================

    The band control panel that slides up from the ^ button: filter type,
    sidechain (SC: the band's dynamics follow the sidechain input), stereo /
    mono, solo, mute, remove, and a row of knobs for the band's settings. The
    knobs come in two pages, picked by the EQ and DYN tabs along the bottom:
    the band's shape, and its dynamics.

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

constexpr int numPanelKnobs = 6;  // knob slots on a page
constexpr int numFilterTypes = 5; // indexed by FilterType
constexpr int numPanelPages = 2;

enum class PanelPage { eq, dynamics };

// What a knob slot controls. The meter shows the band's live dynamic gain
// change; it can't be turned.
enum PanelKnob { freqKnob, gainKnob, qKnob, posKnob, rangeKnob, thresholdKnob, ratioKnob, attackKnob, releaseKnob, liveGainMeter };

PanelKnob panelKnobAt (PanelPage, int slot);

struct PanelTarget
{
    enum class Kind { none, toggle, background, previous, next, type, sidechain, stereo, solo, mute, remove, knob, page };

    Kind kind = Kind::none;
    int index = 0; // which type, PanelKnob or PanelPage

    bool operator== (const PanelTarget& other) const    { return kind == other.kind && index == other.index; }
};

struct BandPanelLayout
{
    juce::Rectangle<float> panel, previous, title, next, sidechain, stereo, solo, mute, remove;
    std::array<juce::Rectangle<float>, numFilterTypes> types;
    std::array<juce::Rectangle<float>, numPanelKnobs> knobs; // slots, left to right
    std::array<juce::Rectangle<float>, numPanelPages> pages;  // the tabs
    juce::Point<float> toggleCentre;
    float scale = 1;
    PanelPage page = PanelPage::eq;

    // Window size in component pixels. openAmount: 0 = closed (only the ^ button shows), 1 = fully up.
    static BandPanelLayout make (float width, float height, float openAmount, PanelPage page);

    // position is in component pixels.
    PanelTarget hit (juce::Point<float> position, bool open, bool hasBand) const;
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
void drawBandPanel (juce::Graphics&, const BandPanelLayout&, float dpr, float openAmount,
                    const Band* band, bool soloed, PanelTarget hover, int activeKnob, bool sidechainConnected);

} // namespace threedq
