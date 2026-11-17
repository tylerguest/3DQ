/*
  ==============================================================================

    SphereView's 2D overlay, drawn over the particles each frame: band nodes,
    labels, the title and the help list.

  ==============================================================================
*/

#include "sphere_view_detail.h"
#include "orbitron_font.h"

namespace threedq
{

using namespace view;

namespace
{
// The 3DQ wordmark: "3DQ" in Orbitron Black, as an outline one unit high from
// the top of the capitals to the baseline, starting at x = 0.
juce::Path makeLogo()
{
    const auto typeface = juce::Typeface::createSystemTypefaceFor (fonts::orbitron, fonts::orbitronSize);
    const juce::FontVariableSetting black ("wght", 900.0f);
    const auto heavy = typeface != nullptr ? typeface->cloneWithVariableSettings ({ &black, 1 }) : nullptr;
    const auto font = juce::Font (juce::FontOptions{}.withTypeface (heavy != nullptr ? heavy : typeface).withHeight (100.0f));

    auto outline = [&] (const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        juce::Path p;
        glyphs.createPath (p);
        return p;
    };

    // The D has no overshoot or tail, so it gives the capital height exactly.
    const auto caps = outline ("D").getBounds();
    auto logo = outline ("3DQ");
    const auto bounds = logo.getBounds();
    logo.applyTransform (juce::AffineTransform::translation (-bounds.getX(), -caps.getY()).scaled (1.0f / caps.getHeight()));
    return logo;
}
}

//==============================================================================
void SphereView::drawOverlay (juce::Graphics& g, const BandSet& bands, const Camera& camera, int hovered, int dragged, int selected, float dt)
{
    const auto dirs = directionsOf (bands);

    // Back nodes first so front ones draw on top.
    std::array<std::pair<float, const Band*>, maxBands> order;
    int count = 0;
    for (const auto& b : bands)
        order[(size_t) count++] = { camera.toView (bandDirection (b)).z, &b };

    std::sort (order.begin(), order.begin() + count, [] (const auto& a, const auto& b) { return a.first < b.first; });

    for (int i = 0; i < count; ++i)
    {
        const auto& b = *order[(size_t) i].second;
        const float target = b.id == dragged ? 1.0f : b.id == hovered ? 0.7f : b.id == selected ? 0.85f : 0.0f;
        auto& emphasis = nodeEmphasis[(size_t) b.id];
        emphasis += (target - emphasis) * (1.0f - std::exp (-dt / 0.08f));
        drawNode (g, b, screenPosition (bands, dirs, b, camera), emphasis, b.id == selected, live.levelDb[(size_t) b.id]);
    }

    const int labelled = dragged >= 0 ? dragged : hovered;
    for (const auto& b : bands)
        if (b.id == labelled)
            drawLabel (g, b, screenPosition (bands, dirs, b, camera), camera, live.levelDb[(size_t) b.id]);
}

// A band node, styled like a small hardware control: a ball in the band's
// colour, with a ring that fills clockwise for boost and anticlockwise for cut.
void SphereView::drawNode (juce::Graphics& g, const Band& b, Vec3 s, float emphasis, bool selected, float levelDb)
{
    // The selected band stays visible round the back of the sphere.
    const float facing = std::max (selected ? 0.75f : 0.0f, 0.22f + 0.78f * std::clamp ((s.z + 0.3f) / 0.45f, 0.0f, 1.0f));
    const float core = (5.5f + 2.0f * emphasis) * dpr;
    const float ringR = core + (5.0f + 1.5f * emphasis) * dpr;
    const bool cut = b.isCut();
    const float liveGain = b.displayGain();
    const float amount = cut ? (float) b.slope / 48.0f : std::clamp (liveGain / maxGainDb, -1.0f, 1.0f); // a cut's arc shows its slope
    const bool neutral = ! cut && std::abs (liveGain) < 0.05f;
    const auto colour = b.muted || neutral ? warmGrey : cut || liveGain < 0 ? cutColour : boostColour;
    const float range = cut ? 0.0f : b.range;
    const float x = s.x, y = s.y;

    auto faded = [facing] (juce::Colour c) { return c.withMultipliedAlpha (facing); };
    auto circle = [&] (float radius) { return juce::Rectangle<float> (x - radius, y - radius, radius * 2.0f, radius * 2.0f); };
    auto stroke = [&] (const juce::Path& p, float thickness)
    {
        g.strokePath (p, juce::PathStrokeType (thickness * dpr, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    if (! b.muted)
    {
        const float glowR = ringR * 2.2f;
        const auto inner = colour.withAlpha ((0.28f + 0.2f * emphasis) * facing);
        juce::ColourGradient glow (inner, x, y, colour.withAlpha (0.0f), x + glowR, y, true);
        glow.addColour (core * 0.5f / glowR, inner);
        g.setGradientFill (glow);
        g.fillEllipse (circle (glowR));
    }

    // Selected: a soft pale halo.
    if (selected)
    {
        const float glowR = ringR * 2.8f;
        juce::ColourGradient glow (pale.withAlpha (0.3f * facing), x, y, pale.withAlpha (0.0f), x + glowR, y, true);
        g.setGradientFill (glow);
        g.fillEllipse (circle (glowR));
    }

    // Backing disc so the ring reads over bright particles.
    g.setColour (faded (graphite.withAlpha (0.55f)));
    g.fillEllipse (circle (ringR + 2.5f * dpr));

    // Track.
    g.setColour (faded (selected ? pale.withAlpha (0.9f) : b.muted ? warmGrey.withAlpha (0.25f) : textColour.withAlpha (0.22f)));
    g.drawEllipse (circle (ringR), (selected ? 1.8f : 1.5f) * dpr);

    // Gain arc from 12 o'clock, showing the band's gain right now.
    if (! b.muted && ! neutral)
    {
        g.setColour (faded (colour));
        stroke (arc (x, y, ringR, 0.0f, amount * twoPi), 2.2f);
    }

    // Dynamic band: a dashed outer arc spanning how far the gain can move, with
    // a tick at the static gain.
    if (range != 0.0f)
    {
        auto angleFor = [] (float db) { return std::clamp (db / maxGainDb, -1.0f, 1.0f) * twoPi; };
        const float outer = ringR + 3.5f * dpr;
        const float from = angleFor (b.gainDb), to = angleFor (b.gainDb + range);
        const float dashes[] = { 2.0f * dpr, 2.0f * dpr };
        juce::Path dashed;
        juce::PathStrokeType (1.4f * dpr).createDashedStroke (dashed, arc (x, y, outer, from, to), dashes, 2);
        g.setColour (faded ((range > 0 ? boostColour : cutColour).withAlpha (b.muted ? 0.3f : 0.75f)));
        g.fillPath (dashed);

        const float c = std::cos (from - halfPi), n = std::sin (from - halfPi);
        juce::Path tick;
        tick.startNewSubPath (x + c * (outer - 2.0f * dpr), y + n * (outer - 2.0f * dpr));
        tick.lineTo (x + c * (outer + 2.0f * dpr), y + n * (outer + 2.0f * dpr));
        g.setColour (faded (textColour.withAlpha (0.8f)));
        stroke (tick, 1.4f);
    }

    // Dynamic band: a thin meter inside the ring for what its detector hears,
    // from 12 o'clock round to full scale at the top again, with a tick at the
    // threshold. It's faint below the threshold and lights up in the range's
    // colour above it, when the band is moving.
    if (range != 0.0f && ! b.muted)
    {
        auto angleFor = [] (float db) { return std::clamp ((db - minThresholdDb) / (maxThresholdDb - minThresholdDb), 0.0f, 1.0f) * twoPi; };
        const float meterR = core + 2.4f * dpr;
        const float levelAngle = angleFor (levelDb), thresholdAngle = angleFor (b.threshold);
        const auto over = (range > 0 ? boostColour : cutColour).withAlpha (0.95f);

        if (levelAngle > 0.02f)
        {
            g.setColour (faded (textColour.withAlpha (0.3f)));
            stroke (arc (x, y, meterR, 0.0f, std::min (levelAngle, thresholdAngle)), 1.2f);
        }

        if (levelAngle > thresholdAngle)
        {
            g.setColour (faded (over));
            stroke (arc (x, y, meterR, thresholdAngle, levelAngle), 1.4f);
        }

        const float c = std::cos (thresholdAngle - halfPi), n = std::sin (thresholdAngle - halfPi);
        juce::Path tick;
        tick.startNewSubPath (x + c * (meterR - 1.5f * dpr), y + n * (meterR - 1.5f * dpr));
        tick.lineTo (x + c * (meterR + 1.5f * dpr), y + n * (meterR + 1.5f * dpr));
        g.setColour (faded (levelAngle > thresholdAngle ? over : textColour.withAlpha (0.7f)));
        stroke (tick, 1.2f);
    }

    // Stereo band: a dot either side of the ring, for the two channels it acts on.
    if (b.stereo)
    {
        const float dotOffset = ringR + 4.5f * dpr, dotR = 1.7f * dpr;
        g.setColour (faded (textColour.withAlpha (b.muted ? 0.35f : 0.85f)));
        g.fillEllipse (x - dotOffset - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
        g.fillEllipse (x + dotOffset - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    // Core ball.
    if (b.muted)
    {
        const juce::Colour mutedLine (110, 107, 100);
        g.setColour (faded (juce::Colour (26, 26, 24)));
        g.fillEllipse (circle (core));
        g.setColour (faded (mutedLine));
        g.drawEllipse (circle (core), 1.5f * dpr);
        const float k = core * 0.5f;
        juce::Path slash;
        slash.startNewSubPath (x - k, y + k);
        slash.lineTo (x + k, y - k);
        stroke (slash, 1.5f);
    }
    else
    {
        // Flat colour with a soft highlight toward the top-left, so it reads as
        // a ball without heavy shading.
        const float hx = x - core * 0.35f, hy = y - core * 0.4f;
        juce::ColourGradient shade (faded (colour.interpolatedWith (juce::Colours::white, 0.45f)), hx, hy,
                                    faded (colour.interpolatedWith (graphite, 0.3f)), hx + core * 1.53f, hy, true);
        shade.addColour (0.5, faded (colour));
        g.setGradientFill (shade);
        g.fillEllipse (circle (core));

        if (emphasis > 0.01f)
        {
            g.setColour (faded (pale.withAlpha (0.85f * emphasis)));
            g.drawEllipse (circle (core), 1.2f * dpr);
        }
    }

    // Cut glyph on the core: a slope rising to a flat top (low cut), or falling from one.
    if (cut)
    {
        const float k = core * 0.55f;
        const float dir = b.type == FilterType::lowCut ? 1.0f : -1.0f;
        juce::Path glyph;
        glyph.startNewSubPath (x - dir * k, y + k * 0.7f);
        glyph.lineTo (x - dir * k * 0.1f, y - k * 0.45f);
        glyph.lineTo (x + dir * k, y - k * 0.45f);
        g.setColour (faded (b.muted ? juce::Colour (110, 107, 100) : graphite.withAlpha (0.85f)));
        stroke (glyph, 1.4f);
    }

    // Shelf glyph: a step, raised on the shelf's side when boosting and lowered when cutting.
    if (b.isShelf())
    {
        const float k = core * 0.55f;
        const float dir = b.type == FilterType::lowShelf ? 1.0f : -1.0f;
        const float rise = (liveGain < 0 ? -1.0f : 1.0f) * k * 0.45f;
        juce::Path glyph;
        glyph.startNewSubPath (x - dir * k, y - rise);
        glyph.lineTo (x - dir * k * 0.25f, y - rise);
        glyph.lineTo (x + dir * k * 0.25f, y + rise);
        glyph.lineTo (x + dir * k, y + rise);
        g.setColour (faded (b.muted ? juce::Colour (110, 107, 100) : neutral ? textColour.withAlpha (0.8f) : graphite.withAlpha (0.85f)));
        stroke (glyph, 1.4f);
    }
}

void SphereView::drawLabel (juce::Graphics& g, const Band& b, Vec3 s, const Camera& camera, float liveLevelDb)
{
    juce::StringArray lines;
    const auto freq = formatFreq (latToFreq (b.lat));

    if (b.isCut())
    {
        lines.add ((b.type == FilterType::lowCut ? "Low cut  " : "High cut  ") + freq);
        lines.add (juce::String (b.slope) + " dB/oct  " + dot + "  " + (b.stereo ? juce::String ("Stereo") : panLabel (b.lon)));
    }
    else
    {
        const juce::String shelfName = b.type == FilterType::lowShelf ? "Low shelf  " : b.type == FilterType::highShelf ? "High shelf  " : "";
        lines.add (shelfName + freq + "  " + formatGain (b.gainDb));
        lines.add ("Q " + formatQ (b.q) + "  " + dot + "  " + (b.stereo ? juce::String ("Stereo") : panLabel (b.lon)));
    }

    if (! b.stereo)
        lines.add (midSideLabel (b.lon));

    if (b.range != 0.0f && ! b.isCut())
    {
        lines.add ("Dyn " + formatGain (b.range) + "  " + dot + "  Thresh " + formatDb (b.threshold) + " dB"
                   + (b.externalSidechain ? "  " + dot + "  SC" : juce::String()));
        lines.add (formatRatio (b.ratio) + "  " + dot + "  Attack " + formatMs (b.attackMs) + "  " + dot + "  Release " + formatMs (b.releaseMs));
        lines.add ("Now " + formatGain (b.displayGain()) + "  " + dot + "  Level " + juce::String (std::max (-99.0f, liveLevelDb), 1) + " dB");
    }

    if (b.muted)
        lines.add ("Muted " + dot + " right-click to unmute");

    const float fontPx = 12.0f * dpr;
    const auto font = uiFont (fontPx);
    const float padding = 6.0f * dpr;
    const float lineHeight = std::round (fontPx * 1.35f); // whole pixels, so the lines space evenly

    float textWidth = 0;
    for (const auto& line : lines)
        textWidth = std::max (textWidth, juce::GlyphArrangement::getStringWidth (font, line));

    const float width = textWidth + padding * 2.0f;
    const float height = lineHeight * (float) lines.size() + padding * 2.0f - (lineHeight - fontPx);
    float x = s.x + 16.0f * dpr;
    float y = s.y - height - 10.0f * dpr;

    if (x + width > camera.width - 8.0f * dpr)
        x = s.x - width - 16.0f * dpr;
    if (y < 8.0f * dpr)
        y = s.y + 14.0f * dpr;

    const juce::Rectangle<float> box (x, y, width, height);
    g.setColour (graphite.withAlpha (0.88f));
    g.fillRoundedRectangle (box, 6.0f * dpr);
    g.setColour (warmGrey.withAlpha (0.45f));
    g.drawRoundedRectangle (box, 6.0f * dpr, 1.0f * dpr);

    g.setFont (font);
    g.setColour (textColour);

    for (int i = 0; i < lines.size(); ++i)
        g.drawSingleLineText (lines[i], juce::roundToInt (x + padding), juce::roundToInt (y + padding + (float) i * lineHeight + font.getAscent()));
}

void SphereView::drawTitle (juce::Graphics& g, const Camera& camera, int soloed)
{
    static const juce::Path logo = makeLogo();

    // Capitals 12 points tall, centred on the preset bar. Size and inset grow
    // with the window like the buttons, so it stays level with the tools on the right.
    const float unit = dpr * uiScale (camera.width / dpr, camera.height / dpr);
    const float height = 12.0f * unit, left = 21.0f * unit, top = 26.0f * unit - height / 2.0f;

    g.setColour (juce::Colours::white);
    g.fillPath (logo, juce::AffineTransform::scale (height).translated (left, top));

    // The maker's name, after a hairline, quietly level with the logo.
    const float logoRight = left + logo.getBounds().getRight() * height;
    const float divider = logoRight + 9.0f * unit, centreY = top + height / 2.0f;
    g.setColour (warmGrey.withAlpha (0.3f));
    g.fillRect (divider, centreY - 6.0f * unit, 1.0f * unit, 12.0f * unit);

    auto mark = uiFont (9.0f * unit, true);
    mark.setExtraKerningFactor (0.22f);
    g.setFont (mark);
    g.setColour (warmGrey.withAlpha (0.6f));
    g.drawSingleLineText ("4KDSP", juce::roundToInt (divider + 9.0f * unit), juce::roundToInt (centreY + mark.getAscent() * 0.38f));

    const int x = juce::roundToInt (left), baseline = juce::roundToInt (top + height + 18.0f * unit);

    // Solo changes what you hear, so it stays on show with the panel closed.
    if (soloed >= 0)
    {
        g.setFont (uiFont (12.0f * unit));
        g.setColour (textColour.withAlpha (0.85f));
        g.drawSingleLineText ("Solo  " + juce::String::charToString ((juce::juce_wchar) 0xb7) + "  Band " + juce::String (soloed + 1),
                              x, baseline);
    }
}

// The bar along the bottom: a strip whose ends lean in, with the input gain at
// the left, the host's track in the middle and the output gain at the right.
// The ? button has the corner beside it. trimHot: the gain field hovered or
// dragged (0 input, 1 output), or -1.
void SphereView::drawBottomBar (juce::Graphics& g, const Camera& camera, int trimHot,
                                const juce::String& trackName, std::optional<juce::Colour> trackColour)
{
    const float unit = dpr * uiScale (camera.width / dpr, camera.height / dpr);
    const float w = camera.width, bottom = camera.height;
    const float top = bottom - bottomBarHeight * unit;
    const float inset = bottomBarSideInset * unit, slant = bottomBarSlant * unit;

    // A trapezoid, its corners well rounded; the edge is drawn up the ends and along
    // the top, not along the window's bottom.
    const auto trapezoid = [&] (float bottomLeft, float topLeft, float topRight, float bottomRight, bool closed)
    {
        juce::Path p;
        p.startNewSubPath (bottomLeft, bottom + 2.0f * unit);
        p.lineTo (topLeft, top);
        p.lineTo (topRight, top);
        p.lineTo (bottomRight, bottom + 2.0f * unit);
        if (closed)
            p.closeSubPath();
        return p.createPathWithRoundedCorners (9.0f * unit);
    };

    const auto bar = trapezoid (inset, inset + slant, w - inset - slant, w - inset, true);
    // The same surface as the band panel, with its line of light along the top.
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (31, 31, 30).withAlpha (0.92f), top, juce::Colour (22, 22, 21).withAlpha (0.92f), bottom));
    g.fillPath (bar);
    g.setColour (textColour.withAlpha (0.12f));
    g.strokePath (trapezoid (inset, inset + slant, w - inset - slant, w - inset, false), juce::PathStrokeType (1.0f * unit));

    // The track: its name, centred, after a dot in its colour. Long names are
    // cut short before they reach the gains.
    if (trackName.isNotEmpty())
    {
        const auto font = uiFont (11.0f * unit);
        const bool dot = trackColour.has_value();
        const float dotSpace = dot ? 13.0f * unit : 0.0f;
        const float flare = 8.0f * unit, pad = 20.0f * unit; // the tab's, below
        const float room = 2.0f * (trimFieldArea (w, bottom, unit, 1).getX() - w / 2.0f - flare - pad - 4.0f * unit) - dotSpace;
        const float textWidth = std::min (juce::GlyphArrangement::getStringWidth (font, trackName), room);

        if (textWidth > 20.0f * unit)
        {
            const float left = w / 2.0f - (dotSpace + textWidth) / 2.0f;
            const float tabTop = top + 3.0f * unit;
            const float centreY = (tabTop + bottom) / 2.0f;

            // The tab it sits in, pressed into the bar: a little lower than the
            // bar's top, its sides rounding out as they go down, darker inside
            // with a shadow under its top edge, and a faint lit rim outside it.
            {
                const float tabLeft = left - pad, tabRight = left + dotSpace + textWidth + pad;
                const auto outline = [&] (bool closed)
                {
                    juce::Path p;
                    p.startNewSubPath (tabLeft - flare, bottom + 2.0f * unit);
                    p.lineTo (tabLeft, tabTop);
                    p.lineTo (tabRight, tabTop);
                    p.lineTo (tabRight + flare, bottom + 2.0f * unit);
                    if (closed)
                        p.closeSubPath();
                    return p.createPathWithRoundedCorners (7.0f * unit);
                };

                const auto well = outline (true);
                g.setColour (textColour.withAlpha (0.1f));
                g.strokePath (outline (false), juce::PathStrokeType (1.0f * unit),
                              juce::AffineTransform::translation (0.0f, -0.8f * unit)); // the rim, catching the light
                g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (12, 12, 12), tabTop, juce::Colour (17, 17, 16), bottom));
                g.fillPath (well);

                const juce::Graphics::ScopedSaveState clipped (g);
                g.reduceClipRegion (well);
                g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.55f), tabTop, juce::Colours::black.withAlpha (0.0f), tabTop + 5.0f * unit));
                g.strokePath (outline (false), juce::PathStrokeType (3.0f * unit)); // the shadow just inside its edge
            }

            if (dot)
            {
                g.setColour (trackColour->withAlpha (1.0f));
                g.fillEllipse (left + 1.0f * unit, centreY - 3.5f * unit, 7.0f * unit, 7.0f * unit);
            }

            g.setFont (font);
            g.setColour (textColour.withAlpha (0.88f));
            g.drawText (trackName, juce::Rectangle<float> (left + dotSpace, tabTop, textWidth + 2.0f * unit, bottom - tabTop),
                        juce::Justification::centredLeft, true);
        }
    }

    // The input and output gains: just their values; hovering or dragging
    // one lights a soft field behind it.
    for (int which = 0; which < 2; ++which)
    {
        const bool hot = trimHot == which;
        const auto field = trimFieldArea (w, bottom, unit, which);
        if (hot)
        {
            g.setColour (textColour.withAlpha (0.06f));
            g.fillRoundedRectangle (field, 4.0f * unit);
        }

        g.setFont (uiFont (11.0f * unit));
        g.setColour (textColour.withAlpha (hot ? 1.0f : 0.88f));
        g.drawText (trimText (live.trimGainDb[(size_t) which]), field, juce::Justification::centred, false);
    }
}

// The level meter, up the right-hand edge: one slim bar per channel, the
// output bright over the input dimmed behind it (so where the EQ cuts, the
// input shows above the output), a held output peak on each, and a light at
// the top that stays on for a while after the output goes over 0 dB.
void SphereView::drawLevelMeter (juce::Graphics& g, const Camera& camera, float dt)
{
    constexpr float floorDb = -60.0f, fallDbPerSecond = 24.0f, peakHoldSeconds = 1.5f, peakFallDbPerSecond = 18.0f, clipHoldSeconds = 2.0f;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float in = live.outputPeakDb[ch];
        meterLevel[ch] = std::max (in, meterLevel[ch] - fallDbPerSecond * dt);
        meterInputLevel[ch] = std::max (live.inputPeakDb[ch], meterInputLevel[ch] - fallDbPerSecond * dt);

        if (in >= meterPeak[ch])
        {
            meterPeak[ch] = in;
            meterPeakTime[ch] = now;
        }
        else if (now - meterPeakTime[ch] > peakHoldSeconds)
        {
            meterPeak[ch] = std::max (in, meterPeak[ch] - peakFallDbPerSecond * dt);
        }

        if (in > 0.0f)
            meterClipTime[ch] = now;
    }

    const float unit = dpr * uiScale (camera.width / dpr, camera.height / dpr);
    const float centreX = camera.width - helpButtonInset * unit;
    const float top = (presetBarBottom + 22.0f) * unit;
    const float bottom = camera.height - (bottomBarHeight + 14.0f) * unit;
    if (bottom - top < 40.0f * unit)
        return;

    const float barWidth = 3.0f * unit, gap = 2.0f * unit;
    const auto yFor = [=] (float db) { return bottom - (bottom - top) * std::clamp ((db - floorDb) / -floorDb, 0.0f, 1.0f); };

    // The scale: a few levels, faint, to the bars' left.
    g.setFont (uiFont (8.0f * unit));
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const float y = yFor (db);
        g.setColour (warmGrey.withAlpha (0.35f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (centreX - barWidth - gap - 26.0f * unit, y - 5.0f * unit, 20.0f * unit, 10.0f * unit),
                    juce::Justification::centredRight, false);
        g.setColour (warmGrey.withAlpha (0.18f));
        g.fillRect (centreX - barWidth - gap - 4.0f * unit, y - 0.5f * unit, 2.5f * unit, 1.0f * unit);
    }

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float x = ch == 0 ? centreX - gap / 2.0f - barWidth : centreX + gap / 2.0f;
        const juce::Rectangle<float> track (x, top, barWidth, bottom - top);

        g.setColour (textColour.withAlpha (0.06f));
        g.fillRoundedRectangle (track, barWidth / 2.0f);

        // The input, behind, in its own colour.
        const float inputY = yFor (meterInputLevel[ch]);
        if (bottom - inputY > 0.5f)
        {
            g.setColour (inputMeterColour.withAlpha (0.85f));
            g.fillRoundedRectangle (track.withTop (inputY), barWidth / 2.0f);
        }

        // The output: pale, brighter as it nears the top.
        const float levelY = yFor (meterLevel[ch]);
        if (bottom - levelY > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient::vertical (pale.withAlpha (0.95f), top, warmGrey.withAlpha (0.55f), bottom));
            g.fillRoundedRectangle (track.withTop (levelY), barWidth / 2.0f);
        }

        // The held peak.
        if (meterPeak[ch] > floorDb)
        {
            g.setColour (pale.withAlpha (0.9f));
            g.fillRect (x, yFor (meterPeak[ch]) - 0.5f * unit, barWidth, 1.2f * unit);
        }

        // Over 0 dB: a red light above the bar.
        const bool clipped = now - meterClipTime[ch] < clipHoldSeconds;
        g.setColour (clipped ? cutColour : textColour.withAlpha (0.08f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x, top - 6.0f * unit, barWidth, 3.0f * unit), 1.0f * unit);
    }
}

// The ? button, and the list of controls that appears while hovering over it.
void SphereView::drawHelp (juce::Graphics& g, const Camera& camera, bool hovered, float dt)
{
    helpAmount += ((hovered ? 1.0f : 0.0f) - helpAmount) * (1.0f - std::exp (-dt / 0.08f));

    const float unit = dpr * uiScale (camera.width / dpr, camera.height / dpr); // one scaled point, in device pixels
    const auto [cx, cy] = helpButtonCentre (camera.width, camera.height, unit);
    const float radius = helpButtonRadius * unit;
    const juce::Rectangle<float> button (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // A hairline ring, filling in faintly on hover.
    g.setColour (textColour.withAlpha (0.06f * helpAmount));
    g.fillEllipse (button);
    g.setColour (warmGrey.withAlpha (0.25f + 0.4f * helpAmount));
    g.drawEllipse (button.reduced (0.5f * unit), 1.0f * unit);
    g.setFont (uiFont (10.5f * unit));
    g.setColour (textColour.withAlpha (0.5f + 0.5f * helpAmount));
    g.drawText ("?", button, juce::Justification::centred, false);

    if (helpAmount < 0.01f)
        return;

    static constexpr std::array<std::pair<const char*, const char*>, 17> rows {{
        { "Double-click sphere",        "Add band" },
        { "Double-click band",          "Remove band" },
        { "Drag band",                  "Move band" },
        { "Alt+hold band",              "Solo while held" },
        { "Drag empty space",           "Rotate" },
        { "Scroll empty space",         "Zoom" },
        { "Hold wheel and drag",        "Pan view" },
        { "Double-click background",    "Recentre view" },
        { "Scroll band or Ctrl+drag",   "Gain" },
        { "Shift+scroll band",          "Width (cuts: slope)" },
        { "Alt+scroll band",            "Dynamic range" },
        { "Ctrl+scroll band",           "Threshold" },
        { "Right-click band",           "Mute" },
        { "Shift+right-click band",     "Stereo / mono" },
        { "Shift+double-click band",    "Peak / cuts / shelves" },
        { "Drag bar's dB values",       "Input (left) / output gain" },
        { "Click empty space",          "Hide panel (click a band: show)" },
    }};

    const auto font = uiFont (12.0f * unit);
    const float padding = 10.0f * unit;
    const float gap = 18.0f * unit;
    const float lineHeight = std::round (12.0f * unit * 1.45f);

    float gestureWidth = 0, actionWidth = 0;
    for (const auto& [gesture, action] : rows)
    {
        gestureWidth = std::max (gestureWidth, juce::GlyphArrangement::getStringWidth (font, gesture));
        actionWidth = std::max (actionWidth, juce::GlyphArrangement::getStringWidth (font, action));
    }

    const float width = padding * 2.0f + gestureWidth + gap + actionWidth;
    const float height = padding * 2.0f + lineHeight * (float) rows.size();
    const float x = std::max (8.0f * unit, cx + radius - width);
    const float y = std::max (8.0f * unit, cy - radius - 8.0f * unit - height);
    const juce::Rectangle<float> panel (x, y, width, height);

    g.setColour (graphite.withAlpha (0.92f * helpAmount));
    g.fillRoundedRectangle (panel, 6.0f * unit);
    g.setColour (warmGrey.withAlpha (0.45f * helpAmount));
    g.drawRoundedRectangle (panel, 6.0f * unit, 1.0f * unit);

    g.setFont (font);
    const int baselineOffset = juce::roundToInt (y + padding + font.getAscent());

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int baseline = baselineOffset + juce::roundToInt ((float) i * lineHeight);
        g.setColour (textColour.withAlpha (0.6f * helpAmount));
        g.drawSingleLineText (rows[i].first, juce::roundToInt (x + padding), baseline);
        g.setColour (textColour.withAlpha (helpAmount));
        g.drawSingleLineText (rows[i].second, juce::roundToInt (x + padding + gestureWidth + gap), baseline);
    }
}

} // namespace threedq
