/*
  ==============================================================================

    BandPanel: see band_panel.h.

  ==============================================================================
*/

#include "band_panel.h"
#include "theme.h"

namespace threedq
{

namespace
{
constexpr float panelHeight = 124.0f;
constexpr float maxPanelWidth = 456.0f;
// The body's top edge. Gain sits on its own raised disc, the housing, which
// overlaps that edge: its top quarter stands out above the panel.
constexpr float bodyTop = 36.0f;
constexpr float housingRadius = 51.0f;
constexpr float gainY = 52.0f;             // Gain's centre, from the panel's top
constexpr float sideKnobY = 76.0f, sideKnobSpacing = 76.0f; // Freq's and Q's centres, from Gain's: clear of its well
constexpr float labelY = 102.0f, labelHeight = 16.0f;       // every knob's name or value, on one line
constexpr float toolY = 45.0f, toolSize = 22.0f, toolGap = 5.0f; // the corner buttons
constexpr float pillY = 84.0f, pillHeight = 26.0f;
constexpr float edgeInset = 12.0f;
constexpr float panelBottomInset = bottomBarHeight + 14.0f; // floating clear of the bar
constexpr float panelViewGap = 10.0f;      // between the sphere's room and Gain's housing
constexpr float panelFitMargin = 10.0f;    // kept clear above and below the sphere when it is fitted
constexpr float gainKnobRadius = 30.0f;
constexpr float thresholdRadius = 36.5f, thresholdWidth = 5.0f; // the inner ring round Gain
constexpr float rangeRadius = 44.5f, rangeWidth = 6.0f;         // the outer one
constexpr float ringSplit = 40.5f;         // where the threshold ring's grab area ends and the range's starts
constexpr float wellRadius = 49.0f;        // the dark well both rings sit in
constexpr float sideKnobRadius = 22.0f;
constexpr float popupKnobRadius = 18.0f;   // ratio, attack and release
constexpr float menuItemHeight = 26.0f, menuPad = 5.0f, menuGap = 6.0f, shoulderGap = 8.0f, separatorHeight = 11.0f;
constexpr float knobStartAngle = -2.35619f; // 12 o'clock is 0, clockwise
constexpr float knobSweep = 4.71239f;

// Log-scaled knobs: position 0..1 <-> value.
float fromLog (float t, float lo, float hi)     { return lo * std::pow (hi / lo, std::clamp (t, 0.0f, 1.0f)); }
float toLog (float v, float lo, float hi)       { return std::log (v / lo) / std::log (hi / lo); }

// How far the band's gain has moved from its static gain, peak-held so it can
// be read.
float liveMovement (const Band& b)    { return b.hasLiveGain ? b.peakMovementDb : 0.0f; }

float qCeiling (const Band& b)    { return b.isShelf() ? maxShelfQ : maxQ; }

int slopeIndex (const Band& b)
{
    const auto& slopes = cutSlopes;
    const auto found = std::find (slopes.begin(), slopes.end(), b.slope);
    return found == slopes.end() ? 3 : (int) (found - slopes.begin());
}

float snapDb (float db)    { return std::round (db * 20.0f) / 20.0f; }

const char* knobName (const Band& b, int knob)
{
    switch (knob)
    {
        case freqKnob:   return "FREQ";
        case gainKnob:   return "GAIN";
        case qKnob:      return b.isCut() ? "SLOPE" : "Q";
        case posKnob:    return "POS";
        case rangeKnob:  return "RANGE";
        case thresholdKnob: return "THRESH";
        case ratioKnob:  return "RATIO";
        case attackKnob: return "ATTACK";
        case releaseKnob: return "RELEASE";
        default:         return "GR";
    }
}

juce::String positionText (float lon)
{
    const int degrees = (int) std::lround (lon * 180.0f / pi);

    switch (std::abs (degrees))
    {
        case 0:    return "Mid";
        case 180:  return "Side";
        case 90:   return degrees < 0 ? "L" : "R";
        default:   return (degrees > 0 ? "+" : "") + juce::String (degrees) + juce::String::charToString ((juce::juce_wchar) 0xb0);
    }
}

juce::String knobText (const Band& b, int knob)
{
    if (knob == liveGainMeter)
    {
        if (! b.hasLiveGain) // a static band, a cut, or muted
            return "-";

        // Always one decimal, so the number keeps its width as it changes.
        const float db = std::round (liveMovement (b) * 10.0f) / 10.0f;
        return (db > 0.0f ? "+" : db < 0.0f ? "-" : "") + juce::String (std::abs (db), 1) + " dB";
    }

    if (! knobEnabled (b, knob))
        return "-";

    switch (knob)
    {
        case freqKnob:   return formatFreq (latToFreq (b.lat));
        case gainKnob:   return formatGain (b.gainDb);
        case qKnob:      return b.isCut() ? juce::String (b.slope) + " dB/oct" : formatQ (b.q);
        case posKnob:    return positionText (b.lon);
        case rangeKnob:  return b.range == 0.0f ? juce::String ("Off") : formatGain (b.range);
        case thresholdKnob: return formatDb (b.threshold) + " dB";
        case ratioKnob:  return formatRatio (b.ratio);
        case attackKnob: return formatMs (b.attackMs);
        default:         return formatMs (b.releaseMs);
    }
}

bool knobIsBipolar (int knob)    { return knob == gainKnob || knob == posKnob || knob == rangeKnob || knob == liveGainMeter; }

juce::Path typeGlyph (int type, juce::Point<float> c)
{
    juce::Path p;

    const auto line = [&p, c] (std::initializer_list<juce::Point<float>> points)
    {
        bool first = true;
        for (const auto& point : points)
        {
            if (first)
                p.startNewSubPath (c + point);
            else
                p.lineTo (c + point);
            first = false;
        }
    };

    switch ((FilterType) type)
    {
        case FilterType::peak:
            p.startNewSubPath (c.x - 7.0f, c.y + 3.0f);
            p.cubicTo (c.x - 3.0f, c.y + 3.0f, c.x - 2.0f, c.y - 4.0f, c.x, c.y - 4.0f);
            p.cubicTo (c.x + 2.0f, c.y - 4.0f, c.x + 3.0f, c.y + 3.0f, c.x + 7.0f, c.y + 3.0f);
            break;
        case FilterType::lowCut:     line ({ { -7.0f, 4.0f }, { -2.0f, -3.0f }, { 7.0f, -3.0f } }); break;
        case FilterType::highCut:    line ({ { -7.0f, -3.0f }, { 2.0f, -3.0f }, { 7.0f, 4.0f } }); break;
        case FilterType::lowShelf:   line ({ { -7.0f, -3.0f }, { -2.0f, -3.0f }, { 2.0f, 3.0f }, { 7.0f, 3.0f } }); break;
        case FilterType::highShelf:  line ({ { -7.0f, 3.0f }, { -2.0f, 3.0f }, { 2.0f, -3.0f }, { 7.0f, -3.0f } }); break;
    }

    return p;
}

const char* typeName (int type)
{
    static constexpr std::array<const char*, numFilterTypes> names { "Peak", "High pass", "Low pass", "Low shelf", "High shelf" };
    return names[(size_t) type];
}

const char* stereoChoiceName (int choice)
{
    static constexpr std::array<const char*, numStereoChoices> names { "Stereo (both channels)", "Mid", "Side", "Left", "Right" };
    return names[(size_t) choice];
}

void strokeRounded (juce::Graphics& g, const juce::Path& p, float thickness)
{
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// The panel's body: a plain rounded rectangle below the top edge.
juce::Path panelBody (juce::Rectangle<float> panel)
{
    juce::Path p;
    p.addRoundedRectangle (panel.withTop (panel.getY() + bodyTop), 12.0f);
    return p;
}

// Gain's housing: a disc over the body's top edge.
juce::Rectangle<float> housingArea (juce::Rectangle<float> panel)
{
    return juce::Rectangle<float> (2.0f * housingRadius, 2.0f * housingRadius).withCentre ({ panel.getCentreX(), panel.getY() + gainY });
}

// Everything the panel covers, for hit-testing: the body and the housing.
juce::Path panelOutline (juce::Rectangle<float> panel)
{
    auto p = panelBody (panel);
    p.addEllipse (housingArea (panel));
    return p;
}

// A surface: a matte graphite body, barely lighter at the top, edged with a
// fine neutral line.
void surface (juce::Graphics& g, const juce::Path& shape, juce::Rectangle<float> area, float alpha)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (37, 37, 35).withAlpha (alpha), area.getY(),
                                                       juce::Colour (27, 27, 26).withAlpha (alpha), area.getBottom()));
    g.fillPath (shape);
    g.setColour (textColour.withAlpha (0.08f * alpha));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}
}

int bandsByFrequency (const BandSet& bands, std::array<const Band*, maxBands>& order)
{
    int count = 0;
    for (const auto& b : bands)
        order[(size_t) count++] = &b;
    std::sort (order.begin(), order.begin() + count, [] (const Band* a, const Band* b) { return a->lat < b->lat; });
    return count;
}

bool stereoChoiceIsOn (const Band& b, int choice)
{
    if (choice == stereoBoth)
        return b.stereo;

    const int degrees = (int) std::lround (b.lon * 180.0f / pi);
    switch (choice)
    {
        case stereoMid:   return ! b.stereo && degrees == 0;
        case stereoSide:  return ! b.stereo && std::abs (degrees) == 180;
        case stereoLeft:  return ! b.stereo && degrees == -90;
        default:          return ! b.stereo && degrees == 90;
    }
}

void applyStereoChoice (Band& b, int choice)
{
    b.stereo = choice == stereoBoth;
    switch (choice)
    {
        case stereoMid:   b.lon = 0; break;
        case stereoSide:  b.lon = pi; break;
        case stereoLeft:  b.lon = -halfPi; break;
        case stereoRight: b.lon = halfPi; break;
        default:          break; // both channels: the position stays for when it's mono again
    }
}

BandPanelLayout BandPanelLayout::make (float windowWidth, float windowHeight, float openAmount, PanelPopup popup, int bandCount)
{
    BandPanelLayout l;
    l.popup = popup;
    l.bandCount = std::clamp (bandCount, 0, maxBands);
    l.scale = uiScale (windowWidth, windowHeight);
    const float width = windowWidth / l.scale, height = windowHeight / l.scale;
    const float panelWidth = std::min (width - 8.0f, maxPanelWidth);
    const float openTop = height - panelBottomInset - panelHeight;
    const float closedTop = height + 6.0f;
    const float top = closedTop + (openTop - closedTop) * openAmount;
    const float left = (width - panelWidth) / 2.0f, cx = left + panelWidth / 2.0f;
    const float x = left + edgeInset, right = left + panelWidth - edgeInset;

    l.panel = { left, top, panelWidth, panelHeight };

    // Top left: on / off, solo, the dynamics.
    l.power = { x, top + toolY, toolSize, toolSize };
    l.solo = l.power.translated (toolSize + toolGap, 0.0f);
    l.gear = l.solo.translated (toolSize + toolGap, 0.0f);

    // Top right: < number >, remove, close.
    l.close = { right - toolSize, top + toolY, toolSize, toolSize };
    l.remove = l.close.translated (-(toolSize + toolGap), 0.0f);
    l.next = { l.remove.getX() - toolGap - 2.0f - 14.0f, top + toolY + 1.0f, 14.0f, 20.0f };
    l.number = { l.next.getX() - 20.0f, l.next.getY(), 20.0f, 20.0f };
    l.previous = { l.number.getX() - 14.0f, l.next.getY(), 14.0f, 20.0f };

    // Bottom corners: the type and stereo menus, as wide as fits beside Freq and Q.
    const float pillWidth = std::max (64.0f, panelWidth / 2.0f - sideKnobSpacing - sideKnobRadius - 8.0f - edgeInset);
    l.typePill = { x, top + pillY, pillWidth, pillHeight };
    l.stereoPill = { right - pillWidth, top + pillY, pillWidth, pillHeight };

    l.gainCentre = { cx, top + gainY };
    l.freqCentre = { cx - sideKnobSpacing, top + sideKnobY };
    l.qCentre = { cx + sideKnobSpacing, top + sideKnobY };

    // The menus open upwards, over the sphere: the type and stereo menus above
    // their pills, the dynamics and the band list on the shoulders either side
    // of Gain, pointing at the button that opened them.
    const float shoulderBottom = top + bodyTop - shoulderGap;
    const float housingReach = std::sqrt (housingRadius * housingRadius - juce::square (gainY - bodyTop + shoulderGap)); // its half-width there
    const float shoulderWidth = panelWidth / 2.0f - housingReach - 4.0f;
    if (popup == PanelPopup::types)
    {
        const float h = menuItemHeight * numFilterTypes + 2.0f * menuPad;
        l.popupArea = { x, l.typePill.getY() - menuGap - h, 150.0f, h };
        for (int i = 0; i < numFilterTypes; ++i)
            l.typeItems[(size_t) i] = { l.popupArea.getX() + menuPad, l.popupArea.getY() + menuPad + menuItemHeight * (float) i,
                                        l.popupArea.getWidth() - 2.0f * menuPad, menuItemHeight };
    }
    else if (popup == PanelPopup::stereo)
    {
        constexpr float sliderRow = 30.0f, menuWidth = 196.0f;
        const float h = 2.0f * menuPad + menuItemHeight * numStereoChoices + 2.0f * separatorHeight + sliderRow;
        l.popupArea = { right - menuWidth, l.stereoPill.getY() - menuGap - h, menuWidth, h };
        float y = l.popupArea.getY() + menuPad;
        for (int i = 0; i < numStereoChoices; ++i)
        {
            l.stereoItems[(size_t) i] = { l.popupArea.getX() + menuPad, y, menuWidth - 2.0f * menuPad, menuItemHeight };
            y += menuItemHeight + (i == stereoBoth ? separatorHeight : 0.0f);
        }
        y += separatorHeight;
        l.positionValue = { l.popupArea.getRight() - 14.0f - 56.0f, y + 7.0f, 56.0f, 16.0f };
        l.positionSlider = { l.popupArea.getX() + 14.0f, y + 7.0f, l.positionValue.getX() - 8.0f - (l.popupArea.getX() + 14.0f), 16.0f };
    }
    else if (popup == PanelPopup::dynamics)
    {
        constexpr float cell = 56.0f, row = 22.0f;
        const float menuWidth = std::min (174.0f, shoulderWidth);
        const float h = 9.0f + cell + 12.0f + row + 8.0f;
        l.popupArea = { left, shoulderBottom - h, menuWidth, h };
        const float cellWidth = (menuWidth - 16.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            l.dynamicsKnobs[(size_t) i] = { l.popupArea.getX() + 8.0f + cellWidth * (float) i, l.popupArea.getY() + 9.0f, cellWidth, cell };
        const float rowY = l.popupArea.getY() + 9.0f + cell + 12.0f;
        l.sidechain = { l.popupArea.getX() + 8.0f, rowY, 34.0f, row };
        l.sidechainNote = { l.sidechain.getRight() + 7.0f, rowY, 52.0f, row };
        const float meterX = l.sidechainNote.getRight() + 6.0f;
        l.grMeter = { meterX, rowY + row / 2.0f - 2.5f, l.popupArea.getRight() - 8.0f - meterX, 5.0f };
        l.arrowX = l.gear.getCentreX();
    }
    else if (popup == PanelPopup::bands)
    {
        const float menuWidth = std::min (186.0f, shoulderWidth);
        const float room = shoulderBottom - (presetBarBottom + 6.0f) - 2.0f * menuPad;
        const float rowHeight = l.bandCount > 0 ? std::clamp (room / (float) l.bandCount, 12.0f, menuItemHeight) : menuItemHeight;
        const float h = 2.0f * menuPad + rowHeight * (float) std::max (1, l.bandCount);
        l.popupArea = { left + panelWidth - menuWidth, shoulderBottom - h, menuWidth, h };
        for (int i = 0; i < l.bandCount; ++i)
            l.bandItems[(size_t) i] = { l.popupArea.getX() + menuPad, l.popupArea.getY() + menuPad + rowHeight * (float) i,
                                        menuWidth - 2.0f * menuPad, rowHeight };
        l.arrowX = l.number.getCentreX();
    }

    return l;
}

PanelTarget BandPanelLayout::hit (juce::Point<float> position, bool open, const Band* band) const
{
    using Kind = PanelTarget::Kind;
    const auto p = position / scale;

    if (! open)
        return { Kind::none };

    if (band != nullptr && popup != PanelPopup::none && popupArea.contains (p))
    {
        switch (popup)
        {
            case PanelPopup::types:
                for (int i = 0; i < numFilterTypes; ++i)
                    if (typeItems[(size_t) i].contains (p))
                        return { Kind::type, i, true };
                break;

            case PanelPopup::stereo:
                for (int i = 0; i < numStereoChoices; ++i)
                    if (stereoItems[(size_t) i].contains (p))
                        return { Kind::stereoItem, i, true };
                if (positionSlider.expanded (6.0f, 7.0f).contains (p))
                    return { Kind::positionSlider, 0, true };
                if (positionValue.expanded (0.0f, 7.0f).contains (p) && knobEnabled (*band, posKnob))
                    return { Kind::knob, posKnob, true, true };
                break;

            case PanelPopup::dynamics:
                for (int i = 0; i < 3; ++i)
                    if (dynamicsKnobs[(size_t) i].contains (p))
                        return { Kind::knob, ratioKnob + i, true, valueArea (ratioKnob + i).contains (p) };
                if (sidechain.contains (p))
                    return { Kind::sidechain, 0, true };
                break;

            case PanelPopup::bands:
                for (int i = 0; i < bandCount; ++i)
                    if (bandItems[(size_t) i].contains (p))
                        return { Kind::bandItem, i, true };
                break;

            case PanelPopup::none:
                break;
        }

        return { Kind::background, 0, true };
    }

    if (! panelOutline (panel).contains (p))
        return { Kind::none };

    if (band != nullptr)
    {
        if (power.contains (p))       return { Kind::mute };
        if (solo.contains (p))        return { Kind::solo };
        if (gear.contains (p) && ! band->isCut()) return { Kind::dynamicsMenu };
        if (previous.contains (p))    return { Kind::previous };
        if (number.contains (p))      return { Kind::bandList };
        if (next.contains (p))        return { Kind::next };
        if (remove.contains (p))      return { Kind::remove };
        if (close.contains (p))       return { Kind::close };
        if (typePill.contains (p))    return { Kind::typeMenu };
        if (stereoPill.contains (p))  return { Kind::stereoMenu };

        for (int knob : { freqKnob, gainKnob, qKnob })
            if (valueArea (knob).contains (p) && knobEnabled (*band, knob))
                return { Kind::knob, knob, false, true };

        const float fromGain = p.getDistanceFrom (gainCentre);
        if (fromGain <= gainKnobRadius + 1.5f)
            return { Kind::knob, gainKnob };
        if (fromGain <= ringSplit)
            return { Kind::knob, thresholdKnob };
        if (fromGain <= wellRadius)
            return { Kind::knob, rangeKnob };

        for (const auto [centre, knob] : { std::pair { freqCentre, freqKnob }, std::pair { qCentre, qKnob } })
            if (p.getDistanceFrom (centre) <= sideKnobRadius + 4.0f)
                return { Kind::knob, knob };
    }

    return { Kind::background };
}

juce::Rectangle<float> BandPanelLayout::valueArea (int knob) const
{
    // Matches where drawBandPanel writes each name and value.
    const float y = panel.getY() + labelY;
    switch (knob)
    {
        case freqKnob:  return { freqCentre.x - 36.0f, y, 72.0f, labelHeight };
        case qKnob:     return { qCentre.x - 36.0f, y, 72.0f, labelHeight };
        case gainKnob:  return { gainCentre.x - 40.0f, y, 80.0f, labelHeight };
        case posKnob:   return positionValue;
        case ratioKnob:
        case attackKnob:
        case releaseKnob:
        {
            const auto area = dynamicsKnobs[(size_t) (knob - ratioKnob)];
            return { area.getX(), area.getY() + 2.0f * popupKnobRadius + 4.0f, area.getWidth(), labelHeight };
        }
        default:        return {};
    }
}

void drawValueEntry (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, bool allSelected, float fontSize, float unit)
{
    const auto font = uiFont (fontSize);
    const auto field = area.expanded (2.0f * unit, 1.0f * unit);
    g.setColour (juce::Colour (0xff0d0d0c));
    g.fillRoundedRectangle (field, 3.0f * unit);
    g.setColour (textColour.withAlpha (0.35f));
    g.drawRoundedRectangle (field, 3.0f * unit, 1.0f * unit);

    const float width = juce::GlyphArrangement::getStringWidth (font, text);
    const float x = area.getCentreX() - width / 2.0f;
    if (allSelected && text.isNotEmpty())
    {
        g.setColour (textColour.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<float> (x - 1.0f * unit, area.getY() + 1.0f * unit, width + 2.0f * unit, area.getHeight() - 2.0f * unit));
    }

    g.setFont (font);
    g.setColour (textColour);
    g.drawText (text, area, juce::Justification::centred, false);

    // The caret blinks after the text once typing has started.
    if (! allSelected && (juce::Time::getMillisecondCounter() / 530) % 2 == 0)
        g.fillRect (juce::Rectangle<float> (x + width + 1.0f * unit, area.getY() + 2.0f * unit, 1.0f * unit, area.getHeight() - 4.0f * unit));
}

PanelViewFit fitViewAbovePanel (float width, float height, float zoom, float panY, SurfaceExtent extent)
{
    // The space between the preset bar and the panel.
    const float scale = uiScale (width, height);
    const float roomTop = scale * presetBarBottom;
    const float room = height - scale * (panelHeight + panelBottomInset + panelViewGap);
    const float radius = std::min (width, height) * sphereRadiusFraction * zoom;
    const float fittedRadius = std::min (radius, (room - roomTop - 2.0f * panelFitMargin) / (extent.top - extent.bottom));

    // Where the sphere's centre sits when the shape touches the top of the
    // space, and the bottom; halfway between centres the shape we actually see.
    const float highest = roomTop + panelFitMargin + extent.top * fittedRadius;
    const float lowest = room - panelFitMargin + extent.bottom * fittedRadius;
    const float centre = std::min (std::max ((highest + lowest) / 2.0f + panY, highest), lowest);
    return { fittedRadius / radius, centre - (height / 2.0f + panY) };
}

float knobPosition (const Band& b, int knob)
{
    switch (knob)
    {
        case freqKnob:   return (b.lat / halfPi + 1.0f) / 2.0f;
        case gainKnob:   return (b.gainDb + maxGainDb) / (2.0f * maxGainDb);
        case qKnob:
            if (b.isCut())
                return (float) slopeIndex (b) / (float) (cutSlopes.size() - 1);
            return std::log (b.q / minQ) / std::log (qCeiling (b) / minQ);
        case posKnob:    return (b.lon / pi + 1.0f) / 2.0f;
        case rangeKnob:  return (b.range + maxGainDb) / (2.0f * maxGainDb);
        case thresholdKnob: return (b.threshold - minThresholdDb) / (maxThresholdDb - minThresholdDb);
        case ratioKnob:  return toLog (b.ratio, minRatio, maxRatio);
        case attackKnob: return toLog (b.attackMs, minAttackMs, maxAttackMs);
        case releaseKnob: return toLog (b.releaseMs, minReleaseMs, maxReleaseMs);
        default:
            // The meter: the live change as a share of the range, round the centre.
            return b.range == 0.0f ? 0.5f : 0.5f + 0.5f * std::clamp (liveMovement (b) / std::abs (b.range), -1.0f, 1.0f);
    }
}

void setKnobPosition (Band& b, int knob, float position)
{
    const float t = std::clamp (position, 0.0f, 1.0f);

    switch (knob)
    {
        case freqKnob:  b.lat = (t * 2.0f - 1.0f) * halfPi; break;
        case gainKnob:  b.gainDb = snapDb ((t * 2.0f - 1.0f) * maxGainDb); break;
        case qKnob:
            if (b.isCut())
                b.slope = cutSlopes[(size_t) std::lround (t * (float) (cutSlopes.size() - 1))];
            else
                b.q = minQ * std::pow (qCeiling (b) / minQ, t);
            break;
        case posKnob:   b.lon = (t * 2.0f - 1.0f) * pi; break;
        case rangeKnob:
            b.range = snapDb ((t * 2.0f - 1.0f) * maxGainDb);
            if (std::abs (b.range) < 0.05f)
                b.range = 0;
            break;
        case thresholdKnob:
            b.threshold = snapDb (minThresholdDb + t * (maxThresholdDb - minThresholdDb));
            break;
        case ratioKnob:   b.ratio = std::round (fromLog (t, minRatio, maxRatio) * 10.0f) / 10.0f; break;
        case attackKnob:  b.attackMs = fromLog (t, minAttackMs, maxAttackMs); break;
        case releaseKnob: b.releaseMs = fromLog (t, minReleaseMs, maxReleaseMs); break;
        default:          break; // the meter can't be turned
    }
}

void resetKnob (Band& b, int knob)
{
    switch (knob)
    {
        case freqKnob:   b.lat = freqToLat (1000.0f); break;
        case gainKnob:   b.gainDb = 0; break;
        case qKnob:
            if (b.isCut())
                b.slope = defaultCutSlope;
            else
                b.q = 1;
            break;
        case posKnob:    b.lon = 0; break;
        case rangeKnob:  b.range = 0; break;
        case thresholdKnob: b.threshold = defaultThresholdDb; break;
        case ratioKnob:  b.ratio = defaultRatio; break;
        case attackKnob: b.attackMs = defaultAttackMs; break;
        case releaseKnob: b.releaseMs = defaultReleaseMs; break;
        default:         break;
    }
}

bool knobEnabled (const Band& b, int knob)
{
    if (knob == posKnob)
        return ! b.stereo;
    if (knob == liveGainMeter)
        return false; // a readout
    if (knob == gainKnob || knob == rangeKnob || knob == thresholdKnob || knob == ratioKnob || knob == attackKnob || knob == releaseKnob)
        return ! b.isCut();
    return true;
}

bool knobIsStepped (const Band& b, int knob)    { return knob == qKnob && b.isCut(); }

juce::String knobValueText (const Band& b, int knob)    { return knobText (b, knob); }

std::optional<float> parseTypedNumber (const juce::String& text, bool seconds)
{
    auto t = text.toLowerCase().removeCharacters (" \t").replaceCharacter (',', '.');
    float scale = 1;

    // "2k5": the k stands for the point.
    const int k = t.indexOfChar ('k');
    if (k > 0 && k + 1 < t.length() && juce::CharacterFunctions::isDigit (t[k - 1]) && juce::CharacterFunctions::isDigit (t[k + 1]))
    {
        t = t.replaceSection (k, 1, ".");
        scale = 1000;
    }

    int end = 0;
    if (end < t.length() && (t[end] == '+' || t[end] == '-'))
        ++end;
    bool digits = false, point = false;
    for (; end < t.length(); ++end)
    {
        if (juce::CharacterFunctions::isDigit (t[end]))
            digits = true;
        else if (t[end] == '.' && ! point)
            point = true;
        else
            break;
    }

    if (! digits)
        return std::nullopt;

    const auto unit = t.substring (end);
    if (unit.startsWithChar ('k'))
        scale = 1000;
    else if (seconds && unit.startsWithChar ('s'))
        scale = 1000;

    return t.substring (0, end).getFloatValue() * scale;
}

bool setKnobFromText (Band& b, int knob, const juce::String& text)
{
    if (! knobEnabled (b, knob))
        return false;

    if (knob == posKnob)
    {
        const auto word = text.trim().toLowerCase();
        for (const auto& [names, degrees] : { std::pair { juce::StringArray { "m", "mid", "c", "centre", "center" }, 0.0f },
                                              std::pair { juce::StringArray { "l", "left" }, -90.0f },
                                              std::pair { juce::StringArray { "r", "right" }, 90.0f },
                                              std::pair { juce::StringArray { "s", "side" }, 180.0f } })
            if (names.contains (word))
            {
                b.lon = degrees * pi / 180.0f;
                return true;
            }
    }

    if (knob == rangeKnob && text.trim().equalsIgnoreCase ("off"))
    {
        b.range = 0;
        return true;
    }

    const auto value = parseTypedNumber (text, knob == attackKnob || knob == releaseKnob);
    if (! value.has_value())
        return false;

    const float v = *value;

    switch (knob)
    {
        case freqKnob:  b.lat = freqToLat (std::clamp (v, minFreq, maxFreq)); break;
        case gainKnob:  b.gainDb = snapDb (std::clamp (v, -maxGainDb, maxGainDb)); break;
        case qKnob:
            if (b.isCut())
                b.slope = *std::min_element (cutSlopes.begin(), cutSlopes.end(),
                                             [v] (int x, int y) { return std::abs ((float) x - v) < std::abs ((float) y - v); });
            else
                b.q = std::clamp (v, minQ, qCeiling (b));
            break;
        case posKnob:   b.lon = std::clamp (v, -180.0f, 180.0f) * pi / 180.0f; break;
        case rangeKnob:
            b.range = snapDb (std::clamp (v, -maxGainDb, maxGainDb));
            if (std::abs (b.range) < 0.05f)
                b.range = 0;
            break;
        case thresholdKnob: b.threshold = snapDb (std::clamp (v, minThresholdDb, maxThresholdDb)); break;
        case ratioKnob:   b.ratio = std::round (std::clamp (v, minRatio, maxRatio) * 10.0f) / 10.0f; break;
        case attackKnob:  b.attackMs = std::clamp (v, minAttackMs, maxAttackMs); break;
        case releaseKnob: b.releaseMs = std::clamp (v, minReleaseMs, maxReleaseMs); break;
        default:          return false;
    }

    return true;
}

namespace
{
// A knob: a matte disc with a thin arc just inside its edge for the value
// (from the middle when bipolar) and a dot where it ends. Without track
// there's no arc, only a tick: Gain's rings stand in for it. position runs 0..1.
void drawKnob (juce::Graphics& g, juce::Point<float> c, float radius, float position, bool bipolar, bool lit, bool enabled,
               float alpha, bool track, juce::Colour arcColour)
{
    const float angle = knobStartAngle + std::clamp (position, 0.0f, 1.0f) * knobSweep;
    const float anchor = bipolar ? knobStartAngle + 0.5f * knobSweep : knobStartAngle;
    const float body = radius - 0.5f;
    const float arcRadius = body - (radius < 20.0f ? 3.5f : 4.0f), stroke = 2.0f;
    const auto bodyArea = juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c);

    // A soft shadow under it, the face lit from above, and a fine rim.
    g.setColour (juce::Colours::black.withAlpha (0.55f * alpha));
    g.fillEllipse (bodyArea.translated (0.0f, 1.5f).expanded (0.5f));
    juce::ColourGradient face (juce::Colour (58, 57, 52).brighter (lit ? 0.12f : 0.0f).withMultipliedAlpha (alpha), c.x, c.y - body * 0.75f,
                               juce::Colour (24, 24, 22).withMultipliedAlpha (alpha), c.x, c.y + body, true);
    face.addColour (0.6, juce::Colour (36, 35, 31).withMultipliedAlpha (alpha));
    g.setGradientFill (face);
    g.fillEllipse (bodyArea);
    g.setGradientFill (juce::ColourGradient::vertical (textColour.withAlpha ((lit ? 0.2f : 0.14f) * alpha), bodyArea.getY(),
                                                       textColour.withAlpha (0.02f * alpha), bodyArea.getBottom()));
    g.drawEllipse (bodyArea.reduced (0.5f), 1.0f);

    if (track)
    {
        g.setColour (juce::Colours::black.withAlpha (0.4f * alpha));
        strokeRounded (g, arc (c.x, c.y, arcRadius, knobStartAngle, knobStartAngle + knobSweep), stroke + 1.0f);
    }

    if (track && enabled && std::abs (angle - anchor) > 0.01f)
    {
        g.setColour (arcColour.withMultipliedAlpha (alpha));
        strokeRounded (g, arc (c.x, c.y, arcRadius, std::min (anchor, angle), std::max (anchor, angle)), stroke);
    }

    const auto at = [&c, angle] (float r) { return juce::Point<float> (c.x + std::sin (angle) * r, c.y - std::cos (angle) * r); };
    g.setColour (pale.withMultipliedAlpha (alpha));
    if (track)
    {
        const auto dot = at (arcRadius - 6.0f);
        g.fillEllipse (juce::Rectangle<float> (3.2f, 3.2f).withCentre (dot));
    }
    else
    {
        juce::Path tick;
        tick.startNewSubPath (at (body * 0.5f));
        tick.lineTo (at (body - 5.0f));
        strokeRounded (g, tick, 2.0f);
    }
}

juce::Path powerIcon (juce::Point<float> c)
{
    juce::Path p;
    p.addCentredArc (c.x, c.y + 0.6f, 4.2f, 4.2f, 0.0f, 0.7f, 2.0f * pi - 0.7f, true);
    p.startNewSubPath (c.x, c.y - 4.6f);
    p.lineTo (c.x, c.y - 0.2f);
    return p;
}

juce::Path gearIcon (juce::Point<float> c)
{
    juce::Path p;
    p.addEllipse (c.x - 2.4f, c.y - 2.4f, 4.8f, 4.8f);
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i * pi / 4.0f;
        p.startNewSubPath (c.x + std::cos (a) * 3.9f, c.y + std::sin (a) * 3.9f);
        p.lineTo (c.x + std::cos (a) * 5.4f, c.y + std::sin (a) * 5.4f);
    }
    return p;
}

juce::Path binIcon (juce::Point<float> c)
{
    const auto at = [c] (float x, float y) { return juce::Point<float> (c.x + x - 6.0f, c.y + y - 6.0f); };
    juce::Path p;
    p.startNewSubPath (at (2.2f, 3.4f));
    p.lineTo (at (9.8f, 3.4f));
    p.startNewSubPath (at (4.6f, 3.4f));
    p.lineTo (at (4.6f, 2.0f));
    p.lineTo (at (7.4f, 2.0f));
    p.lineTo (at (7.4f, 3.4f));
    p.startNewSubPath (at (3.4f, 3.4f));
    p.lineTo (at (4.0f, 10.2f));
    p.lineTo (at (8.0f, 10.2f));
    p.lineTo (at (8.6f, 3.4f));
    return p;
}

juce::Path crossIcon (juce::Point<float> c, float half)
{
    juce::Path p;
    p.startNewSubPath (c.x - half, c.y - half);
    p.lineTo (c.x + half, c.y + half);
    p.startNewSubPath (c.x + half, c.y - half);
    p.lineTo (c.x - half, c.y + half);
    return p;
}

juce::Path chevron (juce::Point<float> c, float direction, float size)
{
    juce::Path p;
    p.startNewSubPath (c.x - direction * size * 0.5f, c.y - size);
    p.lineTo (c.x + direction * size * 0.5f, c.y);
    p.lineTo (c.x - direction * size * 0.5f, c.y + size);
    return p;
}
} // namespace

void drawBandPanel (juce::Graphics& g, const BandPanelLayout& l, float dpr, float amount,
                    const BandSet& bands, int selected, bool soloed, PanelTarget hover, int activeKnob, bool sidechainConnected,
                    float levelDb, const ValueEdit* edit)
{
    using Kind = PanelTarget::Kind;

    if (amount <= 0.01f)
        return;

    const auto* band = bands.find (selected);
    const auto editing = [edit] (int knob) { return edit != nullptr && edit->knob == knob; };
    const auto entry = [&] (juce::Rectangle<float> area) { drawValueEntry (g, area, edit->text, edit->allSelected, 10.5f, 1.0f); };

    const juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (juce::AffineTransform::scale (dpr * l.scale));

    const auto faded = [amount] (juce::Colour c) { return c.withMultipliedAlpha (amount); };
    const auto idle = textColour.withAlpha (0.6f), bright = textColour, faint = textColour.withAlpha (0.38f);
    const auto text = [&] (const juce::String& s, juce::Rectangle<float> area, float size, juce::Colour c,
                           juce::Justification j = juce::Justification::centred, bool bold = false)
    {
        g.setFont (uiFont (size, bold));
        g.setColour (faded (c));
        g.drawText (s, area, j, true);
    };
    const auto stroke = [&] (const juce::Path& p, juce::Colour c, float thickness)
    {
        g.setColour (faded (c));
        strokeRounded (g, p, thickness);
    };
    // A button: bare, with a soft fill only while hovered.
    const auto toolBack = [&] (juce::Rectangle<float> area, bool hovered)
    {
        if (hovered)
        {
            g.setColour (faded (textColour.withAlpha (0.08f)));
            g.fillRoundedRectangle (area, 6.0f);
        }
    };
    // A menu or switch: a faint flat patch, no edge, lighter when hovered or open.
    const auto patch = [&] (juce::Rectangle<float> area, float corner, bool hovered)
    {
        g.setColour (faded (textColour.withAlpha (hovered ? 0.08f : 0.035f)));
        g.fillRoundedRectangle (area, corner);
    };
    const auto popupBack = [&] (juce::Rectangle<float> area, bool pointer)
    {
        juce::Path shape;
        shape.addRoundedRectangle (area, 10.0f);
        g.setColour (faded (juce::Colours::black.withAlpha (0.45f)));
        g.fillRoundedRectangle (area.translated (0.0f, 6.0f).expanded (2.0f), 12.0f);
        if (pointer)
        {
            // A point down to the button that opened it.
            juce::Path tip;
            tip.addTriangle (l.arrowX - 6.0f, area.getBottom() - 1.0f, l.arrowX + 6.0f, area.getBottom() - 1.0f, l.arrowX, area.getBottom() + 6.0f);
            shape.addPath (tip);
        }
        surface (g, shape, area, amount);
    };
    const auto menuItem = [&] (juce::Rectangle<float> area, bool hovered)
    {
        if (hovered)
        {
            g.setColour (faded (textColour.withAlpha (0.085f)));
            g.fillRoundedRectangle (area, 6.0f);
        }
    };
    // A knob's name, or its value while hovered or turned: then on a dark chip.
    const auto label = [&] (juce::Rectangle<float> area, const juce::String& name, const juce::String& value, bool showing, bool enabled, float size = 9.5f)
    {
        if (showing && enabled)
        {
            const auto font = uiFont (10.5f);
            const float w = juce::GlyphArrangement::getStringWidth (font, value) + 12.0f;
            g.setColour (faded (juce::Colours::black.withAlpha (0.35f)));
            g.fillRoundedRectangle (juce::Rectangle<float> (w, area.getHeight()).withCentre (area.getCentre()), 4.0f);
            text (value, area.expanded (12.0f, 0.0f), 10.5f, pale);
        }
        else
        {
            g.setFont (uiFont (size, true).withExtraKerningFactor (0.06f));
            g.setColour (faded (textColour.withMultipliedAlpha (enabled ? 0.92f : 0.3f)));
            g.drawText (name, area.expanded (12.0f, 0.0f), juce::Justification::centred, false);
        }
    };

    // A value: plain, or on a dark chip while hovered or turned.
    const auto value = [&] (juce::Rectangle<float> area, const juce::String& s, bool lit, float size = 10.5f)
    {
        if (lit)
        {
            const float w = juce::GlyphArrangement::getStringWidth (uiFont (size), s) + 12.0f;
            g.setColour (faded (juce::Colours::black.withAlpha (0.35f)));
            g.fillRoundedRectangle (juce::Rectangle<float> (w, area.getHeight()).withCentre (area.getCentre()), 4.0f);
        }
        text (s, area.expanded (12.0f, 0.0f), size, lit ? pale : bright);
    };

    // The panel, solid.
    // The body, and Gain's housing over its top edge: one surface, the
    // housing's edge meeting the body's.
    {
        auto shape = panelBody (l.panel);
        shape.addEllipse (housingArea (l.panel));
        surface (g, shape, l.panel, amount);
        // The body's edge crosses inside the housing: cover it there.
        const auto disc = housingArea (l.panel);
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (37, 37, 35).withMultipliedAlpha (amount), l.panel.getY(),
                                                           juce::Colour (27, 27, 26).withMultipliedAlpha (amount), l.panel.getBottom()));
        g.fillEllipse (disc.reduced (0.5f));
    }

    if (band == nullptr)
    {
        text ("Double-click the sphere to add a band", l.panel.withTrimmedTop (bodyTop), 12.0f, idle);
        return;
    }

    const bool dynamicsUsable = ! band->isCut();
    const bool dynamic = dynamicsUsable && band->range != 0.0f;
    const auto isLit = [&] (int knob, bool inPopup = false)
    {
        return activeKnob == knob || (hover.kind == Kind::knob && hover.index == knob && hover.inPopup == inPopup);
    };
    const auto signColour = [] (float db) { return db < 0.0f ? cutColour : boostColour; };

    // Top left: on / off, solo, the gear.
    {
        const bool on = ! band->muted, hovered = hover.kind == Kind::mute;
        toolBack (l.power, hovered);
        stroke (powerIcon (l.power.getCentre()), on ? bright : hovered ? idle : faint, 1.5f);
    }
    {
        const bool hovered = hover.kind == Kind::solo;
        if (soloed)
        {
            g.setColour (faded (accentColour));
            g.fillRoundedRectangle (l.solo, 6.0f);
        }
        else
        {
            toolBack (l.solo, hovered);
        }
        text ("S", l.solo.translated (0.0f, 0.5f), 11.0f, soloed ? graphite : hovered ? bright : faint, juce::Justification::centred, true);
    }
    {
        const bool open = l.popup == PanelPopup::dynamics, hovered = hover.kind == Kind::dynamicsMenu;
        toolBack (l.gear, hovered && dynamicsUsable);
        stroke (gearIcon (l.gear.getCentre()), ! dynamicsUsable ? faint.withMultipliedAlpha (0.4f) : open ? accentColour : hovered ? bright : faint, 1.3f);
    }

    // Top right: < number >, remove, close.
    {
        std::array<const Band*, maxBands> order;
        const int count = bandsByFrequency (bands, order);
        int place = 0;
        for (int i = 0; i < count; ++i)
            if (order[(size_t) i]->id == band->id)
                place = i;

        stroke (chevron (l.previous.getCentre(), -1.0f, 3.5f), hover.kind == Kind::previous ? bright : faint, 1.5f);
        stroke (chevron (l.next.getCentre(), 1.0f, 3.5f), hover.kind == Kind::next ? bright : faint, 1.5f);
        const bool listOpen = l.popup == PanelPopup::bands, hovered = hover.kind == Kind::bandList;
        if (listOpen || hovered)
        {
            g.setColour (faded (textColour.withAlpha (0.085f)));
            g.fillRoundedRectangle (l.number, 4.0f);
        }
        text (juce::String (place + 1), l.number, 11.0f, listOpen ? accentColour : hovered ? pale : idle);
    }
    toolBack (l.remove, hover.kind == Kind::remove);
    stroke (binIcon (l.remove.getCentre()), hover.kind == Kind::remove ? bright : faint, 1.3f);
    toolBack (l.close, hover.kind == Kind::close);
    stroke (crossIcon (l.close.getCentre(), 3.0f), hover.kind == Kind::close ? bright : faint, 1.5f);

    // The pills: a glyph, the choice, and a caret for the menu it opens.
    const auto pill = [&] (juce::Rectangle<float> area, bool open, bool hovered, const juce::String& name)
    {
        patch (area, 7.0f, open || hovered);
        text (name, area.withTrimmedLeft (31.0f).withTrimmedRight (18.0f), 11.0f, bright, juce::Justification::centredLeft);
        juce::Path caret;
        const auto c = juce::Point<float> (area.getRight() - 11.0f, area.getCentreY());
        caret.startNewSubPath (c.x - 2.5f, c.y - 1.2f);
        caret.lineTo (c.x, c.y + 1.3f);
        caret.lineTo (c.x + 2.5f, c.y - 1.2f);
        stroke (caret, faint, 1.3f);
    };
    pill (l.typePill, l.popup == PanelPopup::types, hover.kind == Kind::typeMenu, typeName ((int) band->type));
    stroke (typeGlyph ((int) band->type, { l.typePill.getX() + 17.0f, l.typePill.getCentreY() }), idle, 1.4f);
    pill (l.stereoPill, l.popup == PanelPopup::stereo, hover.kind == Kind::stereoMenu, band->stereo ? juce::String ("Stereo") : positionText (band->lon));
    {
        const auto c = juce::Point<float> (l.stereoPill.getX() + 17.0f, l.stereoPill.getCentreY());
        juce::Path rings;
        rings.addEllipse (c.x - 6.5f, c.y - 3.5f, 7.0f, 7.0f);
        rings.addEllipse (c.x - 0.5f, c.y - 3.5f, 7.0f, 7.0f);
        stroke (rings, idle, 1.3f);
    }

    // Freq and Q, their values always shown, on a chip while hovered or turned.
    for (const auto [c, which] : { std::pair { l.freqCentre, freqKnob }, std::pair { l.qCentre, qKnob } })
    {
        const bool enabled = knobEnabled (*band, which), lit = enabled && isLit (which);
        drawKnob (g, c, sideKnobRadius, knobPosition (*band, which), knobIsBipolar (which), lit, enabled,
                  amount * (enabled ? 1.0f : 0.32f), true, textColour);
        if (editing (which))
            entry (l.valueArea (which));
        else
            value (l.valueArea (which), knobText (*band, which), lit);
    }

    // Gain in its well, and round it the two rings. The outer one: the static
    // gain out from 0 dB (thin), the range, as far from the gain as the band
    // can move, with a handle at its end, and over it the move it's making
    // now. The inner one: the level the band hears, and a notch at the
    // threshold. The ring under the pointer lights up.
    {
        const auto c = l.gainCentre;
        const auto angleFor = [] (float db) { return knobStartAngle + std::clamp ((db + maxGainDb) / (2.0f * maxGainDb), 0.0f, 1.0f) * knobSweep; };
        const bool rangeLit = dynamicsUsable && isLit (rangeKnob), thresholdLit = dynamicsUsable && isLit (thresholdKnob);

        g.setColour (faded (juce::Colours::black.withAlpha (0.34f)));
        g.fillEllipse (juce::Rectangle<float> (2.0f * wellRadius, 2.0f * wellRadius).withCentre (c));
        g.setColour (faded (textColour.withAlpha (0.05f)));
        g.drawEllipse (juce::Rectangle<float> (2.0f * wellRadius, 2.0f * wellRadius).withCentre (c), 1.0f);

        const auto groove = [&] (float radius, float width, bool lit)
        {
            stroke (arc (c.x, c.y, radius, knobStartAngle, knobStartAngle + knobSweep),
                    textColour.withAlpha (! dynamicsUsable ? 0.02f : lit ? 0.15f : 0.055f), width);
        };
        groove (rangeRadius, rangeWidth, rangeLit);

        if (knobEnabled (*band, gainKnob) && std::abs (band->gainDb) >= 0.05f)
        {
            const float zero = angleFor (0.0f), gainAt = angleFor (band->gainDb);
            stroke (arc (c.x, c.y, rangeRadius, std::min (zero, gainAt), std::max (zero, gainAt)), signColour (band->gainDb).withAlpha (0.7f), 2.0f);
        }

        if (dynamicsUsable)
        {
            const auto colour = signColour (band->range);
            const float from = angleFor (band->gainDb), reach = angleFor (band->gainDb + band->range);
            if (dynamic)
            {
                stroke (arc (c.x, c.y, rangeRadius, std::min (from, reach), std::max (from, reach)), colour.withAlpha (rangeLit ? 0.65f : 0.5f), rangeWidth - 2.0f);

                const float now = angleFor (band->gainDb + liveMovement (*band));
                if (std::abs (now - from) > 0.01f)
                {
                    const auto moving = arc (c.x, c.y, rangeRadius, std::min (from, now), std::max (from, now));
                    stroke (moving, colour.withAlpha (0.25f), rangeWidth + 4.0f); // it glows as it moves
                    stroke (moving, colour, rangeWidth - 2.0f);
                }
            }

            // The range's handle, where the band can go: there while it's
            // dynamic, or the ring is under the pointer.
            if (dynamic || rangeLit)
            {
                const auto handle = juce::Point<float> (c.x + std::sin (reach) * rangeRadius, c.y - std::cos (reach) * rangeRadius);
                const auto dot = juce::Rectangle<float> (7.2f, 7.2f).withCentre (handle);
                g.setColour (faded (dynamic ? colour : warmGrey));
                g.fillEllipse (dot);
                g.setColour (faded (graphite));
                g.drawEllipse (dot, 1.2f);
            }
        }

        groove (thresholdRadius, thresholdWidth, thresholdLit);
        if (dynamicsUsable)
        {
            const auto levelAngle = [] (float db) { return knobStartAngle + std::clamp ((db - minThresholdDb) / (maxThresholdDb - minThresholdDb), 0.0f, 1.0f) * knobSweep; };
            const float at = levelAngle (band->threshold);
            if (dynamic && levelDb > minThresholdDb)
            {
                const float level = levelAngle (levelDb);
                stroke (arc (c.x, c.y, thresholdRadius, knobStartAngle, std::min (level, at)), textColour.withAlpha (0.3f), thresholdWidth - 2.0f);
                if (level > at)
                    stroke (arc (c.x, c.y, thresholdRadius, at, level), signColour (band->range).withAlpha (0.85f), thresholdWidth - 2.0f);
            }

            const float inner = thresholdRadius - 4.0f, outer = thresholdRadius + 4.0f;
            juce::Path notch;
            notch.startNewSubPath (c.x + std::sin (at) * inner, c.y - std::cos (at) * inner);
            notch.lineTo (c.x + std::sin (at) * outer, c.y - std::cos (at) * outer);
            stroke (notch, pale.withAlpha (dynamic || thresholdLit ? 1.0f : 0.35f), 2.6f);
        }

        const bool enabled = knobEnabled (*band, gainKnob);
        drawKnob (g, c, gainKnobRadius, knobPosition (*band, gainKnob), true, enabled && isLit (gainKnob), enabled,
                  amount * (enabled ? 1.0f : 0.32f), false, bright);

        // Below: GAIN, or the value of whichever part is under the pointer or
        // being turned, named when it's a ring.
        int shown = gainKnob;
        for (int ring : { rangeKnob, thresholdKnob })
            if (dynamicsUsable && isLit (ring))
                shown = ring;
        const bool lit = shown != gainKnob || (enabled && isLit (gainKnob));
        if (editing (gainKnob))
            entry (l.valueArea (gainKnob));
        else if (! enabled && shown == gainKnob)
            label (l.valueArea (gainKnob), "GAIN", {}, false, false); // a cut has no gain
        else
            value (l.valueArea (gainKnob), (shown == rangeKnob ? "Range " : shown == thresholdKnob ? "Thresh " : "") + knobText (*band, shown), lit);
    }

    // An open menu, over everything else.
    switch (l.popup)
    {
        case PanelPopup::types:
            popupBack (l.popupArea, false);
            for (int i = 0; i < numFilterTypes; ++i)
            {
                const auto area = l.typeItems[(size_t) i];
                const bool on = (int) band->type == i, hovered = hover.kind == Kind::type && hover.index == i;
                menuItem (area, hovered);
                stroke (typeGlyph (i, { area.getX() + 15.0f, area.getCentreY() }), on ? accentColour : idle, 1.4f);
                text (typeName (i), area.withTrimmedLeft (30.0f), 11.0f, on ? accentColour : bright, juce::Justification::centredLeft);
            }
            break;

        case PanelPopup::stereo:
        {
            popupBack (l.popupArea, false);
            for (int i = 0; i < numStereoChoices; ++i)
            {
                const auto area = l.stereoItems[(size_t) i];
                const bool on = stereoChoiceIsOn (*band, i);
                menuItem (area, hover.kind == Kind::stereoItem && hover.index == i);
                text (stereoChoiceName (i), area.withTrimmedLeft (9.0f), 11.0f, on ? accentColour : bright, juce::Justification::centredLeft);
            }
            for (const float y : { l.stereoItems[0].getBottom() + separatorHeight / 2.0f, l.stereoItems[numStereoChoices - 1].getBottom() + separatorHeight / 2.0f })
            {
                g.setColour (faded (textColour.withAlpha (0.07f)));
                g.fillRect (l.popupArea.getX() + 9.0f, y, l.popupArea.getWidth() - 18.0f, 1.0f);
            }

            // Any position: drag along it, double-click for Mid, or type one.
            const auto s = l.positionSlider;
            const bool lit = hover.kind == Kind::positionSlider || isLit (posKnob, true);
            g.setColour (faded (textColour.withAlpha (lit ? 0.16f : 0.1f)));
            g.fillRoundedRectangle (s.getX(), s.getCentreY() - 1.0f, s.getWidth(), 2.0f, 1.0f);
            const auto thumb = juce::Point<float> (s.getX() + s.getWidth() * knobPosition (*band, posKnob), s.getCentreY());
            const auto colour = band->stereo ? warmGrey : accentColour;
            g.setColour (faded (colour.withAlpha (0.18f)));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (thumb));
            g.setColour (faded (colour));
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (thumb));
            if (editing (posKnob))
                entry (l.positionValue);
            else if (isLit (posKnob, true))
                label (l.positionValue, {}, knobText (*band, posKnob), true, true);
            else
                text (band->stereo ? juce::String ("Both") : knobText (*band, posKnob), l.positionValue, 10.5f, band->stereo ? faint : bright);
            break;
        }

        case PanelPopup::dynamics:
        {
            popupBack (l.popupArea, true);
            for (int i = 0; i < 3; ++i)
            {
                const int which = ratioKnob + i;
                const auto area = l.dynamicsKnobs[(size_t) i];
                const bool lit = dynamicsUsable && isLit (which, true);
                drawKnob (g, { area.getCentreX(), area.getY() + popupKnobRadius + 1.0f }, popupKnobRadius, knobPosition (*band, which), false,
                          lit, dynamicsUsable, amount * (dynamicsUsable ? 1.0f : 0.32f), true, textColour);
                if (editing (which))
                    entry (l.valueArea (which).reduced (2.0f, 0.0f));
                else
                    value (l.valueArea (which), knobText (*band, which), lit, 10.0f);
            }

            g.setColour (faded (textColour.withAlpha (0.07f)));
            g.fillRect (l.popupArea.getX() + 8.0f, l.sidechain.getY() - 7.0f, l.popupArea.getWidth() - 16.0f, 1.0f);

            // SC: dynamics follow the sidechain. It turns on only while a
            // sidechain is coming in, so without one it is greyed out. A
            // band already set to it stays lit, dimmer, so the setting is
            // never hidden, and can still be turned off.
            const bool on = dynamicsUsable && band->externalSidechain;
            const bool clickable = dynamicsUsable && (on || sidechainConnected);
            const bool hovered = clickable && hover.kind == Kind::sidechain;
            if (on)
            {
                g.setColour (faded (accentColour.withMultipliedAlpha (sidechainConnected ? 1.0f : 0.6f)));
                g.fillRoundedRectangle (l.sidechain, 6.0f);
            }
            else
            {
                patch (l.sidechain, 6.0f, hovered);
            }
            text ("SC", l.sidechain, 10.0f, on ? graphite : ! clickable ? idle.withMultipliedAlpha (0.45f) : hovered ? bright : idle,
                  juce::Justification::centred, true);
            text (sidechainConnected ? (on ? "Sidechain" : "Own signal") : "No SC in", l.sidechainNote, 9.5f, faint, juce::Justification::centredLeft);

            // GR: the move the band is making now, out from the middle, as a
            // share of its range.
            const auto meter = l.grMeter;
            g.setColour (faded (juce::Colour (12, 12, 11)));
            g.fillRoundedRectangle (meter, 2.5f);
            if (dynamic)
            {
                const float move = liveMovement (*band);
                const float share = std::clamp (std::abs (move) / std::abs (band->range), 0.0f, 1.0f);
                if (share > 0.0f)
                {
                    const float w = meter.getWidth() / 2.0f * share;
                    g.setColour (faded (signColour (move)));
                    g.fillRoundedRectangle (move < 0.0f ? meter.withX (meter.getCentreX() - w).withWidth (w) : meter.withX (meter.getCentreX()).withWidth (w), 2.5f);
                }
            }
            break;
        }

        case PanelPopup::bands:
        {
            popupBack (l.popupArea, true);
            std::array<const Band*, maxBands> order;
            const int count = std::min (bandsByFrequency (bands, order), l.bandCount);
            const float rowHeight = count > 0 ? l.bandItems[0].getHeight() : menuItemHeight; // smaller when there are many
            const float fontSize = std::min (11.0f, rowHeight - 3.0f), ringSize = std::min (16.0f, rowHeight - 2.0f);
            for (int i = 0; i < count; ++i)
            {
                const auto& b = *order[(size_t) i];
                const auto area = l.bandItems[(size_t) i];
                const bool hovered = hover.kind == Kind::bandItem && hover.index == i;
                if (b.id == band->id)
                {
                    g.setColour (faded (accentColour.withAlpha (0.12f)));
                    g.fillRoundedRectangle (area, 6.0f);
                }
                menuItem (area, hovered);

                const float rowAlpha = b.muted ? 0.45f : 1.0f;
                const auto ring = juce::Rectangle<float> (ringSize, ringSize).withCentre ({ area.getX() + 13.0f, area.getCentreY() });
                g.setColour (faded (graphite));
                g.fillEllipse (ring);
                g.setColour (faded ((b.muted ? warmGrey : signColour (b.isCut() ? -1.0f : b.gainDb)).withMultipliedAlpha (rowAlpha)));
                g.drawEllipse (ring.reduced (0.75f), 1.5f);
                text (juce::String (i + 1), ring, std::min (8.5f, ringSize - 6.0f), bright.withMultipliedAlpha (rowAlpha), juce::Justification::centred, true);
                stroke (typeGlyph ((int) b.type, { area.getX() + 37.0f, area.getCentreY() }), idle.withMultipliedAlpha (rowAlpha), 1.3f);
                text (formatFreq (latToFreq (b.lat)), area.withTrimmedLeft (52.0f), fontSize, bright.withMultipliedAlpha (rowAlpha), juce::Justification::centredLeft);
                text (b.isCut() ? juce::String (b.slope) + " dB/oct" : formatGain (b.gainDb), area.withTrimmedRight (8.0f), fontSize - 1.0f,
                      idle.withMultipliedAlpha (rowAlpha), juce::Justification::centredRight);
            }
            break;
        }

        case PanelPopup::none:
            break;
    }
}

} // namespace threedq
