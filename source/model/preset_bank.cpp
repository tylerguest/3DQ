/*
  ==============================================================================

    PresetBank: see preset_bank.h.

  ==============================================================================
*/

#include "preset_bank.h"

namespace threedq
{

namespace
{
const juce::String defaultPresetName ("Default");
constexpr const char* presetExtension = ".3dqpreset";
constexpr double editRunSeconds = 0.5;
constexpr size_t maxUndoSteps = 200;

double nowSeconds()    { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

bool sameBands (const BandSet& a, const BandSet& b)
{
    if (a.count != b.count)
        return false;

    const auto near = [] (float x, float y, float tolerance) { return std::abs (x - y) <= tolerance; };

    for (const auto& x : a)
    {
        const auto* y = b.find (x.id);

        if (y == nullptr || x.muted != y->muted || x.type != y->type || x.slope != y->slope || x.stereo != y->stereo
            || x.externalSidechain != y->externalSidechain
            || ! near (x.lat, y->lat, 0.002f) || ! near (x.lon, y->lon, 0.003f) || ! near (x.gainDb, y->gainDb, 0.02f)
            || ! near (x.q, y->q, x.q * 0.005f) || ! near (x.range, y->range, 0.02f) || ! near (x.threshold, y->threshold, 0.06f))
            return false;
    }

    return true;
}

std::unique_ptr<juce::XmlElement> toXml (const BandSet& bands)
{
    auto root = std::make_unique<juce::XmlElement> ("THREEDQ_PRESET");
    root->setAttribute ("version", 1);

    for (const auto& b : bands)
    {
        auto* e = root->createNewChildElement ("BAND");
        e->setAttribute ("slot", b.id);
        e->setAttribute ("freq", latToFreq (b.lat));
        e->setAttribute ("gain", b.gainDb);
        e->setAttribute ("q", b.q);
        e->setAttribute ("pos", b.lon * 180.0f / pi);
        e->setAttribute ("mute", b.muted);
        e->setAttribute ("range", b.range);
        e->setAttribute ("threshold", b.threshold);
        e->setAttribute ("type", (int) b.type);
        e->setAttribute ("slope", b.slope);
        e->setAttribute ("stereo", b.stereo);
        e->setAttribute ("sidechain", b.externalSidechain);
    }

    return root;
}

std::optional<BandSet> fromXml (const juce::XmlElement* root)
{
    if (root == nullptr || ! root->hasTagName ("THREEDQ_PRESET"))
        return {};

    BandSet bands;

    for (const auto* e : root->getChildWithTagNameIterator ("BAND"))
    {
        Band b;
        b.id = e->getIntAttribute ("slot", -1);

        if (b.id < 0 || b.id >= maxBands || bands.find (b.id) != nullptr)
            continue;

        b.lat = freqToLat (std::clamp ((float) e->getDoubleAttribute ("freq", 1000.0), minFreq, maxFreq));
        b.lon = std::clamp ((float) e->getDoubleAttribute ("pos", 0.0), -180.0f, 180.0f) * pi / 180.0f;
        b.gainDb = std::clamp ((float) e->getDoubleAttribute ("gain", 0.0), -maxGainDb, maxGainDb);
        b.muted = e->getBoolAttribute ("mute", false);
        b.range = std::clamp ((float) e->getDoubleAttribute ("range", 0.0), -maxGainDb, maxGainDb);
        b.threshold = std::clamp ((float) e->getDoubleAttribute ("threshold", defaultThresholdDb), minThresholdDb, maxThresholdDb);
        b.type = (FilterType) std::clamp (e->getIntAttribute ("type", 0), 0, 4);
        b.stereo = e->getBoolAttribute ("stereo", false);
        b.externalSidechain = e->getBoolAttribute ("sidechain", false);
        b.q = std::clamp ((float) e->getDoubleAttribute ("q", 1.0), minQ, b.isShelf() ? maxShelfQ : maxQ);

        const int slope = e->getIntAttribute ("slope", defaultCutSlope);
        const auto& slopes = cutSlopes;
        b.slope = std::find (slopes.begin(), slopes.end(), slope) != slopes.end() ? slope : defaultCutSlope;

        bands.add (b);
    }

    return bands;
}
}

juce::File PresetBank::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("3DQ").getChildFile ("Presets");
}

PresetBank::PresetBank()
    : PresetBank (Storage {})
{
}

PresetBank::PresetBank (Storage s)
    : storage (std::move (s))
{
    slots[0].now.name = slots[1].now.name = defaultPresetName;
    slots[0].now.saved = slots[1].now.saved = BandSet {};
    rescan();
}

juce::String PresetBank::legalName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim());
}

bool PresetBank::isReservedName (const juce::String& name)
{
    return legalName (name).equalsIgnoreCase (defaultPresetName);
}

juce::File PresetBank::fileFor (const juce::String& name) const
{
    return storage.folder.getChildFile (legalName (name) + presetExtension);
}

bool PresetBank::exists (const juce::String& name) const
{
    return fileFor (name).existsAsFile();
}

void PresetBank::rescan()
{
    juce::StringArray found;

    for (const auto& file : storage.folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension))
        found.add (file.getFileNameWithoutExtension());

    found.sortNatural();
    found.insert (0, defaultPresetName);
    names = found;
}

void PresetBank::syncFromHost (const BandSet& bands, const juce::String& sessionName)
{
    auto& now = slots[active].now;
    const auto name = sessionName.isEmpty() ? defaultPresetName : sessionName;

    if (name != now.name)
    {
        now.name = name;
        now.saved.reset();

        if (name == defaultPresetName)
            now.saved = BandSet {};
        else if (const auto xml = juce::XmlDocument::parse (fileFor (name)))
            now.saved = fromXml (xml.get());
    }

    now.bands = bands;

    if (! synced)
    {
        slots[1 - active].now = now; // B starts out as what A is
        synced = true;
    }
}

void PresetBank::remember()
{
    auto& slot = slots[active];
    slot.undo.push_back (slot.now);

    if (slot.undo.size() > maxUndoSteps)
        slot.undo.erase (slot.undo.begin());

    slot.redo.clear();
}

void PresetBank::replaceNow (Setup setup)
{
    remember();
    slots[active].now = std::move (setup);
    lastEdit = 0;
}

void PresetBank::edited (const BandSet& bands)
{
    auto& now = slots[active].now;

    if (sameBands (now.bands, bands))
    {
        now.bands = bands;
        return;
    }

    const auto time = currentTime();

    if (time - lastEdit > editRunSeconds)
        remember();

    lastEdit = time;
    now.bands = bands;
}

bool PresetBank::undo()
{
    auto& slot = slots[active];

    if (slot.undo.empty())
        return false;

    slot.redo.push_back (slot.now);
    slot.now = slot.undo.back();
    slot.undo.pop_back();
    lastEdit = 0;
    return true;
}

bool PresetBank::redo()
{
    auto& slot = slots[active];

    if (slot.redo.empty())
        return false;

    slot.undo.push_back (slot.now);
    slot.now = slot.redo.back();
    slot.redo.pop_back();
    lastEdit = 0;
    return true;
}

void PresetBank::toggleAB()
{
    active = 1 - active;
    lastEdit = 0;
}

void PresetBank::copyToOtherSlot()
{
    auto& other = slots[1 - active];
    other.undo.push_back (other.now);
    other.redo.clear();
    other.now = slots[active].now;
}

bool PresetBank::load (int index)
{
    if (! juce::isPositiveAndBelow (index, names.size()))
        return false;

    Setup setup;
    setup.name = names[index];

    if (index == 0)
    {
        setup.saved = BandSet {};
    }
    else
    {
        const auto xml = juce::XmlDocument::parse (fileFor (setup.name));
        setup.saved = fromXml (xml.get());

        if (! setup.saved.has_value())
            return false;

        setup.bands = *setup.saved;
    }

    replaceNow (std::move (setup));
    return true;
}

bool PresetBank::step (int direction)
{
    rescan();
    const int count = names.size();
    const int current = names.indexOf (name());
    const int next = current < 0 ? (direction > 0 ? 0 : count - 1) : (current + direction + count) % count;
    return load (next);
}

bool PresetBank::saveAs (const juce::String& newName)
{
    const auto file = fileFor (newName);

    if (! file.getParentDirectory().createDirectory().wasOk())
        return false;

    const auto xml = toXml (bands());

    if (! xml->writeTo (file))
        return false;

    auto& now = slots[active].now;
    now.name = file.getFileNameWithoutExtension();
    now.saved = now.bands;
    rescan();
    return true;
}

bool PresetBank::save()
{
    return saveAs (name());
}

bool PresetBank::remove (int index)
{
    if (index <= 0 || index >= names.size())
        return false;

    const auto removed = names[index];

    const auto file = fileFor (removed);

    if (! (storage.removeToTrash ? file.moveToTrash() : file.deleteFile()))
        return false;

    for (auto& slot : slots)
        if (slot.now.name == removed)
            slot.now.saved.reset();

    rescan();
    return true;
}

double PresetBank::currentTime() const
{
    return storage.clock ? storage.clock() : nowSeconds();
}

PresetInfo PresetBank::info() const
{
    const auto& slot = slots[active];
    PresetInfo result;
    result.name = slot.now.name;
    result.dirty = ! slot.now.saved.has_value() || ! sameBands (slot.now.bands, *slot.now.saved);
    result.canUndo = ! slot.undo.empty();
    result.canRedo = ! slot.redo.empty();
    result.showingB = active == 1;
    result.presets = names;
    result.current = names.indexOf (slot.now.name);
    result.canSave = result.current > 0 && result.dirty;
    return result;
}

} // namespace threedq
