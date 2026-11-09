/*
  ==============================================================================

    PresetBar: see PresetBar.h.

  ==============================================================================
*/

#include "PresetBar.h"
#include "BandPanel.h"
#include "SphereUi.h"

namespace sphere
{

namespace
{
constexpr float barTop = 12.0f, barHeight = 28.0f, buttonHeight = 24.0f;
constexpr float undoWidth = 24.0f, abWidth = 36.0f, copyWidth = 38.0f, arrowWidth = 20.0f, groupGap = 4.0f;
constexpr float toolsWidth = undoWidth * 2.0f + groupGap + abWidth + copyWidth;
constexpr float toolsRightInset = 14.0f, toolsClearance = 8.0f; // from the window's edge, and from the preset selector
constexpr float minNameWidth = 90.0f, maxNameWidth = 170.0f;
constexpr float menuWidth = 240.0f, menuGap = 6.0f, menuInset = 4.0f, menuDivider = 9.0f, menuMargin = 12.0f;
constexpr int maxMenuRows = 12;

void drawUndoArrow (juce::Graphics& g, juce::Point<float> c, float direction, juce::Colour colour)
{
    const auto at = [&] (float x, float y) { return juce::Point<float> (c.x + x * direction, c.y + y); };

    juce::Path head;
    head.startNewSubPath (at (-1.5f, -5.0f));
    head.lineTo (at (-5.0f, -1.8f));
    head.lineTo (at (-1.5f, 1.4f));

    juce::Path shaft;
    shaft.startNewSubPath (at (-5.0f, -1.8f));
    shaft.lineTo (at (1.0f, -1.8f));
    shaft.cubicTo (at (6.5f, -1.8f), at (6.5f, 5.0f), at (1.0f, 5.0f));
    shaft.lineTo (at (-2.5f, 5.0f));

    g.setColour (colour);
    for (const auto* p : { &head, &shaft })
        g.strokePath (*p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawChevron (juce::Graphics& g, juce::Point<float> c, float direction, juce::Colour colour)
{
    juce::Path p;
    p.startNewSubPath (c.x - 2.0f * direction, c.y - 4.0f);
    p.lineTo (c.x + 2.0f * direction, c.y);
    p.lineTo (c.x - 2.0f * direction, c.y + 4.0f);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}

PresetBarLayout PresetBarLayout::make (float windowWidth, float windowHeight, int presetCount, int scroll)
{
    PresetBarLayout l;
    l.scale = panelScale (windowWidth, windowHeight);
    l.presetCount = presetCount;
    const float width = windowWidth / l.scale, height = windowHeight / l.scale;

    // The preset selector is centred on the window, with the tools to its right.
    const float nameWidth = std::clamp (width - 2.0f * (toolsRightInset + toolsWidth + toolsClearance + arrowWidth), minNameWidth, maxNameWidth);
    const float centre = width / 2.0f;

    const float y = barTop + (barHeight - buttonHeight) / 2.0f;
    float x = centre - nameWidth / 2.0f - arrowWidth;
    const auto take = [&] (float w)
    {
        const juce::Rectangle<float> r (x, y, w, buttonHeight);
        x += w;
        return r;
    };

    l.previous = take (arrowWidth);
    l.name = take (nameWidth);
    l.next = take (arrowWidth);

    x = width - toolsRightInset - toolsWidth;
    l.undo = take (undoWidth);
    l.redo = take (undoWidth);
    x += groupGap;
    l.ab = take (abWidth);
    l.copy = take (copyWidth);

    const float shownMenuWidth = std::min (menuWidth, width - 16.0f);
    const float menuLeft = (width - shownMenuWidth) / 2.0f;
    const float menuTop = barTop + barHeight + menuGap;
    l.save = { menuLeft + menuInset, menuTop + menuInset, shownMenuWidth - menuInset * 2.0f, l.rowHeight };
    l.saveAs = l.save.translated (0.0f, l.rowHeight);
    l.listTop = l.saveAs.getBottom() + menuDivider;

    const int room = std::max (1, (int) ((height - menuMargin - menuInset - l.listTop) / l.rowHeight));
    l.visibleRows = std::min ({ presetCount, room, maxMenuRows });
    l.firstVisible = std::clamp (scroll, 0, presetCount - l.visibleRows);
    l.menu = { menuLeft, menuTop, shownMenuWidth, l.listTop + l.rowHeight * (float) l.visibleRows + menuInset - menuTop };
    return l;
}

juce::Rectangle<float> PresetBarLayout::row (int preset) const
{
    return { save.getX(), listTop + rowHeight * (float) (preset - firstVisible), save.getWidth(), rowHeight };
}

juce::Rectangle<float> PresetBarLayout::removeButton (int preset) const
{
    return row (preset).removeFromRight (24.0f);
}

PresetTarget PresetBarLayout::hit (juce::Point<float> position, bool menuOpen) const
{
    using Kind = PresetTarget::Kind;
    const auto p = position / scale;

    if (menuOpen && menu.contains (p))
    {
        if (save.contains (p))
            return { Kind::save };
        if (saveAs.contains (p))
            return { Kind::saveAs };

        for (int i = firstVisible; i < firstVisible + visibleRows; ++i)
            if (row (i).contains (p))
                return { i > 0 && removeButton (i).contains (p) ? Kind::remove : Kind::item, i };

        return { Kind::menuBackground };
    }

    for (const auto& [area, kind] : { std::pair (undo, Kind::undo), std::pair (redo, Kind::redo), std::pair (ab, Kind::ab), std::pair (copy, Kind::copy),
                                      std::pair (previous, Kind::previous), std::pair (name, Kind::name), std::pair (next, Kind::next) })
        if (area.contains (p))
            return { kind };

    return {};
}

void drawPresetBar (juce::Graphics& g, const PresetBarLayout& l, float dpr, const PresetBarInfo& info, PresetTarget hover, float menuAmount)
{
    using Kind = PresetTarget::Kind;

    const juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (juce::AffineTransform::scale (dpr * l.scale));

    const auto text = [&] (const juce::String& s, juce::Rectangle<float> area, juce::Colour c, juce::Justification justification = juce::Justification::centred)
    {
        g.setFont (uiFont (12.0f));
        g.setColour (c);
        g.drawText (s, area, justification, true);
    };
    const auto hoverBack = [&] (juce::Rectangle<float> r, float alpha)
    {
        g.setColour (textColour.withAlpha (alpha));
        g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
    };
    const auto hovered = [&] (Kind kind) { return hover.kind == kind; };
    const auto brightness = [&] (Kind kind, bool enabled = true)
    {
        return textColour.withAlpha (! enabled ? 0.22f : hovered (kind) ? 1.0f : 0.7f);
    };

    for (const auto& [area, kind, enabled] : { std::tuple (l.undo, Kind::undo, info.canUndo), std::tuple (l.redo, Kind::redo, info.canRedo),
                                               std::tuple (l.ab, Kind::ab, true), std::tuple (l.copy, Kind::copy, true),
                                               std::tuple (l.previous, Kind::previous, true), std::tuple (l.name, Kind::name, true),
                                               std::tuple (l.next, Kind::next, true) })
        if (enabled && hovered (kind))
            hoverBack (area, 0.08f);

    drawUndoArrow (g, l.undo.getCentre(), 1.0f, brightness (Kind::undo, info.canUndo));
    drawUndoArrow (g, l.redo.getCentre(), -1.0f, brightness (Kind::redo, info.canRedo));

    const auto abCentre = l.ab.getCentre();
    const auto letter = [&] (const char* s, float offset, bool lit)
    {
        text (s, { abCentre.x + offset - 8.0f, l.ab.getY(), 16.0f, l.ab.getHeight() }, textColour.withAlpha (lit ? (hovered (Kind::ab) ? 1.0f : 0.9f) : 0.3f));
    };
    letter ("A", -9.0f, ! info.showingB);
    letter ("/", 0.0f, false);
    letter ("B", 9.0f, info.showingB);

    text ("Copy", l.copy, brightness (Kind::copy));
    drawChevron (g, l.previous.getCentre(), -1.0f, brightness (Kind::previous));
    drawChevron (g, l.next.getCentre(), 1.0f, brightness (Kind::next));
    text (info.dirty ? info.name + " *" : info.name, l.name.reduced (4.0f, 0.0f), textColour.withAlpha (hovered (Kind::name) ? 1.0f : 0.9f));

    if (menuAmount < 0.01f)
        return;

    g.setColour (graphite.withAlpha (0.94f * menuAmount));
    g.fillRoundedRectangle (l.menu, 8.0f);

    const auto rowText = [&] (const juce::String& s, juce::Rectangle<float> r, float alpha, float rightInset = 8.0f)
    {
        text (s, r.withTrimmedLeft (16.0f).withTrimmedRight (rightInset), textColour.withAlpha (alpha * menuAmount), juce::Justification::centredLeft);
    };

    if (hovered (Kind::save) && info.canSave)
        hoverBack (l.save, 0.08f * menuAmount);
    if (hovered (Kind::saveAs))
        hoverBack (l.saveAs, 0.08f * menuAmount);

    rowText ("Save", l.save, info.canSave ? 0.9f : 0.3f);
    rowText (juce::String ("Save As") + juce::String::charToString ((juce::juce_wchar) 0x2026), l.saveAs, 0.9f);

    g.setColour (warmGrey.withAlpha (0.3f * menuAmount));
    g.fillRect (l.save.getX() + 4.0f, l.listTop - menuDivider / 2.0f, l.save.getWidth() - 8.0f, 1.0f);

    for (int i = l.firstVisible; i < l.firstVisible + l.visibleRows; ++i)
    {
        const auto r = l.row (i);
        const bool overRow = (hover.kind == Kind::item || hover.kind == Kind::remove) && hover.index == i;

        if (overRow)
            hoverBack (r, 0.08f * menuAmount);

        if (i == info.current)
        {
            g.setColour (textColour.withAlpha (0.9f * menuAmount));
            g.fillEllipse (r.getX() + 6.0f, r.getCentreY() - 2.0f, 4.0f, 4.0f);
        }

        rowText (info.presets[i], r, i == info.current ? 1.0f : 0.72f, 26.0f);

        if (overRow && i > 0)
        {
            const auto c = l.removeButton (i).getCentre();
            g.setColour (textColour.withAlpha ((hover.kind == Kind::remove ? 1.0f : 0.45f) * menuAmount));
            g.drawLine (c.x - 3.5f, c.y - 3.5f, c.x + 3.5f, c.y + 3.5f, 1.3f);
            g.drawLine (c.x + 3.5f, c.y - 3.5f, c.x - 3.5f, c.y + 3.5f, 1.3f);
        }
    }

    if (l.visibleRows < l.presetCount)
    {
        const float track = l.rowHeight * (float) l.visibleRows;
        const float thumb = track * (float) l.visibleRows / (float) l.presetCount;
        const float top = l.listTop + (track - thumb) * (float) l.firstVisible / (float) (l.presetCount - l.visibleRows);
        g.setColour (textColour.withAlpha (0.3f * menuAmount));
        g.fillRoundedRectangle (l.menu.getRight() - 4.0f, top, 2.0f, thumb, 1.0f);
    }
}

} // namespace sphere
