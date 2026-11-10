/*
  ==============================================================================

    The controls along the top of the window: the current preset, centred, with
    its menu for saving and loading, and undo, redo, A/B and copy on the right.

    Layout, hit-testing and drawing are here. SphereView owns the gestures and
    the editor carries out the commands.
    Rectangles are in unscaled units, as in BandPanelLayout.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/PresetTypes.h"

namespace threedq
{

struct PresetTarget
{
    enum class Kind { none, undo, redo, ab, copy, previous, name, next, menuBackground, save, saveAs, item, remove };

    Kind kind = Kind::none;
    int index = 0; // which preset

    bool operator== (const PresetTarget& other) const    { return kind == other.kind && index == other.index; }
};

struct PresetBarLayout
{
    juce::Rectangle<float> undo, redo, ab, copy, previous, name, next;
    juce::Rectangle<float> menu, save, saveAs;
    float listTop = 0, rowHeight = 24, scale = 1;
    int presetCount = 0, firstVisible = 0, visibleRows = 0;

    // Window size in component pixels. scroll: the first preset shown in the menu.
    static PresetBarLayout make (float width, float height, int presetCount, int scroll);

    juce::Rectangle<float> row (int preset) const;
    juce::Rectangle<float> removeButton (int preset) const;

    // position is in component pixels.
    PresetTarget hit (juce::Point<float> position, bool menuOpen) const;
};

// menuAmount: 0 = closed, 1 = fully shown.
void drawPresetBar (juce::Graphics&, const PresetBarLayout&, float dpr, const PresetInfo&, PresetTarget hover, float menuAmount);

} // namespace threedq
