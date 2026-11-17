// The band controls: one compact panel, with Gain rising out of its top. Knobs
// show their name; hovering or dragging shows the value, and clicking it types
// a new one. Drag up and down (Shift for fine), scroll, double-click to reset.
// Gain's outer ring is the dynamic range and its inner ring the threshold.
"use strict";

(() => {
  const SVG = "http://www.w3.org/2000/svg";
  const START = -0.75 * Math.PI, SWEEP = 1.5 * Math.PI; // 7:30 to 4:30, clockwise
  const DRAG_PIXELS = 220, FINE = 0.2;
  const W = 456, H = 124, BODY_TOP = 26; // the panel, and where its body starts below Gain's collar
  const GAIN_SIZE = 100, GAIN_TOP = 2, COLLAR = 52; // Gain's surround, and the collar round it

  const el = (tag, attrs = {}, children = []) => {
    const e = tag.startsWith("svg:") ? document.createElementNS(SVG, tag.slice(4)) : document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === "class") e.setAttribute("class", v);
      else if (k === "text") e.textContent = v;
      else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
      else e.setAttribute(k, v);
    }
    for (const c of children) e.append(c);
    return e;
  };
  const icon = (d, w, h, cls = "") => el("svg:svg", { class: cls, viewBox: `0 0 ${w} ${h}`, fill: "none", stroke: "currentColor",
                                                     "stroke-width": 1.5, "stroke-linecap": "round", "stroke-linejoin": "round" },
                                         [el("svg:path", { d })]);

  const polar = (c, r, a) => [c + r * Math.sin(a), c - r * Math.cos(a)];
  const arcPath = (c, r, a0, a1) => {
    if (Math.abs(a1 - a0) < 0.002) return "";
    const lo = Math.min(a0, a1), hi = Math.max(a0, a1);
    const [x0, y0] = polar(c, r, lo), [x1, y1] = polar(c, r, hi);
    return `M${x0.toFixed(2)} ${y0.toFixed(2)} A${r} ${r} 0 ${hi - lo > Math.PI ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
  };
  const angleOf = (t) => START + clamp(t, 0, 1) * SWEEP;
  const signColour = (v) => (v < 0 ? EQ.cut : EQ.boost);
  const enabledFor = (def, b) => !def.enabled || def.enabled(b);

  const glyphs = {
    peak: "M1 10 C6 10 7 2 10 2 C13 2 14 10 19 10",
    lowCut: "M1 11 C3 11 5 3 9 3 L19 3",
    highCut: "M1 3 L11 3 C15 3 17 11 19 11",
    lowShelf: "M1 3 L5 3 C9 3 10 9 14 9 L19 9",
    highShelf: "M1 9 L5 9 C9 9 10 3 14 3 L19 3",
  };
  const glyph = (type) => icon(glyphs[type], 20, 12, "glyph");
  const caret = () => icon("M1 2.5 L3.5 5 L6 2.5", 7, 7, "caret");

  let active = null;  // the control being dragged
  let openPop = null; // the menu or popover open over the panel: { name, el }

  // Typing a value: an input in place of the label. Return or clicking away
  // sets it, Escape drops it; a second click straight away is a double-click,
  // which drops it and resets instead.
  const editValue = (label, def, getBand = () => store.band()) => {
    const band = getBand();
    if (!band || !enabledFor(def, band)) return;
    const original = def.text(band);
    const input = el("input", { class: "value-input", value: original, spellcheck: "false" });
    const opened = performance.now();
    let done = false;
    const finish = (commit) => {
      if (done) return;
      done = true;
      const b = getBand();
      if (commit && b && input.value !== original && def.type(b, input.value)) store.changed();
      input.replaceWith(label);
      update();
    };
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") finish(true);
      else if (e.key === "Escape") finish(false);
      e.stopPropagation();
    });
    input.addEventListener("blur", () => finish(true));
    input.addEventListener("pointerdown", (e) => {
      e.stopPropagation();
      if (performance.now() - opened < 350) { finish(false); reset(def); }
    });
    label.replaceWith(input);
    input.focus();
    input.select();
  };

  const reset = (def) => {
    const band = store.band();
    if (!band || !enabledFor(def, band)) return;
    def.reset(band);
    store.changed();
  };

  // A vertical drag in steps of the control's range; a stepped control (a
  // cut's slope) moves a notch at a time.
  const dragKnob = (target, def, e) => {
    const band = store.band();
    if (!band || !def || !enabledFor(def, band)) return;
    e.preventDefault();
    target.setPointerCapture(e.pointerId);
    let lastY = e.clientY, position = def.get(band), accumulated = 0;
    const steps = def.steps ? def.steps(band) : 0;
    active = def;
    update();
    const move = (m) => {
      const delta = (lastY - m.clientY) / DRAG_PIXELS * (m.shiftKey ? FINE : 1);
      lastY = m.clientY;
      if (steps) {
        accumulated += delta * 2.5;
        const n = Math.trunc(accumulated * (steps - 1));
        if (!n) return;
        accumulated -= n / (steps - 1);
        position = clamp(position + n / (steps - 1), 0, 1);
      } else {
        position = clamp(position + delta, 0, 1);
      }
      def.set(store.band(), position);
      store.changed();
    };
    const up = () => {
      target.removeEventListener("pointermove", move);
      target.removeEventListener("pointerup", up);
      target.removeEventListener("pointercancel", up);
      active = null;
      update();
    };
    target.addEventListener("pointermove", move);
    target.addEventListener("pointerup", up);
    target.addEventListener("pointercancel", up);
  };

  const wheelKnob = (def, e) => {
    const band = store.band();
    if (!band || !def || !enabledFor(def, band)) return;
    e.preventDefault();
    const steps = def.steps ? def.steps(band) : 0;
    const change = steps ? -Math.sign(e.deltaY) / (steps - 1) : -e.deltaY / 2400 * (e.shiftKey ? FINE : 1);
    def.set(band, clamp(def.get(band) + change, 0, 1));
    store.changed();
  };

  // A knob: a matte disc with the value as a thin arc round its face.
  // partAt picks which control a press is on (Gain's rings), or null for this one.
  const makeKnob = (key, size, colourOf, extra = null) => {
    const def = KNOBS[key];
    const full = extra ? extra.size : size, c = full / 2, bodyR = size / 2 - 1, arcR = bodyR - 4;
    const fill = el("svg:path", { class: "fill", "stroke-width": size > 56 ? 3 : 2.5 });
    const dot = el("svg:circle", { r: 1.6 });
    const svg = el("svg:svg", { class: "dial", width: full, height: full, viewBox: `0 0 ${full} ${full}` }, [
      ...(extra ? extra.under(c) : []),
      el("svg:circle", { cx: c, cy: c + 1.5, r: bodyR + 0.5, fill: "rgba(0,0,0,0.55)" }),
      el("svg:circle", { cx: c, cy: c, r: bodyR, fill: "url(#knobFace)" }),
      el("svg:circle", { cx: c, cy: c, r: bodyR - 0.5, fill: "none", stroke: "url(#knobEdge)", "stroke-width": 1 }),
      fill, dot,
    ]);
    const label = el("div", { class: "knob-label", text: def.label });
    const root = el("div", { class: `knob knob-${key}` }, [svg, label]);

    let hovered = false, hoverPart = null;
    const part = (e) => (extra && e ? extra.partAt(e, svg, c, bodyR) : def);
    const shown = () => (extra && (KNOBS.range === active || KNOBS.threshold === active) ? active : hoverPart || def);
    svg.addEventListener("pointerdown", (e) => dragKnob(svg, part(e), e));
    svg.addEventListener("dblclick", (e) => reset(part(e)));
    svg.addEventListener("wheel", (e) => wheelKnob(part(e), e), { passive: false });
    svg.addEventListener("pointermove", (e) => { const p = part(e); if (p !== hoverPart) { hoverPart = p; update(); } });
    root.addEventListener("pointerenter", () => { hovered = true; update(); });
    root.addEventListener("pointerleave", () => { hovered = false; hoverPart = null; update(); });
    label.addEventListener("click", () => editValue(label, shown()));

    return {
      root,
      update(b) {
        const enabled = enabledFor(def, b);
        root.classList.toggle("disabled", !enabled);
        const p = shown();
        const showing = enabled && (hovered || active === def || active === p);
        root.classList.toggle("showing", showing);
        // A ring's value says which ring it is, so Range and Threshold aren't mistaken for Gain.
        const ring = p !== def && extra ? extra.names[p === KNOBS.range ? "range" : "threshold"] + " " : "";
        // Freq always shows the frequency; the others show their name until hovered or turned.
        label.textContent = showing ? ring + p.text(b) : key === "freq" ? p.text(b) : (p.labelFor ? p.labelFor(b) : p.label);
        const a = angleOf(def.get(b));
        const colour = colourOf(b);
        fill.setAttribute("d", enabled ? arcPath(c, arcR, def.bipolar ? 0 : START, a) : "");
        fill.setAttribute("stroke", colour);
        const [dx, dy] = polar(c, arcR - 6, a);
        dot.setAttribute("cx", dx); dot.setAttribute("cy", dy); dot.setAttribute("fill", enabled ? "#fffcf4" : "transparent");
        if (extra) extra.update(b, c, enabled, hovered || active ? p : null);
      },
    };
  };

  // Gain's surround: a dark well with two wide rings to grab. The inner one is
  // the threshold, a notch you drag round; the outer one is the range, an arc
  // from 12 o'clock with a handle at its end. The ring under the pointer lights
  // up, and the label names it.
  const gainRings = (() => {
    const rangeR = 44.5, thresholdR = 36.5, split = 40.5; // the rings, and where one's grab area ends
    const parts = {};
    return {
      size: GAIN_SIZE,
      names: { range: "Range", threshold: "Thresh" },
      under(c) {
        parts.well = el("svg:circle", { cx: c, cy: c, r: 49, fill: "rgba(0,0,0,0.34)", stroke: "rgba(255,255,255,0.05)" });
        const track = (r, width) => el("svg:path", { class: "ring", "stroke-width": width, d: arcPath(c, r, START, START + SWEEP) });
        parts.rangeTrack = track(rangeR, 6);
        parts.rangeFill = el("svg:path", { class: "ring", "stroke-width": 4 });
        parts.live = el("svg:path", { class: "ring", "stroke-width": 1.5, stroke: "#fffcf4" });
        parts.rangeHandle = el("svg:circle", { r: 3.6, stroke: "#151513", "stroke-width": 1.2 });
        parts.thresholdTrack = track(thresholdR, 5);
        parts.level = el("svg:path", { class: "ring", "stroke-width": 3, stroke: "rgba(232,228,218,0.3)" });
        parts.notch = el("svg:line", { class: "tick", stroke: "#fffcf4", "stroke-width": 2.6 });
        return [parts.well, parts.rangeTrack, parts.rangeFill, parts.live, parts.rangeHandle,
                parts.thresholdTrack, parts.level, parts.notch];
      },
      partAt(e, svg, c, bodyR) {
        const r = svg.getBoundingClientRect();
        const d = Math.hypot(e.clientX - r.left - c, e.clientY - r.top - c);
        return d <= bodyR + 1.5 ? KNOBS.gain : d <= split ? KNOBS.threshold : KNOBS.range;
      },
      update(b, c, enabled, hot) {
        const range = KNOBS.range, threshold = KNOBS.threshold;
        parts.rangeTrack.setAttribute("stroke", hot === range ? "rgba(255,255,255,0.15)" : "rgba(255,255,255,0.06)");
        parts.thresholdTrack.setAttribute("stroke", hot === threshold ? "rgba(255,255,255,0.15)" : "rgba(255,255,255,0.05)");

        const colour = b.range < 0 ? EQ.cut : EQ.boost;
        const ra = angleOf(range.get(b));
        parts.rangeFill.setAttribute("d", enabled ? arcPath(c, rangeR, 0, ra) : "");
        parts.rangeFill.setAttribute("stroke", colour);
        const [hx, hy] = polar(c, rangeR, ra);
        parts.rangeHandle.setAttribute("cx", hx); parts.rangeHandle.setAttribute("cy", hy);
        parts.rangeHandle.setAttribute("fill", b.range ? colour : "#6b6862");
        parts.rangeHandle.style.opacity = enabled && (b.range || hot === range) ? 1 : 0;

        const at = angleOf(threshold.get(b));
        const [x0, y0] = polar(c, thresholdR - 4, at), [x1, y1] = polar(c, thresholdR + 4, at);
        parts.notch.setAttribute("x1", x0); parts.notch.setAttribute("y1", y0); parts.notch.setAttribute("x2", x1); parts.notch.setAttribute("y2", y1);
        parts.notch.style.opacity = !enabled ? 0 : b.range || hot === threshold ? 1 : 0.35;

        // A made-up detector level and gain change, so a dynamic band's rings move.
        const time = performance.now() / 1000;
        const dynamic = enabled && b.range !== 0;
        const level = -34 + 14 * Math.sin(time * 2.1) + 6 * Math.sin(time * 5.3);
        parts.level.setAttribute("d", dynamic ? arcPath(c, thresholdR, START, angleOf((level - EQ.minThreshold) / 60)) : "");
        const move = dynamic ? clamp(Math.max(0, level - b.threshold) / 20, 0, 1) * b.range : 0;
        parts.live.setAttribute("d", dynamic ? arcPath(c, rangeR, 0, angleOf((move + EQ.maxGain) / (2 * EQ.maxGain))) : "");
      },
    };
  })();

  // The panel's shape: a rounded body with a round collar where Gain's top
  // quarter rises out of it, eased into the top edge.
  const outline = () => {
    const cx = W / 2, cy = GAIN_TOP + GAIN_SIZE / 2, t = BODY_TOP, r = 12, ease = 7;
    const meet = Math.sqrt(COLLAR ** 2 - (cy - t) ** 2);              // where the collar crosses the top edge
    const lift = Math.sqrt(COLLAR ** 2 - (cy - t + ease) ** 2);       // a little above it, where the easing ends
    return `M${r} ${t} H${cx - meet - 9} Q${cx - meet + 1} ${t} ${cx - lift} ${t - ease}`
         + ` A${COLLAR} ${COLLAR} 0 0 1 ${cx + lift} ${t - ease} Q${cx + meet - 1} ${t} ${cx + meet + 9} ${t}`
         + ` H${W - r} Q${W} ${t} ${W} ${t + r} V${H - r} Q${W} ${H} ${W - r} ${H} H${r} Q0 ${H} 0 ${H - r} V${t + r} Q0 ${t} ${r} ${t} Z`;
  };

  const panel = document.getElementById("panel");
  const dock = document.getElementById("dock");

  const bg = el("svg:svg", { class: "panel-bg", viewBox: `0 0 ${W} ${H}`, preserveAspectRatio: "none" }, [
    el("svg:defs", {}, [
      el("svg:linearGradient", { id: "panelFill", x1: 0, y1: 0, x2: 0, y2: 1 }, [
        el("svg:stop", { offset: "0", "stop-color": "#2b2a27" }),
        el("svg:stop", { offset: "1", "stop-color": "#1a1a18" }),
      ]),
      el("svg:linearGradient", { id: "panelEdge", x1: 0, y1: 0, x2: 1, y2: 0 }, [
        el("svg:stop", { offset: "0", "stop-color": "rgba(255,255,255,0.08)" }),
        el("svg:stop", { offset: "0.5", "stop-color": "rgba(242,193,78,0.45)" }),
        el("svg:stop", { offset: "1", "stop-color": "rgba(255,255,255,0.08)" }),
      ]),
      el("svg:radialGradient", { id: "knobFace", cx: "50%", cy: "28%", r: "80%" }, [
        el("svg:stop", { offset: "0", "stop-color": "#3a3934" }),
        el("svg:stop", { offset: "0.6", "stop-color": "#24231f" }),
        el("svg:stop", { offset: "1", "stop-color": "#181816" }),
      ]),
      el("svg:linearGradient", { id: "knobEdge", x1: 0, y1: 0, x2: 0, y2: 1 }, [
        el("svg:stop", { offset: "0", "stop-color": "rgba(255,255,255,0.16)" }),
        el("svg:stop", { offset: "1", "stop-color": "rgba(255,255,255,0.02)" }),
      ]),
    ]),
    el("svg:path", { class: "panel-body", d: outline() }),
    el("svg:path", { class: "panel-edge", d: outline() }),
  ]);
  bg.setAttribute("style", "overflow:visible");

  const content = el("div", { class: "panel-content" });
  panel.append(bg, content);

  const button = (cls, title, d, onclick, size = 12) => el("button", { class: `icon-btn ${cls}`, title, onclick: (e) => { e.stopPropagation(); onclick(e); } },
                                                        [icon(d, size, size)]);

  // Top left: the band's on/off, solo and dynamics. Top right: its number, remove and close.
  const power = button("boxed power", "Band on / off", "M6 1.6 L6 5.6 M3.4 3.2 A4 4 0 1 0 8.6 3.2",
                       () => { store.band().muted = !store.band().muted; store.changed(); });
  const close = button("boxed close", "Hide panel", "M3 3 L9 9 M9 3 L3 9", () => { store.panelHidden = true; store.changed(); });
  const ordered = () => [...store.bands].sort((x, y) => x.freq - y.freq);
  const step = (dir) => {
    const list = ordered(), i = list.findIndex((b) => b.id === store.selected);
    store.selected = list[(i + dir + list.length) % list.length].id;
    closePop();
    store.changed();
  };
  const navNum = el("button", { class: "nav-num", title: "All bands", onclick: (e) => { e.stopPropagation(); togglePop("bands"); } });
  const nav = el("div", { class: "nav" }, [
    button("prev", "Previous band", "M5.5 1 L2.5 4 L5.5 7", () => step(-1), 8), navNum,
    button("next", "Next band", "M2.5 1 L5.5 4 L2.5 7", () => step(1), 8),
  ]);

  const removeBtn = button("boxed remove", "Remove band", "M2.2 3.4 H9.8 M4.6 3.4 V2 H7.4 V3.4 M3.4 3.4 L4 10.2 H8 L8.6 3.4", () => {
    const i = store.bands.findIndex((b) => b.id === store.selected);
    store.bands.splice(i, 1);
    store.selected = store.bands.length ? store.bands[Math.min(i, store.bands.length - 1)].id : -1;
    closePop();
    store.changed();
  });
  const soloBtn = button("boxed solo", "Solo: hear only this band", "M8.6 3.2 C7.8 2.1 3.6 2 3.6 4.3 C3.6 6.6 8.4 5.6 8.4 8 C8.4 10.2 4.2 10.2 3.4 9",
                         () => { store.soloed = store.soloed === store.selected ? -1 : store.selected; store.changed(); });
  const dynBtn = button("boxed dyn", "Dynamics: ratio, attack, release, sidechain",
                        "M6 3.8 A2.2 2.2 0 1 0 6 8.2 A2.2 2.2 0 1 0 6 3.8 M6 1 L6 2.2 M6 9.8 L6 11 M1 6 L2.2 6 M9.8 6 L11 6 M2.5 2.5 L3.3 3.3 M8.7 8.7 L9.5 9.5 M2.5 9.5 L3.3 8.7 M8.7 3.3 L9.5 2.5",
                        () => togglePop("dyn"));
  const leftTools = el("div", { class: "side-tools left" }, [power, soloBtn, dynBtn]);
  const rightTools = el("div", { class: "side-tools right" }, [nav, removeBtn, close]);

  // The type pill and the stereo pill, each opening a menu.
  const typeGlyph = el("span");
  const typeName = el("span");
  const typePill = el("button", { class: "pill type", title: "Filter type", onclick: (e) => { e.stopPropagation(); togglePop("type"); } },
                      [typeGlyph, typeName, caret()]);
  const stereoIcon = icon("M5 2.5 A3.5 3.5 0 1 0 5 9.5 A3.5 3.5 0 1 0 5 2.5 M9 2.5 A3.5 3.5 0 1 0 9 9.5 A3.5 3.5 0 1 0 9 2.5", 14, 12, "glyph");
  const stereoName = el("span");
  const stereoPill = el("button", { class: "pill stereo", title: "Stereo placement", onclick: (e) => { e.stopPropagation(); togglePop("stereo"); } },
                        [stereoIcon, stereoName, caret()]);

  const freq = makeKnob("freq", 46, () => "#f2c14e");
  const gain = makeKnob("gain", 62, (b) => signColour(b.gain), gainRings);
  const q = makeKnob("q", 46, () => "#f2c14e");

  content.append(leftTools, rightTools, typePill, stereoPill, freq.root, gain.root, q.root);

  // Menus and popovers.
  const closePop = () => {
    if (!openPop) return;
    openPop.el.remove();
    openPop = null;
    update();
  };
  const place = (pop, anchor) => {
    const p = panel.getBoundingClientRect(), a = anchor.getBoundingClientRect();
    if (pop.classList.contains("shoulder")) {
      // The dynamics sit on the left shoulder and the band list on the right,
      // either side of Gain, each pointing at the button that opened it.
      const left = pop.classList.contains("dyn-pop") ? 0 : W - pop.offsetWidth;
      pop.style.bottom = H - BODY_TOP + 8 + "px";
      pop.style.left = left + "px";
      pop.style.setProperty("--arrow", a.left + a.width / 2 - p.left - left + "px");
      return;
    }
    pop.style.bottom = p.bottom - a.top + 6 + "px";
    pop.style.left = clamp(a.left + a.width / 2 - p.left - pop.offsetWidth / 2, 0, W - pop.offsetWidth) + "px";
  };

  const typeMenu = () => {
    const b = store.band();
    return el("div", { class: "pop" }, EQ.types.map((t) => el("button", {
      class: "menu-item" + (b.type === t ? " on" : ""),
      onclick: () => { b.type = t; if (isShelf(b)) b.q = Math.min(b.q, EQ.maxShelfQ); closePop(); store.changed(); },
    }, [glyph(t), el("span", { text: EQ.typeNames[t] })])));
  };

  const stereoMenu = () => {
    const b = store.band();
    const choose = (stereo, pos) => () => { b.stereo = stereo; if (pos !== null) b.pos = pos; closePop(); store.changed(); };
    const item = (text, on, action) => el("button", { class: "menu-item" + (on ? " on" : ""), onclick: action, text });
    const thumb = el("div", { class: "angle-thumb" });
    const angle = el("div", { class: "angle", title: "Drag for any position; double-click for Mid" }, [thumb]);
    const angleLabel = el("div", { class: "knob-label", title: "Click to type: L, R, Mid, Side or degrees" });
    const refresh = () => {
      thumb.style.left = KNOBS.pos.get(b) * 100 + "%";
      angleLabel.textContent = b.stereo ? "Both" : fmt.pos(b.pos);
    };
    const setFrom = (e) => {
      const r = angle.getBoundingClientRect();
      b.stereo = false;
      KNOBS.pos.set(b, clamp((e.clientX - r.left) / r.width, 0, 1));
      store.changed();
      refresh();
    };
    angle.addEventListener("pointerdown", (e) => {
      angle.setPointerCapture(e.pointerId);
      setFrom(e);
      const move = (m) => setFrom(m);
      const up = () => { angle.removeEventListener("pointermove", move); angle.removeEventListener("pointerup", up); };
      angle.addEventListener("pointermove", move);
      angle.addEventListener("pointerup", up);
    });
    angle.addEventListener("dblclick", () => { b.stereo = false; b.pos = 0; store.changed(); refresh(); });
    angleLabel.addEventListener("click", () => { b.stereo = false; editValue(angleLabel, KNOBS.pos); });
    refresh();
    const atPos = (deg) => !b.stereo && Math.round(b.pos) === deg;
    return el("div", { class: "pop", style: "width: 196px" }, [
      item("Stereo (both channels)", b.stereo, choose(true, null)),
      el("div", { class: "menu-sep" }),
      item("Mid", atPos(0), choose(false, 0)),
      item("Side", atPos(180) || atPos(-180), choose(false, 180)),
      item("Left", atPos(-90), choose(false, -90)),
      item("Right", atPos(90), choose(false, 90)),
      el("div", { class: "menu-sep" }),
      el("div", { class: "menu-row" }, [angle, angleLabel]),
    ]);
  };

  // Every band, low to high: its number, shape, frequency and gain. Click one to edit it.
  const bandsMenu = () => el("div", { class: "pop shoulder bands-pop" }, [el("div", { class: "band-list" }, ordered().map((b, i) => el("button", {
    class: "band-row" + (b.id === store.selected ? " on" : "") + (b.muted ? " muted" : ""),
    onclick: () => { store.selected = b.id; closePop(); store.changed(); },
  }, [
    el("span", { class: "band-num", text: String(i + 1), style: `--tone: ${b.muted ? "#6b6862" : signColour(isCut(b) ? -1 : b.gain)}` }),
    glyph(b.type),
    el("span", { class: "band-freq", text: fmt.freq(b.freq) }),
    el("span", { class: "band-gain", text: isCut(b) ? b.slope + " dB/oct" : fmt.gain(b.gain) }),
  ])))]);

  // Dynamics: ratio, attack and release, the sidechain switch and the GR meter.
  const ratio = makeKnob("ratio", 38, () => "#e8e4da");
  const attack = makeKnob("attack", 38, () => "#e8e4da");
  const release = makeKnob("release", 38, () => "#e8e4da");
  const scBtn = el("button", { class: "sc", text: "SC", title: "Dynamics follow the sidechain input", onclick: () => {
    const b = store.band();
    if (b.sc || store.sidechainConnected) { b.sc = !b.sc; store.changed(); }
  } });
  const scNote = el("div", { class: "sc-note" });
  const grBar = el("div", { class: "gr-bar" });
  const dynPop = el("div", { class: "pop shoulder dyn-pop" }, [
    ratio.root, attack.root, release.root,
    el("div", { class: "dyn-row" }, [scBtn, scNote, el("div", { class: "gr-meter", title: "Gain change now" }, [grBar])]),
  ]);
  dynPop.addEventListener("pointerdown", (e) => e.stopPropagation());

  function togglePop(name) {
    const was = openPop && openPop.name;
    closePop();
    if (was === name) return;
    const anchor = { type: typePill, stereo: stereoPill, dyn: dynBtn, bands: navNum }[name];
    const pop = name === "dyn" ? dynPop : name === "type" ? typeMenu() : name === "bands" ? bandsMenu() : stereoMenu();
    pop.addEventListener("pointerdown", (e) => e.stopPropagation());
    panel.append(pop);
    place(pop, anchor);
    openPop = { name, el: pop };
    update();
  }

  // A click anywhere else closes a menu (the dynamics stay until the gear).
  document.addEventListener("pointerdown", () => { if (openPop && openPop.name !== "dyn") closePop(); });
  document.addEventListener("keydown", (e) => { if (e.key === "Escape") closePop(); });

  // The gain fields in the bottom bar.
  const trims = [...document.querySelectorAll(".trim")].map((btn) => {
    const which = btn.dataset.trim, valueSpan = btn.querySelector(".trim-value");
    const def = {
      label: which === "in" ? "INPUT" : "OUTPUT",
      get: () => (store.trim[which] + EQ.maxTrim) / (2 * EQ.maxTrim),
      set: (_, t) => { store.trim[which] = Math.round((t * 2 - 1) * EQ.maxTrim * 10) / 10; },
      text: () => fmt.trim(store.trim[which]),
      type: (_, s) => { const v = parseTyped(s); if (v == null) return false; store.trim[which] = clamp(v, -EQ.maxTrim, EQ.maxTrim); return true; },
      reset: () => { store.trim[which] = 0; },
    };
    let press = null;
    btn.addEventListener("pointerdown", (e) => { press = { y: e.clientY, moved: false }; btn.setPointerCapture(e.pointerId); });
    btn.addEventListener("pointermove", (e) => {
      if (!press || (!press.moved && Math.abs(e.clientY - press.y) < 3)) return;
      press.moved = true;
      def.set(null, clamp(def.get() + (press.y - e.clientY) / 400 * (e.shiftKey ? FINE : 1), 0, 1));
      press.y = e.clientY;
      store.changed();
    });
    btn.addEventListener("pointerup", () => { const clicked = press && !press.moved; press = null; if (clicked) editValue(valueSpan, def, () => ({})); });
    btn.addEventListener("dblclick", () => { def.reset(); store.changed(); });
    btn.addEventListener("wheel", (e) => { e.preventDefault(); def.set(null, clamp(def.get() - e.deltaY / 4800, 0, 1)); store.changed(); }, { passive: false });
    return { update: () => { valueSpan.textContent = def.text(); } };
  });

  document.querySelector(".track-name").textContent = store.track.name;
  document.querySelector(".track-dot").style.background = store.track.colour;

  function update() {
    trims.forEach((t) => t.update());
    const b = store.band();
    dock.classList.toggle("away", !b || !!store.panelHidden);
    if (!b) { closePop(); return; }

    const list = ordered();
    navNum.textContent = String(list.indexOf(b) + 1);
    power.classList.toggle("on", !b.muted);
    power.style.color = b.muted ? "" : signColour(isCut(b) ? -1 : b.gain);

    typeGlyph.replaceChildren(glyph(b.type));
    typeName.textContent = EQ.typeNames[b.type];
    typePill.classList.toggle("open", openPop?.name === "type");
    stereoName.textContent = b.stereo ? "Stereo" : fmt.pos(b.pos);
    stereoPill.classList.toggle("open", openPop?.name === "stereo");

    freq.update(b); gain.update(b); q.update(b);

    soloBtn.classList.toggle("on", store.soloed === b.id);
    dynBtn.disabled = isCut(b);
    dynBtn.classList.toggle("on", openPop?.name === "dyn");
    navNum.classList.toggle("open", openPop?.name === "bands");
    if (openPop?.name === "dyn" && isCut(b)) closePop();

    if (openPop?.name === "dyn") {
      ratio.update(b); attack.update(b); release.update(b);
      scBtn.classList.toggle("on", b.sc);
      scBtn.disabled = !b.sc && !store.sidechainConnected;
      scNote.textContent = store.sidechainConnected ? (b.sc ? "Sidechain" : "Own signal") : "No SC in";
    }
  }

  // The rings and the GR meter move with the made-up audio.
  const tick = () => {
    const b = store.band();
    if (b && !store.panelHidden) {
      gain.update(b);
      if (openPop?.name === "dyn") {
        const level = -34 + 14 * Math.sin(performance.now() / 1000 * 2.1);
        const moveDb = b.range ? clamp(Math.max(0, level - b.threshold) / 20, 0, 1) * b.range : 0;
        const h = Math.abs(moveDb) / EQ.maxGain * 50;
        grBar.style.width = h + "%";
        grBar.style.left = moveDb < 0 ? 50 - h + "%" : "50%";
        grBar.style.background = moveDb < 0 ? EQ.cut : EQ.boost;
      }
    }
    requestAnimationFrame(tick);
  };

  store.on(update);
  update();
  requestAnimationFrame(tick);
})();
