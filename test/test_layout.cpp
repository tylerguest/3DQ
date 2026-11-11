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
            auto hitAt = [&] (juce::Point<float> p)    { return l.hit (p * l.scale, true, true); };

            expect (hitAt (l.previous.getCentre()).kind == Kind::previous, "previous " + name);
            expect (hitAt (l.next.getCentre()).kind == Kind::next, "next " + name);
            expect (hitAt (l.typePill.getCentre()).kind == Kind::typeMenu, "type " + name);
            expect (hitAt (l.solo.getCentre()).kind == Kind::solo, "solo " + name);
            expect (hitAt (l.mute.getCentre()).kind == Kind::mute, "mute " + name);
            expect (hitAt (l.remove.getCentre()).kind == Kind::remove, "remove " + name);
            expect (hitAt (l.gear.getCentre()).kind == Kind::dynamicsMenu, "gear " + name);
            expect (hitAt (l.stereo.getCentre().translated (-10.0f, 0.0f)) == PanelTarget { Kind::stereo, 0 }, "mono half " + name);
            expect (hitAt (l.stereo.getCentre().translated (10.0f, 0.0f)) == PanelTarget { Kind::stereo, 1 }, "stereo half " + name);
            expect (hitAt (l.positionPill.getCentre()) == PanelTarget { Kind::knob, posKnob }, "position " + name);
            expect (hitAt (l.gainCentre) == PanelTarget { Kind::knob, gainKnob }, "gain " + name);
            expect (hitAt (l.freqCentre) == PanelTarget { Kind::knob, freqKnob }, "freq " + name);
            expect (hitAt (l.qCentre) == PanelTarget { Kind::knob, qKnob }, "q " + name);
        }

        beginTest ("The side controls sit in the panel's shoulders, below the wave, and don't overlap");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::none);
            const auto name = sizeName (w, h);
            const float shoulder = l.panel.getY() + 30.0f;

            for (const auto& r : { l.previous, l.next, l.typePill, l.solo, l.mute, l.remove, l.stereo, l.positionPill })
            {
                expectGreaterOrEqual (r.getY(), shoulder, "below the shoulder " + name);
                expectLessOrEqual (r.getBottom(), l.panel.getBottom(), "inside the panel " + name);
                expect (l.panel.contains (r), name);
            }

            expectLessOrEqual (l.previous.getBottom(), l.typePill.getY(), name);
            expectLessOrEqual (l.solo.getBottom(), l.stereo.getY(), name);
            expectLessOrEqual (l.stereo.getBottom(), l.positionPill.getY(), name);
            expectLessThan (l.typePill.getRight(), l.freqCentre.x - 17.0f, "the type pill is clear of Freq " + name);
            expectGreaterThan (l.stereo.getX(), l.qCentre.x + 17.0f, "stereo is clear of Q " + name);
        }

        beginTest ("The outline: a crown over Gain, lower shoulders either side");
        {
            const auto l = BandPanelLayout::make (760.0f, 560.0f, 1.0f, PanelPopup::none);
            auto hitAt = [&] (float x, float y)    { return l.hit (juce::Point<float> (x, y) * l.scale, true, true).kind; };
            const float top = l.panel.getY();

            expect (hitAt (l.panel.getCentreX(), top + 6.0f) != Kind::none, "the crown rises over the centre");
            expect (hitAt (l.panel.getX() + 30.0f, top + 6.0f) == Kind::none, "left of it the panel starts lower");
            expect (hitAt (l.panel.getRight() - 30.0f, top + 6.0f) == Kind::none, "and right of it");
            expect (hitAt (l.panel.getX() + 30.0f, top + 34.0f) != Kind::none, "the shoulder itself is panel");
            expect (hitAt (l.panel.getX() + 1.0f, l.panel.getBottom() - 1.0f) == Kind::none, "rounded corners");
            expect (hitAt (l.panel.getCentreX(), l.panel.getBottom() + 4.0f) == Kind::none, "nothing below it");
            expect (hitAt (l.panel.getCentreX() - 60.0f, l.panel.getBottom() - 6.0f) == Kind::background, "empty panel is background");
        }

        beginTest ("Closed, nothing hits; with no band, the panel is only background");
        {
            const auto l = BandPanelLayout::make (760.0f, 560.0f, 1.0f, PanelPopup::none);
            const auto gain = l.gainCentre * l.scale;
            expect (l.hit (gain, false, true).kind == Kind::none);
            expect (l.hit (gain, true, false).kind == Kind::background);
            expect (l.hit (l.solo.getCentre() * l.scale, true, false).kind == Kind::background);
        }

        beginTest ("The type menu opens above its pill; its items hit as types in the menu");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::types);
            const auto name = sizeName (w, h);
            expectLessOrEqual (l.popupArea.getBottom(), l.typePill.getY(), name);
            for (int i = 0; i < numFilterTypes; ++i)
                expect (l.hit (l.typeItems[(size_t) i].getCentre() * l.scale, true, true) == PanelTarget { Kind::type, i, true }, name);
            expect (l.hit (l.popupArea.getTopLeft().translated (1.0f, 1.0f) * l.scale, true, true) == PanelTarget { Kind::background, 0, true },
                    "the menu's edge keeps it open " + name);
            expect (l.hit (l.typeItems[0].getCentre() * l.scale, true, false).kind == Kind::none, "no band, no menu " + name);
        }

        beginTest ("The dynamics menu floats above the crown, inside the window, with its knobs and sidechain");
        for (const auto [w, h] : windowSizes)
        {
            const auto l = BandPanelLayout::make (w, h, 1.0f, PanelPopup::dynamics);
            const auto name = sizeName (w, h);
            expectLessThan (l.popupArea.getBottom(), l.panel.getY(), "above the crown " + name);
            expectGreaterOrEqual (l.popupArea.getY(), 0.0f, name);
            expectGreaterOrEqual (l.popupArea.getX(), 0.0f, name);
            expectLessOrEqual (l.popupArea.getRight() * l.scale, w, name);
            expectWithinAbsoluteError (l.popupArea.getCentreX(), l.panel.getCentreX(), 1.0e-3f, "centred " + name);

            for (int i = 0; i < 3; ++i)
                expect (l.hit (l.dynamicsKnobs[(size_t) i].getCentre() * l.scale, true, true) == PanelTarget { Kind::knob, ratioKnob + i, true }, name);
            expect (l.hit (l.sidechain.getCentre() * l.scale, true, true) == PanelTarget { Kind::sidechain, 0, true }, name);
            expect (! l.sidechain.intersects (l.dynamicsKnobs[2]), name);
        }
    }
};

static LayoutTests layoutTests;

} // namespace threedq::test
