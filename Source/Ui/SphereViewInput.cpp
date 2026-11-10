/*
  ==============================================================================

    SphereView's mouse and wheel handling: the sphere, the band panel and the
    preset bar.

  ==============================================================================
*/

#include "SphereViewDetail.h"

namespace threedq
{

using namespace view;

//==============================================================================
void SphereView::mouseEnter (const juce::MouseEvent&)
{
    const std::lock_guard lock (stateLock);
    state.pointerInside = true;
    state.lastInteraction = nowSeconds();
}

void SphereView::mouseExit (const juce::MouseEvent&)
{
    const std::lock_guard lock (stateLock);
    state.pointerInside = false;
    state.helpHovered = false;
    state.panelHover = {};
    state.presetHover = {};
    state.lastInteraction = nowSeconds();

    if (state.dragged < 0)
        state.hovered = -1;
}

bool SphereView::presetMouseDown (const juce::MouseEvent& e)
{
    using Kind = PresetTarget::Kind;

    std::optional<std::pair<PresetCommand, int>> command;

    {
        const std::lock_guard lock (stateLock);
        const auto target = presetLayoutLocked().hit (e.position, state.presetMenuOpen);

        // A click away from an open menu only closes it.
        if (target.kind == Kind::none)
        {
            const bool wasOpen = state.presetMenuOpen;
            state.presetMenuOpen = false;
            return wasOpen;
        }

        state.lastInteraction = nowSeconds();

        if (! e.mods.isLeftButtonDown())
            return true;

        const auto& info = *state.presetInfo;

        switch (target.kind)
        {
            case Kind::undo:      if (info.canUndo) command = { PresetCommand::undo, 0 }; break;
            case Kind::redo:      if (info.canRedo) command = { PresetCommand::redo, 0 }; break;
            case Kind::ab:        command = { PresetCommand::toggleAB, 0 }; break;
            case Kind::copy:      command = { PresetCommand::copyAB, 0 }; break;
            case Kind::previous:  command = { PresetCommand::previous, 0 }; break;
            case Kind::next:      command = { PresetCommand::next, 0 }; break;
            case Kind::name:
                state.presetMenuOpen = ! state.presetMenuOpen;
                if (state.presetMenuOpen)
                {
                    state.presetMenuScroll = std::max (0, info.current - 5);
                    command = { PresetCommand::refresh, 0 };
                }
                break;
            case Kind::save:      if (info.canSave) command = { PresetCommand::save, 0 }; break;
            case Kind::saveAs:    command = { PresetCommand::saveAs, 0 }; break;
            case Kind::item:      command = { PresetCommand::load, target.index }; break;
            case Kind::remove:    command = { PresetCommand::remove, target.index }; break;
            case Kind::menuBackground:
            case Kind::none:
                break;
        }

        if (target.kind == Kind::save || target.kind == Kind::saveAs || target.kind == Kind::item)
            state.presetMenuOpen = false;
    }

    if (command.has_value() && onPresetCommand != nullptr)
        onPresetCommand (command->first, command->second);

    return true;
}

bool SphereView::presetPointerMoved (const juce::MouseEvent& e)
{
    const std::lock_guard lock (stateLock);

    if (knobDrag.has_value() || pressed.has_value() || gainDrag.has_value() || panPosition.has_value() || state.rotating || state.dragged >= 0)
    {
        state.presetHover = {};
        return false;
    }

    state.presetHover = presetLayoutLocked().hit (e.position, state.presetMenuOpen);

    if (state.presetHover.kind == PresetTarget::Kind::none)
        return false;

    state.pointerInside = true;
    state.lastInteraction = nowSeconds();
    state.helpHovered = false;
    state.panelHover = {};
    state.hovered = -1;
    updateCursorLocked();
    return true;
}

bool SphereView::presetWheel (const juce::MouseEvent& e, float delta)
{
    const std::lock_guard lock (stateLock);
    auto layout = presetLayoutLocked();
    const auto target = layout.hit (e.position, state.presetMenuOpen);

    if (target.kind == PresetTarget::Kind::none)
        return false;

    if (state.presetMenuOpen && layout.menu.contains (e.position / layout.scale))
    {
        const int steps = delta > 0.0f ? 1 : -1;
        state.presetMenuScroll = std::clamp (layout.firstVisible + steps, 0, std::max (0, layout.presetCount - layout.visibleRows));
        state.presetHover = presetLayoutLocked().hit (e.position, true);
    }

    return true;
}

bool SphereView::panelMouseDown (const juce::MouseEvent& e)
{
    using Kind = PanelTarget::Kind;

    BandSet edited;
    bool changed = false;

    {
        const std::lock_guard lock (stateLock);
        const auto target = panelTargetLocked (e.position);

        if (target.kind == Kind::none)
            return false;

        state.lastInteraction = nowSeconds();

        if (! e.mods.isLeftButtonDown())
            return true;

        auto* band = state.bands.find (state.selected);

        switch (target.kind)
        {
            case Kind::toggle:
                state.panelOpen = ! state.panelOpen;
                if (state.panelOpen)
                    updatePanelFitLocked();
                else
                    state.restCentre = restCentreLocked(); // the panel's fit hides this until it slides away
                break;
            case Kind::previous:  selectNeighbourLocked (-1); break;
            case Kind::next:      selectNeighbourLocked (1); break;
            case Kind::type:
                band->type = (FilterType) target.index;
                if (band->isShelf())
                    band->q = std::min (band->q, maxShelfQ);
                changed = true;
                break;
            case Kind::stereo:    band->stereo = ! band->stereo; changed = true; break;
            case Kind::solo:      state.soloLatched = state.soloLatched == band->id ? -1 : band->id; break;
            case Kind::mute:      band->muted = ! band->muted; changed = true; break;
            case Kind::remove:
                state.bands.remove (band->id);
                validateSelectionLocked();
                changed = true;
                break;
            case Kind::knob:
                if (knobEnabled (*band, target.index))
                {
                    knobDrag = KnobDrag { target.index, e.position.y, knobPosition (*band, target.index) };
                    updateCursorLocked();
                }
                break;
            case Kind::background:
            case Kind::none:
                break;
        }

        if (changed)
            edited = state.bands;
    }

    if (changed)
        notifyBandsEdited (edited);

    publishSolo();
    return true;
}

bool SphereView::panelPointerMoved (const juce::MouseEvent& e)
{
    BandSet edited;

    {
        const std::lock_guard lock (stateLock);

        if (! knobDrag.has_value())
        {
            if (pressed.has_value() || gainDrag.has_value() || panPosition.has_value() || state.rotating || state.dragged >= 0)
            {
                state.panelHover = {};
                return false;
            }

            state.panelHover = panelTargetLocked (e.position);

            if (state.panelHover.kind == PanelTarget::Kind::none)
                return false;

            state.pointerInside = true;
            state.lastInteraction = nowSeconds();
            state.helpHovered = false;
            state.hovered = -1;
            updateCursorLocked();
            return true;
        }

        state.lastInteraction = nowSeconds();
        auto* band = state.bands.find (state.selected);

        if (band == nullptr)
        {
            knobDrag.reset();
            return true;
        }

        const float speed = (e.mods.isShiftDown() ? knobFineFactor : 1.0f) / knobPixelsFullRange;
        knobDrag->position = std::clamp (knobDrag->position + (knobDrag->lastY - e.position.y) * speed, 0.0f, 1.0f);
        knobDrag->lastY = e.position.y;
        setKnobPosition (*band, knobDrag->knob, knobDrag->position);
        edited = state.bands;
    }

    notifyBandsEdited (edited);
    return true;
}

bool SphereView::panelDoubleClick (const juce::MouseEvent& e)
{
    BandSet edited;

    {
        const std::lock_guard lock (stateLock);
        const auto target = panelTargetLocked (e.position);

        if (target.kind == PanelTarget::Kind::none)
            return false;

        auto* band = state.bands.find (state.selected);

        if (target.kind != PanelTarget::Kind::knob || band == nullptr || ! knobEnabled (*band, target.index))
            return true;

        resetKnob (*band, target.index);
        edited = state.bands;
    }

    notifyBandsEdited (edited);
    return true;
}

bool SphereView::panelWheel (const juce::MouseEvent& e, float delta)
{
    BandSet edited;

    {
        const std::lock_guard lock (stateLock);
        const auto target = panelTargetLocked (e.position);

        if (target.kind == PanelTarget::Kind::none)
            return false;

        auto* band = state.bands.find (state.selected);

        if (target.kind != PanelTarget::Kind::knob || band == nullptr || ! knobEnabled (*band, target.index))
            return true;

        state.lastInteraction = nowSeconds();
        float change;

        if (knobIsStepped (*band, target.index))
        {
            // One step per notch; small trackpad deltas add up.
            slopeAccumulator += delta;
            const int steps = (int) (slopeAccumulator / 100.0f);
            slopeAccumulator -= (float) steps * 100.0f;
            change = -(float) steps / (float) (cutSlopes.size() - 1);
        }
        else
        {
            change = -delta / 100.0f / 120.0f * (e.mods.isShiftDown() ? knobFineFactor : 1.0f);
        }

        setKnobPosition (*band, target.index, knobPosition (*band, target.index) + change);
        edited = state.bands;
    }

    notifyBandsEdited (edited);
    return true;
}

void SphereView::mouseDown (const juce::MouseEvent& e)
{
    presetTookPress = presetMouseDown (e);

    if (presetTookPress || overHelpButton (e.position) || panelMouseDown (e))
        return;

    BandSet edited;
    bool changed = false;

    {
        const std::lock_guard lock (stateLock);
        state.lastInteraction = nowSeconds();
        state.recentring = false;

        if (e.mods.isMiddleButtonDown())
        {
            panPosition = e.position;
            state.hovered = -1;
            state.yawVelocity = state.pitchVelocity = 0;
            updateCursorLocked();
            return;
        }

        const auto camera = cameraLocked();
        const int hit = hitBand (toDevice (e.position), camera);

        if (hit >= 0)
            state.selected = hit;

        // Right-click a band to mute / unmute it. macOS reports Ctrl+click as a
        // right-click; that's the gain drag, so it's ignored here.
        if (e.mods.isRightButtonDown())
        {
            if (! e.mods.isCtrlDown() && hit >= 0)
            {
                auto* band = state.bands.find (hit);

                if (e.mods.isShiftDown())
                    band->stereo = ! band->stereo;
                else
                    band->muted = ! band->muted;

                edited = state.bands;
                changed = true;
            }
        }
        else if (e.mods.isLeftButtonDown())
        {
            auto* band = state.bands.find (hit);
            state.yawVelocity = state.pitchVelocity = 0; // catching the sphere stops it

            if (band != nullptr && ! band->isCut() && commandHeld (e.mods))
            {
                gainDrag = GainDrag { hit, e.position.y, band->gainDb };
                state.hovered = hit;
                updateCursorLocked();
            }
            else
            {
                // Remember where on the node it was grabbed, so it doesn't jump
                // to centre itself under the cursor.
                Press press;
                press.position = e.position;
                press.time = nowSeconds();
                press.band = hit;

                if (band != nullptr)
                {
                    const auto dirs = directionsOf (state.bands);
                    press.radius = handleRadius (*band, dirs);
                    const auto d = bandDirection (*band);
                    const auto s = camera.toScreen (camera.toView ({ d.x * press.radius, d.y * press.radius, d.z * press.radius }));
                    const auto device = toDevice (e.position);
                    press.offsetX = s.x - device.x;
                    press.offsetY = s.y - device.y;
                    state.hovered = hit;

                    if (e.mods.isAltDown())
                        state.soloHeld = hit;
                }

                pressed = press;
                lastPosition = e.position;
                rotateSamples.clear();
            }
        }
    }

    if (changed)
        notifyBandsEdited (edited);

    publishSolo();
}

void SphereView::pointerMoved (const juce::MouseEvent& e)
{
    if (presetPointerMoved (e) || panelPointerMoved (e))
        return;

    BandSet edited;
    bool changed = false;

    {
        const std::lock_guard lock (stateLock);
        state.pointerInside = true;
        const auto now = nowSeconds();
        state.lastInteraction = now;
        const auto camera = cameraLocked();
        const auto device = toDevice (e.position);
        state.helpHovered = ! pressed.has_value() && ! gainDrag.has_value() && ! panPosition.has_value() && overHelpButton (e.position);

        if (panPosition.has_value())
        {
            const auto delta = e.position - *panPosition;
            panPosition = e.position;
            state.panX += delta.x;
            state.panY += delta.y;
            clampPanLocked();
            return;
        }

        if (gainDrag.has_value())
        {
            if (auto* band = state.bands.find (gainDrag->band))
            {
                const float db = gainDrag->startGain + (gainDrag->startY - e.position.y) * gainDbPerPixel;
                band->gainDb = std::round (std::clamp (db, -maxGainDb, maxGainDb) * 10.0f) / 10.0f;
                edited = state.bands;
                changed = true;
            }
        }
        else
        {
            if (pressed.has_value() && state.dragged < 0 && ! state.rotating
                && pressed->position.getDistanceFrom (e.position) >= dragThreshold)
            {
                if (pressed->band >= 0)
                    state.dragged = pressed->band;
                else
                    state.rotating = true;
            }

            if (state.dragged >= 0 && pressed.has_value())
            {
                Vec3 p;
                auto* band = state.bands.find (state.dragged);
                if (band != nullptr && camera.pickSphere (device.x + pressed->offsetX, device.y + pressed->offsetY, true, pressed->radius, p))
                {
                    setLatLon (*band, p);
                    edited = state.bands;
                    changed = true;
                }
            }
            else if (state.rotating)
            {
                const auto delta = e.position - lastPosition;
                lastPosition = e.position;
                state.yaw += delta.x * rotateSpeed;
                state.pitch += delta.y * rotateSpeed;
                rotateSamples.push_back ({ now, delta.x, delta.y });
                rotateSamples.erase (rotateSamples.begin(),
                                     std::find_if (rotateSamples.begin(), rotateSamples.end(),
                                                   [now] (const RotateSample& s) { return now - s.time <= 0.1; }));
            }
            else if (! pressed.has_value())
            {
                state.hovered = state.helpHovered ? -1 : hitBand (device, camera);
            }

            updateCursorLocked();
        }
    }

    if (changed)
        notifyBandsEdited (edited);
}

void SphereView::mouseMove (const juce::MouseEvent& e)    { pointerMoved (e); }
void SphereView::mouseDrag (const juce::MouseEvent& e)    { pointerMoved (e); }

void SphereView::mouseUp (const juce::MouseEvent& e)
{
    {
        const std::lock_guard lock (stateLock);
        state.soloHeld = -1;
    }

    publishSolo();

    const std::lock_guard lock (stateLock);
    const auto now = nowSeconds();

    const bool clickedEmpty = pressed.has_value() && pressed->band < 0
                              && (! state.rotating || (now - pressed->time < clickMaxSeconds
                                                       && pressed->position.getDistanceFrom (e.position) < clickMaxTravel));

    if (state.rotating && ! clickedEmpty)
    {
        // Fling with the speed of the last ~80 ms of movement; if the pointer
        // was held still before letting go, don't fling at all.
        float dx = 0, dy = 0;
        int count = 0;
        double first = now;

        for (const auto& s : rotateSamples)
        {
            if (now - s.time > 0.08)
                continue;
            first = std::min (first, s.time);
            dx += s.dx;
            dy += s.dy;
            ++count;
        }

        if (count >= 2)
        {
            const float span = (float) std::max (0.016, now - first);
            state.yawVelocity = std::clamp (dx * rotateSpeed / span / 60.0f, -0.15f, 0.15f);
            state.pitchVelocity = std::clamp (dy * rotateSpeed / span / 60.0f, -0.15f, 0.15f);
        }
    }

    // A plain click on empty space clears the highlight. An open panel always
    // shows a band, so it keeps its selection.
    if (clickedEmpty && ! state.panelOpen)
        state.selected = -1;

    state.rotating = false;
    state.dragged = -1;
    gainDrag.reset();
    panPosition.reset();
    knobDrag.reset();
    pressed.reset();
    state.lastInteraction = now;
    state.helpHovered = overHelpButton (e.position);
    state.panelHover = panelTargetLocked (e.position);
    state.presetHover = presetLayoutLocked().hit (e.position, state.presetMenuOpen);
    state.hovered = state.helpHovered || state.panelHover.kind != PanelTarget::Kind::none || state.presetHover.kind != PresetTarget::Kind::none
                        ? -1 : hitBand (toDevice (e.position), cameraLocked());
    updateCursorLocked();
}

void SphereView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (presetTookPress || e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown() || overHelpButton (e.position) || panelDoubleClick (e))
        return;

    BandSet edited;

    {
        const std::lock_guard lock (stateLock);
        state.lastInteraction = nowSeconds();

        if (commandHeld (e.mods) || e.mods.isAltDown()) // two quick gain drags or solos shouldn't remove the band
            return;

        const auto camera = cameraLocked();
        const auto device = toDevice (e.position);
        const int hit = hitBand (device, camera);

        if (hit >= 0 && e.mods.isShiftDown())
        {
            auto* band = state.bands.find (hit);
            band->type = band->type == FilterType::peak     ? FilterType::lowCut
                       : band->type == FilterType::lowCut   ? FilterType::highCut
                       : band->type == FilterType::highCut  ? FilterType::lowShelf
                       : band->type == FilterType::lowShelf ? FilterType::highShelf
                                                            : FilterType::peak;

            if (band->isShelf())
                band->q = std::min (band->q, maxShelfQ);
        }
        else if (hit >= 0)
        {
            state.bands.remove (hit);
            state.hovered = -1;
        }
        else
        {
            Vec3 surface;

            if (! camera.pickSphere (device.x, device.y, false, 1.0f, surface))
            {
                // The background: back to the centre, which with the panel open
                // is the middle of the space above it.
                state.recentring = true;
                state.recentreFit = panelFitLocked (0.0f);
                state.recentreRest = restCentreLocked();
                return;
            }

            const int slot = state.bands.freeSlot();

            if (slot < 0)
                return;

            Band band;
            band.id = slot;
            setLatLon (band, surface);
            state.bands.add (band);

            // The new band's own bump can lift its node; re-pick on the lifted
            // surface so the node lands under the cursor.
            auto* added = state.bands.find (slot);
            Vec3 lifted;
            if (camera.pickSphere (device.x, device.y, true, handleRadius (*added, directionsOf (state.bands)), lifted))
                setLatLon (*added, lifted);

            state.hovered = slot;
            state.selected = slot;
        }

        edited = state.bands;
    }

    notifyBandsEdited (edited);
}

void SphereView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Normalised to ~100 per wheel notch, positive scrolling down. Wheels report
    // a platform-specific amount per notch, so each event simply counts as one;
    // trackpads report continuous pixels. Some systems turn shift+wheel or
    // alt+wheel into horizontal scrolling, so either axis counts.
    const float raw = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (raw == 0.0f || overHelpButton (e.position))
        return;

    const float delta = wheel.isSmooth ? -raw * 512.0f : (raw > 0.0f ? -100.0f : 100.0f);

    if (presetWheel (e, delta) || panelWheel (e, delta))
        return;

    BandSet edited;
    bool changed = false;

    {
        const std::lock_guard lock (stateLock);
        const double now = nowSeconds() * 1000.0;
        state.lastInteraction = now / 1000.0;

        // Gain changes move the node, so keep a scroll gesture locked to the
        // band it started on until the wheel has been still for a moment.
        const double interval = now - lastWheelTime;
        if (wheelBand < 0 || interval > 300.0 || state.bands.find (wheelBand) == nullptr)
            wheelBand = hitBand (toDevice (e.position), cameraLocked());
        lastWheelTime = now;

        auto* band = state.bands.find (wheelBand);

        // 0.25 dB per notch scrolling slowly, up to 1.25 dB spinning the wheel
        // fast. Trackpads (small, frequent deltas) stay linear and fine.
        const bool notchy = std::abs (delta) >= 50.0f;
        const float accel = notchy && interval < 250.0 ? std::clamp (90.0f / (float) std::max (interval, 1.0), 1.0f, 5.0f) : 1.0f;
        const float dbStep = -delta * 0.0025f * accel;
        auto snap = [] (float v) { return std::round (v * 100.0f) / 100.0f; };

        if (band != nullptr && band->isCut())
        {
            // Cuts: Shift+scroll steps the slope, one step per notch.
            if (e.mods.isShiftDown())
            {
                slopeAccumulator += delta;
                const int steps = (int) (slopeAccumulator / 100.0f);

                if (steps != 0)
                {
                    slopeAccumulator -= (float) steps * 100.0f;
                    const auto& slopes = cutSlopes;
                    const auto found = std::find (slopes.begin(), slopes.end(), band->slope);
                    const int index = found == slopes.end() ? 3 : (int) (found - slopes.begin());
                    band->slope = slopes[(size_t) std::clamp (index - steps, 0, (int) slopes.size() - 1)];
                    changed = true;
                }
            }

            state.hovered = band->id;
        }
        else if (band != nullptr)
        {
            if (e.mods.isAltDown())
                band->range = snap (std::clamp (band->range + dbStep, -maxGainDb, maxGainDb));
            else if (commandHeld (e.mods))
                band->threshold = snap (std::clamp (band->threshold + dbStep, minThresholdDb, maxThresholdDb));
            else if (e.mods.isShiftDown())
                band->q = std::clamp (band->q * std::exp (-delta * 0.002f), minQ, band->isShelf() ? maxShelfQ : maxQ);
            else
                band->gainDb = snap (std::clamp (band->gainDb + dbStep, -maxGainDb, maxGainDb));

            if (std::abs (band->range) < 0.05f)
                band->range = 0;

            state.hovered = band->id;
            changed = true;
        }
        else
        {
            state.zoom = std::clamp (state.zoom * std::exp (-delta * 0.001f), minZoom, maxZoom);
        }

        if (changed)
            edited = state.bands;
    }

    if (changed)
        notifyBandsEdited (edited);
}

} // namespace threedq
