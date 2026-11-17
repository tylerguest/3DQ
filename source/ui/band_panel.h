/*
  ==============================================================================

    The band control panel, floating above the bottom bar whenever there is a
    band. One piece, with Gain on a raised round housing whose top quarter
    stands above the panel's top edge, Freq and Q either side, and in the corners:
      top left      the band's on / off, solo, and the gear for its dynamics
      top right     its number (click it for every band) with < >, remove, close
      bottom left   the filter type, a menu
      bottom right  stereo or a position round the sphere, a menu
    Knobs show their name, and their value while hovered or turned; click the
    value to type one. Round Gain are two rings: the outer one is the dynamic
    range, the inner one the threshold. The gear opens ratio, attack, release
    and the sidechain switch (SC: the dynamics follow the sidechain) on the left
    shoulder; the number opens the band list on the right one.

    Layout, hit-testing and value mapping are here, with drawing. SphereView
    owns the gestures and the shared state.
    The panel is laid out at its default-window size and scaled up as a whole
    with the window; BandPanelLayout's rectangles are in those unscaled units.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <optional>
#include "../model/camera.h"

namespace threedq
{

constexpr int numFilterTypes = 5; // indexed by FilterType

// What a control sets. Gain is the big knob. Round it are two rings: the outer
// one is the dynamic range, with the live gain change (the meter) moving along
// it; the inner one is the threshold, with the level the band hears. Ratio, attack, release and the GR
// meter are in the dynamics menu, opened from the gear.
enum PanelKnob { freqKnob, gainKnob, qKnob, posKnob, rangeKnob, thresholdKnob, ratioKnob, attackKnob, releaseKnob, liveGainMeter };

// A menu that opens over the panel: the filter types, the stereo placement,
// the dynamics settings, or the list of bands.
enum class PanelPopup { none, types, stereo, dynamics, bands };

// The stereo menu's choices, in order: both channels, or a position.
enum StereoChoice { stereoBoth, stereoMid, stereoSide, stereoLeft, stereoRight, numStereoChoices };

struct PanelTarget
{
    enum class Kind { none, background, previous, next, bandList, bandItem, typeMenu, type, stereoMenu, stereoItem, positionSlider,
                      dynamicsMenu, sidechain, solo, mute, remove, close, knob };

    Kind kind = Kind::none;
    int index = 0;        // which type, StereoChoice, PanelKnob, or band in the list (in frequency order)
    bool inPopup = false; // in the open menu, so clicking it keeps the menu open
    bool onValue = false; // a knob's value text: a click without dragging types a new one

    bool operator== (const PanelTarget& other) const
    {
        return kind == other.kind && index == other.index && inPopup == other.inPopup && onValue == other.onValue;
    }
};

struct BandPanelLayout
{
    juce::Rectangle<float> panel, power, solo, gear, previous, number, next, remove, close, typePill, stereoPill;
    juce::Point<float> freqCentre, gainCentre, qCentre;

    // The open menu, if any: its area and what's in it. A menu on a shoulder
    // points down at its button, from arrowX.
    PanelPopup popup = PanelPopup::none;
    juce::Rectangle<float> popupArea, sidechain, sidechainNote, grMeter, positionSlider, positionValue;
    float arrowX = 0;
    std::array<juce::Rectangle<float>, numFilterTypes> typeItems;
    std::array<juce::Rectangle<float>, numStereoChoices> stereoItems;
    std::array<juce::Rectangle<float>, 3> dynamicsKnobs; // ratio, attack, release: each its dial and label
    std::array<juce::Rectangle<float>, maxBands> bandItems; // the band list, low to high
    int bandCount = 0;

    float scale = 1;

    // Window size in component pixels. openAmount: 0 = away below the window,
    // 1 = fully up. bandCount sizes the band list.
    static BandPanelLayout make (float width, float height, float openAmount, PanelPopup popup, int bandCount = 0);

    // position is in component pixels. band: the selected band, or null. Controls
    // the band doesn't have (a cut's gear and gain) never hit.
    PanelTarget hit (juce::Point<float> position, bool open, const Band* band) const;

    // Where a knob's name and value are written, in layout units: the area a
    // typed value is edited in. Empty for the meter.
    juce::Rectangle<float> valueArea (int knob) const;
};

// A value being typed in: a band knob's or one of the gain fields', the text
// so far, and whether it is all selected (the first key then replaces it).
struct ValueEdit
{
    int knob = -1; // a PanelKnob, or -1
    int trim = -1; // the gain field: 0 input, 1 output, or -1
    juce::String text;
    bool allSelected = true;
    juce::String original; // the value as shown: left as it is, nothing changes (it's rounded)
};

// A value being typed: a field over the value with the text, all of it
// selected until the first key replaces it, or a caret after it.
void drawValueEntry (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool allSelected, float fontSize, float unit);

// How the view moves as the panel opens, so the sphere starts clear of it. It's
// worked out once, when the panel opens: zooming or panning after that is
// applied on top, and closing the panel puts the view back.
struct PanelViewFit
{
    float zoomScale = 1, panDeltaY = 0;
};

PanelViewFit fitViewAbovePanel (float width, float height, float zoom, float panY, SurfaceExtent extent);

// The bands from low to high, as the list and < > step through them.
int bandsByFrequency (const BandSet&, std::array<const Band*, maxBands>& order);

// Whether a band is at a stereo menu choice now.
bool stereoChoiceIsOn (const Band&, int choice);
void applyStereoChoice (Band&, int choice);

// Knob positions run 0..1.
float knobPosition (const Band&, int knob);
void setKnobPosition (Band&, int knob, float position);
void resetKnob (Band&, int knob);
bool knobEnabled (const Band&, int knob);

// A knob's value as the panel writes it: "1.00 kHz", "+3 dB", "Mid".
juce::String knobValueText (const Band&, int knob);

// A number typed into a value field: "1.5k" and "2k5" are thousands, and with
// seconds a trailing s (not ms) is too. Units and spaces are ignored. Empty
// when there is no number.
std::optional<float> parseTypedNumber (const juce::String& text, bool seconds = false);

// Sets a knob from what was typed into its value, clamped to its range; a
// cut's slope goes to the nearest one, and the position also takes L, R, Mid
// and Side. Returns false, leaving the band alone, when it isn't a value.
bool setKnobFromText (Band&, int knob, const juce::String& text);
bool knobIsStepped (const Band&, int knob);

// selected: the band shown, an id in bands, or -1.
// sidechainConnected: whether the host is feeding the sidechain input.
// levelDb: what the band's detector hears now, shown against its threshold.
void drawBandPanel (juce::Graphics&, const BandPanelLayout&, float dpr, float openAmount,
                    const BandSet& bands, int selected, bool soloed, PanelTarget hover, int activeKnob, bool sidechainConnected,
                    float levelDb, const ValueEdit* edit = nullptr);

} // namespace threedq
