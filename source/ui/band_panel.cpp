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
constexpr float panelHeight = 118.0f;
// The panel's outline follows its controls: lower along the sides, rising in
// a rounded crown over the Gain ring.
constexpr float sideTopInset = 30.0f, crownTopInset = 2.0f;
constexpr float sideColumnWidth = 72.0f;   // the band and type on the left; stereo and solo / mute / remove on the right
constexpr float sideKnobSpacing = 76.0f;   // from Gain's centre to Freq's and Q's
constexpr float panelBottomInset = bottomBarHeight + 14.0f; // floating clear of the bar
constexpr float panelViewGap = 10.0f;      // between the sphere's room and the panel's crown
constexpr float panelFitMargin = 10.0f;    // kept clear above and below the sphere when it is fitted
constexpr float gainKnobRadius = 22.0f;
constexpr float ringRadius = 36.0f, ringWidth = 5.0f;           // the range ring round Gain
constexpr float thresholdRadius = 28.5f, thresholdWidth = 3.0f; // the threshold ring, inside it
constexpr float sideKnobRadius = 17.0f;
constexpr float popupKnobRadius = 12.0f; // ratio, attack and release
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
    static constexpr std::array<const char*, numFilterTypes> names { "Peak", "Low cut", "High cut", "Low shelf", "High shelf" };
    return names[(size_t) type];
}

void strokeRounded (juce::Graphics& g, const juce::Path& p, float thickness)
{
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// The panel's outline, contoured to its controls (see sideTopInset): low
// shoulders over the side columns, and over Gain one soft wave, each side a
// single S-curve up to a short level top.
juce::Path panelOutline (juce::Rectangle<float> panel)
{
    constexpr float corner = 12.0f;
    const float l = panel.getX(), r = panel.getRight(), cx = panel.getCentreX();
    const float top = panel.getY() + sideTopInset, crown = panel.getY() + crownTopInset;
    const float bottom = panel.getBottom();

    juce::Path p;
    p.startNewSubPath (l + corner, top);
    p.lineTo (cx - 140.0f, top);
    p.cubicTo (cx - 96.0f, top, cx - 84.0f, crown, cx - 40.0f, crown);
    p.lineTo (cx + 40.0f, crown);
    p.cubicTo (cx + 84.0f, crown, cx + 96.0f, top, cx + 140.0f, top);
    p.lineTo (r - corner, top);
    p.quadraticTo (r, top, r, top + corner);
    p.lineTo (r, bottom - corner);
    p.quadraticTo (r, bottom, r - corner, bottom);
    p.lineTo (l + corner, bottom);
    p.quadraticTo (l, bottom, l, bottom - corner);
    p.lineTo (l, top + corner);
    p.quadraticTo (l, top, l + corner, top);
    p.closeSubPath();
    return p;
}

// A surface: a dark graphite body, a shade lighter at the top, and an edge
// that's just there: a faint line of light along the top, fading away down
// the sides.
void glass (juce::Graphics& g, const juce::Path& shape, juce::Rectangle<float> area, float alpha, float bodyAlpha)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (33, 33, 32).withAlpha (bodyAlpha * alpha), area.getY(),
                                                       juce::Colour (21, 21, 20).withAlpha (bodyAlpha * alpha), area.getBottom()));
    g.fillPath (shape);

    g.setGradientFill (juce::ColourGradient::vertical (textColour.withAlpha (0.13f * alpha), area.getY(),
                                                       textColour.withAlpha (0.03f * alpha), area.getY() + 40.0f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}
}

BandPanelLayout BandPanelLayout::make (float windowWidth, float windowHeight, float openAmount, PanelPopup popup)
{
    BandPanelLayout l;
    l.popup = popup;
    l.scale = uiScale (windowWidth, windowHeight);
    const float width = windowWidth / l.scale, height = windowHeight / l.scale;
    const float panelWidth = std::min (width - 8.0f, std::clamp (width - 96.0f, 364.0f, 430.0f));
    const float openTop = height - panelBottomInset - panelHeight;
    const float closedTop = height + 6.0f;
    const float top = closedTop + (openTop - closedTop) * openAmount;
    const float left = (width - panelWidth) / 2.0f;
    const float x = left + 12.0f, right = left + panelWidth - 12.0f;

    l.panel = { left, top, panelWidth, panelHeight };

    // Left column: the band picker, then the filter type.
    // The side columns sit low, level with the knobs, under the panel's
    // shoulders; the crown rises over Gain between them.
    constexpr float row1 = 40.0f, row2 = 66.0f, row3 = 93.0f;
    l.previous = { x, top + row1, 16.0f, 22.0f };
    l.title = { x + 16.0f, top + row1, sideColumnWidth - 32.0f, 22.0f };
    l.next = { x + sideColumnWidth - 16.0f, top + row1, 16.0f, 22.0f };
    l.typePill = { x, top + row2, sideColumnWidth, 24.0f };

    // Right column: solo, mute and remove, then stereo / mono and the position.
    const float rx = right - sideColumnWidth;
    l.solo = { rx + 6.0f, top + row1, 22.0f, 22.0f };
    l.mute = { rx + 28.0f, top + row1, 22.0f, 22.0f };
    l.remove = { rx + 50.0f, top + row1, 22.0f, 22.0f };
    l.stereo = { rx, top + row2, sideColumnWidth, 24.0f };
    l.positionPill = { rx, top + row3, sideColumnWidth, 20.0f };

    // Centre: Freq, Gain inside its two dynamics rings, and Q. The gear sits
    // off the rings' upper right.
    const juce::Point<float> gain { left + panelWidth / 2.0f, top + 54.0f };
    l.gainCentre = gain;
    // Freq and Q sit a little lower, so their captions share Gain's line.
    l.freqCentre = { gain.x - sideKnobSpacing, gain.y + 10.0f };
    l.qCentre = { gain.x + sideKnobSpacing, gain.y + 10.0f };
    l.gear = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ gain.x + 42.0f, gain.y - 34.0f });

    // The menus open upwards, over the sphere, so the panel stays as it is.
    if (popup == PanelPopup::types)
    {
        constexpr float itemHeight = 20.0f;
        l.popupArea = { x, l.typePill.getY() - 6.0f - (itemHeight * numFilterTypes + 8.0f), sideColumnWidth + 20.0f, itemHeight * numFilterTypes + 8.0f };
        for (int i = 0; i < numFilterTypes; ++i)
            l.typeItems[(size_t) i] = { l.popupArea.getX() + 4.0f, l.popupArea.getY() + 4.0f + itemHeight * (float) i, l.popupArea.getWidth() - 8.0f, itemHeight };
    }
    else if (popup == PanelPopup::dynamics)
    {
        constexpr float knobWidth = 54.0f, menuWidth = 4.0f * knobWidth + 56.0f, menuHeight = 72.0f;
        const float centreX = left + panelWidth / 2.0f; // centred over the panel
        l.popupArea = { centreX - menuWidth / 2.0f, top - 8.0f - menuHeight, menuWidth, menuHeight };
        for (int i = 0; i < 3; ++i)
            l.dynamicsKnobs[(size_t) i] = { l.popupArea.getX() + 6.0f + knobWidth * (float) i, l.popupArea.getY() + 6.0f, knobWidth, menuHeight - 12.0f };
        l.grMeter = { l.popupArea.getX() + 6.0f + knobWidth * 3.0f, l.popupArea.getY() + 6.0f, knobWidth, menuHeight - 12.0f };
        l.sidechain = { l.popupArea.getRight() - 46.0f, l.popupArea.getY() + 16.0f, 38.0f, 22.0f };
    }

    return l;
}

PanelTarget BandPanelLayout::hit (juce::Point<float> position, bool open, bool hasBand) const
{
    using Kind = PanelTarget::Kind;
    const auto p = position / scale;

    if (! open)
        return { Kind::none };

    if (hasBand && popup != PanelPopup::none && popupArea.contains (p))
    {
        if (popup == PanelPopup::types)
        {
            for (int i = 0; i < numFilterTypes; ++i)
                if (typeItems[(size_t) i].contains (p))
                    return { Kind::type, i, true };
        }
        else
        {
            for (int i = 0; i < 3; ++i)
                if (dynamicsKnobs[(size_t) i].contains (p))
                    return { Kind::knob, ratioKnob + i, true };
            if (sidechain.contains (p))
                return { Kind::sidechain, 0, true };
        }

        return { Kind::background, 0, true };
    }

    if (! panelOutline (panel).contains (p))
        return { Kind::none };

    if (hasBand)
    {
        if (previous.contains (p))    return { Kind::previous };
        if (next.contains (p))        return { Kind::next };
        if (typePill.contains (p))    return { Kind::typeMenu };
        if (stereo.contains (p))      return { Kind::stereo, p.x >= stereo.getCentreX() ? 1 : 0 }; // 0 = Mono, 1 = Stereo
        if (positionPill.contains (p)) return { Kind::knob, posKnob };
        if (solo.contains (p))        return { Kind::solo };
        if (mute.contains (p))        return { Kind::mute };
        if (remove.contains (p))      return { Kind::remove };
        if (gear.contains (p))        return { Kind::dynamicsMenu };

        const float fromGain = p.getDistanceFrom (gainCentre);
        if (fromGain <= gainKnobRadius + 1.0f)
            return { Kind::knob, gainKnob };
        if (fromGain <= (thresholdRadius + ringRadius) / 2.0f)
            return { Kind::knob, thresholdKnob };
        if (fromGain <= ringRadius + 6.0f)
            return { Kind::knob, rangeKnob };

        // A side knob takes its label and value too.
        for (const auto [centre, knob] : { std::pair { freqCentre, freqKnob }, std::pair { qCentre, qKnob } })
            if (juce::Rectangle<float> (60.0f, 70.0f).withCentre ({ centre.x, centre.y + 12.0f }).contains (p))
                return { Kind::knob, knob };
    }

    return { Kind::background };
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

namespace
{
// A knob: a dark disc with a thin arc just inside its edge for the value (from
// the middle when bipolar) and a short tick where it ends. Without track
// there's no arc: Gain's rings stand in for it. position runs 0..1.
void drawKnob (juce::Graphics& g, juce::Point<float> c, float radius, float position, bool bipolar, bool lit, bool showValue,
               float alpha, bool track)
{
    const auto bright = textColour;
    const float angle = knobStartAngle + std::clamp (position, 0.0f, 1.0f) * knobSweep;
    const float anchor = bipolar ? knobStartAngle + 0.5f * knobSweep : knobStartAngle;
    const bool small = radius <= 12.0f;
    const float body = radius - 0.5f;
    const float arcRadius = body - (small ? 2.5f : 3.5f), stroke = small ? 1.5f : 2.0f;
    const auto bodyArea = juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c);

    juce::Path cap;
    cap.addEllipse (bodyArea);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (40, 40, 39).brighter (lit ? 0.12f : 0.0f).withMultipliedAlpha (alpha), bodyArea.getY(),
                                                       juce::Colour (22, 22, 21).withMultipliedAlpha (alpha), bodyArea.getBottom()));
    g.fillPath (cap);
    g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
    g.strokePath (cap, juce::PathStrokeType (1.2f));
    g.setColour (textColour.withAlpha ((lit ? 0.16f : 0.08f) * alpha));
    strokeRounded (g, arc (c.x, c.y, body - 0.8f, -1.2f, 1.2f), 0.8f); // light catching the top edge

    if (track)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        strokeRounded (g, arc (c.x, c.y, arcRadius, knobStartAngle, knobStartAngle + knobSweep), stroke);

        if (showValue && std::abs (angle - anchor) > 0.01f)
        {
            g.setColour (bright.withAlpha ((lit ? 1.0f : 0.85f) * alpha));
            strokeRounded (g, arc (c.x, c.y, arcRadius, std::min (anchor, angle), std::max (anchor, angle)), stroke);
        }
    }

    juce::Path tick;
    const float inner = track ? arcRadius - (small ? 4.0f : 6.0f) : body * 0.45f;
    const float outer = track ? arcRadius - stroke : body - 4.0f;
    tick.startNewSubPath (c.x + std::sin (angle) * inner, c.y - std::cos (angle) * inner);
    tick.lineTo (c.x + std::sin (angle) * outer, c.y - std::cos (angle) * outer);
    g.setColour (bright.withMultipliedAlpha (alpha));
    strokeRounded (g, tick, small ? 1.4f : 1.8f);
}
} // namespace

void drawBandPanel (juce::Graphics& g, const BandPanelLayout& l, float dpr, float amount,
                    const Band* band, bool soloed, PanelTarget hover, int activeKnob, bool sidechainConnected,
                    float levelDb)
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
    // A control's name, in plain capitals.
    const auto caption = [&] (const juce::String& s, juce::Rectangle<float> area, juce::Colour c)
    {
        g.setFont (uiFont (9.5f).withExtraKerningFactor (0.05f));
        g.setColour (c.withMultipliedAlpha (0.95f));
        g.drawText (s, area, juce::Justification::centred, false);
    };
    const auto buttonBack = [&] (juce::Rectangle<float> area, bool active, bool hovered)
    {
        if (! active && ! hovered)
            return;
        g.setColour (faded (textColour.withAlpha (active ? 0.18f : 0.08f)));
        g.fillRoundedRectangle (area.reduced (1.0f), 4.0f);
    };
    // A pill: a flat, lighter patch to click or drag, no outline; lighter
    // still when hovered or switched on.
    const auto pill = [&] (juce::Rectangle<float> area, bool active, bool hovered)
    {
        g.setColour (faded (textColour.withAlpha (active ? 0.13f : hovered ? 0.09f : 0.055f)));
        g.fillRoundedRectangle (area, 6.0f);
    };
    const auto popupBack = [&] (juce::Rectangle<float> area)
    {
        juce::Path shape;
        shape.addRoundedRectangle (area, 10.0f);
        glass (g, shape, area, amount, 0.94f);
    };

    if (amount > 0.01f)
    {
        // The panel: nearly solid, the sphere only just showing through.
        glass (g, panelOutline (l.panel), l.panel, amount, 0.84f);

        if (band == nullptr)
        {
            text ("Double-click the sphere to add a band", l.panel, 12.0f, faded (textColour.withAlpha (0.6f)));
        }
        else
        {
            const auto idle = textColour.withAlpha (0.6f), bright = textColour;
            const bool dynamicsUsable = ! band->isCut();
            const bool dynamic = dynamicsUsable && band->range != 0.0f;
            const auto isLit = [&] (int knob, bool inPopup = false)
            {
                return activeKnob == knob || (hover.kind == Kind::knob && hover.index == knob && hover.inPopup == inPopup);
            };

            const auto knob = [&] (juce::Point<float> c, float radius, int which, bool lit, bool enabled, float dim = 1.0f, bool track = true)
            {
                drawKnob (g, c, radius, knobPosition (*band, which), knobIsBipolar (which), lit, enabled,
                          amount * dim * (enabled ? 1.0f : 0.32f), track);
            };

            // Left column: the band picker and the filter type.
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
            text ("Band " + juce::String (band->id + 1), l.title, 11.0f, faded (bright));

            {
                const bool open = l.popup == PanelPopup::types, hovered = hover.kind == Kind::typeMenu;
                // The type's shape and name; the whole pill opens the list.
                pill (l.typePill, open, hovered);
                g.setColour (faded (bright));
                strokeRounded (g, typeGlyph ((int) band->type, { l.typePill.getX() + 13.0f, l.typePill.getCentreY() }), 1.4f);
                g.setFont (uiFont (10.0f));
                g.drawText (typeName ((int) band->type), l.typePill.withTrimmedLeft (25.0f).withTrimmedRight (4.0f), juce::Justification::centredLeft, true);
            }

            // Right column: solo, mute, remove, stereo / mono, position.
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

            // Mono | Stereo: a two-way switch, the chosen side filled in.
            {
                pill (l.stereo, false, false);
                for (int side = 0; side < 2; ++side)
                {
                    const auto half = side == 0 ? l.stereo.withWidth (l.stereo.getWidth() / 2.0f) : l.stereo.withTrimmedLeft (l.stereo.getWidth() / 2.0f);
                    const bool chosen = band->stereo == (side == 1), hovered = hover.kind == Kind::stereo && hover.index == side;
                    if (chosen || hovered)
                    {
                        g.setColour (faded (textColour.withAlpha (chosen ? 0.14f : 0.06f)));
                        g.fillRoundedRectangle (half.reduced (2.0f), 4.0f);
                    }
                    text (side == 0 ? "Mono" : "Stereo", half, 9.5f, faded (chosen ? bright : hovered ? bright.withAlpha (0.8f) : idle));
                }
            }

            // The position: drag or scroll it. A line along its foot shows where
            // round the sphere it is, L at the left, Side at both ends.
            {
                // A stereo band has no position, so there's nothing to show.
                const bool enabled = knobEnabled (*band, posKnob), lit = enabled && isLit (posKnob);
                if (enabled)
                {
                    pill (l.positionPill, false, lit);
                    text (knobText (*band, posKnob), l.positionPill.withTrimmedBottom (4.0f), 10.0f, faded (lit ? bright : bright.withAlpha (0.85f)));
                    const float y = l.positionPill.getBottom() - 4.0f, x0 = l.positionPill.getX() + 10.0f, x1 = l.positionPill.getRight() - 10.0f;
                    g.setColour (faded (textColour.withAlpha (0.18f)));
                    g.drawLine (x0, y, x1, y, 1.0f);
                    const float at = x0 + (x1 - x0) * knobPosition (*band, posKnob);
                    g.setColour (faded (bright));
                    g.fillEllipse (at - 2.0f, y - 2.0f, 4.0f, 4.0f);
                }
            }

            // Freq and Q, with their names and values below.
            const auto sideKnob = [&] (juce::Point<float> c, int which)
            {
                const bool enabled = knobEnabled (*band, which), lit = enabled && isLit (which);
                knob (c, sideKnobRadius, which, lit, enabled);
                const float dim = enabled ? 1.0f : 0.32f;
                caption (knobName (*band, which), { c.x - 40.0f, c.y + 21.0f, 80.0f, 12.0f }, faded (idle.withMultipliedAlpha (dim)));
                text (knobText (*band, which), { c.x - 40.0f, c.y + 33.0f, 80.0f, 14.0f }, 10.5f,
                      faded ((lit ? bright : bright.withAlpha (0.8f)).withMultipliedAlpha (dim)));
            };
            sideKnob (l.freqCentre, freqKnob);
            sideKnob (l.qCentre, qKnob);

            // Gain, and round it the range ring: the static gain out from 0 dB
            // (thinner), the range, as far from the gain as the band can move,
            // and over it the move it's making now.
            {
                const auto c = l.gainCentre;
                const auto angleFor = [] (float db) { return knobStartAngle + std::clamp ((db + maxGainDb) / (2.0f * maxGainDb), 0.0f, 1.0f) * knobSweep; };
                const bool ringLit = dynamicsUsable && isLit (rangeKnob);

                // The ring's groove, sunk into the panel.
                const auto groove = arc (c.x, c.y, ringRadius, knobStartAngle, knobStartAngle + knobSweep);
                g.setColour (faded (juce::Colours::black.withAlpha (0.32f)));
                strokeRounded (g, groove, ringWidth + 1.0f);
                g.setColour (faded (textColour.withAlpha (dynamicsUsable ? (ringLit ? 0.12f : 0.05f) : 0.02f)));
                strokeRounded (g, groove, ringWidth - 1.0f);

                // The static gain, out from 0 dB at the top.
                if (knobEnabled (*band, gainKnob) && std::abs (band->gainDb) >= 0.05f)
                {
                    const float zero = angleFor (0.0f), gainAt = angleFor (band->gainDb);
                    g.setColour (faded (band->gainDb > 0.0f ? boostColour : cutColour));
                    strokeRounded (g, arc (c.x, c.y, ringRadius, std::min (zero, gainAt), std::max (zero, gainAt)), ringWidth - 2.0f);
                }

                if (dynamic)
                {
                    const auto colour = band->range > 0.0f ? boostColour : cutColour;
                    const float from = angleFor (band->gainDb), reach = angleFor (band->gainDb + band->range);
                    g.setColour (faded (colour.withAlpha (ringLit ? 0.55f : 0.4f)));
                    strokeRounded (g, arc (c.x, c.y, ringRadius, std::min (from, reach), std::max (from, reach)), ringWidth);

                    const float now = angleFor (band->gainDb + liveMovement (*band));
                    if (std::abs (now - from) > 0.01f)
                    {
                        const auto moving = arc (c.x, c.y, ringRadius, std::min (from, now), std::max (from, now));
                        g.setColour (faded (colour.withAlpha (0.25f)));
                        strokeRounded (g, moving, ringWidth + 5.0f); // it glows as it moves
                        g.setColour (faded (colour));
                        strokeRounded (g, moving, ringWidth);
                    }
                }

                // The threshold ring: the level the band hears fills it from the
                // left, coloured past the threshold, where the band starts to
                // move; a bright notch marks the threshold.
                {
                    const bool lit = dynamicsUsable && isLit (thresholdKnob);
                    const auto levelAngle = [] (float db) { return knobStartAngle + std::clamp ((db - minThresholdDb) / (maxThresholdDb - minThresholdDb), 0.0f, 1.0f) * knobSweep; };
                    const auto track = arc (c.x, c.y, thresholdRadius, knobStartAngle, knobStartAngle + knobSweep);
                    g.setColour (faded (juce::Colours::black.withAlpha (0.32f)));
                    strokeRounded (g, track, thresholdWidth + 1.0f);
                    g.setColour (faded (textColour.withAlpha (dynamicsUsable ? (lit ? 0.14f : 0.06f) : 0.02f)));
                    strokeRounded (g, track, thresholdWidth - 1.0f);

                    if (dynamicsUsable)
                    {
                        const float at = levelAngle (band->threshold);
                        if (dynamic && levelDb > minThresholdDb)
                        {
                            const float level = levelAngle (levelDb);
                            g.setColour (faded (textColour.withAlpha (0.35f)));
                            strokeRounded (g, arc (c.x, c.y, thresholdRadius, knobStartAngle, std::min (level, at)), thresholdWidth);
                            if (level > at)
                            {
                                g.setColour (faded ((band->range > 0.0f ? boostColour : cutColour).withAlpha (0.85f)));
                                strokeRounded (g, arc (c.x, c.y, thresholdRadius, at, level), thresholdWidth);
                            }
                        }

                        const float inner = thresholdRadius - 3.5f, outer = thresholdRadius + 3.5f;
                        juce::Path notch;
                        notch.startNewSubPath (c.x + std::sin (at) * inner, c.y - std::cos (at) * inner);
                        notch.lineTo (c.x + std::sin (at) * outer, c.y - std::cos (at) * outer);
                        g.setColour (faded (bright.withAlpha (dynamic || lit ? 1.0f : 0.5f)));
                        strokeRounded (g, notch, lit ? 2.2f : 1.8f);
                    }
                }

                const bool enabled = knobEnabled (*band, gainKnob);
                knob (c, gainKnobRadius, gainKnob, enabled && isLit (gainKnob), enabled, 1.0f, false);

                // Below: Gain's value, or the range's or threshold's while one
                // is being set, and the live gain change while it's dynamic.
                int shown = gainKnob;
                for (int ringKnob : { rangeKnob, thresholdKnob })
                    if (dynamicsUsable && isLit (ringKnob))
                        shown = ringKnob;
                const float dim = enabled || shown != gainKnob ? 1.0f : 0.32f;
                caption (shown == gainKnob ? "GAIN" : knobName (*band, shown), { c.x - 40.0f, c.y + 31.0f, 80.0f, 12.0f }, faded (idle.withMultipliedAlpha (dim)));
                if (dim == 1.0f) // a cut has no gain: just its name, dimmed
                    text (knobText (*band, shown), { c.x - 40.0f, c.y + 43.0f, 80.0f, 14.0f }, 10.5f, faded (bright));

            }

            // The gear, for the rest of the dynamics; cuts have none.
            if (dynamicsUsable)
            {
                const bool open = l.popup == PanelPopup::dynamics, hovered = hover.kind == Kind::dynamicsMenu;
                const auto c = l.gear.getCentre();
                g.setColour (faded (open || hovered ? bright : idle.withMultipliedAlpha (0.8f)));
                g.drawEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f, 1.2f);
                juce::Path teeth;
                for (int i = 0; i < 8; ++i)
                {
                    const float a = (float) i * pi / 4.0f;
                    teeth.startNewSubPath (c.x + std::cos (a) * 4.5f, c.y + std::sin (a) * 4.5f);
                    teeth.lineTo (c.x + std::cos (a) * 6.5f, c.y + std::sin (a) * 6.5f);
                }
                strokeRounded (g, teeth, 1.3f);
            }

            // An open menu, over everything else.
            if (l.popup == PanelPopup::types)
            {
                popupBack (l.popupArea);
                for (int i = 0; i < numFilterTypes; ++i)
                {
                    const auto area = l.typeItems[(size_t) i];
                    const bool active = (int) band->type == i, hovered = hover.kind == Kind::type && hover.index == i;
                    buttonBack (area, active, hovered);
                    g.setColour (faded (active || hovered ? bright : idle));
                    strokeRounded (g, typeGlyph (i, { area.getX() + 14.0f, area.getCentreY() }), 1.4f);
                    g.setFont (uiFont (10.5f));
                    g.drawText (typeName (i), area.withTrimmedLeft (28.0f), juce::Justification::centredLeft, false);
                }
            }
            else if (l.popup == PanelPopup::dynamics)
            {
                popupBack (l.popupArea);
                for (int i = 0; i < 3; ++i)
                {
                    const int which = ratioKnob + i;
                    const auto area = l.dynamicsKnobs[(size_t) i];
                    const bool lit = dynamicsUsable && isLit (which, true);
                    const float dim = dynamicsUsable ? 1.0f : 0.32f;
                    caption (knobName (*band, which), { area.getX(), area.getY(), area.getWidth(), 12.0f }, faded (idle.withMultipliedAlpha (dim)));
                    knob ({ area.getCentreX(), area.getY() + 27.0f }, popupKnobRadius, which, lit, dynamicsUsable);
                    text (knobText (*band, which), { area.getX(), area.getY() + 44.0f, area.getWidth(), 14.0f }, 10.5f,
                          faded ((lit ? bright : bright.withAlpha (0.8f)).withMultipliedAlpha (dim)));
                }

                // GR: how far the band has moved, a bar that fills down from the
                // top as it reaches its whole range, and the number under it.
                {
                    const auto area = l.grMeter;
                    const float movement = std::round (liveMovement (*band) * 10.0f) / 10.0f; // as the text shows it
                    const auto colour = movement > 0.0f ? boostColour : movement < 0.0f ? cutColour : idle;
                    caption ("GR", { area.getX(), area.getY(), area.getWidth(), 12.0f }, faded (idle.withMultipliedAlpha (dynamic ? 1.0f : 0.32f)));

                    const auto bar = juce::Rectangle<float> (5.0f, 26.0f).withCentre ({ area.getCentreX(), area.getY() + 28.0f });
                    g.setColour (faded (juce::Colours::black.withAlpha (0.35f)));
                    g.fillRoundedRectangle (bar, 2.5f);
                    g.setColour (faded (textColour.withAlpha (0.12f)));
                    g.drawRoundedRectangle (bar, 2.5f, 1.0f);
                    if (dynamic)
                    {
                        const float share = std::clamp (std::abs (liveMovement (*band)) / std::abs (band->range), 0.0f, 1.0f);
                        if (share > 0.0f)
                        {
                            g.setColour (faded (colour));
                            g.fillRoundedRectangle (bar.withHeight (std::max (2.0f, bar.getHeight() * share)), 2.5f);
                        }
                    }

                    text (knobText (*band, liveGainMeter), { area.getX(), area.getY() + 44.0f, area.getWidth(), 14.0f }, 10.5f,
                          faded (dynamic ? (movement != 0.0f ? colour : bright.withAlpha (0.8f)) : idle.withMultipliedAlpha (0.4f)));
                }

                // SC: dynamics follow the sidechain. It turns on only while a
                // sidechain is coming in, so without one it is greyed out. A
                // band already set to it stays lit, dimmer, so the setting is
                // never hidden, and can still be turned off.
                const bool on = dynamicsUsable && band->externalSidechain;
                const bool clickable = dynamicsUsable && (on || sidechainConnected);
                const bool hovered = clickable && hover.kind == Kind::sidechain;
                pill (l.sidechain, on, hovered);
                text ("SC", l.sidechain, 11.0f, faded (! clickable ? idle.withMultipliedAlpha (0.4f)
                                                       : on ? (sidechainConnected ? bright : bright.withAlpha (0.7f))
                                                       : hovered ? bright : idle));
                text (sidechainConnected ? "SC in" : "No SC in", l.sidechain.translated (0.0f, 24.0f).expanded (8.0f, 0.0f), 9.0f,
                      faded (idle.withMultipliedAlpha (0.8f)));
            }
        }
    }
}

} // namespace threedq
