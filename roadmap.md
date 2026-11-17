# 3DQ roadmap

Work through the phases in order. Each phase lands as its own commit or commits, with the tests passing (`test/builds/linux_makefile`, `./build/3dq_tests`). Tick items off as they land and note the commit next to them.

## Phase 0: Housekeeping

- [ ] Set `companyWebsite` in `3dq.jucer` once the site has a URL (it is a single space now)

## Phase 1: Open review bugs

Done; see below.

## Phase 2: Accurate filters

Done; see below.

## Phase 3: An accurate sphere

Done; see below.

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

- [x] Phase 3: bells on the sphere follow their real response, round, with the rings and colour from the same shape; per-bell lookup tables keep it cheap (414aa82). A Q 4 bell half an octave out draws at its real height, not 72% of its peak. Above Q 4 the drawn width narrows more slowly (Q 40 draws as Q 8) so it never falls between particles
- [x] Phase 2: bells, shelves, high cuts and the listen filters keep their analog shape up to Nyquist, with tests against the analog curves at 44.1, 48 and 96 kHz (2f1786c). A +12 dB bell at 15 kHz is +9.6 dB at 18 kHz (analog +10.3, was +4.6). Saved sessions sound slightly different above about 8 kHz
- [x] Phase 1: all eight review bugs fixed, with tests for the meter, panel hits and track colours (6b0ac5d). The meters hold peaks at any sample rate and start fresh on open; gain-field clicks close panel menus and give way to the preset menu; hidden panel controls never hit; alpha-0 track colours show; the track is passed on only when it changes
- [x] Phase 0: removed the `spikes_*.png` screenshots from the repo root (moved to the trash; the site has its own copies)
- [x] Phase 0: the SC button can't be switched on without a sidechain signal, but can always be switched off (dafe7fa)
