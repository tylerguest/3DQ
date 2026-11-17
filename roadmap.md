# 3DQ roadmap

Work through the phases in order. Each phase lands as its own commit or commits, with the tests passing (`test/builds/linux_makefile`, `./build/3dq_tests`). Tick items off as they land and note the commit next to them.

## Phase 0: Housekeeping

- [ ] Set `companyWebsite` in `3dq.jucer` once the site has a URL (it is a single space now)

## Phase 1: Open review bugs

Meters:
- [ ] The output meter reads only the last 2048 samples each frame, so it misses peaks at 96 kHz and above. Read everything since the last frame (`plugin_editor.cpp`, `meterWindow`)
- [ ] The input peak builds up while the editor is closed. Clear it when the editor opens (`takeInputPeak`)

Clicks and wheel (`sphere_view_input.cpp`):
- [ ] A click on a gain field leaves an open type or dynamics menu up. Close it, as every other click outside the panel does
- [ ] The gain fields and the preset menu disagree over the wheel and the hover highlight. Use one rule in mouse move, mouse up and the wheel

Band panel (`band_panel.cpp`):
- [ ] `hit()` still returns hidden controls: the position control on stereo bands and the gear on cuts
- [ ] Whether the gain value is drawn depends on `dim == 1.0f`. Use `knobEnabled` instead

Host:
- [ ] Accept host track colours sent with alpha 0 (`sphere_view_overlay.cpp`)
- [ ] Call `setTrack` only when the track name or colour changes, not on every timer tick (`plugin_editor.cpp`)

Add a test for each fix that the suite can reach.

## Phase 2: Accurate filters

- [ ] Replace the RBJ bell with a design that matches the analog shape up to Nyquist, at no extra CPU. Now a +12 dB bell at 15 kHz at 44.1 kHz is +4.6 dB at 18 kHz where it should be +10.3
- [ ] Do the same for the shelves
- [ ] Check the cuts near 20 kHz and the band-passes the dynamics and solo listen through
- [ ] Tests: each filter type within tolerance of its analog curve at 44.1, 48 and 96 kHz
- Note: saved sessions will sound slightly different above about 8 kHz

## Phase 3: An accurate sphere

- [ ] Draw bell bumps from the real filter response along frequency, as shelves and cuts already are. Now a Q 4 bell at half an octave away draws at 72% of its peak where the filter is at 13%
- [ ] Keep it cheap: compute each band's response once per frame for each frequency row, then look it up per particle
- [ ] Update `test_bands.cpp` for the new shape
- [ ] Check visually that the bulge, the colour and the spikes agree at Q 0.5, 1 and 4

## Phase 4: Workflow

- [ ] Keyboard: undo and redo, delete the selected band, Escape to close menus or deselect, arrows to nudge
- [ ] Fade the host's bypass instead of switching abruptly
- [ ] Double-click a knob to reset it; click a value to type a number (if not there already)
- [ ] Optional: auto-gain
- [ ] Optional: screen-reader names for the controls
- Deferred until after release: linear phase (adds latency and a second engine)

## Phase 5: Shipping

- [ ] Decide which platforms and formats ship, and how the plugin is sold or distributed
- [ ] Mac and Windows exporters. Find out whether `patches/juce_9.0.5_linux_opengl_multi_instance.patch` is needed there
- [ ] AU for Logic; CLAP optional
- [ ] CI: build all platforms, run the tests and pluginval on every push
- [ ] Release: version numbers, Mac signing and notarisation, installers
- [ ] Site (`../3dq-site`): sharp high-resolution screenshots after Phases 2 and 3, and a real link behind "Coming soon"

## Done

Move finished items here with their commit, newest first.

- [x] Phase 0: removed the `spikes_*.png` screenshots from the repo root (moved to the trash; the site has its own copies)
- [x] Phase 0: the SC button can't be switched on without a sidechain signal, but can always be switched off (dafe7fa)
