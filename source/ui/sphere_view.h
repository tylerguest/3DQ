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
      click a value           type a new one (Return sets it, Escape doesn't)

    Hovering the ? button in the bottom-right corner lists these. While there
    are bands, a panel of controls for the selected one floats above the bottom
    bar; a click on empty space puts it away, and picking a band brings it back.
    Along the top are the preset menu, undo and A/B.

    Threads: mouse events arrive on the message thread and rendering runs on
    OpenGL's own thread, so a busy host UI can't slow the sphere. `state` is
    shared between them and always accessed under `stateLock`, held only long
    enough to read or change it.

    Source files: sphere_view.cpp (set-up, state, render loop),
    sphere_view_input.cpp (mouse and wheel) and sphere_view_overlay.cpp (nodes,
    labels, title, help).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../render/sphere_field.h"
#include "../render/sprite_renderer.h"
#include "band_panel.h"
#include "preset_bar.h"

namespace threedq
{

class SphereView  : public juce::Component,
                    private juce::OpenGLRenderer
{
public:
    // What the view shows from the audio side; filled in on the render thread.
    struct LiveAudio
    {
        StereoSpectrum::Result spectrum;
        StereoSpectrum::Result sidechain; // valid when hasSidechain
        bool hasSidechain = false;
        std::array<float, maxBands> gainDb {}, levelDb {};
        std::array<float, 2> outputPeakDb { -120.0f, -120.0f }; // L, R
        std::array<float, 2> inputPeakDb { -120.0f, -120.0f }; // L, R: after the input gain
        std::array<float, 2> trimGainDb {};                     // input, output
    };

    SphereView();
    ~SphereView() override;

    // Set these before the view is shown.
    std::function<void (const BandSet&)> onBandsEdited; // message thread
    std::function<void (LiveAudio&)> readLiveAudio;             // render thread
    std::function<void (int band)> onSoloChanged;               // message thread; -1 = none
    std::function<void (PresetCommand, int preset)> onPresetCommand; // message thread
    // The input (which = 0) and output (1) gains in dB, all on the message
    // thread. Edits come between a gesture's start (true) and end (false).
    std::function<float (int which)> getTrimGain;
    std::function<void (int which, float db)> onTrimGainEdited;
    std::function<void (int which, bool starting)> onTrimGainGesture;

    // Bands pushed from outside (host automation, preset load). Ignored during a drag.
    void setBands (const BandSet& bands);

    // The same, for a new EQ as a whole (preset, undo, A/B): the view eases over
    // to centre the new shape.
    void showNewBands (const BandSet& bands);

    void setPresetInfo (PresetInfo);

    // The host's track, shown in the middle of the bottom bar. Message thread.
    void setTrack (const juce::String& name, std::optional<juce::Colour> colour);

    // A host's track colour (ARGB) as one to show. Some hosts leave the alpha
    // at 0 for a real colour, so it's made opaque; all zeros means none.
    static std::optional<juce::Colour> hostTrackColour (std::optional<juce::uint32> argb);

    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusLost (FocusChangeType) override;

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
        bool fromValue = false; // pressed on its value: it waits to be dragged, as a click there types one
        float startY = 0;
        bool moved = false;
    };

    struct SharedState
    {
        BandSet bands;
        float yaw = 0, pitch = -0.35f, zoom = 0.905f, yawVelocity = 0, pitchVelocity = 0;
        float panX = 0, panY = 0; // component pixels
        bool recentring = false;
        PanelViewFit recentreFit; // where a recentre ends up with the panel open
        // Panel closed: how far below the window's centre the sphere's centre sits,
        // in sphere radii, so the EQ'd shape is what looks centred.
        float restCentre = 0, recentreRest = 0;
        bool restCentrePlaced = false, easingRestCentre = false;
        bool rotating = false, pointerInside = false, helpHovered = false, panelOpen = false;
        int trimHovered = -1, trimDragging = -1; // which gain field: 0 input, 1 output
        bool panelPlaced = false; // the panel is shown or not from the first frame, without sliding
        bool panelHidden = false; // clicked away; picking a band brings it back
        juce::String trackName;
        std::optional<juce::Colour> trackColour;
        PanelTarget panelHover;
        PresetTarget presetHover;
        std::shared_ptr<const PresetInfo> presetInfo = std::make_shared<PresetInfo>();
        bool presetMenuOpen = false;
        int presetMenuScroll = 0;
        int selected = -1; // the band last clicked: highlighted, and what the panel controls
        int soloLatched = -1, soloHeld = -1; // from the panel's S button, and Alt+hold on a node
        float panelAmount = 0; // eased 0..1, as the panel slides
        PanelPopup panelPopup = PanelPopup::none; // a menu open over the panel
        PanelViewFit panelFit;
        int hovered = -1, dragged = -1;
        std::array<float, maxBands> liveLevelDb {};
        bool sidechainConnected = false; // the host is feeding the sidechain input
        std::optional<ValueEdit> valueEdit; // a value being typed in
        double lastInteraction = 0;
        bool clicked = false; // a click anywhere stops the auto-rotation until the pointer has gone
    };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // Callers hold stateLock for all of these.
    Camera cameraLocked() const;
    Camera makeCameraLocked (float width, float height, float scale) const;
    void updatePanelFitLocked();
    PanelViewFit panelFitLocked (float panY) const;
    float restCentreLocked() const;
    Vec3 bandScreenPos (const Band&, const BandDirections&, const Camera&) const;
    float handleRadius (const Band&, const BandDirections&) const;
    int hitBand (juce::Point<float> devicePosition, const Camera&) const;
    void updateCursorLocked();
    void clampPanLocked();
    void validateSelectionLocked();
    void selectNeighbourLocked (int direction);
    PanelTarget panelTargetLocked (juce::Point<float> position) const;
    PresetBarLayout presetLayoutLocked() const;
    bool overHelpButton (juce::Point<float> position) const;
    int trimFieldAt (juce::Point<float> position) const; // -1 when over neither
    // The gain field the pointer works on: none while the preset menu is open,
    // which takes the pointer first.
    int trimTargetLocked (juce::Point<float> position) const;
    // Closes the band panel's open menu, if any. Returns whether one was open.
    bool dismissPanelPopupLocked();
    void editTrimGain (int which, float db, bool wholeGesture);

    // Typing a value: a click on a band knob's value (knob) or a gain field
    // (trim) opens a field there, once it's clear the click isn't the first
    // of a double-click (which resets instead). Keys go to the field until
    // Return or a click elsewhere sets it, or Escape drops it.
    void scheduleValueEdit (int knob, int trim);
    void beginValueEdit (int knob, int trim);
    void endValueEdit (bool commit);

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

    void notifyBandsEdited (const BandSet&);
    void publishSolo();
    juce::Point<float> toDevice (juce::Point<float> position) const;

    void drawOverlay (juce::Graphics&, const BandSet&, const Camera&, int hovered, int dragged, int selected, float dt);
    // levelDb: what a dynamic band's detector hears right now.
    void drawNode (juce::Graphics&, const Band&, Vec3 screen, float emphasis, bool selected, float levelDb);
    void drawLabel (juce::Graphics&, const Band&, Vec3 screen, const Camera&, float liveLevelDb);
    void drawTitle (juce::Graphics&, const Camera&, int soloed);
    void drawBottomBar (juce::Graphics&, const Camera&, int trimHot, const juce::String& trackName, std::optional<juce::Colour> trackColour,
                        const std::optional<ValueEdit>& edit);
    void drawHelp (juce::Graphics&, const Camera&, bool hovered, float dt);
    void drawLevelMeter (juce::Graphics&, const Camera&, float dt);

    juce::OpenGLContext context;
    SphereField field;
    SpriteRenderer renderer;
    SpriteList sprites;
    LiveAudio live;

    // Render thread only.
    double lastFrameTime = 0;
    float autoBlend = 0;
    std::optional<SurfaceExtent> shownExtent; // eased, as the surface's springs ease
    float helpAmount = 0, presetMenuAmount = 0;
    std::array<float, maxBands> nodeEmphasis {};
    // The GR meter, per band slot: the held peak of the dynamic movement, when
    // it was caught, and what's shown (eased toward the peak).
    std::array<float, maxBands> peakMovement {}, shownMovement {};
    std::array<double, maxBands> peakTime {};
    // The level meter, per channel: the output level shown (it falls back
    // slowly) and the input's behind it, the output's held peak and when it
    // was caught, and when the output last went over 0 dB.
    std::array<float, 2> meterLevel { -120.0f, -120.0f }, meterInputLevel { -120.0f, -120.0f }, meterPeak { -120.0f, -120.0f };
    std::array<double, 2> meterPeakTime {}, meterClipTime { -10.0, -10.0 };
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
    std::optional<float> trimDragY; // last pointer y while dragging a gain field
    int trimDragWhich = 0;
    float trimDragDb = 0, trimDragStartY = 0;
    bool trimDragMoved = false;
    int valueEditGeneration = 0; // a press cancels a value edit still waiting to open
    juce::Point<float> lastPosition;
    std::vector<RotateSample> rotateSamples;
    int wheelBand = -1;
    double lastWheelTime = 0;
    float slopeAccumulator = 0;
    int publishedSolo = -1;
    bool presetTookPress = false; // so the second click of a double-click there can't reach the sphere

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SphereView)
};

} // namespace threedq
