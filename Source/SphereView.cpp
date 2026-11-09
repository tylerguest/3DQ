/*
  ==============================================================================

    SphereView: see SphereView.h.

  ==============================================================================
*/

#include "SphereView.h"
#include "SphereUi.h"

// Stock juce_opengl deadlocks on Linux when a second plugin instance opens; see patches/.
#if JUCE_LINUX && ! defined (THREEDQ_PATCHED_JUCE_OPENGL)
 #error "Building against unpatched juce_opengl: use the copy in this repository's modules folder"
#endif

using namespace sphere;
using namespace juce::gl;

namespace
{
constexpr float nodeHitRadius = 16.0f;
constexpr float dragThreshold = 4.0f;
// A press this short counts as a click even if the hand drifted past dragThreshold.
constexpr float clickMaxTravel = 10.0f;
constexpr double clickMaxSeconds = 0.35;
constexpr float rotateSpeed = 0.008f;     // radians per pixel
constexpr float gainDbPerPixel = 0.1f;
constexpr float autoRotate = 0.0035f;     // radians per 1/60 s
constexpr double autoResumeDelay = 1.5;   // seconds
constexpr float minZoom = 0.25f, maxZoom = 2.5f;
constexpr float helpButtonRadius = 10.0f;
constexpr float helpButtonInset = 24.0f;  // from the window's right and bottom edges, to the button's centre
constexpr float minThresholdDb = threedq::minThresholdDb, maxThresholdDb = threedq::maxThresholdDb;
constexpr float minQ = threedq::minQ, maxQ = threedq::maxQ;
constexpr float knobPixelsFullRange = 200.0f;
constexpr float knobFineFactor = 0.2f; // while Shift is held

double nowSeconds()    { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0xb7);

juce::String panLabel (float lon)
{
    const int pan = (int) std::lround (std::sin (lon) * 100.0f);
    if (pan == 0)
        return "Pan C";
    return juce::String ("Pan ") + (pan < 0 ? "L " : "R ") + juce::String (std::abs (pan)) + "%";
}

juce::String midSideLabel (float lon)
{
    const int mid = (int) std::lround ((1.0f + std::cos (lon)) / 2.0f * 100.0f);
    return "Mid " + juce::String (mid) + "% " + dot + " Side " + juce::String (100 - mid) + "%";
}

bool commandHeld (const juce::ModifierKeys& mods)    { return mods.isCtrlDown() || mods.isCommandDown(); }

// Depth comes from the undisplaced direction: a node at the bottom of a deep
// dent is near the centre, but it's still on the side it faces.
Vec3 screenPosition (const BandSet& bands, const BandDirections& dirs, const Band& b, const Camera& camera)
{
    const auto d = bandDirection (b);
    const float r = 1.0f + surfaceDisplacement (bands, dirs, d);
    auto s = camera.toScreen (camera.toView ({ d.x * r, d.y * r, d.z * r }));
    s.z = camera.toView (d).z;
    return s;
}
}

//==============================================================================
SphereView::SphereView()
    : logStats (juce::SystemStats::getEnvironmentVariable ("THREEDQ_STATS", {}).isNotEmpty())
{
    setOpaque (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);

    context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    context.setComponentPaintingEnabled (false);
    context.setContinuousRepainting (true);
    context.setRenderer (this);
    context.attachTo (*this);
}

SphereView::~SphereView()
{
    context.detach();
}

void SphereView::resized()
{
    componentWidth = (float) getWidth();
    componentHeight = (float) getHeight();

    const std::lock_guard lock (stateLock);
    if (state.panelOpen)
        updatePanelFitLocked();
}

void SphereView::setBands (const BandSet& bands)
{
    {
        const std::lock_guard lock (stateLock);

        // Don't yank a band out from under an active drag.
        if (state.dragged >= 0 || gainDrag.has_value() || knobDrag.has_value())
            return;

        state.bands = bands;

        if (! state.restCentrePlaced)
        {
            state.restCentre = restCentreLocked();
            state.restCentrePlaced = true;
        }

        if (state.bands.find (state.hovered) == nullptr)
            state.hovered = -1;
        if (state.bands.find (wheelBand) == nullptr)
            wheelBand = -1;
    }

    publishSolo();
}

void SphereView::notifyBandsEdited (const BandSet& bands)
{
    if (onBandsEdited != nullptr)
        onBandsEdited (bands);

    publishSolo(); // the soloed band may be the one that just went
}

// Tells the audio side which band is soloed, if that changed. A held solo wins
// over the panel's, and a band that's gone stops being soloed.
void SphereView::publishSolo()
{
    int soloed;

    {
        const std::lock_guard lock (stateLock);

        if (state.bands.find (state.soloLatched) == nullptr)
            state.soloLatched = -1;
        if (state.bands.find (state.soloHeld) == nullptr)
            state.soloHeld = -1;

        soloed = state.soloHeld >= 0 ? state.soloHeld : state.soloLatched;
    }

    if (soloed == publishedSolo)
        return;

    publishedSolo = soloed;

    if (onSoloChanged != nullptr)
        onSoloChanged (soloed);
}

juce::Point<float> SphereView::toDevice (juce::Point<float> position) const
{
    return position * publishedScale.load();
}

//==============================================================================
Camera SphereView::cameraLocked() const
{
    const float scale = publishedScale.load();
    return makeCameraLocked (componentWidth.load() * scale, componentHeight.load() * scale, scale);
}

// The view as the user left it, plus however far the panel has moved it.
Camera SphereView::makeCameraLocked (float width, float height, float scale) const
{
    const float fitZoom = 1.0f + (state.panelFit.zoomScale - 1.0f) * state.panelAmount;
    const float fitPanY = state.panelFit.panDeltaY * state.panelAmount;
    const float restPanY = state.restCentre * std::min (width, height) * sphereRadiusFraction * state.zoom * fitZoom * (1.0f - state.panelAmount);
    return Camera::make (width, height, state.yaw, state.pitch, state.zoom * fitZoom, state.panX * scale, (state.panY + fitPanY) * scale + restPanY);
}

void SphereView::updatePanelFitLocked()
{
    state.panelFit = panelFitLocked (state.panY);
}

float SphereView::restCentreLocked() const
{
    const auto extent = surfaceExtent (state.bands, cameraLocked());
    return (extent.top + extent.bottom) / 2.0f;
}

PanelViewFit SphereView::panelFitLocked (float panY) const
{
    return fitViewAbovePanel (componentWidth.load(), componentHeight.load(), state.zoom, panY, surfaceExtent (state.bands, cameraLocked()));
}

Vec3 SphereView::bandScreenPos (const Band& b, const BandDirections& dirs, const Camera& camera) const
{
    return screenPosition (state.bands, dirs, b, camera);
}

// Limited, because on the true radius of a deep dent the whole sphere would
// fit in a few pixels of mouse travel.
float SphereView::handleRadius (const Band& b, const BandDirections& dirs) const
{
    return std::clamp (1.0f + surfaceDisplacement (state.bands, dirs, bandDirection (b)), 0.8f, 1.2f);
}

int SphereView::hitBand (juce::Point<float> p, const Camera& camera) const
{
    const auto dirs = directionsOf (state.bands);
    const float radius = nodeHitRadius * publishedScale.load();
    int best = -1;
    float bestZ = -1.0e9f;

    for (const auto& b : state.bands)
    {
        const auto s = bandScreenPos (b, dirs, camera);
        if (s.z < -0.15f) // on the far side
            continue;

        if (std::hypot (s.x - p.x, s.y - p.y) <= radius && s.z > bestZ)
        {
            best = b.id;
            bestZ = s.z;
        }
    }

    return best;
}

bool SphereView::overHelpButton (juce::Point<float> position) const
{
    const float scale = panelScale ((float) getWidth(), (float) getHeight());
    const juce::Point<float> centre ((float) getWidth() - helpButtonInset * scale, (float) getHeight() - helpButtonInset * scale);
    return position.getDistanceFrom (centre) <= (helpButtonRadius + 3.0f) * scale;
}

// Keeps the sphere's centre inside the window, so it can never be lost off-screen.
void SphereView::clampPanLocked()
{
    const float halfWidth = componentWidth.load() / 2.0f, halfHeight = componentHeight.load() / 2.0f;
    state.panX = std::clamp (state.panX, -halfWidth, halfWidth);
    state.panY = std::clamp (state.panY, -halfHeight, halfHeight);
}

// The panel follows a band that goes away (removed, or replaced by the host).
void SphereView::validateSelectionLocked()
{
    if (state.bands.find (state.selected) != nullptr)
        return;

    state.selected = -1;
    float lowest = 1.0e9f;

    for (const auto& b : state.bands)
    {
        if (b.lat < lowest)
        {
            lowest = b.lat;
            state.selected = b.id;
        }
    }
}

// Steps through the bands in frequency order, wrapping round.
void SphereView::selectNeighbourLocked (int direction)
{
    std::array<const Band*, maxBands> order;
    int count = 0;
    for (const auto& b : state.bands)
        order[(size_t) count++] = &b;

    if (count == 0)
        return;

    std::sort (order.begin(), order.begin() + count, [] (const Band* a, const Band* b) { return a->lat < b->lat; });

    int current = 0;
    for (int i = 0; i < count; ++i)
        if (order[(size_t) i]->id == state.selected)
            current = i;

    state.selected = order[(size_t) ((current + direction + count) % count)]->id;
}

PanelTarget SphereView::panelTargetLocked (juce::Point<float> position) const
{
    const auto layout = BandPanelLayout::make ((float) getWidth(), (float) getHeight(), state.panelOpen ? 1.0f : 0.0f);
    return layout.hit (position, state.panelOpen, state.bands.find (state.selected) != nullptr);
}

void SphereView::updateCursorLocked()
{
    using Kind = PanelTarget::Kind;

    if (knobDrag.has_value())
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (state.panelHover.kind == Kind::knob || state.panelHover.kind == Kind::background)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    else if (state.panelHover.kind != Kind::none)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (state.helpHovered)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    else if (gainDrag.has_value())
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (state.dragged < 0 && ! state.rotating && ! panPosition.has_value() && state.hovered >= 0)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

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
    state.lastInteraction = nowSeconds();

    if (state.dragged < 0)
        state.hovered = -1;
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
                    band->q = std::min (band->q, threedq::maxShelfQ);
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
            change = -(float) steps / (float) (threedq::cutSlopes.size() - 1);
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
    if (overHelpButton (e.position) || panelMouseDown (e))
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
    if (panelPointerMoved (e))
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
    state.hovered = state.helpHovered || state.panelHover.kind != PanelTarget::Kind::none ? -1 : hitBand (toDevice (e.position), cameraLocked());
    updateCursorLocked();
}

void SphereView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown() || overHelpButton (e.position) || panelDoubleClick (e))
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
                band->q = std::min (band->q, threedq::maxShelfQ);
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

    if (panelWheel (e, delta))
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
                    const auto& slopes = threedq::cutSlopes;
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
                band->q = std::clamp (band->q * std::exp (-delta * 0.002f), minQ, band->isShelf() ? threedq::maxShelfQ : maxQ);
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

//==============================================================================
void SphereView::newOpenGLContextCreated()
{
    context.setSwapInterval (1);
    renderer.create();
    lastFrameTime = nowSeconds();
}

void SphereView::openGLContextClosing()
{
    renderer.release();
}

void SphereView::renderOpenGL()
{
    const double frameStart = nowSeconds();
    const float dt = (float) std::clamp (frameStart - lastFrameTime, 0.001, 0.1);
    lastFrameTime = frameStart;
    const float frameScale = dt * 60.0f; // per-frame constants were tuned at 60 fps

    dpr = (float) context.getRenderingScale();
    publishedScale = dpr;
    const int width = juce::roundToInt (componentWidth.load() * dpr);
    const int height = juce::roundToInt (componentHeight.load() * dpr);

    if (width <= 0 || height <= 0)
        return;

    if (readLiveAudio != nullptr)
        readLiveAudio (live);

    BandSet bands;
    Camera camera;
    int hovered, dragged, selected, soloed, activeKnob;
    bool helpHovered, panelOpen;
    float panelAmount;
    PanelTarget panelHover;

    {
        const std::lock_guard lock (stateLock);

        for (auto& b : state.bands)
        {
            b.hasLiveGain = b.range != 0.0f && ! b.muted;
            b.liveGainDb = live.gainDb[(size_t) b.id];
            state.liveLevelDb[(size_t) b.id] = live.levelDb[(size_t) b.id];
        }

        // Inertia after a drag, then a slow auto-rotation that only runs while
        // the pointer is away. It stops quickly and eases back in slowly.
        const bool holding = state.rotating || state.dragged >= 0 || state.hovered >= 0;
        const float autoTarget = ! state.pointerInside && ! holding && frameStart - state.lastInteraction > autoResumeDelay ? 1.0f : 0.0f;
        autoBlend += (autoTarget - autoBlend) * (1.0f - std::exp (-dt / (autoTarget > autoBlend ? 1.2f : 0.12f)));

        if (! state.rotating)
        {
            state.yaw += state.yawVelocity * frameScale;
            state.pitch += state.pitchVelocity * frameScale;
            const float friction = state.hovered >= 0 ? 0.8f : 0.94f; // dies faster over a node so it can be grabbed
            state.yawVelocity *= std::pow (friction, frameScale);
            state.pitchVelocity *= std::pow (friction, frameScale);
            state.yaw += autoRotate * autoBlend * frameScale;
        }

        state.pitch = std::clamp (state.pitch, -1.4f, 1.4f);
        clampPanLocked(); // the window may have shrunk

        if (state.recentring)
        {
            const float keep = std::exp (-dt / 0.09f);
            auto& fit = state.panelFit;
            const auto& goal = state.recentreFit;
            state.panX *= keep;
            state.panY *= keep;
            fit.zoomScale = goal.zoomScale + (fit.zoomScale - goal.zoomScale) * keep;
            fit.panDeltaY = goal.panDeltaY + (fit.panDeltaY - goal.panDeltaY) * keep;
            state.restCentre = state.recentreRest + (state.restCentre - state.recentreRest) * keep;

            if (std::hypot (state.panX, state.panY) < 0.5f && std::abs (fit.panDeltaY - goal.panDeltaY) < 0.5f
                && std::abs (state.restCentre - state.recentreRest) < 0.002f)
            {
                state.panX = state.panY = 0;
                fit = goal;
                state.restCentre = state.recentreRest;
                state.recentring = false;
            }
        }

        state.yaw = std::remainder (state.yaw, twoPi);

        bands = state.bands;
        hovered = state.hovered;
        dragged = state.dragged;
        helpHovered = state.helpHovered;
        // A closed panel leaves this alone, so only a clicked band gets highlighted.
        if (state.panelOpen)
            validateSelectionLocked();

        selected = state.selected;
        soloed = state.soloHeld >= 0 ? state.soloHeld : state.soloLatched;
        panelOpen = state.panelOpen;
        panelHover = state.panelHover;
        activeKnob = knobDrag.has_value() ? knobDrag->knob : -1;

        const float panelGoal = panelOpen ? 1.0f : 0.0f;
        state.panelAmount += (panelGoal - state.panelAmount) * (1.0f - std::exp (-dt / 0.08f));
        if (std::abs (panelGoal - state.panelAmount) < 0.002f)
            state.panelAmount = panelGoal;

        panelAmount = state.panelAmount;
        camera = makeCameraLocked ((float) width, (float) height, dpr);
    }

    sprites.clear();
    field.update (bands, camera, live.spectrum, frameStart, dt, sprites);

    glViewport (0, 0, width, height);
    glClearColor (17.0f / 255.0f, 17.0f / 255.0f, 16.0f / 255.0f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);

    const auto extent = surfaceExtent (bands, camera, 18, 36); // every frame, so a coarse grid

    if (! shownExtent.has_value())
        shownExtent = extent;

    const float ease = 1.0f - std::exp (-dt / 0.12f);
    shownExtent->top += (extent.top - shownExtent->top) * ease;
    shownExtent->bottom += (extent.bottom - shownExtent->bottom) * ease;
    shownExtent->halfWidth += (extent.halfWidth - shownExtent->halfWidth) * ease;

    Backdrop backdrop;
    backdrop.radius = camera.radiusPx();
    backdrop.centreX = camera.centreX();
    backdrop.sphereY = camera.centreY();
    backdrop.lightY = backdrop.sphereY - (shownExtent->top + shownExtent->bottom) / 2.0f * backdrop.radius;
    backdrop.shadowY = backdrop.sphereY + (0.17f - shownExtent->bottom) * backdrop.radius;
    backdrop.shadowHalfWidth = 0.9f * shownExtent->halfWidth * backdrop.radius;
    backdrop.haze = field.getHazeLevels();
    renderer.drawBackdrop (backdrop, width, height);
    renderer.draw (sprites, width, height);

    {
        auto overlay = juce::createOpenGLGraphicsContext (context, width, height);
        juce::Graphics g (*overlay);
        drawOverlay (g, bands, camera, hovered, dragged, selected, dt);
        drawTitle (g, soloed);
        drawBandPanel (g, BandPanelLayout::make (camera.width / dpr, camera.height / dpr, panelAmount), dpr, panelAmount,
                       bands.find (selected), soloed >= 0 && soloed == selected, panelHover, activeKnob);
        drawHelp (g, camera, helpHovered, dt);
    }

    if (logStats)
    {
        statsCpu += nowSeconds() - frameStart;

        if (++statsFrames, frameStart - statsStart >= 1.0)
        {
            if (statsStart > 0)
                std::fprintf (stderr, "3DQ %dx%d  %.1f fps  %.2f ms/frame before swap  %d sprites\n", width, height,
                              statsFrames / (frameStart - statsStart), statsCpu * 1000.0 / statsFrames, sprites.size());
            statsStart = frameStart;
            statsCpu = 0;
            statsFrames = 0;
        }
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

void SphereView::drawTitle (juce::Graphics& g, int soloed)
{
    auto title = uiFont (22.0f * dpr, true);
    title.setExtraKerningFactor (0.09f);

    g.setFont (title);
    g.setColour (textColour);
    const int x = juce::roundToInt (20.0f * dpr), baseline = juce::roundToInt (16.0f * dpr + title.getAscent());
    g.drawSingleLineText ("3DQ", x, baseline);

    // Solo changes what you hear, so it stays on show with the panel closed.
    if (soloed >= 0)
    {
        g.setFont (uiFont (12.0f * dpr));
        g.setColour (textColour.withAlpha (0.85f));
        g.drawSingleLineText ("Solo  " + juce::String::charToString ((juce::juce_wchar) 0xb7) + "  Band " + juce::String (soloed + 1),
                              x + juce::roundToInt (juce::GlyphArrangement::getStringWidth (title, "3DQ") + 14.0f * dpr), baseline);
    }
}

// The ? button, and the list of controls that appears while hovering over it.
void SphereView::drawHelp (juce::Graphics& g, const Camera& camera, bool hovered, float dt)
{
    helpAmount += ((hovered ? 1.0f : 0.0f) - helpAmount) * (1.0f - std::exp (-dt / 0.08f));

    const float unit = dpr * panelScale (camera.width / dpr, camera.height / dpr); // one scaled point, in device pixels
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
