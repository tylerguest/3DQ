/*
  ==============================================================================

    PresetBank: the presets on disk, the A and B setups, and the undo history of
    each. Lives in the processor so it outlasts the editor window. Message
    thread only. The browser's equivalent is ui/src/presets.ts.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SphereModel.h"
#include "PresetBar.h"

namespace sphere
{

class PresetBank
{
public:
    PresetBank();

    // The editor opened, or the session's bands or preset name changed under us.
    void syncFromHost (const BandSet& bands, const juce::String& sessionName);

    // A change the user made on the sphere. A run of quick changes, such as a
    // drag, is one undo step.
    void edited (const BandSet& bands);

    const BandSet& bands() const        { return slots[active].now.bands; }
    const juce::String& name() const    { return slots[active].now.name; }

    bool undo();
    bool redo();
    void toggleAB();
    void copyToOtherSlot();

    bool load (int index);        // false = the file couldn't be read
    bool step (int direction);    // loads the next or previous preset, wrapping round
    bool save();                  // false = couldn't write
    bool saveAs (const juce::String& name);
    bool remove (int index);      // moves the file to the trash

    void rescan();
    bool exists (const juce::String& name) const;
    static bool isReservedName (const juce::String& name);
    static juce::String legalName (const juce::String& name);

    PresetBarInfo info() const;

private:
    struct Setup
    {
        BandSet bands;
        juce::String name;
        std::optional<BandSet> saved; // what the preset holds, to tell if it's been changed
    };

    struct Slot
    {
        Setup now;
        std::vector<Setup> undo, redo;
    };

    void remember();
    void replaceNow (Setup);
    juce::File fileFor (const juce::String& name) const;

    std::array<Slot, 2> slots;
    size_t active = 0;
    juce::StringArray names;
    double lastEdit = 0;
    bool synced = false;
};

} // namespace sphere
