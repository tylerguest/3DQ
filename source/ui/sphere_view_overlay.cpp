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
        drawNode (g, b, screenPosition (bands, dirs, b, camera), emphasis, b.id == selected);
    }

    const int labelled = dragged >= 0 ? dragged : hovered;
    for (const auto& b : bands)
        if (b.id == labelled)
            drawLabel (g, b, screenPosition (bands, dirs, b, camera), camera, live.levelDb[(size_t) b.id]);
}

// A band node, styled like a small hardware control: a ball in the band's
// colour, with a ring that fills clockwise for boost and anticlockwise for cut.
void SphereView::drawNode (juce::Graphics& g, const Band& b, Vec3 s, float emphasis, bool selected)
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
        lines.add ("Q " + juce::String (b.q, 2) + "  " + dot + "  " + (b.stereo ? juce::String ("Stereo") : panLabel (b.lon)));
    }

    if (! b.stereo)
        lines.add (midSideLabel (b.lon));

    if (b.range != 0.0f && ! b.isCut())
    {
        lines.add ("Dyn " + formatGain (b.range) + "  " + dot + "  Thresh " + formatDb (b.threshold) + " dB");
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

    // Capitals 12 points tall, centred on the preset bar; it grows with the
    // window like the buttons.
    const float unit = dpr * uiScale (camera.width / dpr, camera.height / dpr);
    const float height = 12.0f * unit, left = 21.0f * dpr, top = 26.0f * unit - height / 2.0f;

    g.setColour (juce::Colours::white);
    g.fillPath (logo, juce::AffineTransform::scale (height).translated (left, top));

    const int x = juce::roundToInt (left), baseline = juce::roundToInt (top + height + 18.0f * dpr);

    // Solo changes what you hear, so it stays on show with the panel closed.
    if (soloed >= 0)
    {
        g.setFont (uiFont (12.0f * dpr));
        g.setColour (textColour.withAlpha (0.85f));
        g.drawSingleLineText ("Solo  " + juce::String::charToString ((juce::juce_wchar) 0xb7) + "  Band " + juce::String (soloed + 1),
                              x, baseline);
    }
}

// The maker's name, quietly in the bottom-left corner, level with the ? button.
// It makes way while the band panel is open.
void SphereView::drawMakerMark (juce::Graphics& g, const Camera& camera, float panelAmount)
{
    const float alpha = 0.36f * (1.0f - panelAmount);
    if (alpha < 0.005f)
        return;

    const float unit = dpr * uiScale (camera.width / dpr, camera.height / dpr);
    auto font = uiFont (9.0f * unit, true);
    font.setExtraKerningFactor (0.22f);
    g.setFont (font);
    g.setColour (warmGrey.withAlpha (alpha));

    const float centreY = camera.height - helpButtonInset * unit;
    g.drawSingleLineText ("4KDSP", juce::roundToInt (21.0f * dpr), juce::roundToInt (centreY + font.getAscent() * 0.38f));
}

// The ? button, and the list of controls that appears while hovering over it.
void SphereView::drawHelp (juce::Graphics& g, const Camera& camera, bool hovered, float dt)
{
    helpAmount += ((hovered ? 1.0f : 0.0f) - helpAmount) * (1.0f - std::exp (-dt / 0.08f));

    const float unit = dpr * uiScale (camera.width / dpr, camera.height / dpr); // one scaled point, in device pixels
    const float cx = camera.width - helpButtonInset * unit;
    const float cy = camera.height - helpButtonInset * unit;
    const float radius = helpButtonRadius * unit;
    const juce::Rectangle<float> button (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    g.setColour (graphite.withAlpha (0.7f));
    g.fillEllipse (button);
    g.setColour (warmGrey.withAlpha (0.45f + 0.4f * helpAmount));
    g.drawEllipse (button, 1.0f * unit);
    g.setFont (uiFont (12.0f * unit, true));
    g.setColour (textColour.withAlpha (0.55f + 0.45f * helpAmount));
    g.drawText ("?", button, juce::Justification::centred, false);

    if (helpAmount < 0.01f)
        return;

    static constexpr std::array<std::pair<const char*, const char*>, 15> rows {{
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
