/*
  ==============================================================================

    PresetBank: saving and loading, the dirty flag, undo and redo, A/B, and
    files that are broken or out of range. Works in a scratch folder with a
    fake clock, so it never touches the real presets.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/model/preset_bank.h"

namespace threedq::test
{

namespace
{
Band bandAt (int id, float freq, float gainDb)
{
    Band b;
    b.id = id;
    b.lat = freqToLat (freq);
    b.gainDb = gainDb;
    return b;
}

// A band's gain, or NaN (fails any comparison) if the band isn't there.
float gainOf (const BandSet& bands, int id)
{
    const auto* b = bands.find (id);
    return b != nullptr ? b->gainDb : std::numeric_limits<float>::quiet_NaN();
}

BandSet setOf (std::initializer_list<Band> bands)
{
    BandSet set;
    for (const auto& b : bands)
        set.add (b);
    return set;
}

// A scratch preset folder and a clock the test moves by hand.
struct Fixture
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("3dq_preset_tests", "", false);
    double time = 100.0;

    Fixture()     { folder.createDirectory(); }
    ~Fixture()    { folder.deleteRecursively(); }

    PresetBank::Storage storage()
    {
        PresetBank::Storage s;
        s.folder = folder;
        s.clock = [this] { return time; };
        s.removeToTrash = false;
        return s;
    }

    void write (const juce::String& name, const juce::String& text)
    {
        folder.getChildFile (name + ".3dqpreset").replaceWithText (text);
    }
};
}

class PresetBankTests final : public juce::UnitTest
{
public:
    PresetBankTests() : juce::UnitTest ("Preset bank", "model") {}

    void runTest() override
    {
        const auto one = setOf ({ bandAt (0, 1000.0f, 6.0f) });
        const auto two = setOf ({ bandAt (0, 1000.0f, 6.0f), bandAt (3, 200.0f, -4.0f) });
        const auto three = setOf ({ bandAt (0, 5000.0f, 2.0f) });

        beginTest ("A new bank: Default, clean, nothing to undo");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            const auto info = bank.info();
            expectEquals (info.name, juce::String ("Default"));
            expect (! info.dirty && ! info.canUndo && ! info.canRedo && ! info.showingB && ! info.canSave);
            expectEquals (info.presets.joinIntoString ("|"), juce::String ("Default"));
            expectEquals (info.current, 0);
        }

        beginTest ("Bands on Default make it dirty, but Default can't be saved over");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            expect (bank.info().dirty);
            expect (! bank.info().canSave);
        }

        beginTest ("Save as: the file appears, the bank is clean, edits make it dirty again");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            expect (bank.saveAs ("Warm Mids"));
            expect (fx.folder.getChildFile ("Warm Mids.3dqpreset").existsAsFile());
            expect (bank.exists ("Warm Mids"));

            auto info = bank.info();
            expectEquals (info.name, juce::String ("Warm Mids"));
            expectEquals (info.presets.joinIntoString ("|"), juce::String ("Default|Warm Mids"));
            expectEquals (info.current, 1);
            expect (! info.dirty && ! info.canSave);

            bank.edited (two);
            expect (bank.info().dirty && bank.info().canSave);
            expect (bank.save());
            expect (! bank.info().dirty);
        }

        beginTest ("Load: a saved preset comes back as it was saved");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            auto b = bandAt (2, 3150.0f, -7.5f);
            b.q = 3.3f;
            b.lon = 1.1f;
            b.muted = true;
            b.range = -6.0f;
            b.threshold = -24.0f;
            b.type = FilterType::highShelf;
            b.q = 1.7f;
            b.stereo = true;
            auto cut = bandAt (5, 80.0f, 0.0f);
            cut.type = FilterType::lowCut;
            cut.slope = 36;
            bank.syncFromHost (setOf ({ b, cut }), "");
            expect (bank.saveAs ("Everything"));

            expect (bank.load (0));
            expectEquals (bank.bands().count, 0, "Default is empty");
            expect (bank.load (1));
            expectEquals (bank.name(), juce::String ("Everything"));
            expect (! bank.info().dirty);
            expectEquals (bank.bands().count, 2);

            const auto* x = bank.bands().find (2);
            expect (x != nullptr);
            if (x != nullptr)
            {
                expectWithinAbsoluteError (latToFreq (x->lat), 3150.0f, 0.5f);
                expectWithinAbsoluteError (x->lon, 1.1f, 1.0e-4f);
                expectWithinAbsoluteError (x->gainDb, -7.5f, 1.0e-4f);
                expectWithinAbsoluteError (x->q, 1.7f, 1.0e-4f);
                expect (x->muted && x->stereo && x->type == FilterType::highShelf);
                expectWithinAbsoluteError (x->range, -6.0f, 1.0e-4f);
                expectWithinAbsoluteError (x->threshold, -24.0f, 1.0e-4f);
            }

            const auto* y = bank.bands().find (5);
            expect (y != nullptr && y->type == FilterType::lowCut && y->slope == 36);
        }

        beginTest ("Names: Default is reserved however it's written; names are made file-safe");
        expect (PresetBank::isReservedName ("Default"));
        expect (PresetBank::isReservedName ("  default "));
        expect (PresetBank::isReservedName ("DEFAULT"));
        expect (! PresetBank::isReservedName ("Defaults"));
        expect (! PresetBank::legalName ("a/b\\c:d").containsAnyOf ("/\\:"));
        expectEquals (PresetBank::legalName ("  Bright  "), juce::String ("Bright"));

        beginTest ("Undo: a quick run of edits is one step, a pause starts a new one");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost ({}, "");

            bank.edited (one);
            fx.time += 0.1;
            bank.edited (two);
            fx.time += 0.1;
            bank.edited (three);          // one drag: {} -> one -> two -> three
            fx.time += 2.0;
            bank.edited (one);            // a new step

            expect (bank.info().canUndo);
            expect (bank.undo());
            expectEquals (gainOf (bank.bands(), 0), 2.0f, "back to the end of the drag");
            expect (bank.undo());
            expectEquals (bank.bands().count, 0, "back to before the drag");
            expect (! bank.undo(), "nothing more");

            expect (bank.redo());
            expectEquals (gainOf (bank.bands(), 0), 2.0f);
            expect (bank.redo());
            expectEquals (gainOf (bank.bands(), 0), 6.0f);
            expect (! bank.redo());

            expect (bank.undo());
            fx.time += 2.0;
            bank.edited (two);
            expect (! bank.info().canRedo, "a new edit drops the redo history");
        }

        beginTest ("Undo: an edit that changes nothing isn't a step");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            bank.edited (one);
            expect (! bank.info().canUndo);
        }

        beginTest ("Undo: loading a preset can be undone");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (two, "");
            expect (bank.saveAs ("Two"));
            bank.syncFromHost (one, "Two");
            expect (bank.load (0));
            expect (bank.undo());
            expectEquals (bank.bands().count, 1);
            expectEquals (bank.name(), juce::String ("Two"));
        }

        beginTest ("A/B: B starts as A, then each keeps its own settings and history");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            bank.toggleAB();
            expect (bank.info().showingB);
            expectEquals (bank.bands().count, 1, "B starts as what A was");
            expect (! bank.info().canUndo);

            bank.edited (two);
            expectEquals (bank.bands().count, 2);
            bank.toggleAB();
            expect (! bank.info().showingB);
            expectEquals (bank.bands().count, 1, "A is untouched");
            expect (! bank.info().canUndo, "B's edit isn't in A's history");

            bank.copyToOtherSlot();
            bank.toggleAB();
            expectEquals (bank.bands().count, 1, "A copied onto B");
            expect (bank.undo(), "the copy can be undone on B");
            expectEquals (bank.bands().count, 2);
        }

        beginTest ("Step: next and previous wrap round the list");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            expect (bank.saveAs ("A"));
            bank.edited (two);
            expect (bank.saveAs ("B"));
            expect (bank.load (0));

            expect (bank.step (1));
            expectEquals (bank.name(), juce::String ("A"));
            expect (bank.step (1));
            expectEquals (bank.name(), juce::String ("B"));
            expect (bank.step (1));
            expectEquals (bank.name(), juce::String ("Default"), "wraps forwards");
            expect (bank.step (-1));
            expectEquals (bank.name(), juce::String ("B"), "wraps backwards");
        }

        beginTest ("Presets are listed in natural order and new files are found on rescan");
        {
            Fixture fx;
            const juce::String empty ("<THREEDQ_PRESET version=\"1\"/>");
            fx.write ("Preset 10", empty);
            fx.write ("Preset 2", empty);
            PresetBank bank (fx.storage());
            expectEquals (bank.info().presets.joinIntoString ("|"), juce::String ("Default|Preset 2|Preset 10"));
            fx.write ("Another", empty);
            bank.rescan();
            expectEquals (bank.info().presets.size(), 4);
        }

        beginTest ("Remove: deletes the file, never Default; the open preset becomes unsaved");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            expect (bank.saveAs ("Doomed"));
            expect (! bank.remove (0), "Default stays");
            expect (! bank.remove (5), "out of range");
            expect (bank.remove (1));
            expect (! fx.folder.getChildFile ("Doomed.3dqpreset").exists());
            expectEquals (bank.info().presets.joinIntoString ("|"), juce::String ("Default"));
            expect (bank.info().dirty, "nothing on disk holds these bands now");
            expectEquals (bank.bands().count, 1, "the bands stay");
        }

        beginTest ("A broken file doesn't load and leaves things as they were");
        {
            Fixture fx;
            fx.write ("Broken", "this is not xml <<<");
            fx.write ("Wrong", "<SOMETHING_ELSE/>");
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            const int broken = bank.info().presets.indexOf ("Broken");
            const int wrong = bank.info().presets.indexOf ("Wrong");
            expect (! bank.load (broken));
            expect (! bank.load (wrong));
            expect (! bank.load (-1) && ! bank.load (99));
            expectEquals (bank.name(), juce::String ("Default"));
            expectEquals (bank.bands().count, 1);
            expect (! bank.info().canUndo);
        }

        beginTest ("Out-of-range values in a file are clamped; bad slots are skipped");
        {
            Fixture fx;
            fx.write ("Wild",
                      "<THREEDQ_PRESET version=\"1\">"
                      "<BAND slot=\"0\" freq=\"99999\" gain=\"100\" q=\"50\" pos=\"720\" range=\"-99\" threshold=\"20\" type=\"9\" slope=\"7\"/>"
                      "<BAND slot=\"1\" freq=\"1\" gain=\"-100\" q=\"0.01\" type=\"3\" threshold=\"-200\"/>"
                      "<BAND slot=\"2\" q=\"8\" type=\"4\"/>"
                      "<BAND slot=\"3\" q=\"50\"/>"
                      "<BAND slot=\"0\" gain=\"3\"/>"
                      "<BAND slot=\"12\" gain=\"3\"/>"
                      "<BAND slot=\"-1\" gain=\"3\"/>"
                      "</THREEDQ_PRESET>");
            PresetBank bank (fx.storage());
            expect (bank.load (1));
            const auto& bands = bank.bands();
            const Band missing; // stands in for an absent band, so the checks fail instead of crashing
            auto band = [&] (int id) { const auto* b = bands.find (id); return b != nullptr ? b : &missing; };
            expectEquals (bands.count, 4, "duplicate and out-of-range slots dropped");

            const auto* a = band (0);
            expectWithinAbsoluteError (latToFreq (a->lat), maxFreq, 1.0f);
            expectEquals (a->gainDb, maxGainDb);
            expectEquals (a->q, maxShelfQ, "the clamped type is a shelf, so it keeps to the shelf Q limit");
            expectWithinAbsoluteError (a->lon, threedq::pi, 1.0e-5f);
            expectEquals (a->range, -maxGainDb);
            expectEquals (a->threshold, maxThresholdDb);
            expect (a->type == FilterType::highShelf, "type clamped to the last one");
            expectEquals (a->slope, defaultCutSlope, "an unknown slope falls back to the default");

            const auto* b = band (1);
            expectWithinAbsoluteError (latToFreq (b->lat), minFreq, 0.01f);
            expectEquals (b->gainDb, -maxGainDb);
            expectEquals (b->q, minQ);
            expectEquals (b->threshold, minThresholdDb);

            expectEquals (band (2)->q, maxShelfQ, "shelves keep to their own Q limit");
            expectEquals (band (3)->q, maxQ, "a peak can go to the full Q");
        }

        beginTest ("Syncing from the host picks up the session's preset name");
        {
            Fixture fx;
            PresetBank bank (fx.storage());
            bank.syncFromHost (one, "");
            expect (bank.saveAs ("Session"));

            PresetBank reopened (fx.storage());
            reopened.syncFromHost (one, "Session");
            expectEquals (reopened.name(), juce::String ("Session"));
            expect (! reopened.info().dirty, "matches the file");
            reopened.syncFromHost (two, "Session");
            expect (reopened.info().dirty, "differs from the file");
            reopened.syncFromHost (one, "Gone");
            expect (reopened.info().dirty, "a preset with no file is unsaved");
        }
    }
};

static PresetBankTests presetBankTests;

} // namespace threedq::test
