/*
  ==============================================================================

    SphereView: the sphere as a native JUCE component. It owns the OpenGL
    context, the band nodes and labels, and every mouse gesture:

      drag empty space        rotate
      scroll empty space      zoom
      hold wheel and drag     pan the view
      double-click background recentre the view
      double-click sphere     add a band there
      drag a node             move it (height = frequency, around = stereo position)
      Alt+hold a node         solo it while held; drag to sweep it
      scroll on a node        gain
      Ctrl/Cmd+drag a node    gain
      Shift+scroll on node    width (Q); on a cut, its slope
      Alt+scroll on node      dynamic range
      Ctrl/Cmd+scroll on node threshold
      double-click a node     remove it
      Shift+double-click      cycle peak -> low cut -> high cut -> low shelf -> high shelf
      right-click a node      mute
      Shift+right-click node  stereo / mono (both channels, or its one stereo position)

    Hovering the ? button in the bottom-right corner lists these. The ^ button
    at the bottom centre slides up a panel of controls for the selected band.
    Along the top are the preset menu, undo and A/B.

    Threads: mouse events arrive on the message thread and rendering runs on
    OpenGL's own thread, so a busy host UI can't slow the sphere. `state` is
    shared between them and always accessed under `stateLock`, held only long
    enough to read or change it.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SphereField.h"
#include "SpriteRenderer.h"
#include "BandPanel.h"
#include "PresetBar.h"

class SphereView  : public juce::Component,
                    private juce::OpenGLRenderer
{
public:
    // What the view shows from the audio side; filled in on the render thread.
    struct LiveAudio
    {
        StereoSpectrum::Result spectrum;
        std::array<float, sphere::maxBands> gainDb {}, levelDb {};
    };

    SphereView();
    ~SphereView() override;

    // Set these before the view is shown.
    std::function<void (const sphere::BandSet&)> onBandsEdited; // message thread
    std::function<void (LiveAudio&)> readLiveAudio;             // render thread
    std::function<void (int band)> onSoloChanged;               // message thread; -1 = none
    std::function<void (sphere::PresetCommand, int preset)> onPresetCommand; // message thread

    // Bands pushed from outside (host automation, preset load). Ignored during a drag.
    void setBands (const sphere::BandSet& bands);

    // The same, for a new EQ as a whole (preset, undo, A/B): the view eases over
    // to centre the new shape.
    void showNewBands (const sphere::BandSet& bands);

    void setPresetInfo (sphere::PresetBarInfo);

    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct RotateSample
    {
        double time;
        float dx, dy;
    };

    struct Press
    {
        juce::Point<float> position;
        double time = 0;
        int band = -1;
        float offsetX = 0, offsetY = 0, radius = 1;
    };

    struct GainDrag
    {
        int band = -1;
        float startY = 0, startGain = 0;
    };

    struct KnobDrag
    {
        int knob = 0;
        float lastY = 0, position = 0;
    };

    struct SharedState
    {
        sphere::BandSet bands;
        float yaw = 0, pitch = -0.35f, zoom = 0.905f, yawVelocity = 0, pitchVelocity = 0;
        float panX = 0, panY = 0; // component pixels
        bool recentring = false;
        sphere::PanelViewFit recentreFit; // where a recentre ends up with the panel open
        // Panel closed: how far below the window's centre the sphere's centre sits,
        // in sphere radii, so the EQ'd shape is what looks centred.
        float restCentre = 0, recentreRest = 0;
        bool restCentrePlaced = false, easingRestCentre = false;
        bool rotating = false, pointerInside = false, helpHovered = false, panelOpen = false;
        sphere::PanelTarget panelHover;
        sphere::PresetTarget presetHover;
        std::shared_ptr<const sphere::PresetBarInfo> presetInfo = std::make_shared<sphere::PresetBarInfo>();
        bool presetMenuOpen = false;
        int presetMenuScroll = 0;
        int selected = -1; // the band last clicked: highlighted, and what the panel controls
        int soloLatched = -1, soloHeld = -1; // from the panel's S button, and Alt+hold on a node
        float panelAmount = 0; // eased 0..1, as the panel slides
        sphere::PanelViewFit panelFit;
        int hovered = -1, dragged = -1;
        std::array<float, sphere::maxBands> liveLevelDb {};
        double lastInteraction = 0;
    };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // Callers hold stateLock for all of these.
    sphere::Camera cameraLocked() const;
    sphere::Camera makeCameraLocked (float width, float height, float scale) const;
    void updatePanelFitLocked();
    sphere::PanelViewFit panelFitLocked (float panY) const;
    float restCentreLocked() const;
    sphere::Vec3 bandScreenPos (const sphere::Band&, const sphere::BandDirections&, const sphere::Camera&) const;
    float handleRadius (const sphere::Band&, const sphere::BandDirections&) const;
    int hitBand (juce::Point<float> devicePosition, const sphere::Camera&) const;
    void updateCursorLocked();
    void clampPanLocked();
    void validateSelectionLocked();
    void selectNeighbourLocked (int direction);
    sphere::PanelTarget panelTargetLocked (juce::Point<float> position) const;
    sphere::PresetBarLayout presetLayoutLocked() const;
    bool overHelpButton (juce::Point<float> position) const;

    void pointerMoved (const juce::MouseEvent&);

    // Each returns true when the preset bar or its menu took the event.
    bool presetMouseDown (const juce::MouseEvent&);
    bool presetPointerMoved (const juce::MouseEvent&);
    bool presetWheel (const juce::MouseEvent&, float delta);

    // Each returns true when the panel took the event.
    bool panelMouseDown (const juce::MouseEvent&);
    bool panelPointerMoved (const juce::MouseEvent&);
    bool panelDoubleClick (const juce::MouseEvent&);
    bool panelWheel (const juce::MouseEvent&, float delta);

    void notifyBandsEdited (const sphere::BandSet&);
    void publishSolo();
    juce::Point<float> toDevice (juce::Point<float> position) const;

    void drawOverlay (juce::Graphics&, const sphere::BandSet&, const sphere::Camera&, int hovered, int dragged, int selected, float dt);
    void drawNode (juce::Graphics&, const sphere::Band&, sphere::Vec3 screen, float emphasis, bool selected);
    void drawLabel (juce::Graphics&, const sphere::Band&, sphere::Vec3 screen, const sphere::Camera&, float liveLevelDb);
    void drawTitle (juce::Graphics&, int soloed);
    void drawHelp (juce::Graphics&, const sphere::Camera&, bool hovered, float dt);

    juce::OpenGLContext context;
    sphere::SphereField field;
    sphere::SpriteRenderer renderer;
    sphere::SpriteList sprites;
    LiveAudio live;

    // Render thread only.
    double lastFrameTime = 0;
    float autoBlend = 0;
    std::optional<sphere::SurfaceExtent> shownExtent; // eased, as the surface's springs ease
    float helpAmount = 0, presetMenuAmount = 0;
    std::array<float, sphere::maxBands> nodeEmphasis {};
    float dpr = 1;
    double statsStart = 0, statsCpu = 0;
    int statsFrames = 0;
    const bool logStats;

    std::atomic<float> componentWidth { 1 }, componentHeight { 1 }, publishedScale { 1 };

    std::mutex stateLock;
    SharedState state;

    // Message thread only (mouse gesture state; read under stateLock when it affects drawing).
    std::optional<Press> pressed;
    std::optional<GainDrag> gainDrag;
    std::optional<juce::Point<float>> panPosition; // last pointer position while panning
    std::optional<KnobDrag> knobDrag;
    juce::Point<float> lastPosition;
    std::vector<RotateSample> rotateSamples;
    int wheelBand = -1;
    double lastWheelTime = 0;
    float slopeAccumulator = 0;
    int publishedSolo = -1;
    bool presetTookPress = false; // so the second click of a double-click there can't reach the sphere

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SphereView)
};
