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
constexpr float panelHeight = 130.0f;
constexpr float knobRowHeight = 70.0f;
constexpr float panelBottomInset = 10.0f;
constexpr float toggleRadius = 10.0f;
constexpr float toggleClosedInset = 24.0f; // from the window's bottom edge to the button's centre
constexpr float panelViewGap = 6.0f;       // between the sphere's room and the ^ button on the panel's edge
constexpr float panelFitMargin = 10.0f;    // kept clear above and below the sphere when it is fitted
constexpr float knobRadius = 14.0f;
constexpr float knobStartAngle = -2.35619f; // 12 o'clock is 0, clockwise
constexpr float knobSweep = 4.71239f;

constexpr std::array<std::array<PanelKnob, numPanelKnobs>, numPanelPages> pageKnobs {{
    { freqKnob, gainKnob, qKnob, posKnob, rangeKnob, thresholdKnob },
    { rangeKnob, thresholdKnob, ratioKnob, attackKnob, releaseKnob, liveGainMeter },
}};

const char* pageName (int page)    { return page == (int) PanelPage::eq ? "EQ" : "DYN"; }

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
    static constexpr std::array<const char*, numFilterTypes> names { "Peak", "Low cut", "High cut", "Low shelf", "High shelf" };
    return names[(size_t) type];
}

void strokeRounded (juce::Graphics& g, const juce::Path& p, float thickness)
{
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}

PanelKnob panelKnobAt (PanelPage page, int slot)
{
    return pageKnobs[(size_t) page][(size_t) std::clamp (slot, 0, numPanelKnobs - 1)];
}

BandPanelLayout BandPanelLayout::make (float windowWidth, float windowHeight, float openAmount, PanelPage page)
{
    BandPanelLayout l;
    l.page = page;
    l.scale = uiScale (windowWidth, windowHeight);
    const float width = windowWidth / l.scale, height = windowHeight / l.scale;
    const float panelWidth = std::min (width - 8.0f, std::clamp (width - 96.0f, 364.0f, 430.0f));
    const float openTop = height - panelBottomInset - panelHeight;
    const float closedTop = height + 6.0f;
    const float top = closedTop + (openTop - closedTop) * openAmount;
    const float left = (width - panelWidth) / 2.0f;
    const float x = left + 12.0f, right = left + panelWidth - 12.0f;
    const float headerY = top + 12.0f, headerH = 22.0f;

    l.panel = { left, top, panelWidth, panelHeight };
    l.previous = { x, headerY, 18.0f, headerH };
    l.title = { x + 18.0f, headerY, 54.0f, headerH };
    l.next = { x + 72.0f, headerY, 18.0f, headerH };

    for (int i = 0; i < numFilterTypes; ++i)
        l.types[(size_t) i] = { x + 96.0f + 21.0f * (float) i, headerY, 21.0f, headerH };

    // At the narrowest panel these just fit between the types and Stereo.
    l.sidechain = { x + 203.0f, headerY, 22.0f, headerH };
    l.remove = { right - 22.0f, headerY, 22.0f, headerH };
    l.mute = { right - 46.0f, headerY, 22.0f, headerH };
    l.solo = { right - 70.0f, headerY, 22.0f, headerH };
    l.stereo = { right - 112.0f, headerY, 42.0f, headerH };

    const float knobWidth = (panelWidth - 24.0f) / (float) numPanelKnobs;
    for (int i = 0; i < numPanelKnobs; ++i)
        l.knobs[(size_t) i] = { x + knobWidth * (float) i, top + 38.0f, knobWidth, knobRowHeight };

    // The page tabs, centred along the bottom.
    for (int i = 0; i < numPanelPages; ++i)
        l.pages[(size_t) i] = { left + panelWidth / 2.0f + (i == 0 ? -40.0f : 2.0f), top + panelHeight - 22.0f, 38.0f, 16.0f };

    l.toggleCentre = { width / 2.0f, (height - toggleClosedInset) + (openTop - (height - toggleClosedInset)) * openAmount };
    return l;
}

PanelTarget BandPanelLayout::hit (juce::Point<float> position, bool open, bool hasBand) const
{
    using Kind = PanelTarget::Kind;
    const auto p = position / scale;

    if (p.getDistanceFrom (toggleCentre) <= toggleRadius + 3.0f)
        return { Kind::toggle };

    if (! open || ! panel.contains (p))
        return { Kind::none };

    if (hasBand)
    {
        if (previous.contains (p))    return { Kind::previous };
        if (next.contains (p))        return { Kind::next };
        if (sidechain.contains (p))   return { Kind::sidechain };
        if (stereo.contains (p))      return { Kind::stereo };
        if (solo.contains (p))        return { Kind::solo };
        if (mute.contains (p))        return { Kind::mute };
        if (remove.contains (p))      return { Kind::remove };

        for (int i = 0; i < numFilterTypes; ++i)
            if (types[(size_t) i].contains (p))
                return { Kind::type, i };

        for (int i = 0; i < numPanelKnobs; ++i)
            if (knobs[(size_t) i].contains (p))
                return panelKnobAt (page, i) == liveGainMeter ? PanelTarget { Kind::background } // a readout
                                                               : PanelTarget { Kind::knob, panelKnobAt (page, i) };

        for (int i = 0; i < numPanelPages; ++i)
            if (pages[(size_t) i].contains (p))
                return { Kind::page, i };
    }

    return { Kind::background };
}

PanelViewFit fitViewAbovePanel (float width, float height, float zoom, float panY, SurfaceExtent extent)
{
    // The space between the preset bar and the panel.
    const float scale = uiScale (width, height);
    const float roomTop = scale * presetBarBottom;
    const float room = height - scale * (panelHeight + panelBottomInset + toggleRadius + panelViewGap);
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

void drawBandPanel (juce::Graphics& g, const BandPanelLayout& l, float dpr, float amount,
                    const Band* band, bool soloed, PanelTarget hover, int activeKnob, bool sidechainConnected)
{
    using Kind = PanelTarget::Kind;

    const juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (juce::AffineTransform::scale (dpr * l.scale));

    const auto faded = [amount] (juce::Colour c) { return c.withMultipliedAlpha (amount); };
    const auto text = [&] (const juce::String& s, juce::Rectangle<float> area, float size, juce::Colour c, bool bold = false)
    {
        g.setFont (uiFont (size, bold));
        g.setColour (c);
        g.drawText (s, area, juce::Justification::centred, false);
    };
    const auto buttonBack = [&] (juce::Rectangle<float> area, bool active, bool hovered)
    {
        if (! active && ! hovered)
            return;
        g.setColour (faded (textColour.withAlpha (active ? 0.18f : 0.08f)));
        g.fillRoundedRectangle (area.reduced (1.0f), 4.0f);
    };

    if (amount > 0.01f)
    {
        g.setColour (faded (graphite.withAlpha (0.94f)));
        g.fillRoundedRectangle (l.panel, 8.0f);
        g.setColour (faded (warmGrey.withAlpha (0.45f)));
        g.drawRoundedRectangle (l.panel, 8.0f, 1.0f);

        if (band == nullptr)
        {
            text ("Double-click the sphere to add a band", l.panel, 12.0f, faded (textColour.withAlpha (0.6f)));
        }
        else
        {
            const auto idle = textColour.withAlpha (0.6f), bright = textColour;

            const auto chevron = [&] (juce::Rectangle<float> area, float direction, bool hovered)
            {
                const auto c = area.getCentre();
                juce::Path p;
                p.startNewSubPath (c.x - direction * 2.5f, c.y - 4.5f);
                p.lineTo (c.x + direction * 2.5f, c.y);
                p.lineTo (c.x - direction * 2.5f, c.y + 4.5f);
                g.setColour (faded (hovered ? bright : idle));
                strokeRounded (g, p, 1.5f);
            };

            chevron (l.previous, -1.0f, hover.kind == Kind::previous);
            chevron (l.next, 1.0f, hover.kind == Kind::next);
            // Hovering a button names it, in the title.
            juce::String title = "Band " + juce::String (band->id + 1);
            if (hover.kind == Kind::type)
                title = typeName (hover.index);
            else if (hover.kind == Kind::sidechain)
                title = band->isCut() ? "No dyn" : sidechainConnected ? "SC in" : "No SC in";
            text (title, l.title, 11.0f, faded (bright));

            for (int i = 0; i < numFilterTypes; ++i)
            {
                const auto area = l.types[(size_t) i];
                const bool active = (int) band->type == i, hovered = hover.kind == Kind::type && hover.index == i;
                buttonBack (area, active, hovered);
                g.setColour (faded (active || hovered ? bright : idle));
                strokeRounded (g, typeGlyph (i, area.getCentre()), 1.4f);
            }

            // SC: dynamics follow the sidechain. Cuts have no dynamics, so it's
            // greyed out on them; it stays lit without a sidechain, dimmer, so
            // the setting is never hidden.
            {
                const bool usable = ! band->isCut();
                const bool on = usable && band->externalSidechain;
                const bool hovered = usable && hover.kind == Kind::sidechain;
                buttonBack (l.sidechain, on, hovered);
                const auto colour = ! usable ? idle.withMultipliedAlpha (0.4f)
                                  : on ? (sidechainConnected ? bright : bright.withAlpha (0.7f))
                                  : hovered ? bright : idle;
                text ("SC", l.sidechain, 11.0f, faded (colour));
            }

            buttonBack (l.stereo, band->stereo, hover.kind == Kind::stereo);
            text (band->stereo ? "Stereo" : "Mono", l.stereo, 11.0f, faded (band->stereo || hover.kind == Kind::stereo ? bright : idle));

            buttonBack (l.solo, soloed, hover.kind == Kind::solo);
            text ("S", l.solo, 11.0f, faded (soloed || hover.kind == Kind::solo ? bright : idle));

            // Mute: a circle with a slash, like a muted node.
            {
                buttonBack (l.mute, band->muted, hover.kind == Kind::mute);
                const auto c = l.mute.getCentre();
                g.setColour (faded (band->muted || hover.kind == Kind::mute ? bright : idle));
                g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.3f);
                juce::Path slash;
                slash.startNewSubPath (c.x - 3.5f, c.y + 3.5f);
                slash.lineTo (c.x + 3.5f, c.y - 3.5f);
                strokeRounded (g, slash, 1.3f);
            }

            // Remove: a cross.
            {
                buttonBack (l.remove, false, hover.kind == Kind::remove);
                const auto c = l.remove.getCentre();
                juce::Path cross;
                cross.startNewSubPath (c.x - 4.0f, c.y - 4.0f);
                cross.lineTo (c.x + 4.0f, c.y + 4.0f);
                cross.startNewSubPath (c.x + 4.0f, c.y - 4.0f);
                cross.lineTo (c.x - 4.0f, c.y + 4.0f);
                g.setColour (faded (hover.kind == Kind::remove ? bright : idle));
                strokeRounded (g, cross, 1.5f);
            }

            for (int slot = 0; slot < numPanelKnobs; ++slot)
            {
                const int i = panelKnobAt (l.page, slot);
                const auto area = l.knobs[(size_t) slot];
                const bool meter = i == liveGainMeter;
                const bool enabled = meter ? ! band->isCut() && band->range != 0.0f : knobEnabled (*band, i);
                const bool lit = ! meter && enabled && (activeKnob == i || (hover.kind == Kind::knob && hover.index == i));
                const float dim = enabled ? 1.0f : 0.32f;
                const float cx = area.getCentreX(), cy = area.getY() + 30.0f;
                const float position = std::clamp (knobPosition (*band, i), 0.0f, 1.0f);
                const float angle = knobStartAngle + position * knobSweep;
                const float anchor = knobIsBipolar (i) ? knobStartAngle + 0.5f * knobSweep : knobStartAngle;

                text (knobName (*band, i), { area.getX(), area.getY(), area.getWidth(), 12.0f }, 9.5f, faded (idle.withMultipliedAlpha (dim)));

                g.setColour (faded (juce::Colour (34, 34, 32).withMultipliedAlpha (dim)));
                g.fillEllipse (cx - knobRadius + 3.5f, cy - knobRadius + 3.5f, (knobRadius - 3.5f) * 2.0f, (knobRadius - 3.5f) * 2.0f);

                g.setColour (faded (textColour.withAlpha ((lit ? 0.32f : 0.18f) * dim)));
                strokeRounded (g, arc (cx, cy, knobRadius, knobStartAngle, knobStartAngle + knobSweep), 2.0f);

                if (enabled && std::abs (angle - anchor) > 0.01f)
                {
                    const float value = i == gainKnob ? band->gainDb : i == rangeKnob ? band->range : meter ? liveMovement (*band) : 0.0f;
                    g.setColour (faded (value > 0.0f ? boostColour : value < 0.0f ? cutColour : bright));
                    strokeRounded (g, arc (cx, cy, knobRadius, std::min (anchor, angle), std::max (anchor, angle)), 2.2f);
                }

                // A meter has no pointer, as it can't be turned.
                if (! meter)
                {
                    juce::Path pointer;
                    pointer.startNewSubPath (cx + std::sin (angle) * 4.5f, cy - std::cos (angle) * 4.5f);
                    pointer.lineTo (cx + std::sin (angle) * 9.5f, cy - std::cos (angle) * 9.5f);
                    g.setColour (faded (bright.withMultipliedAlpha (dim)));
                    strokeRounded (g, pointer, 1.6f);
                }

                text (knobText (*band, i), { area.getX(), area.getY() + 48.0f, area.getWidth(), 14.0f }, 10.5f,
                      faded ((lit ? bright : bright.withAlpha (0.8f)).withMultipliedAlpha (dim)));
            }

            // Page tabs. DYN gets a dot while the band is dynamic, so it's clear
            // from the EQ page that there's more to see.
            for (int i = 0; i < numPanelPages; ++i)
            {
                const auto area = l.pages[(size_t) i];
                const bool active = (int) l.page == i, hovered = hover.kind == Kind::page && hover.index == i;
                buttonBack (area, active, hovered);
                text (pageName (i), area, 9.5f, faded (active || hovered ? bright : idle));

                if (i == (int) PanelPage::dynamics && ! active && ! band->isCut() && band->range != 0.0f)
                {
                    g.setColour (faded ((band->range > 0 ? boostColour : cutColour).withAlpha (0.9f)));
                    g.fillEllipse (area.getRight() - 6.0f, area.getCentreY() - 1.5f, 3.0f, 3.0f);
                }
            }
        }
    }

    // The ^ button, riding on the panel's top edge once it's open.
    const auto c = l.toggleCentre;
    const bool hovered = hover.kind == Kind::toggle;
    g.setColour (graphite.withAlpha (0.85f));
    g.fillEllipse (c.x - toggleRadius, c.y - toggleRadius, toggleRadius * 2.0f, toggleRadius * 2.0f);
    g.setColour (warmGrey.withAlpha (hovered ? 0.85f : 0.45f));
    g.drawEllipse (c.x - toggleRadius, c.y - toggleRadius, toggleRadius * 2.0f, toggleRadius * 2.0f, 1.0f);

    const float tip = 2.5f - 5.0f * amount; // flips from ^ to v as the panel rises
    juce::Path chevron;
    chevron.startNewSubPath (c.x - 4.0f, c.y + tip);
    chevron.lineTo (c.x, c.y - tip);
    chevron.lineTo (c.x + 4.0f, c.y + tip);
    g.setColour (textColour.withAlpha (hovered ? 1.0f : 0.6f));
    strokeRounded (g, chevron, 1.5f);
}

} // namespace threedq
