/*
  ==============================================================================

    The window's layout: the bottom bar's gain fields and their text, and the
    floating band panel, its outline and what a click on it hits.

  ==============================================================================
*/

#include "test_helpers.h"
#include "../source/ui/band_panel.h"
#include "../source/ui/sphere_view_detail.h"

namespace threedq::test
{

namespace
{
using Kind = PanelTarget::Kind;

const Band peak {}; // a mono peak band: every control shows

// The minimum window, the default, and a big one.
constexpr std::array<std::pair<float, float>, 3> windowSizes { { { 440.0f, 340.0f }, { 760.0f, 560.0f }, { 1100.0f, 720.0f } } };

juce::String sizeName (float w, float h)    { return juce::String (w) + "x" + juce::String (h); }
}

class LayoutTests final : public juce::UnitTest
{
public:
    LayoutTests() : juce::UnitTest ("Layout", "ui") {}

    void runTest() override
    {
        beginTest ("Gain text: signed, one decimal, never -0.0");
        {
            expectEquals (view::trimText (1.5f), juce::String ("+1.5 dB"));
            expectEquals (view::trimText (-6.0f), juce::String ("-6.0 dB"));
            expectEquals (view::trimText (0.0f), juce::String ("0.0 dB"));
            expectEquals (view::trimText (-0.04f), juce::String ("0.0 dB"), "rounds to zero without a sign");
            expectEquals (view::trimText (0.04f), juce::String ("0.0 dB"));
            expectEquals (view::trimText (0.26f), juce::String ("+0.3 dB"));
            expectEquals (view::trimText (24.0f), juce::String ("+24.0 dB"));
            expectEquals (view::trimText (-24.0f), juce::String ("-24.0 dB"));
        }

        beginTest ("Gain fields: input left, output right, mirrored, in the bar and clear of the ? button");
        for (const auto [w, h] : windowSizes)
        {
            const float unit = uiScale (w, h);
            const auto in = view::trimFieldArea (w, h, unit, 0), out = view::trimFieldArea (w, h, unit, 1);
            const auto name = sizeName (w, h);

            expectLessThan (in.getRight(), w / 2.0f, name);
            expectGreaterThan (out.getX(), w / 2.0f, name);
            expectWithinAbsoluteError (in.getX(), w - out.getRight(), 1.0e-3f, "mirrored " + name);
            expect (in.getWidth() == out.getWidth() && in.getY() == out.getY(), name);

            const float barTop = h - bottomBarHeight * unit;
            expectGreaterOrEqual (in.getY(), barTop, "inside the bar " + name);
            expectLessOrEqual (in.getBottom(), h, name);
            expectWithinAbsoluteError (in.getCentreY(), h - bottomBarHeight / 2.0f * unit, 1.0e-3f, "centred in the bar " + name);

            const auto help = view::helpButtonCentre (w, h, unit);
            expectLessThan (out.getRight(), help.x - view::helpButtonRadius * unit, "clear of the ? " + name);
            expectGreaterThan (in.getX(), (view::bottomBarSideInset + view::bottomBarSlant) * unit, "inside the bar's slant " + name);
        }

        beginTest ("The panel floats clear of the bottom bar, and slides away below the window");
        for (const auto [w, h] : windowSizes)
        {
            const auto open = BandPanelLayout::make (w, h, 1.0f, PanelPopup::none);
            const auto closed = BandPanelLayout::make (w, h, 0.0f, PanelPopup::none);
            const auto name = sizeName (w, h);
            const float barTop = h - bottomBarHeight * open.scale;

            expectLessThan (open.panel.getBottom() * open.scale, barTop, "above the bar " + name);
            expectGreaterThan (open.panel.getBottom() * open.scale, barTop - 20.0f * open.scale, "but near it " + name);
            expectGreaterOrEqual (open.panel.getX(), 0.0f, name);
            expectLessOrEqual (open.panel.getRight() * open.scale, w, name);
            expectGreaterThan (closed.panel.getY() * closed.scale, h, "closed = off the bottom " + name);
        }

        beginTest ("Every control hits as itself, at every size");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::none);
            const auto name = sizeName (w, h);
            auto hitAt = [&] (juce::Point<float> p)    { return l.hit (p * l.scale, true, &peak); };

            expect (hitAt (l.power.getCentre()).kind == Kind::mute, "on / off " + name);
            expect (hitAt (l.solo.getCentre()).kind == Kind::solo, "solo " + name);
            expect (hitAt (l.gear.getCentre()).kind == Kind::dynamicsMenu, "gear " + name);
            expect (hitAt (l.previous.getCentre()).kind == Kind::previous, "previous " + name);
            expect (hitAt (l.number.getCentre()).kind == Kind::bandList, "the number opens the band list " + name);
            expect (hitAt (l.next.getCentre()).kind == Kind::next, "next " + name);
            expect (hitAt (l.remove.getCentre()).kind == Kind::remove, "remove " + name);
            expect (hitAt (l.close.getCentre()).kind == Kind::close, "close " + name);
            expect (hitAt (l.typePill.getCentre()).kind == Kind::typeMenu, "type " + name);
            expect (hitAt (l.stereoPill.getCentre()).kind == Kind::stereoMenu, "stereo " + name);
            expect (hitAt (l.gainCentre) == PanelTarget { Kind::knob, gainKnob }, "gain " + name);
            expect (hitAt (l.freqCentre) == PanelTarget { Kind::knob, freqKnob }, "freq " + name);
            expect (hitAt (l.qCentre) == PanelTarget { Kind::knob, qKnob }, "q " + name);
        }

        beginTest ("Gain's rings: the inner one is the threshold, the outer one the range, each wide enough to grab");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::none);
            const auto name = sizeName (w, h);
            for (const float angle : { -2.0f, -1.0f, 0.0f, 1.0f, 2.0f })
            {
                const auto at = [&] (float r) { return l.hit ((l.gainCentre + juce::Point<float> (std::sin (angle) * r, -std::cos (angle) * r)) * l.scale, true, &peak); };
                for (const float r : { 33.0f, 36.5f, 40.0f })
                    expect (at (r) == PanelTarget { Kind::knob, thresholdKnob }, "threshold at " + juce::String (r) + " " + name);
                for (const float r : { 41.0f, 44.5f, 48.0f })
                    expect (at (r) == PanelTarget { Kind::knob, rangeKnob }, "range at " + juce::String (r) + " " + name);
            }
        }

        beginTest ("A knob's name is its value: it hits as that knob, marked as its value; the knob itself isn't");
        for (const auto [w, h] : windowSizes)
        {
            const auto name = sizeName (w, h);
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::none);
            for (int knob : { freqKnob, gainKnob, qKnob })
                expect (l.hit (l.valueArea (knob).getCentre() * l.scale, true, &peak) == PanelTarget { Kind::knob, knob, false, true },
                        juce::String (knob) + " " + name);
            expect (! l.hit (l.gainCentre * l.scale, true, &peak).onValue, name);
            expectWithinAbsoluteError (l.valueArea (freqKnob).getY(), l.valueArea (gainKnob).getY(), 1.0e-3f, "the names share a line " + name);

            Band cut;
            cut.type = FilterType::highCut;
            expect (! l.hit (l.valueArea (gainKnob).getCentre() * l.scale, true, &cut).onValue, "a cut has no gain to type " + name);

            const auto menu = BandPanelLayout::make (w, h, 1.0f, PanelPopup::dynamics);
            for (int knob : { ratioKnob, attackKnob, releaseKnob })
            {
                expect (menu.hit (menu.valueArea (knob).getCentre() * menu.scale, true, &peak) == PanelTarget { Kind::knob, knob, true, true },
                        juce::String (knob) + " " + name);
                expect (menu.dynamicsKnobs[(size_t) (knob - ratioKnob)].contains (menu.valueArea (knob)), name);
            }

            const auto stereoMenu = BandPanelLayout::make (w, h, 1.0f, PanelPopup::stereo);
            expect (stereoMenu.hit (stereoMenu.valueArea (posKnob).getCentre() * stereoMenu.scale, true, &peak) == PanelTarget { Kind::knob, posKnob, true, true },
                    "the position's value " + name);
        }

        beginTest ("Typed numbers: signs, points, commas, k for thousands, units ignored");
        {
            const auto near = [this] (const juce::String& text, float expected, bool seconds = false)
            {
                const auto v = parseTypedNumber (text, seconds);
                expect (v.has_value(), text);
                if (v.has_value())
                    expectWithinAbsoluteError (*v, expected, 1.0e-3f, text);
            };
            near ("250", 250.0f);
            near ("250 Hz", 250.0f);
            near ("1.5k", 1500.0f);
            near ("1.5 kHz", 1500.0f);
            near ("2k5", 2500.0f);
            near ("-6", -6.0f);
            near ("+3.5 dB", 3.5f);
            near ("0,7", 0.7f);
            near (".5", 0.5f);
            near ("4:1", 4.0f);
            near ("+45" + juce::String::charToString ((juce::juce_wchar) 0xb0), 45.0f);
            near ("90 ms", 90.0f, true);
            near ("0.2 s", 200.0f, true);
            near ("0.2s", 0.2f);
            for (auto bad : { "", "dB", "-", "k", "abc" })
                expect (! parseTypedNumber (bad).has_value(), bad);
        }

        beginTest ("Typing sets a knob, clamped to its range; nonsense leaves it alone");
        {
            Band b;
            expect (setKnobFromText (b, freqKnob, "2.5k"));
            expectWithinAbsoluteError (latToFreq (b.lat), 2500.0f, 0.5f);
            expect (setKnobFromText (b, freqKnob, "5"));
            expectWithinAbsoluteError (latToFreq (b.lat), minFreq, 0.01f);

            expect (setKnobFromText (b, gainKnob, "-6.5 dB"));
            expectEquals (b.gainDb, -6.5f);
            expect (setKnobFromText (b, gainKnob, "99"));
            expectEquals (b.gainDb, maxGainDb);

            expect (setKnobFromText (b, qKnob, "4"));
            expectEquals (b.q, 4.0f);
            Band shelf;
            shelf.type = FilterType::lowShelf;
            expect (setKnobFromText (shelf, qKnob, "10"));
            expectEquals (shelf.q, maxShelfQ, "a shelf's Q stops lower");
            Band cut;
            cut.type = FilterType::lowCut;
            expect (setKnobFromText (cut, qKnob, "40 dB/oct"));
            expectEquals (cut.slope, 36, "a cut's slope goes to the nearest one");
            expect (! setKnobFromText (cut, gainKnob, "3"), "a cut has no gain");

            for (const auto& [text, degrees] : { std::pair { "L", -90.0f }, std::pair { "right", 90.0f }, std::pair { "Mid", 0.0f },
                                                 std::pair { "side", 180.0f }, std::pair { "-30", -30.0f }, std::pair { "400", 180.0f } })
            {
                expect (setKnobFromText (b, posKnob, text), text);
                expectWithinAbsoluteError (b.lon * 180.0f / (float) pi, degrees, 1.0e-3f, text);
            }

            expect (setKnobFromText (b, rangeKnob, "-8"));
            expectEquals (b.range, -8.0f);
            expect (setKnobFromText (b, rangeKnob, "off"));
            expectEquals (b.range, 0.0f);
            expect (setKnobFromText (b, thresholdKnob, "-100"));
            expectEquals (b.threshold, minThresholdDb);
            expect (setKnobFromText (b, ratioKnob, "4:1"));
            expectEquals (b.ratio, 4.0f);
            expect (setKnobFromText (b, attackKnob, "10 ms"));
            expectEquals (b.attackMs, 10.0f);
            expect (setKnobFromText (b, releaseKnob, "1.5 s"));
            expectEquals (b.releaseMs, 1500.0f);

            const Band before = b;
            expect (! setKnobFromText (b, gainKnob, "loud"));
            expect (! setKnobFromText (b, freqKnob, ""));
            expectEquals (b.gainDb, before.gainDb);
            expectEquals (b.lat, before.lat);
            expect (! setKnobFromText (b, liveGainMeter, "3"), "the meter is a readout");
        }

        beginTest ("A knob's typed text starts as the value it shows");
        {
            Band b;
            b.gainDb = 3.0f;
            expectEquals (knobValueText (b, gainKnob), juce::String ("+3.0 dB"));
            expect (setKnobFromText (b, gainKnob, knobValueText (b, gainKnob)), "what's shown reads back");
            expectEquals (b.gainDb, 3.0f);
            b.lat = freqToLat (1500.0f);
            expect (setKnobFromText (b, freqKnob, knobValueText (b, freqKnob)));
            expectWithinAbsoluteError (latToFreq (b.lat), 1500.0f, 1.0f);
        }

        beginTest ("Controls a band doesn't have never hit: a cut's gear, a stereo band's position value");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::none);
            const auto menu = BandPanelLayout::make (w, h, 1.0f, PanelPopup::stereo);
            const auto name = sizeName (w, h);
            Band stereo;
            stereo.stereo = true;
            Band cut;
            cut.type = FilterType::lowCut;

            expect (l.hit (l.gear.getCentre() * l.scale, true, &cut).kind == Kind::background, "cut gear " + name);
            expect (l.hit (l.gear.getCentre() * l.scale, true, &stereo).kind == Kind::dynamicsMenu, "a stereo peak keeps its gear " + name);
            expect (menu.hit (menu.positionValue.getCentre() * menu.scale, true, &stereo).kind == Kind::background, "stereo: no position to type " + name);
            expect (menu.hit (menu.positionSlider.getCentre() * menu.scale, true, &stereo).kind == Kind::positionSlider, "but the slider makes it mono " + name);
        }

        beginTest ("Host track colours: alpha 0 still counts, all zeros is none");
        {
            expect (! SphereView::hostTrackColour (std::nullopt).has_value());
            expect (! SphereView::hostTrackColour (0u).has_value());
            expect (SphereView::hostTrackColour (0x00ff8020u) == juce::Colour (0xffff8020u), "alpha 0 is made opaque");
            expect (SphereView::hostTrackColour (0xff336699u) == juce::Colour (0xff336699u), "an opaque colour is kept");
            expect (SphereView::hostTrackColour (0x80336699u) == juce::Colour (0x80336699u), "so is a partial alpha");
        }

        beginTest ("The corner controls sit on the body, inside it, clear of the knobs and each other");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::none);
            const auto name = sizeName (w, h);
            const float body = l.panel.getY() + 36.0f;
            const std::array controls { l.power, l.solo, l.gear, l.previous, l.number, l.next, l.remove, l.close, l.typePill, l.stereoPill };

            for (size_t i = 0; i < controls.size(); ++i)
            {
                expectGreaterOrEqual (controls[i].getY(), body, "below the body's top " + name);
                expect (l.panel.contains (controls[i]), "inside the panel " + name);
                for (size_t j = i + 1; j < controls.size(); ++j)
                    expect (! controls[i].intersects (controls[j]), juce::String ((int) i) + " and " + juce::String ((int) j) + " " + name);
            }

            constexpr float sideKnob = 22.0f, gainWell = 51.0f; // the housing round it
            expectLessThan (l.typePill.getRight(), l.freqCentre.x - sideKnob, "the type pill is clear of Freq " + name);
            expectGreaterThan (l.stereoPill.getX(), l.qCentre.x + sideKnob, "stereo is clear of Q " + name);
            expectLessThan (l.gear.getRight(), l.freqCentre.x - sideKnob, "the left tools are clear of Freq " + name);
            expectGreaterThan (l.previous.getX(), l.qCentre.x + sideKnob, "the right ones of Q " + name);
            for (const auto centre : { l.freqCentre, l.qCentre })
                expectGreaterThan (centre.getDistanceFrom (l.gainCentre) - sideKnob - gainWell, 6.0f, "Freq and Q don't touch Gain's housing " + name);
        }

        beginTest ("The outline: Gain's housing rises over the body's top edge, nothing else does");
        {
            const auto l = BandPanelLayout::make (760.0f, 560.0f, 1.0f, PanelPopup::none);
            auto hitAt = [&] (float x, float y)    { return l.hit (juce::Point<float> (x, y) * l.scale, true, &peak).kind; };
            const float top = l.panel.getY();

            expect (hitAt (l.panel.getCentreX(), top + 2.0f) != Kind::none, "the housing reaches the top");
            expect (hitAt (l.panel.getCentreX() - 25.0f, top + 10.0f) != Kind::none, "round over Gain");
            expect (hitAt (l.panel.getCentreX() - 60.0f, top + 10.0f) == Kind::none, "but no wider");
            expect (hitAt (l.panel.getX() + 30.0f, top + 10.0f) == Kind::none, "left of it the panel starts lower");
            expect (hitAt (l.panel.getRight() - 30.0f, top + 10.0f) == Kind::none, "and right of it");
            expect (hitAt (l.panel.getX() + 30.0f, top + 40.0f) != Kind::none, "the body itself is panel");
            expect (hitAt (l.panel.getX() + 1.0f, l.panel.getBottom() - 1.0f) == Kind::none, "rounded corners");
            expect (hitAt (l.panel.getCentreX(), l.panel.getBottom() + 4.0f) == Kind::none, "nothing below it");
            expect (hitAt (l.panel.getX() + 30.0f, top + 72.0f) == Kind::background, "empty panel is background");
        }

        beginTest ("Closed, nothing hits; with no band, the panel is only background");
        {
            const auto l = BandPanelLayout::make (760.0f, 560.0f, 1.0f, PanelPopup::none);
            const auto gain = l.gainCentre * l.scale;
            expect (l.hit (gain, false, &peak).kind == Kind::none);
            expect (l.hit (gain, true, nullptr).kind == Kind::background);
            expect (l.hit (l.solo.getCentre() * l.scale, true, nullptr).kind == Kind::background);
        }

        beginTest ("The type menu opens above its pill; its items hit as types in the menu");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::types);
            const auto name = sizeName (w, h);
            expectLessOrEqual (l.popupArea.getBottom(), l.typePill.getY(), name);
            for (int i = 0; i < numFilterTypes; ++i)
                expect (l.hit (l.typeItems[(size_t) i].getCentre() * l.scale, true, &peak) == PanelTarget { Kind::type, i, true }, name);
            expect (l.hit (l.popupArea.getTopLeft().translated (1.0f, 1.0f) * l.scale, true, &peak) == PanelTarget { Kind::background, 0, true },
                    "the menu's edge keeps it open " + name);
            expect (l.hit (l.typeItems[0].getCentre() * l.scale, true, nullptr).kind == Kind::none, "no band, no menu " + name);
        }

        beginTest ("The stereo menu opens above its pill, inside the window: its choices, then the position slider");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::stereo);
            const auto name = sizeName (w, h);
            expectLessOrEqual (l.popupArea.getBottom(), l.stereoPill.getY(), name);
            expectLessOrEqual (l.popupArea.getRight(), l.panel.getRight(), name);
            expectGreaterOrEqual (l.popupArea.getY(), 0.0f, name);
            for (int i = 0; i < numStereoChoices; ++i)
                expect (l.hit (l.stereoItems[(size_t) i].getCentre() * l.scale, true, &peak) == PanelTarget { Kind::stereoItem, i, true }, name);
            expect (l.hit (l.positionSlider.getCentre() * l.scale, true, &peak) == PanelTarget { Kind::positionSlider, 0, true }, name);
            expectGreaterThan (l.positionSlider.getY(), l.stereoItems[numStereoChoices - 1].getBottom(), "the slider is last " + name);
        }

        beginTest ("Stereo choices: both channels, or Mid, Side, Left and Right; the one a band is at shows as on");
        {
            Band b;
            applyStereoChoice (b, stereoSide);
            expect (! b.stereo && stereoChoiceIsOn (b, stereoSide) && ! stereoChoiceIsOn (b, stereoMid));
            applyStereoChoice (b, stereoLeft);
            expectWithinAbsoluteError (b.lon, -halfPi, 1.0e-6f);
            expect (stereoChoiceIsOn (b, stereoLeft));
            applyStereoChoice (b, stereoBoth);
            expect (b.stereo && stereoChoiceIsOn (b, stereoBoth) && ! stereoChoiceIsOn (b, stereoLeft), "stereo: no position is on");
            expectWithinAbsoluteError (b.lon, -halfPi, 1.0e-6f, "the position stays for when it's mono again");
            applyStereoChoice (b, stereoRight);
            expect (! b.stereo && stereoChoiceIsOn (b, stereoRight));
        }

        beginTest ("The dynamics sit on the left shoulder, clear of Gain's housing, pointing at the gear");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::dynamics);
            const auto name = sizeName (w, h);
            expectLessThan (l.popupArea.getBottom(), l.panel.getY() + 36.0f, "above the body " + name);
            expectGreaterOrEqual (l.popupArea.getY(), 0.0f, name);
            expectWithinAbsoluteError (l.popupArea.getX(), l.panel.getX(), 1.0e-3f, "on the left edge " + name);
            expectWithinAbsoluteError (l.arrowX, l.gear.getCentreX(), 1.0e-3f, name);
            expect (l.hit (juce::Point<float> (l.popupArea.getRight() + 1.0f, l.popupArea.getBottom() - 1.0f) * l.scale, true, &peak).kind == Kind::none,
                    "the housing starts past it " + name);

            for (int i = 0; i < 3; ++i)
                expect (l.hit (l.dynamicsKnobs[(size_t) i].getCentre() * l.scale, true, &peak) == PanelTarget { Kind::knob, ratioKnob + i, true }, name);
            expect (l.hit (l.sidechain.getCentre() * l.scale, true, &peak) == PanelTarget { Kind::sidechain, 0, true }, name);
            expect (! l.sidechain.intersects (l.dynamicsKnobs[0]), name);
            expectLessOrEqual (l.grMeter.getRight(), l.popupArea.getRight(), name);
        }

        beginTest ("The band list sits on the right shoulder, a row per band, every band fitting in the window");
        for (const auto [w, h] : windowSizes)
            for (const int count : { 1, 4, maxBands })
            {
                const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::bands, count);
                const auto name = sizeName (w, h) + " with " + juce::String (count);
                expectWithinAbsoluteError (l.popupArea.getRight(), l.panel.getRight(), 1.0e-3f, "on the right edge " + name);
                expectWithinAbsoluteError (l.arrowX, l.number.getCentreX(), 1.0e-3f, name);
                expectLessThan (l.popupArea.getBottom(), l.panel.getY() + 36.0f, name);
                expectGreaterOrEqual (l.popupArea.getY(), 0.0f, "inside the window " + name);
                for (int i = 0; i < count; ++i)
                {
                    expect (l.hit (l.bandItems[(size_t) i].getCentre() * l.scale, true, &peak) == PanelTarget { Kind::bandItem, i, true }, name);
                    if (i > 0)
                        expectGreaterOrEqual (l.bandItems[(size_t) i].getY(), l.bandItems[(size_t) i - 1].getBottom() - 1.0e-3f, name);
                }
            }

        beginTest ("Bands by frequency: low to high, whatever order they were added in");
        {
            BandSet set;
            for (const float f : { 5000.0f, 80.0f, 900.0f })
            {
                Band b;
                b.lat = freqToLat (f);
                b.id = set.count;
                set.items[(size_t) set.count++] = b;
            }
            std::array<const Band*, maxBands> order;
            expectEquals (bandsByFrequency (set, order), 3);
            expect (order[0]->id == 1 && order[1]->id == 2 && order[2]->id == 0);
        }
    }
};

static LayoutTests layoutTests;

} // namespace threedq::test
