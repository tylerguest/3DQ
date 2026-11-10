/*
  ==============================================================================

    SphereView: see sphere_view.h. Set-up, shared state and the render loop; mouse
    input is in sphere_view_input.cpp and the 2D overlay in sphere_view_overlay.cpp.

  ==============================================================================
*/

#include "sphere_view_detail.h"

// Stock juce_opengl deadlocks on Linux when a second plugin instance opens; see patches/.
#if JUCE_LINUX && ! defined (THREEDQ_PATCHED_JUCE_OPENGL)
 #error "Building against unpatched juce_opengl: use the copy in this repository's modules folder"
#endif

using namespace juce::gl;

namespace threedq
{

using namespace view;

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

void SphereView::showNewBands (const BandSet& bands)
{
    setBands (bands);

    const std::lock_guard lock (stateLock);
    state.recentreRest = restCentreLocked();
    state.easingRestCentre = true;
}

void SphereView::setPresetInfo (PresetInfo info)
{
    auto shared = std::make_shared<const PresetInfo> (std::move (info));

    const std::lock_guard lock (stateLock);
    state.presetInfo = std::move (shared);
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
    const float scale = uiScale ((float) getWidth(), (float) getHeight());
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

PresetBarLayout SphereView::presetLayoutLocked() const
{
    return PresetBarLayout::make ((float) getWidth(), (float) getHeight(), state.presetInfo->presets.size(), state.presetMenuScroll);
}

void SphereView::updateCursorLocked()
{
    using Kind = PanelTarget::Kind;

    if (knobDrag.has_value())
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (state.presetHover.kind == PresetTarget::Kind::menuBackground)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    else if (state.presetHover.kind != PresetTarget::Kind::none)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
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
    PresetTarget presetHover;
    std::shared_ptr<const PresetInfo> presetInfo;
    bool presetMenuOpen;
    PresetBarLayout presetLayout;

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

        if (state.easingRestCentre && ! state.recentring)
        {
            state.restCentre = state.recentreRest + (state.restCentre - state.recentreRest) * std::exp (-dt / 0.12f);

            if (std::abs (state.restCentre - state.recentreRest) < 0.002f)
            {
                state.restCentre = state.recentreRest;
                state.easingRestCentre = false;
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
        presetHover = state.presetHover;
        presetInfo = state.presetInfo;
        presetMenuOpen = state.presetMenuOpen;
        presetLayout = PresetBarLayout::make (componentWidth.load(), componentHeight.load(), presetInfo->presets.size(), state.presetMenuScroll);
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
        presetMenuAmount += ((presetMenuOpen ? 1.0f : 0.0f) - presetMenuAmount) * (1.0f - std::exp (-dt / 0.06f));
        drawPresetBar (g, presetLayout, dpr, *presetInfo, presetHover, presetMenuAmount);
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

} // namespace threedq
