// The band control panel that slides up from the ^ button: filter type, stereo
// / mono, solo, mute, remove, and a knob for each of the band's settings.
//
// Layout, hit-testing, value mapping and drawing; Sphere.tsx owns the gestures.
// Same maths as Source/BandPanel.cpp. The panel is laid out at its default-window
// size and scaled up as a whole with the window; a layout's rects are in those
// unscaled units.

import {
  Band, CUT_SLOPES, DEFAULT_CUT_SLOPE, DEFAULT_THRESHOLD_DB, FILTER_TYPES, FilterType, MAX_GAIN_DB, MAX_Q, MAX_SHELF_Q, MAX_THRESHOLD_DB, MIN_Q,
  MIN_THRESHOLD_DB, clamp, formatDb, formatFreq, formatGain, freqToLat, isCut, isShelf, latToFreq,
} from "./bands";

export const NUM_PANEL_KNOBS = 6; // frequency, gain, Q or slope, position, range, threshold

export type PanelKind = "toggle" | "background" | "previous" | "next" | "type" | "stereo" | "solo" | "mute" | "remove" | "knob";
export interface PanelTarget { kind: PanelKind; index: number }

interface Rect { x: number; y: number; w: number; h: number }

export interface BandPanelLayout {
  panel: Rect; previous: Rect; title: Rect; next: Rect; stereo: Rect; solo: Rect; mute: Rect; remove: Rect;
  types: Rect[]; knobs: Rect[];
  toggle: [number, number];
  scale: number;
}

const PANEL_REFERENCE_SIZE = 520; // the window side at which the panel is drawn 1:1
const PANEL_HEIGHT = 116;
const PANEL_BOTTOM_INSET = 10;
const TOGGLE_RADIUS = 10;
const TOGGLE_CLOSED_INSET = 24; // from the window's bottom edge to the button's centre
const PANEL_VIEW_GAP = 6; // between the sphere's room and the ^ button on the panel's edge
const PANEL_FIT_MARGIN = 10; // kept clear above and below the sphere when it is fitted
const KNOB_RADIUS = 14;
const KNOB_START_ANGLE = -2.35619; // 12 o'clock is 0, clockwise
const KNOB_SWEEP = 4.71239;
const FREQ = 0, GAIN = 1, Q = 2, POS = 3, RANGE = 4;

const WARM_GREY = "138,134,124", GRAPHITE = "17,17,16", TEXT = "232,228,218";
const BOOST = "255,138,42", CUT = "229,72,77";
const UI_FONT = '"Liberation Sans", system-ui, sans-serif';
const TYPE_NAMES = ["Peak", "Low cut", "High cut", "Low shelf", "High shelf"];

const contains = (r: Rect, x: number, y: number) => x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
const qCeiling = (b: Band) => (isShelf(b) ? MAX_SHELF_Q : MAX_Q);
const snapDb = (db: number) => Math.round(db * 20) / 20;
const slopeIndex = (b: Band) => {
  const i = CUT_SLOPES.indexOf(b.slope ?? DEFAULT_CUT_SLOPE);
  return i < 0 ? 3 : i;
};

// How much the panel and the on-screen buttons grow with the window. It follows
// the window's smaller side, but more slowly than the window, so they don't
// dominate a large one.
export const panelScale = (width: number, height: number) => Math.max(1, Math.sqrt(Math.min(width, height) / PANEL_REFERENCE_SIZE));

export function makeBandPanelLayout(windowWidth: number, windowHeight: number, openAmount: number): BandPanelLayout {
  const scale = panelScale(windowWidth, windowHeight);
  const width = windowWidth / scale, height = windowHeight / scale;
  const panelWidth = Math.min(width - 8, clamp(width - 96, 364, 430));
  const openTop = height - PANEL_BOTTOM_INSET - PANEL_HEIGHT;
  const closedTop = height + 6;
  const top = closedTop + (openTop - closedTop) * openAmount;
  const left = (width - panelWidth) / 2;
  const x = left + 12, right = left + panelWidth - 12;
  const headerY = top + 12, headerH = 22;
  const knobWidth = (panelWidth - 24) / NUM_PANEL_KNOBS;
  const closedToggle = height - TOGGLE_CLOSED_INSET;
  return {
    panel: { x: left, y: top, w: panelWidth, h: PANEL_HEIGHT },
    previous: { x, y: headerY, w: 18, h: headerH },
    title: { x: x + 18, y: headerY, w: 54, h: headerH },
    next: { x: x + 72, y: headerY, w: 18, h: headerH },
    types: FILTER_TYPES.map((_, i) => ({ x: x + 100 + 24 * i, y: headerY, w: 24, h: headerH })),
    remove: { x: right - 22, y: headerY, w: 22, h: headerH },
    mute: { x: right - 46, y: headerY, w: 22, h: headerH },
    solo: { x: right - 70, y: headerY, w: 22, h: headerH },
    stereo: { x: right - 114, y: headerY, w: 44, h: headerH },
    knobs: Array.from({ length: NUM_PANEL_KNOBS }, (_, i) => ({ x: x + knobWidth * i, y: top + 38, w: knobWidth, h: PANEL_HEIGHT - 46 })),
    toggle: [width / 2, closedToggle + (openTop - closedToggle) * openAmount],
    scale,
  };
}

// x and y are in CSS pixels.
export function hitBandPanel(l: BandPanelLayout, cssX: number, cssY: number, open: boolean, hasBand: boolean): PanelTarget | null {
  const x = cssX / l.scale, y = cssY / l.scale;
  if (Math.hypot(x - l.toggle[0], y - l.toggle[1]) <= TOGGLE_RADIUS + 3) return { kind: "toggle", index: 0 };
  if (!open || !contains(l.panel, x, y)) return null;
  if (hasBand) {
    for (const kind of ["previous", "next", "stereo", "solo", "mute", "remove"] as const)
      if (contains(l[kind], x, y)) return { kind, index: 0 };
    const type = l.types.findIndex((r) => contains(r, x, y));
    if (type >= 0) return { kind: "type", index: type };
    const knob = l.knobs.findIndex((r) => contains(r, x, y));
    if (knob >= 0) return { kind: "knob", index: knob };
  }
  return { kind: "background", index: 0 };
}

// How the view moves as the panel opens, so the sphere starts clear of it. It's
// worked out once, when the panel opens: zooming or panning after that is
// applied on top, and closing the panel puts the view back.
export interface PanelViewFit { zoomScale: number; panDeltaY: number }
// How far above and below the centre of view the EQ'd surface reaches on
// screen, in sphere radii (up is positive).
export interface SurfaceExtent { top: number; bottom: number; halfWidth: number }

export function fitViewAbovePanel(width: number, height: number, zoom: number, panY: number, radiusFraction: number,
                                  extent: SurfaceExtent): PanelViewFit {
  const room = height - panelScale(width, height) * (PANEL_HEIGHT + PANEL_BOTTOM_INSET + TOGGLE_RADIUS + PANEL_VIEW_GAP);
  const radius = Math.min(width, height) * radiusFraction * zoom;
  const fittedRadius = Math.min(radius, (room - 2 * PANEL_FIT_MARGIN) / (extent.top - extent.bottom));
  // Where the sphere's centre sits when the shape touches the top of the space,
  // and the bottom; halfway between centres the shape we actually see.
  const highest = PANEL_FIT_MARGIN + extent.top * fittedRadius;
  const lowest = room - PANEL_FIT_MARGIN + extent.bottom * fittedRadius;
  const centre = Math.min(Math.max((highest + lowest) / 2 + panY, highest), lowest);
  return { zoomScale: fittedRadius / radius, panDeltaY: centre - (height / 2 + panY) };
}

// Knob positions run 0..1.
export function knobPosition(b: Band, knob: number) {
  switch (knob) {
    case FREQ: return (b.lat / (Math.PI / 2) + 1) / 2;
    case GAIN: return (b.gainDb + MAX_GAIN_DB) / (2 * MAX_GAIN_DB);
    case Q: return isCut(b) ? slopeIndex(b) / (CUT_SLOPES.length - 1) : Math.log(b.q / MIN_Q) / Math.log(qCeiling(b) / MIN_Q);
    case POS: return (b.lon / Math.PI + 1) / 2;
    case RANGE: return ((b.range ?? 0) + MAX_GAIN_DB) / (2 * MAX_GAIN_DB);
    default: return ((b.threshold ?? DEFAULT_THRESHOLD_DB) - MIN_THRESHOLD_DB) / (MAX_THRESHOLD_DB - MIN_THRESHOLD_DB);
  }
}

export function setKnobPosition(b: Band, knob: number, position: number) {
  const t = clamp(position, 0, 1);
  switch (knob) {
    case FREQ: b.lat = (t * 2 - 1) * (Math.PI / 2); break;
    case GAIN: b.gainDb = snapDb((t * 2 - 1) * MAX_GAIN_DB); break;
    case Q:
      if (isCut(b)) b.slope = CUT_SLOPES[Math.round(t * (CUT_SLOPES.length - 1))];
      else b.q = MIN_Q * Math.pow(qCeiling(b) / MIN_Q, t);
      break;
    case POS: b.lon = (t * 2 - 1) * Math.PI; break;
    case RANGE: {
      const range = snapDb((t * 2 - 1) * MAX_GAIN_DB);
      b.range = Math.abs(range) < 0.05 ? 0 : range;
      break;
    }
    default: b.threshold = snapDb(MIN_THRESHOLD_DB + t * (MAX_THRESHOLD_DB - MIN_THRESHOLD_DB)); break;
  }
}

export function resetKnob(b: Band, knob: number) {
  switch (knob) {
    case FREQ: b.lat = freqToLat(1000); break;
    case GAIN: b.gainDb = 0; break;
    case Q:
      if (isCut(b)) b.slope = DEFAULT_CUT_SLOPE;
      else b.q = 1;
      break;
    case POS: b.lon = 0; break;
    case RANGE: b.range = 0; break;
    default: b.threshold = DEFAULT_THRESHOLD_DB; break;
  }
}

export function knobEnabled(b: Band, knob: number) {
  if (knob === POS) return !b.stereo;
  if (knob === GAIN || knob === RANGE || knob > POS) return !isCut(b);
  return true;
}

export const knobIsStepped = (b: Band, knob: number) => knob === Q && isCut(b);

// Applies a filter type chosen on the panel.
export function setBandType(b: Band, type: FilterType) {
  b.type = type;
  b.slope = b.slope ?? DEFAULT_CUT_SLOPE;
  if (isShelf(b)) b.q = Math.min(b.q, MAX_SHELF_Q);
}

const KNOB_NAMES = ["FREQ", "GAIN", "Q", "POS", "RANGE", "THRESH"];
const knobName = (b: Band, knob: number) => (knob === Q && isCut(b) ? "SLOPE" : KNOB_NAMES[knob]);

function positionText(lon: number) {
  const degrees = Math.round((lon * 180) / Math.PI);
  switch (Math.abs(degrees)) {
    case 0: return "Mid";
    case 180: return "Side";
    case 90: return degrees < 0 ? "L" : "R";
    default: return `${degrees > 0 ? "+" : ""}${degrees}°`;
  }
}

function knobText(b: Band, knob: number) {
  if (!knobEnabled(b, knob)) return "-";
  switch (knob) {
    case FREQ: return formatFreq(latToFreq(b.lat));
    case GAIN: return formatGain(b.gainDb);
    case Q: return isCut(b) ? `${b.slope ?? DEFAULT_CUT_SLOPE} dB/oct` : b.q.toFixed(2);
    case POS: return positionText(b.lon);
    case RANGE: return b.range ? formatGain(b.range) : "Off";
    default: return `${formatDb(b.threshold ?? DEFAULT_THRESHOLD_DB)} dB`;
  }
}

const isBipolar = (knob: number) => knob === GAIN || knob === POS || knob === RANGE;
const center = (r: Rect): [number, number] => [r.x + r.w / 2, r.y + r.h / 2];

function traceTypeGlyph(ctx: CanvasRenderingContext2D, type: number, cx: number, cy: number) {
  const points = [
    null,
    [[-7, 4], [-2, -3], [7, -3]],
    [[-7, -3], [2, -3], [7, 4]],
    [[-7, -3], [-2, -3], [2, 3], [7, 3]],
    [[-7, 3], [-2, 3], [2, -3], [7, -3]],
  ][type];
  ctx.beginPath();
  if (!points) {
    ctx.moveTo(cx - 7, cy + 3);
    ctx.bezierCurveTo(cx - 3, cy + 3, cx - 2, cy - 4, cx, cy - 4);
    ctx.bezierCurveTo(cx + 2, cy - 4, cx + 3, cy + 3, cx + 7, cy + 3);
    return;
  }
  points.forEach(([x, y], i) => (i === 0 ? ctx.moveTo(cx + x, cy + y) : ctx.lineTo(cx + x, cy + y)));
}

export function drawBandPanel(ctx: CanvasRenderingContext2D, l: BandPanelLayout, dpr: number, amount: number,
                              band: Band | null, soloed: boolean, hover: PanelTarget | null, activeKnob: number) {
  ctx.save();
  ctx.scale(dpr * l.scale, dpr * l.scale);
  ctx.globalCompositeOperation = "source-over";
  ctx.globalAlpha = 1;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  const rgba = (rgb: string, a: number) => `rgba(${rgb},${a * amount})`;
  const text = (s: string, cx: number, cy: number, size: number, colour: string) => {
    ctx.font = `${size}px ${UI_FONT}`;
    ctx.fillStyle = colour;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(s, cx, cy);
  };
  const buttonBack = (r: Rect, active: boolean, hovered: boolean) => {
    if (!active && !hovered) return;
    ctx.fillStyle = rgba(TEXT, active ? 0.18 : 0.08);
    ctx.beginPath();
    ctx.roundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4);
    ctx.fill();
  };
  const stroke = (colour: string, width: number) => {
    ctx.strokeStyle = colour;
    ctx.lineWidth = width;
    ctx.stroke();
  };
  const hoverOn = (kind: PanelKind) => hover?.kind === kind;

  if (amount > 0.01) {
    const p = l.panel;
    ctx.beginPath();
    ctx.roundRect(p.x, p.y, p.w, p.h, 8);
    ctx.fillStyle = rgba(GRAPHITE, 0.94);
    ctx.fill();
    stroke(rgba(WARM_GREY, 0.45), 1);

    if (!band) {
      text("Double-click the sphere to add a band", p.x + p.w / 2, p.y + p.h / 2, 12, rgba(TEXT, 0.6));
    } else {
      const chevron = (r: Rect, direction: number, hovered: boolean) => {
        const [cx, cy] = center(r);
        ctx.beginPath();
        ctx.moveTo(cx - direction * 2.5, cy - 4.5);
        ctx.lineTo(cx + direction * 2.5, cy);
        ctx.lineTo(cx - direction * 2.5, cy + 4.5);
        stroke(rgba(TEXT, hovered ? 1 : 0.6), 1.5);
      };
      chevron(l.previous, -1, hoverOn("previous"));
      chevron(l.next, 1, hoverOn("next"));
      text(hover?.kind === "type" ? TYPE_NAMES[hover.index] : `Band ${band.id + 1}`, ...center(l.title), 11, rgba(TEXT, 1));

      const bandType = FILTER_TYPES.indexOf(band.type ?? "peak");
      l.types.forEach((r, i) => {
        const active = bandType === i, hovered = hover?.kind === "type" && hover.index === i;
        buttonBack(r, active, hovered);
        traceTypeGlyph(ctx, i, ...center(r));
        stroke(rgba(TEXT, active || hovered ? 1 : 0.6), 1.4);
      });

      buttonBack(l.stereo, !!band.stereo, hoverOn("stereo"));
      text(band.stereo ? "Stereo" : "Mono", ...center(l.stereo), 11, rgba(TEXT, band.stereo || hoverOn("stereo") ? 1 : 0.6));

      buttonBack(l.solo, soloed, hoverOn("solo"));
      text("S", ...center(l.solo), 11, rgba(TEXT, soloed || hoverOn("solo") ? 1 : 0.6));

      // Mute: a circle with a slash, like a muted node.
      buttonBack(l.mute, !!band.muted, hoverOn("mute"));
      {
        const [cx, cy] = center(l.mute);
        const colour = rgba(TEXT, band.muted || hoverOn("mute") ? 1 : 0.6);
        ctx.beginPath();
        ctx.arc(cx, cy, 5, 0, Math.PI * 2);
        stroke(colour, 1.3);
        ctx.beginPath();
        ctx.moveTo(cx - 3.5, cy + 3.5);
        ctx.lineTo(cx + 3.5, cy - 3.5);
        stroke(colour, 1.3);
      }

      // Remove: a cross.
      buttonBack(l.remove, false, hoverOn("remove"));
      {
        const [cx, cy] = center(l.remove);
        ctx.beginPath();
        ctx.moveTo(cx - 4, cy - 4);
        ctx.lineTo(cx + 4, cy + 4);
        ctx.moveTo(cx + 4, cy - 4);
        ctx.lineTo(cx - 4, cy + 4);
        stroke(rgba(TEXT, hoverOn("remove") ? 1 : 0.6), 1.5);
      }

      l.knobs.forEach((area, i) => {
        const enabled = knobEnabled(band, i);
        const lit = enabled && (activeKnob === i || (hover?.kind === "knob" && hover.index === i));
        const dim = enabled ? 1 : 0.32;
        const cx = area.x + area.w / 2, cy = area.y + 30;
        const angle = KNOB_START_ANGLE + clamp(knobPosition(band, i), 0, 1) * KNOB_SWEEP;
        const anchor = isBipolar(i) ? KNOB_START_ANGLE + 0.5 * KNOB_SWEEP : KNOB_START_ANGLE;
        const toCanvas = (a: number) => a - Math.PI / 2;

        text(knobName(band, i), cx, area.y + 6, 9.5, rgba(TEXT, 0.6 * dim));

        ctx.beginPath();
        ctx.arc(cx, cy, KNOB_RADIUS - 3.5, 0, Math.PI * 2);
        ctx.fillStyle = rgba("34,34,32", dim);
        ctx.fill();

        ctx.beginPath();
        ctx.arc(cx, cy, KNOB_RADIUS, toCanvas(KNOB_START_ANGLE), toCanvas(KNOB_START_ANGLE + KNOB_SWEEP));
        stroke(rgba(TEXT, (lit ? 0.32 : 0.18) * dim), 2);

        if (enabled && Math.abs(angle - anchor) > 0.01) {
          const value = i === GAIN ? band.gainDb : i === RANGE ? band.range ?? 0 : 0;
          ctx.beginPath();
          ctx.arc(cx, cy, KNOB_RADIUS, toCanvas(Math.min(anchor, angle)), toCanvas(Math.max(anchor, angle)));
          stroke(rgba(value > 0 ? BOOST : value < 0 ? CUT : TEXT, 1), 2.2);
        }

        ctx.beginPath();
        ctx.moveTo(cx + Math.sin(angle) * 4.5, cy - Math.cos(angle) * 4.5);
        ctx.lineTo(cx + Math.sin(angle) * 9.5, cy - Math.cos(angle) * 9.5);
        stroke(rgba(TEXT, dim), 1.6);

        text(knobText(band, i), cx, area.y + 55, 10.5, rgba(TEXT, (lit ? 1 : 0.8) * dim));
      });
    }
  }

  // The ^ button, riding on the panel's top edge once it's open.
  const [cx, cy] = l.toggle;
  const hovered = hoverOn("toggle");
  ctx.beginPath();
  ctx.arc(cx, cy, TOGGLE_RADIUS, 0, Math.PI * 2);
  ctx.fillStyle = `rgba(${GRAPHITE},0.85)`;
  ctx.fill();
  stroke(`rgba(${WARM_GREY},${hovered ? 0.85 : 0.45})`, 1);

  const tip = 2.5 - 5 * amount; // flips from ^ to v as the panel rises
  ctx.beginPath();
  ctx.moveTo(cx - 4, cy + tip);
  ctx.lineTo(cx, cy - tip);
  ctx.lineTo(cx + 4, cy + tip);
  stroke(`rgba(${TEXT},${hovered ? 1 : 0.6})`, 1.5);

  ctx.restore();
}
