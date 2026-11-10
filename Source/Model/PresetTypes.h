/*
  ==============================================================================

    What the preset bank tells the preset bar, and the commands the bar sends
    back through the editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace threedq
{

// What the preset bar shows.
struct PresetInfo
{
    juce::String name;
    bool dirty = false, canUndo = false, canRedo = false, showingB = false, canSave = false;
    juce::StringArray presets; // the built-in default first, then the saved ones
    int current = -1;          // index in presets; -1 = not in the list
};

enum class PresetCommand { undo, redo, toggleAB, copyAB, previous, next, refresh, save, saveAs, load, remove };

} // namespace threedq
