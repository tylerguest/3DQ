import { useEffect, useRef } from "react";
import { NUM_SPECTRUM_BANDS, SPECTRUM_RANGE_DB, Spectrum, emptySpectrum, powerToLevel } from "./spectrum";
import type { BandDynamics } from "./pluginBridge";
import { PresetBank, isReservedPresetName, legalPresetName } from "./presets";
import { PresetBarLayout, PresetTarget, drawPresetBar, hitPresetBar, makePresetBarLayout, presetMenuContains } from "./presetBar";
import { closePresetDialog, showPresetDialog } from "./presetDialog";
import { BandPanelLayout, PanelTarget, PanelViewFit, SurfaceExtent, drawBandPanel, fitViewAbovePanel, hitBandPanel, panelScale, knobEnabled, knobIsStepped, knobPosition, makeBandPanelLayout, resetKnob, setBandType, setKnobPosition } from "./bandPanel";
import { BackdropLayer } from "./backdrop";
import { CanvasSpriteSink, GlSpriteSink, SpriteProfile, SpriteSink, SpriteStyle } from "./sprites";
import {
  Band, MAX_BANDS, MAX_FREQ, MIN_FREQ, axisOverlap, cutResponseDb, MAX_GAIN_DB, MAX_Q, MIN_Q, bandDirection, bandWidth, clamp, formatDb, formatFreq, formatGain,
  MAX_SHELF_Q, bandOverlap, isShelf, shelfA, shelfDbAt, shelfK, shelfResponseDb,
  CUT_SLOPES, DEFAULT_CUT_SLOPE, DEFAULT_THRESHOLD_DB, FILTER_TYPES, MAX_BOOST_DISPLACEMENT, MAX_CUT_DISPLACEMENT, gainDisplacement, limitDisplacement, isCut, MAX_THRESHOLD_DB, MIN_THRESHOLD_DB, displayGain, gainShape, latToFreq, midSideLabel, panLabel, surfaceDisplacement, surfaceRadius,
} from "./bands";

// Rotating particle sphere drawn with Canvas 2D (no WebGL needed, so it runs in
// any WebView). Points sit on a unit sphere, coloured by frequency (blue lows
// -> green mids -> pale highs) and drawn additively so they glow. EQ bands are
// nodes on the surface; their gain bulges or dents the sphere around them.
//
// Gestures:
//   drag empty space       rotate
//   scroll empty space     zoom
//   hold wheel and drag    pan the view
//   double-click background recentre the view
//   double-click sphere    add a band there
//   drag a node            move it (height = frequency, around = stereo position)
//   scroll on a node       gain (bump out / dent in)
//   ctrl+drag a node       gain
//   shift+scroll on node   width (Q)
//   double-click a node    remove it

// Particle counts: denser with WebGL (one draw call, cheap), lighter for the
// Canvas 2D fallback (one drawImage per particle).
const GL_SURFACE_POINTS = 16000;
const GL_HAZE_POINTS = 2200;
const CANVAS_SURFACE_POINTS = 5000;
const CANVAS_HAZE_POINTS = 1200;
// Extra surface points for a big sphere (WebGL only): the dots stay nearly the
// same size in pixels, so without more of them a larger surface looks thinner and paler.
const GL_EXTRA_POINTS = 48000;
const EXTRA_START_RADIUS_PX = 200;
const EXTRA_FADE_COUNT = 2000; // the newest extras fade in as the sphere grows
const EXTRA_EDGE_FADE = 0.2;   // extras fade in over this much of the front half
const SPRING_UNSET = 1e9;      // an extra that wasn't drawn last frame
const NUM_FREQUENCY_COLOURS = 24;
const TINT_STEPS = 7;
const NUM_TINTS = 1 + 2 * TINT_STEPS; // none, then towards boost, then towards cut
const NUM_COLOUR_BUCKETS = NUM_FREQUENCY_COLOURS * NUM_TINTS;
const SPRITE_SIZE = 64; // Canvas 2D fallback sprites; big enough not to blur when scaled up
const CAMERA_DISTANCE = 3.2;
const NODE_HIT_RADIUS = 16; // CSS px
const SPHERE_RADIUS_FRACTION = 0.32; // of the window's smaller side, at zoom 1
const KNOB_PIXELS_FULL_RANGE = 200; // CSS px of drag from one end of a panel knob to the other
const KNOB_FINE_FACTOR = 0.2; // while Shift is held
const HELP_BUTTON_RADIUS = 10; // CSS px
const HELP_BUTTON_INSET = 24; // CSS px, from the right and bottom edges to the button's centre
// The font the plugin ends up with on Linux, so both versions set text alike.
const UI_FONT = '"Liberation Sans", system-ui, sans-serif';
// Everything the ? button lists. Keep in step with SphereView::drawHelp.
const HELP_ROWS: [string, string][] = [
  ["Double-click sphere", "Add band"],
  ["Double-click band", "Remove band"],
  ["Drag band", "Move band"],
  ["Alt+hold band", "Solo while held"],
  ["Drag empty space", "Rotate"],
  ["Scroll empty space", "Zoom"],
  ["Hold wheel and drag", "Pan view"],
  ["Double-click background", "Recentre view"],
  ["Scroll band or Ctrl+drag", "Gain"],
  ["Shift+scroll band", "Width (cuts: slope)"],
  ["Alt+scroll band", "Dynamic range"],
  ["Ctrl+scroll band", "Threshold"],
  ["Right-click band", "Mute"],
  ["Shift+right-click band", "Stereo / mono"],
  ["Shift+double-click band", "Peak / cuts / shelves"],
];

type RGB = [number, number, number];
type Vec3 = [number, number, number];

// A dot's colour is its frequency until the EQ is changing real signal there.
// Orange and red only ever mean boost and cut. Stops are positions on the log
// frequency axis (0 = 20 Hz, 1 = 20 kHz).
const FREQUENCY_COLOURS: { t: number; rgb: RGB }[] = [
  { t: 0, rgb: [150, 96, 255] },      // sub: violet
  { t: 0.17, rgb: [62, 120, 255] },   // ~65 Hz, bass: blue
  { t: 0.33, rgb: [40, 205, 235] },   // ~200 Hz, low mids: cyan
  { t: 0.5, rgb: [70, 225, 120] },    // ~630 Hz, mids: green
  { t: 0.67, rgb: [205, 235, 70] },   // ~2 kHz, upper mids: lime
  { t: 0.83, rgb: [255, 225, 90] },   // ~6.3 kHz, presence: yellow
  { t: 1, rgb: [238, 240, 255] },     // air: ice white
];
const POLE_DIM = 0.82; // brightness at the poles relative to the equator
const DOT_BOOST: RGB = [255, 120, 24];
const DOT_CUT: RGB = [232, 52, 48];
const PARTICLE_ALPHA = 0.65;
const PARTICLE_SIZE = 0.78;
// A soft light from the upper left, fixed to the view, so the sphere has form.
const LIGHT_X = -0.36, LIGHT_Y = 0.62, LIGHT_Z = 0.7;
const LIGHT_FLOOR = 0.74, LIGHT_AMOUNT = 0.46;
const WARM_GREY: RGB = [138, 134, 124];
const PANEL_FILL = "rgba(17,17,16,0.88)";
const PANEL_STROKE = "rgba(138,134,124,0.45)";
const TEXT_COLOUR = "#e8e4da";

interface Point {
  dir: Vec3;   // unit direction
  shell: number; // base radius (1 on the surface, ~0.85-1.15 for haze)
  size: number;
  colour: number; // index into the frequency colours
  twinkle: number; // phase of the gentle brightness shimmer
  twinkleRate: number; // radians per second
}

const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
const lerpRGB = (a: RGB, b: RGB, t: number): RGB => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];

// Particle colour at a position on the log frequency axis (0..1).
function colourForFrequency(t: number): RGB {
  let i = 0;
  while (i < FREQUENCY_COLOURS.length - 2 && t > FREQUENCY_COLOURS[i + 1].t) ++i;
  const a = FREQUENCY_COLOURS[i], b = FREQUENCY_COLOURS[i + 1];
  // Eased, so each range holds its own colour and changes over between ranges.
  const along = clamp((t - a.t) / (b.t - a.t), 0, 1);
  const [r, g, bl] = lerpRGB(a.rgb, b.rgb, along * along * (3 - 2 * along));
  const y = Math.sin((t * 2 - 1) * (Math.PI / 2));
  const k = 1 - (1 - POLE_DIM) * y * y;
  return [r * k, g * k, bl * k];
}

function bucketColour(i: number): RGB {
  const base = colourForFrequency(Math.floor(i / NUM_TINTS) / (NUM_FREQUENCY_COLOURS - 1));
  const tint = i % NUM_TINTS;
  return tint === 0 ? base
       : tint <= TINT_STEPS ? lerpRGB(base, DOT_BOOST, tint / TINT_STEPS)
                            : lerpRGB(base, DOT_CUT, (tint - TINT_STEPS) / TINT_STEPS);
}

const rgbString = ([r, g, b]: RGB, a = 1) => `rgba(${Math.round(r)},${Math.round(g)},${Math.round(b)},${a})`;

// One soft glow sprite per colour bucket; drawing images is much faster than arcs.
function buildSprites(): HTMLCanvasElement[] {
  const sprites: HTMLCanvasElement[] = [];

  for (let i = 0; i < NUM_COLOUR_BUCKETS; ++i) {
    const rgb = bucketColour(i);
    const c = document.createElement("canvas");
    c.width = c.height = SPRITE_SIZE;
    const ctx = c.getContext("2d")!;
    const half = SPRITE_SIZE / 2;
    // A small crisp core in a faint wisp of gas, lit from the upper left (close
    // to the WebGL shader in sprites.ts, with the core at its default-size width).
    const grad = ctx.createRadialGradient(half - 0.1 * half, half - 0.14 * half, 0, half, half, half);
    const lit = lerpRGB(rgb, [255, 255, 255], 0.08);
    const coreR = 0.45;
    for (let t = 0; t <= 1; t += 0.05) {
      const core = Math.pow(Math.max(1 - (t * t) / (coreR * coreR), 0), 2);
      grad.addColorStop(Math.min(t, 1), rgbString(lit, Math.min(1, 0.8 * (0.82 * core + 0.2 * Math.pow(1 - t * t, 3)))));
    }
    grad.addColorStop(1, rgbString(lit, 0));
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, SPRITE_SIZE, SPRITE_SIZE);
    sprites.push(c);
  }

  return sprites;
}

// Single-colour glow sprite, used to tint boosted regions warm.
function buildTintSprite(rgb: RGB): HTMLCanvasElement {
  const c = document.createElement("canvas");
  c.width = c.height = SPRITE_SIZE;
  const ctx = c.getContext("2d")!;
  const half = SPRITE_SIZE / 2;
  const grad = ctx.createRadialGradient(half, half, 0, half, half, half);
  grad.addColorStop(0, rgbString(rgb));
  grad.addColorStop(0.4, rgbString(rgb, 0.35));
  grad.addColorStop(1, rgbString(rgb, 0));
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, SPRITE_SIZE, SPRITE_SIZE);
  return c;
}

const BOOST_COLOUR: RGB = [255, 138, 42];
const CUT_COLOUR: RGB = [229, 72, 77];
const MID_LINE_COLOUR: RGB = [255, 252, 244];
// Depth-ring colour by how many dB that ring is at: pale for a little, fully
// saturated at ±30 dB. Sign still reads as orange (boost) or red (cut).
const NUM_RING_COLOURS = 24;
const BOOST_RING_RAMP: RGB[] = [[255, 232, 176], [255, 170, 60], [255, 104, 14]];
const CUT_RING_RAMP: RGB[] = [[255, 200, 186], [235, 70, 62], [186, 16, 22]];
const rampColour = (ramp: RGB[], t: number) =>
  t < 0.5 ? lerpRGB(ramp[0], ramp[1], t * 2) : lerpRGB(ramp[1], ramp[2], t * 2 - 1);
const MID_LINE_WIDTH = 0.035; // radians either side of the Mid meridian


function randomDirection(): Vec3 {
  const u = Math.random() * 2 - 1;
  const theta = Math.random() * Math.PI * 2;
  const r = Math.sqrt(1 - u * u);
  return [r * Math.cos(theta), u, r * Math.sin(theta)];
}

// Frequency colour for a unit direction's y: latitude is the log frequency axis.
const frequencyColourOf = (y: number) =>
  Math.round((Math.asin(clamp(y, -1, 1)) / Math.PI + 0.5) * (NUM_FREQUENCY_COLOURS - 1));

function buildPoints(numSurface: number, numHaze: number, numExtra: number): Point[] {
  const points: Point[] = [];
  const goldenAngle = Math.PI * (3 - Math.sqrt(5));
  // Particle sizes scale with spacing, so the sphere looks equally full at any count.
  const sizeScale = Math.sqrt(5000 / numSurface);
  // Fibonacci lattice, jittered by about a third of the spacing in both
  // directions so its spiral rows don't show up as moiré stripes.
  const spacing = Math.sqrt((4 * Math.PI) / numSurface);
  const jitter = () => (Math.random() - 0.5) * spacing * 0.7;

  for (let i = 0; i < numSurface; ++i) {
    const y0 = 1 - ((i + 0.5) / numSurface) * 2;
    const lat = clamp(Math.asin(y0) + jitter(), -Math.PI / 2, Math.PI / 2);
    const y = Math.sin(lat);
    const r = Math.cos(lat);
    const theta = goldenAngle * i + jitter() / Math.max(r, 0.05);
    points.push({ dir: [Math.cos(theta) * r, y, Math.sin(theta) * r],
                  shell: 1 + (Math.random() - 0.5) * 0.02,
                  size: (0.5 + Math.random() * 1.0) * sizeScale, colour: frequencyColourOf(y),
                  twinkle: Math.random() * Math.PI * 2, twinkleRate: 0.6 + Math.random() * 1.4 });
  }

  // Sparse haze just inside / outside the shell.
  for (let i = 0; i < numHaze; ++i) {
    const dir = randomDirection();
    const shell = 0.85 + Math.random() * 0.3;
    points.push({ dir, shell, size: (0.3 + Math.random() * 0.6) * sizeScale, colour: frequencyColourOf(dir[1]),
                  twinkle: Math.random() * Math.PI * 2, twinkleRate: 0.4 + Math.random() * 0.8 });
  }

  // A low-discrepancy sequence: however many are in use, they're evenly spread.
  for (let k = 0; k < numExtra; ++k) {
    const y0 = 2 * ((0.5 + k * 0.7548776662) % 1) - 1;
    const lat = clamp(Math.asin(y0) + jitter(), -Math.PI / 2, Math.PI / 2);
    const y = Math.sin(lat);
    const r = Math.cos(lat);
    const theta = Math.PI * 2 * ((0.5 + k * 0.569840291) % 1) + jitter() / Math.max(r, 0.05);
    points.push({ dir: [Math.cos(theta) * r, y, Math.sin(theta) * r],
                  shell: 1 + (Math.random() - 0.5) * 0.02,
                  size: (0.5 + Math.random() * 1.0) * sizeScale, colour: frequencyColourOf(y),
                  twinkle: Math.random() * Math.PI * 2, twinkleRate: 0.6 + Math.random() * 1.4 });
  }

  return points;
}

interface SphereProps {
  // Called with a snapshot of the bands whenever the user adds, moves, changes or removes one.
  onBandsChange?: (bands: Band[]) => void;
  // Bands pushed from outside (the plugin); bump `serial` to apply a new set.
  externalBands?: { bands: Band[]; serial: number };
  // Polled every frame for the current stereo spectrum (null when there's no audio).
  getSpectrum?: () => Spectrum | null;
  // Live gain / detector level per band id for dynamic bands (null when no audio).
  getDynamics?: () => Map<number, BandDynamics> | null;
}

export default function Sphere({ onBandsChange, externalBands, getSpectrum, getDynamics }: SphereProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const glCanvasRef = useRef<HTMLCanvasElement>(null);
  const backdropCanvasRef = useRef<HTMLCanvasElement>(null);
  const onBandsChangeRef = useRef(onBandsChange);
  onBandsChangeRef.current = onBandsChange;
  const getSpectrumRef = useRef(getSpectrum);
  getSpectrumRef.current = getSpectrum;
  const getDynamicsRef = useRef(getDynamics);
  getDynamicsRef.current = getDynamics;
  const replaceBandsRef = useRef<(bands: Band[]) => void>(() => {});

  useEffect(() => {
    if (externalBands) replaceBandsRef.current(externalBands.bands);
  }, [externalBands]);

  useEffect(() => {
    const canvas = canvasRef.current!;
    const ctx = canvas.getContext("2d")!;
    const sprites = buildSprites();
    const midSprite = buildTintSprite(MID_LINE_COLOUR);

    // Glowing dots (particles, Mid seam, vortex rings) go through a sprite
    // sink: three.js/WebGL when available, Canvas 2D otherwise. Style ids:
    // 0..NUM_COLOUR_BUCKETS-1 = particle colours, then the Mid seam, then the
    // boost and cut ring ramps.
    const STYLE_MID = NUM_COLOUR_BUCKETS;
    const STYLE_RING_BOOST = STYLE_MID + 1;
    const STYLE_RING_CUT = STYLE_RING_BOOST + NUM_RING_COLOURS;
    const styles: SpriteStyle[] = sprites.map((image, i) => ({
      rgb: bucketColour(i),
      profile: SpriteProfile.Particle,
      image,
    }));
    styles.push({ rgb: MID_LINE_COLOUR, profile: SpriteProfile.Tint, image: midSprite });
    for (const ramp of [BOOST_RING_RAMP, CUT_RING_RAMP]) {
      for (let i = 0; i < NUM_RING_COLOURS; ++i) {
        const rgb = rampColour(ramp, i / (NUM_RING_COLOURS - 1));
        styles.push({ rgb, profile: SpriteProfile.Tint, image: buildTintSprite(rgb) });
      }
    }

    // Style for a depth ring at this many dB. The curve opens up the everyday
    // range, so 3 dB and 9 dB look different as well as 12 dB and 30 dB.
    const ringStyle = (db: number) =>
      (db > 0 ? STYLE_RING_BOOST : STYLE_RING_CUT) +
      Math.round(Math.pow(Math.min(1, Math.abs(db) / MAX_GAIN_DB), 0.6) * (NUM_RING_COLOURS - 1));

    const glSink = GlSpriteSink.create(glCanvasRef.current!, styles);
    const backdrop = new BackdropLayer(backdropCanvasRef.current!);
    const canvasSink = new CanvasSpriteSink(ctx, styles);
    let sink: SpriteSink = canvasSink;
    // Dev benchmark (?bench): WebGL particle count, nothing drawn, so the CPU
    // side can be timed even without a GPU.
    const bench = process.env.NODE_ENV !== "production" && new URLSearchParams(location.search).has("bench");
    const nullSink: SpriteSink = { begin() {}, add() {}, end() {} };
    const useGlCounts = !!glSink || bench;
    const numSurface = useGlCounts ? GL_SURFACE_POINTS : CANVAS_SURFACE_POINTS;
    const numBase = numSurface + (useGlCounts ? GL_HAZE_POINTS : CANVAS_HAZE_POINTS);
    const points = buildPoints(numSurface, numBase - numSurface, useGlCounts ? GL_EXTRA_POINTS : 0);

    // Flat per-particle arrays for the hot render loop (no objects or
    // allocations per particle per frame).
    const N = points.length;
    const pX = new Float32Array(N), pY = new Float32Array(N), pZ = new Float32Array(N);
    const drawOrder = new Int32Array(N);
    const pShell = new Float32Array(N), pSize = new Float32Array(N), pColour = new Uint8Array(N);
    const pTwinkle = new Float32Array(N), pTwinkleRate = new Float32Array(N);
    const pMid = new Float32Array(N); // Mid-seam weight (0 = not on the seam)
    points.forEach((p, i) => {
      [pX[i], pY[i], pZ[i]] = p.dir;
      pShell[i] = p.shell;
      pSize[i] = p.size;
      pColour[i] = p.colour;
      pTwinkle[i] = p.twinkle;
      pTwinkleRate[i] = p.twinkleRate;
      // Mid (centre pan) meridian: lon 0 is the x = 0, z > 0 half of the sphere.
      const w = Math.exp(-(p.dir[0] * p.dir[0]) / (MID_LINE_WIDTH * MID_LINE_WIDTH));
      pMid[i] = i < numBase && p.dir[2] > 0 && p.shell > 0.99 && p.shell < 1.01 && w > 0.05 ? w : 0;
    });

    // For cuts: each particle's frequency and longitude (as cos / sin).
    const pFreq = new Float32Array(N), pCosLon = new Float32Array(N), pSinLon = new Float32Array(N);
    points.forEach((p, i) => {
      pFreq[i] = latToFreq(Math.asin(clamp(p.dir[1], -1, 1)));
      const lon = Math.atan2(p.dir[0], p.dir[2]);
      pCosLon[i] = Math.cos(lon);
      pSinLon[i] = Math.sin(lon);
    });
    const sLow = new Uint8Array(MAX_BANDS), sFc = new Float32Array(MAX_BANDS), sA = new Float32Array(MAX_BANDS);
    const sK = new Float32Array(MAX_BANDS), sCos = new Float32Array(MAX_BANDS), sSin = new Float32Array(MAX_BANDS);
    const cBias = new Float32Array(MAX_BANDS), sBias = new Float32Array(MAX_BANDS);
    const rSin = new Float32Array(MAX_BANDS), rCos = new Float32Array(MAX_BANDS), rAmp = new Float32Array(MAX_BANDS), rInvW2 = new Float32Array(MAX_BANDS);
    const cLow = new Uint8Array(MAX_BANDS), cFc = new Float32Array(MAX_BANDS), cTwoN = new Float32Array(MAX_BANDS);
    const cCos = new Float32Array(MAX_BANDS), cSin = new Float32Array(MAX_BANDS);

    // Per-frame band data for the loop (same maths as surfaceDisplacement).
    const bX = new Float32Array(MAX_BANDS), bY = new Float32Array(MAX_BANDS), bZ = new Float32Array(MAX_BANDS);
    const bAmp = new Float32Array(MAX_BANDS), bInvW2 = new Float32Array(MAX_BANDS);

    // Audio response: each particle shows the energy at its own frequency
    // (latitude) along its own stereo axis (longitude). Precompute where each
    // particle reads the spectrum (its stereo axis comes from lonPos, below).
    const specPos = new Float32Array(points.length);
    points.forEach((p, i) => {
      const t = (Math.asin(clamp(p.dir[1], -1, 1)) / (Math.PI / 2) + 1) / 2;
      specPos[i] = clamp(t * NUM_SPECTRUM_BANDS - 0.5, 0, NUM_SPECTRUM_BANDS - 1);
    });

    // Smoothed spectrum: jumps up instantly, falls back gently (peak hold
    // with a time-based release, so it behaves the same at any frame rate).
    const held = emptySpectrum();
    const smoothed = emptySpectrum(); // `held`, blurred a little across neighbouring bands
    const BLUR = [0.06, 0.24, 0.4, 0.24, 0.06];
    // Spectrum smoothing time constants (s): quick enough to follow drums,
    // slow enough to average out the frame-to-frame noise of the analysis.
    const SPECTRUM_ATTACK = 0.035;
    const SPECTRUM_RELEASE = 0.24;
    let audioMix = 0; // 0 = static sphere, 1 = fully audio-reactive
    const HAZE_ATTACK = 0.25, HAZE_RELEASE = 0.9; // seconds
    const hazeLevels = [0, 0, 0]; // the backdrop's glow: lows, mids, highs, 0..1
    let shownExtent: SurfaceExtent | null = null; // eased, as the surface's springs ease

    const updateSpectrum = (dt: number) => {
      const spec = getSpectrumRef.current?.() ?? null;
      const attack = 1 - Math.exp(-dt / SPECTRUM_ATTACK);
      const release = 1 - Math.exp(-dt / SPECTRUM_RELEASE);
      let peak = 0;
      for (let b = 0; b < NUM_SPECTRUM_BANDS; ++b) {
        // One coefficient per band for ll, rr and lr together, so they stay a
        // consistent set (lr can't outgrow ll and rr).
        const inLL = spec ? spec.ll[b] : 0, inRR = spec ? spec.rr[b] : 0, inLR = spec ? spec.lr[b] : 0;
        const k = inLL + inRR > held.ll[b] + held.rr[b] ? attack : release;
        held.ll[b] += (inLL - held.ll[b]) * k;
        held.rr[b] += (inRR - held.rr[b]) * k;
        held.lr[b] += (inLR - held.lr[b]) * k;
        peak = Math.max(peak, held.ll[b], held.rr[b]);
      }

      // Blur across frequency (Gaussian, ~1.2 bands) so single harmonics and
      // band edges don't show up as rings on the sphere.
      for (const key of ["ll", "rr", "lr"] as const) {
        const src = held[key], dst = smoothed[key];
        for (let b = 0; b < NUM_SPECTRUM_BANDS; ++b) {
          let sum = 0;
          for (let j = -2; j <= 2; ++j) sum += BLUR[j + 2] * src[clamp(b + j, 0, NUM_SPECTRUM_BANDS - 1)];
          dst[b] = sum;
        }
      }

      const target = powerToLevel(peak) > 0.02 ? 1 : 0;
      audioMix += (target - audioMix) * (1 - Math.pow(0.94, dt * 60));

      // The backdrop's glow: the loudest band in each third of the spectrum, on
      // the same fixed scale as the dots, but slow enough to read as weather.
      hazeLevels.forEach((held, zone) => {
        let loudest = 0;
        for (let b = Math.floor((zone * NUM_SPECTRUM_BANDS) / 3); b < Math.floor(((zone + 1) * NUM_SPECTRUM_BANDS) / 3); ++b)
          loudest = Math.max(loudest, (smoothed.ll[b] + smoothed.rr[b]) / 2);
        const level = clamp((powerToLevel(loudest) - 0.2) / 0.6, 0, 1);
        hazeLevels[zone] = held + (level - held) * (1 - Math.exp(-dt / (level > held ? HAZE_ATTACK : HAZE_RELEASE)));
      });
    };

    // Each particle's radius rides a damped spring toward the EQ's shape.
    // Slightly under-damped, so the surface gives a soft, jelly-like bounce
    // when bands are added, moved or removed.
    const springOffset = new Float32Array(points.length); // current radius offset
    const springVelocity = new Float32Array(points.length);
    springOffset.fill(SPRING_UNSET, numBase);
    const SPRING_OMEGA = 2 * Math.PI * 4.5; // natural frequency (rad/s)
    const SPRING_DAMPING = 0.42; // damping ratio (< 1 = a little overshoot)
    const SPRING_STEP = 1 / 120; // max integration step (s), keeps the spring stable

    // Audio, on a fixed scale in level units.
    const LOUD_FLOOR = 0.2, LOUD_WIDTH = 0.6; // -68 dB to -32 dB per band
    const QUIET_ALPHA = 0.4, LOUD_ALPHA = 1.5, QUIET_SIZE = 0.75, LOUD_SIZE = 1.75;
    // How much of a dot's colour the EQ gets: full at this many dB of change,
    // on signal at least this loud coming in.
    const TINT_FULL_DB = 12;
    const TINT_FLOOR = 0.25, TINT_WIDTH = 0.3;

    // Display level per (band, longitude): computed once per frame on a small
    // grid, then each particle interpolates it. Much cheaper than evaluating
    // logs per particle, and smooth since the stereo weights vary smoothly.
    const LON_CELLS = 64;
    const levelTable = new Float32Array(NUM_SPECTRUM_BANDS * LON_CELLS);
    const cellLL = new Float32Array(LON_CELLS), cellRR = new Float32Array(LON_CELLS), cellLR = new Float32Array(LON_CELLS);
    for (let c = 0; c < LON_CELLS; ++c) {
      const lon = (c / LON_CELLS) * Math.PI * 2 - Math.PI;
      const phi = Math.PI / 4 + lon / 2;
      cellLL[c] = Math.cos(phi) ** 2;
      cellRR[c] = Math.sin(phi) ** 2;
      cellLR[c] = 2 * Math.cos(phi) * Math.sin(phi);
    }
    const lonPos = new Float32Array(points.length);
    points.forEach((p, i) => {
      lonPos[i] = ((Math.atan2(p.dir[0], p.dir[2]) + Math.PI) / (Math.PI * 2)) * LON_CELLS;
    });

    const updateLevelTable = () => {
      for (let b = 0; b < NUM_SPECTRUM_BANDS; ++b) {
        const ll = smoothed.ll[b], rr = smoothed.rr[b], lr = smoothed.lr[b];
        for (let c = 0; c < LON_CELLS; ++c)
          levelTable[b * LON_CELLS + c] = powerToLevel(Math.max(0, cellLL[c] * ll + cellRR[c] * rr + cellLR[c] * lr));
      }
    };

    // Particle i's level in band b (clamped), interpolated around in longitude.
    // Note: longitude -π and +π are both the back (Side), so the grid wraps.
    const levelInBand = (i: number, b: number) => {
      const row = clamp(b, 0, NUM_SPECTRUM_BANDS - 1) * LON_CELLS;
      const x = lonPos[i];
      const c0 = Math.floor(x) % LON_CELLS;
      const c1 = (c0 + 1) % LON_CELLS;
      const f = x - Math.floor(x);
      return levelTable[row + c0] + (levelTable[row + c1] - levelTable[row + c0]) * f;
    };

    const audioLevelAt = (i: number) => {
      const sp = specPos[i];
      const b = sp | 0;
      const t = sp - b;
      const p0 = levelInBand(i, b - 1), p1 = levelInBand(i, b), p2 = levelInBand(i, b + 1), p3 = levelInBand(i, b + 2);
      const v = p1 + 0.5 * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
      return clamp(v, 0, 1);
    };

    const bands: Band[] = [];
    // Band ids are plugin parameter slots: take the lowest free one.
    const freeSlot = () => {
      for (let id = 0; id < MAX_BANDS; ++id) if (!bands.some((b) => b.id === id)) return id;
      return -1;
    };

    replaceBandsRef.current = (incoming) => {
      // Don't yank a band out from under an active drag.
      if (draggedBand || knobDrag) return;
      const keep = (b: Band | null) => (b ? bands.find((x) => x.id === b.id) ?? null : null);
      bands.length = 0;
      for (const b of incoming) bands.push({ ...b });
      hoveredBand = keep(hoveredBand);
      selectedBand = keep(selectedBand);
      soloLatched = keep(soloLatched);
      soloHeld = keep(soloHeld);
      if (!restCentrePlaced) {
        restCentre = restCentreOfShape();
        restCentrePlaced = true;
      }
      wheelBand = keep(wheelBand);
    };
    let hoveredBand: Band | null = null;
    let selectedBand: Band | null = null; // the band last clicked: highlighted, and what the panel controls
    let soloLatched: Band | null = null, soloHeld: Band | null = null; // from the panel's S button, and Alt+hold on a node
    // A held solo wins over the panel's, and a band that's gone stops being soloed.
    const soloedBand = () => [soloHeld, soloLatched].find((b) => b && bands.indexOf(b) >= 0) ?? null;
    let panelOpen = false;
    let panelAmount = 0; // eased, so the panel slides up and down
    let panelFit: PanelViewFit = { zoomScale: 1, panDeltaY: 0 };
    let panelFitWidth = 0, panelFitHeight = 0;
    const CLICK_MAX_MS = 350, CLICK_MAX_TRAVEL = 10; // CSS px
    let recentring = false;
    let recentreFit: PanelViewFit = { zoomScale: 1, panDeltaY: 0 }; // where a recentre ends up with the panel open
    // Panel closed: how far below the window's centre the sphere's centre sits,
    // in sphere radii, so the EQ'd shape is what looks centred.
    let restCentre = 0, recentreRest = 0;
    let restCentrePlaced = false;
    let panelHover: PanelTarget | null = null;
    let knobDrag: { knob: number; lastY: number; position: number } | null = null;
    let draggedBand: Band | null = null;
    let wheelBand: Band | null = null;
    let lastWheelTime = 0;
    let slopeAccumulator = 0; // wheel delta toward the next cut-slope step
    let liveDynamics: Map<number, BandDynamics> | null = null;
    const sendBandsOut = () => {
      const soloed = soloedBand();
      onBandsChangeRef.current?.(bands.map((b) => ({ ...b, soloed: b === soloed })));
    };
    const presetBank = new PresetBank();
    let presetInfo = presetBank.info();
    let presetHover: PresetTarget | null = null;
    let presetMenuOpen = false, presetMenuScroll = 0, presetMenuAmount = 0;
    let presetTookPress = false; // so the second click of a double-click there can't reach the sphere
    let easingRestCentre = false;
    const notify = () => {
      presetBank.edited(bands);
      presetInfo = presetBank.info();
      sendBandsOut();
    };

    // View state: yaw spins around the vertical axis, pitch tilts toward the viewer.
    const view = { yaw: 0, pitch: -0.35, zoom: 0.905, yawVelocity: 0, pitchVelocity: 0, panX: 0, panY: 0 }; // pan in CSS px
    const AUTO_ROTATE = 0.0035; // radians per frame
    let rotating = false;
    let lastX = 0;
    let lastY = 0;
    // A press only becomes a drag (band move or rotation) once the pointer has
    // moved a few pixels, so clicks and double-clicks don't nudge anything.
    const DRAG_THRESHOLD = 4; // CSS px
    let pressed: { x: number; y: number; time: number; band: Band | null; offsetX: number; offsetY: number; radius: number } | null = null;
    // Recent rotation moves, to work out the fling speed on release.
    let rotateSamples: { t: number; dx: number; dy: number }[] = [];
    const ROTATE_SPEED = 0.008; // radians per CSS px
    // Auto-rotation stops while the pointer is over the sphere, so bands hold
    // still to be clicked, and eases back in a moment after it leaves.
    let pointerInside = false;
    let lastInteraction = 0; // seconds (performance.now)
    let autoBlend = 0;
    const AUTO_RESUME_DELAY = 1.5; // s after the pointer leaves / last interaction
    const touch = () => { lastInteraction = performance.now() / 1000; };

    let dpr = 1;

    const resize = () => {
      dpr = Math.min(window.devicePixelRatio || 1, 3);
      const width = Math.round(canvas.clientWidth * dpr), height = Math.round(canvas.clientHeight * dpr);
      if (canvas.width !== width) canvas.width = width;
      if (canvas.height !== height) canvas.height = height;
    };
    const observer = new ResizeObserver(resize);
    observer.observe(canvas);
    resize();

    // ---- view maths ------------------------------------------------------
    const radiusPx = () => Math.min(canvas.width, canvas.height) * SPHERE_RADIUS_FRACTION * view.zoom * (1 + (panelFit.zoomScale - 1) * panelAmount);
    const centreX = () => canvas.width / 2 + view.panX * dpr;
    const centreY = () => canvas.height / 2 + (view.panY + panelFit.panDeltaY * panelAmount) * dpr + restCentre * radiusPx() * (1 - panelAmount);
    // Keeps the sphere's centre inside the window, so it can never be lost off-screen.
    const clampPan = () => {
      view.panX = clamp(view.panX, -canvas.clientWidth / 2, canvas.clientWidth / 2);
      view.panY = clamp(view.panY, -canvas.clientHeight / 2, canvas.clientHeight / 2);
    };

    // Model space -> view space (yaw about Y, then pitch about X). +z faces the viewer.
    // Rotation trig, recomputed only when the view angle changes.
    let trigYaw = NaN, trigPitch = NaN, cy = 1, sy = 0, cp = 1, sp = 0;
    const syncTrig = () => {
      if (view.yaw === trigYaw && view.pitch === trigPitch) return;
      trigYaw = view.yaw;
      trigPitch = view.pitch;
      cy = Math.cos(trigYaw); sy = Math.sin(trigYaw);
      cp = Math.cos(trigPitch); sp = Math.sin(trigPitch);
    };

    const toView = ([x, y, z]: Vec3): Vec3 => {
      syncTrig();
      const x1 = x * cy + z * sy;
      const z1 = -x * sy + z * cy;
      return [x1, y * cp - z1 * sp, y * sp + z1 * cp];
    };

    const fromView = ([x1, y2, z2]: Vec3): Vec3 => {
      const cy = Math.cos(view.yaw), sy = Math.sin(view.yaw);
      const cp = Math.cos(view.pitch), sp = Math.sin(view.pitch);
      const y = y2 * cp + z2 * sp;
      const z1 = -y2 * sp + z2 * cp;
      return [x1 * cy - z1 * sy, y, x1 * sy + z1 * cy];
    };

    // View space -> canvas pixels (perspective), plus depth.
    const toScreen = ([x, y, z]: Vec3): Vec3 => {
      const scale = CAMERA_DISTANCE / (CAMERA_DISTANCE - z);
      const r = radiusPx();
      return [centreX() + x * r * scale, centreY() - y * r * scale, z];
    };

    // Canvas pixel -> unit direction (model space) of the point on a sphere of
    // the given radius under the pixel. When `clampToEdge` is set, positions
    // outside the silhouette snap to the sphere's edge.
    const pickSphere = (px: number, py: number, clampToEdge: boolean, radius = 1): Vec3 | null => {
      const r = radiusPx();
      const u = (px - centreX()) / r;
      const v = -(py - centreY()) / r;
      const D = CAMERA_DISTANCE;
      // Ray from (0,0,D) through (u,v,0) at t=1: P(t) = (t u, t v, D - t D).
      const a = u * u + v * v + D * D;
      const b = -2 * D * D;
      const c = D * D - radius * radius;
      const disc = b * b - 4 * a * c;
      let p: Vec3;
      if (disc >= 0) {
        const t = (-b - Math.sqrt(disc)) / (2 * a);
        p = [(t * u) / radius, (t * v) / radius, (D - t * D) / radius];
      } else if (clampToEdge) {
        const t = D * D / a; // closest approach to the centre
        const q: Vec3 = [t * u, t * v, D - t * D];
        const len = Math.hypot(...q);
        p = [q[0] / len, q[1] / len, q[2] / len];
      } else {
        return null;
      }
      return fromView(p);
    };

    // Radius the surface is pushed to at band b's own position.
    const radiusAtBand = (b: Band) => 1 + surfaceDisplacement(bands, bandDirs(), bandDirection(b));

    const toLatLon = ([x, y, z]: Vec3) => ({ lat: Math.asin(clamp(y, -1, 1)), lon: Math.atan2(x, z) });

    const bandDirs = () => bands.map(bandDirection);

    const bandScreenPos = (b: Band, dirs: Vec3[]) => {
      const d = bandDirection(b);
      const r = 1 + surfaceDisplacement(bands, dirs, d);
      const [sx, sy] = toScreen(toView([d[0] * r, d[1] * r, d[2] * r]));
      // Depth from the undisplaced direction: a node at the bottom of a deep
      // dent is near the centre, but it's still on the side it faces.
      return [sx, sy, toView(d)[2]] as Vec3;
    };

    // Radius to pick against when placing a node. Limited, because on the true
    // radius of a deep dent the whole sphere would fit in a few pixels of mouse travel.
    const handleRadius = (b: Band) => clamp(radiusAtBand(b), 0.8, 1.2);

    const eventPos = (e: MouseEvent): [number, number] => {
      const rect = canvas.getBoundingClientRect();
      return [(e.clientX - rect.left) * dpr, (e.clientY - rect.top) * dpr];
    };

    // Front-most band node under the pointer, if any.
    const hitBand = (px: number, py: number): Band | null => {
      const dirs = bandDirs();
      let best: Band | null = null;
      let bestZ = -Infinity;
      for (const b of bands) {
        const [sx, sy, z] = bandScreenPos(b, dirs);
        if (z < -0.15) continue; // on the far side
        if (Math.hypot(sx - px, sy - py) <= NODE_HIT_RADIUS * dpr && z > bestZ) {
          best = b;
          bestZ = z;
        }
      }
      return best;
    };

    // ---- input -----------------------------------------------------------
    // Ctrl (Cmd on macOS) + drag on a band: vertical drag sets its gain.
    const GAIN_DB_PER_PIXEL = 0.1;
    let gainDrag: { band: Band; startY: number; startGain: number } | null = null;
    let panDrag: { x: number; y: number } | null = null; // last pointer position while panning

    let helpHovered = false;
    let helpAmount = 0; // eased, so the panel fades in and out

    const overHelpButton = (e: MouseEvent) => {
      const rect = canvas.getBoundingClientRect();
      const scale = panelScale(rect.width, rect.height);
      return Math.hypot(e.clientX - rect.left - (rect.width - HELP_BUTTON_INSET * scale),
                        e.clientY - rect.top - (rect.height - HELP_BUTTON_INSET * scale)) <= (HELP_BUTTON_RADIUS + 3) * scale;
    };

    // The panel follows a band that goes away (removed, or replaced by the host).
    const validateSelection = () => {
      if (selectedBand && bands.indexOf(selectedBand) >= 0) return;
      selectedBand = bands.reduce<Band | null>((low, b) => (!low || b.lat < low.lat ? b : low), null);
    };
    // Steps through the bands in frequency order, wrapping round.
    const selectNeighbour = (direction: number) => {
      if (!bands.length) return;
      const order = [...bands].sort((a, b) => a.lat - b.lat);
      const current = selectedBand ? Math.max(0, order.indexOf(selectedBand)) : 0;
      selectedBand = order[(current + direction + order.length) % order.length];
    };
    // The EQ can make the shape very lopsided, so the panel fits what's on screen.
    const surfaceExtent = (ROWS = 36, COLUMNS = 72): SurfaceExtent => {
      const dirs = bandDirs();
      const extent = { top: 0, bottom: 0, halfWidth: 0 };
      for (let row = 0; row < ROWS; ++row) {
        const lat = -Math.PI / 2 + (Math.PI * row) / (ROWS - 1);
        for (let column = 0; column < COLUMNS; ++column) {
          const lon = (Math.PI * 2 * column) / COLUMNS;
          const d: Vec3 = [Math.cos(lat) * Math.sin(lon), Math.sin(lat), Math.cos(lat) * Math.cos(lon)];
          const r = surfaceRadius(1 + surfaceDisplacement(bands, dirs, d));
          const [x, y, z] = toView([d[0] * r, d[1] * r, d[2] * r]);
          const perspective = CAMERA_DISTANCE / (CAMERA_DISTANCE - z);
          const screenY = y * perspective;
          extent.halfWidth = Math.max(extent.halfWidth, Math.abs(x * perspective));
          extent.top = Math.max(extent.top, screenY);
          extent.bottom = Math.min(extent.bottom, screenY);
        }
      }
      return extent;
    };

    const restCentreOfShape = () => {
      const extent = surfaceExtent();
      return (extent.top + extent.bottom) / 2;
    };
    const updatePanelFit = () => {
      panelFitWidth = canvas.clientWidth;
      panelFitHeight = canvas.clientHeight;
      panelFit = fitViewAbovePanel(panelFitWidth, panelFitHeight, view.zoom, view.panY, SPHERE_RADIUS_FRACTION, surfaceExtent());
    };
    const panelLayout = (amount: number): BandPanelLayout => makeBandPanelLayout(canvas.clientWidth, canvas.clientHeight, amount);
    const panelTarget = (e: MouseEvent) => {
      const rect = canvas.getBoundingClientRect();
      return hitBandPanel(panelLayout(panelOpen ? 1 : 0), e.clientX - rect.left, e.clientY - rect.top, panelOpen, !!selectedBand && bands.indexOf(selectedBand) >= 0);
    };
    const presetLayout = (): PresetBarLayout => makePresetBarLayout(canvas.clientWidth, canvas.clientHeight, presetInfo.presets.length, presetMenuScroll);
    const presetTarget = (e: MouseEvent) => {
      const rect = canvas.getBoundingClientRect();
      return hitPresetBar(presetLayout(), e.clientX - rect.left, e.clientY - rect.top, presetMenuOpen);
    };

    // After the bank's current setup changed: the view eases over to centre the new shape.
    const showBankBands = () => {
      replaceBandsRef.current(presetBank.bands());
      presetInfo = presetBank.info();
      recentreRest = restCentreOfShape();
      easingRestCentre = true;
      sendBandsOut();
    };
    const presetError = (message: string) => showPresetDialog({ title: "3DQ", message, confirmLabel: "OK", onConfirm: () => {} });
    const loadedPreset = (ok: boolean) => {
      if (ok) showBankBands();
      else presetError("That preset couldn't be read.");
    };
    const saveUnderName = (name: string) => {
      if (!presetBank.saveAs(name)) presetError("That preset couldn't be saved.");
      presetInfo = presetBank.info();
    };
    const askToSaveAs = () => {
      const current = presetBank.name();
      showPresetDialog({
        title: "Save preset",
        initialText: isReservedPresetName(current) ? "New preset" : current,
        confirmLabel: "Save",
        onConfirm: (entered) => {
          const name = legalPresetName(entered);
          if (!name) return;
          if (isReservedPresetName(name)) presetError(`"${name}" is the built-in preset. Choose another name.`);
          else if (presetBank.exists(name)) showPresetDialog({ title: "Replace preset?", message: `"${name}" already exists.`, confirmLabel: "Replace", onConfirm: () => saveUnderName(name) });
          else saveUnderName(name);
        },
      });
    };

    // Returns true when the bar or its menu took the press.
    const presetPress = (e: PointerEvent) => {
      const target = presetTarget(e);
      if (!target) {
        const wasOpen = presetMenuOpen;
        presetMenuOpen = false;
        return wasOpen;
      }
      touch();
      if (e.button !== 0) return true;
      const info = presetInfo;
      switch (target.kind) {
        case "undo": if (info.canUndo && presetBank.undo()) showBankBands(); break;
        case "redo": if (info.canRedo && presetBank.redo()) showBankBands(); break;
        case "ab": presetBank.toggleAB(); showBankBands(); break;
        case "copy": presetBank.copyToOtherSlot(); presetInfo = presetBank.info(); break;
        case "previous": loadedPreset(presetBank.step(-1)); break;
        case "next": loadedPreset(presetBank.step(1)); break;
        case "name":
          presetMenuOpen = !presetMenuOpen;
          if (presetMenuOpen) {
            presetMenuScroll = Math.max(0, info.current - 5);
            presetBank.rescan();
            presetInfo = presetBank.info();
          }
          break;
        case "save":
          if (info.canSave) {
            if (!presetBank.save()) presetError("That preset couldn't be saved.");
            presetInfo = presetBank.info();
            presetMenuOpen = false;
          }
          break;
        case "saveAs": presetMenuOpen = false; askToSaveAs(); break;
        case "item": presetMenuOpen = false; loadedPreset(presetBank.load(target.index)); break;
        case "remove":
          if (!presetBank.remove(target.index)) presetError("That preset couldn't be removed.");
          presetInfo = presetBank.info();
          break;
        default: break;
      }
      return true;
    };

    const panelKnobCursor = () => (panelHover?.kind === "knob" || panelHover?.kind === "background" ? "default" : "pointer");

    const updateCursor = () => {
      if (knobDrag) { canvas.style.cursor = "ns-resize"; return; }
      if (presetHover) { canvas.style.cursor = presetHover.kind === "menuBackground" ? "default" : "pointer"; return; }
      if (panelHover) { canvas.style.cursor = panelKnobCursor(); return; }
      canvas.style.cursor = helpHovered ? "default" : gainDrag ? "ns-resize" : draggedBand || rotating || panDrag ? "grabbing" : hoveredBand ? "pointer" : "grab";
    };

    const onPointerDown = (e: PointerEvent) => {
      presetTookPress = presetPress(e);
      if (presetTookPress || overHelpButton(e)) return;
      touch();
      recentring = false;
      const target = panelTarget(e);
      if (target) {
        const band = selectedBand;
        if (e.button !== 0) return;
        switch (target.kind) {
          case "toggle":
            panelOpen = !panelOpen;
            if (panelOpen) updatePanelFit();
            else restCentre = restCentreOfShape(); // the panel's fit hides this until it slides away
            break;
          case "previous": selectNeighbour(-1); break;
          case "next": selectNeighbour(1); break;
          case "type": setBandType(band!, FILTER_TYPES[target.index]); notify(); break;
          case "stereo": band!.stereo = !band!.stereo; notify(); break;
          case "solo": soloLatched = soloLatched === band ? null : band; notify(); break;
          case "mute": band!.muted = !band!.muted; notify(); break;
          case "remove":
            bands.splice(bands.indexOf(band!), 1);
            selectedBand = null;
            validateSelection();
            notify();
            break;
          case "knob":
            if (knobEnabled(band!, target.index)) {
              canvas.setPointerCapture(e.pointerId);
              knobDrag = { knob: target.index, lastY: e.clientY, position: knobPosition(band!, target.index) };
              updateCursor();
            }
            break;
        }
        return;
      }
      if (e.button === 1) {
        canvas.setPointerCapture(e.pointerId);
        panDrag = { x: e.clientX, y: e.clientY };
        hoveredBand = null;
        view.yawVelocity = view.pitchVelocity = 0;
        updateCursor();
        return;
      }
      if (e.button !== 0) return; // right button is mute (contextmenu), not drag
      const [px, py] = eventPos(e);
      canvas.setPointerCapture(e.pointerId);
      const band = hitBand(px, py);
      if (band) selectedBand = band;
      if (band && !isCut(band) && (e.ctrlKey || e.metaKey)) {
        gainDrag = { band, startY: e.clientY, startGain: band.gainDb };
        hoveredBand = band;
        view.yawVelocity = view.pitchVelocity = 0;
        updateCursor();
        return;
      }
      // Remember where on the node it was grabbed, so the node doesn't jump
      // to centre itself under the cursor.
      let offsetX = 0, offsetY = 0, radius = 1;
      if (band) {
        radius = handleRadius(band);
        const d = bandDirection(band);
        const [sx, sy] = toScreen(toView([d[0] * radius, d[1] * radius, d[2] * radius]));
        offsetX = sx - px;
        offsetY = sy - py;
        hoveredBand = band;
        if (e.altKey) {
          soloHeld = band;
          notify();
        }
      }
      pressed = { x: e.clientX, y: e.clientY, time: performance.now(), band, offsetX, offsetY, radius };
      lastX = e.clientX;
      lastY = e.clientY;
      rotateSamples = [];
      view.yawVelocity = view.pitchVelocity = 0; // catching the sphere stops it
    };

    const onPointerMove = (e: PointerEvent) => {
      pointerInside = true;
      touch();
      if (knobDrag) {
        if (selectedBand) {
          knobDrag.position = clamp(knobDrag.position + (knobDrag.lastY - e.clientY) * (e.shiftKey ? KNOB_FINE_FACTOR : 1) / KNOB_PIXELS_FULL_RANGE, 0, 1);
          knobDrag.lastY = e.clientY;
          setKnobPosition(selectedBand, knobDrag.knob, knobDrag.position);
          notify();
        }
        return;
      }
      presetHover = pressed || gainDrag || panDrag || rotating || draggedBand ? null : presetTarget(e);
      if (presetHover) {
        panelHover = null;
        helpHovered = false;
        hoveredBand = null;
        updateCursor();
        return;
      }
      panelHover = pressed || gainDrag || panDrag || rotating || draggedBand ? null : panelTarget(e);
      if (panelHover) {
        helpHovered = false;
        hoveredBand = null;
        updateCursor();
        return;
      }
      const [px, py] = eventPos(e);
      helpHovered = !pressed && !gainDrag && !panDrag && overHelpButton(e);

      if (panDrag) {
        view.panX += e.clientX - panDrag.x;
        view.panY += e.clientY - panDrag.y;
        panDrag = { x: e.clientX, y: e.clientY };
        clampPan();
        return;
      }

      if (gainDrag) {
        const db = gainDrag.startGain + (gainDrag.startY - e.clientY) * GAIN_DB_PER_PIXEL;
        gainDrag.band.gainDb = Math.round(clamp(db, -MAX_GAIN_DB, MAX_GAIN_DB) * 10) / 10;
        notify();
        return;
      }

      if (pressed && !draggedBand && !rotating &&
          Math.hypot(e.clientX - pressed.x, e.clientY - pressed.y) >= DRAG_THRESHOLD) {
        if (pressed.band) draggedBand = pressed.band;
        else rotating = true;
      }

      if (draggedBand && pressed) {
        const p = pickSphere(px + pressed.offsetX, py + pressed.offsetY, true, pressed.radius)!;
        Object.assign(draggedBand, toLatLon(p));
        notify();
      } else if (rotating) {
        const dx = e.clientX - lastX;
        const dy = e.clientY - lastY;
        lastX = e.clientX;
        lastY = e.clientY;
        view.yaw += dx * ROTATE_SPEED;
        view.pitch += dy * ROTATE_SPEED;
        const t = performance.now();
        rotateSamples.push({ t, dx, dy });
        while (rotateSamples.length && t - rotateSamples[0].t > 100) rotateSamples.shift();
      } else if (!pressed) {
        hoveredBand = helpHovered ? null : hitBand(px, py);
      }

      updateCursor();
    };

    const onPointerUp = (e: PointerEvent) => {
      // A press this short counts as a click even if the hand drifted past DRAG_THRESHOLD.
      const clickedEmpty = e.type === "pointerup" && !!pressed && !pressed.band
        && (!rotating || (performance.now() - pressed.time < CLICK_MAX_MS
                          && Math.hypot(e.clientX - pressed.x, e.clientY - pressed.y) < CLICK_MAX_TRAVEL));
      if (rotating && !clickedEmpty) {
        // Fling with the speed of the last ~80 ms of movement; if the pointer
        // was held still before letting go, don't fling at all.
        const t = performance.now();
        const recent = rotateSamples.filter((r) => t - r.t <= 80);
        if (recent.length >= 2) {
          const span = Math.max(16, t - recent[0].t) / 1000; // seconds
          let dx = 0, dy = 0;
          for (const r of recent) { dx += r.dx; dy += r.dy; }
          // Velocities are radians per 1/60 s frame.
          view.yawVelocity = clamp((dx * ROTATE_SPEED) / span / 60, -0.15, 0.15);
          view.pitchVelocity = clamp((dy * ROTATE_SPEED) / span / 60, -0.15, 0.15);
        }
      }
      // A plain click on empty space clears the highlight. An open panel always
      // shows a band, so it keeps its selection.
      if (clickedEmpty && !panelOpen) selectedBand = null;

      if (soloHeld) {
        soloHeld = null;
        notify();
      }
      rotating = false;
      draggedBand = null;
      gainDrag = null;
      panDrag = null;
      knobDrag = null;
      pressed = null;
      touch();
      panelHover = panelTarget(e);
      presetHover = presetTarget(e);
      if (canvas.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
      const [px, py] = eventPos(e);
      helpHovered = overHelpButton(e);
      hoveredBand = helpHovered || panelHover || presetHover ? null : hitBand(px, py);
      updateCursor();
    };

    const onPointerLeave = () => {
      pointerInside = false;
      helpHovered = false;
      panelHover = null;
      presetHover = null;
      touch();
      if (!draggedBand) hoveredBand = null;
    };

    // Right-click a band to mute / unmute it. A muted band keeps its settings
    // but flattens out and stops affecting the sound.
    const onContextMenu = (e: MouseEvent) => {
      e.preventDefault();
      if (presetTookPress || overHelpButton(e)) return;
      touch();
      if (e.ctrlKey) return; // macOS reports Ctrl+click as a right-click; that's the gain drag
      if (panelTarget(e)) return;
      const [px, py] = eventPos(e);
      const hit = hitBand(px, py);
      if (!hit) return;
      selectedBand = hit;
      if (e.shiftKey) hit.stereo = !hit.stereo;
      else hit.muted = !hit.muted;
      notify();
    };

    const onDoubleClick = (e: MouseEvent) => {
      if (presetTookPress || overHelpButton(e)) return;
      touch();
      const target = panelTarget(e);
      if (target) {
        if (target.kind === "knob" && selectedBand && knobEnabled(selectedBand, target.index)) {
          resetKnob(selectedBand, target.index);
          notify();
        }
        return;
      }
      if (e.ctrlKey || e.metaKey || e.altKey) return; // two quick gain drags or solos shouldn't remove the band
      const [px, py] = eventPos(e);
      const hit = hitBand(px, py);

      // Shift+double-click a band: cycle Peak -> Low cut -> High cut -> Low shelf -> High shelf.
      if (hit && e.shiftKey) {
        const next = FILTER_TYPES[(FILTER_TYPES.indexOf(hit.type ?? "peak") + 1) % FILTER_TYPES.length];
        hit.type = next;
        hit.slope ??= DEFAULT_CUT_SLOPE;
        if (isShelf(hit)) hit.q = Math.min(hit.q, MAX_SHELF_Q);
        notify();
        return;
      }

      if (hit) {
        bands.splice(bands.indexOf(hit), 1);
        hoveredBand = null;
        notify();
        return;
      }

      const surface = pickSphere(px, py, false);
      if (!surface) {
        // The background: back to the centre, which with the panel open is the
        // middle of the space above it.
        recentring = true;
        recentreFit = fitViewAbovePanel(canvas.clientWidth, canvas.clientHeight, view.zoom, 0, SPHERE_RADIUS_FRACTION, surfaceExtent());
        recentreRest = restCentreOfShape();
        return;
      }
      const slot = freeSlot();
      if (slot < 0) return;
      const band: Band = { id: slot, ...toLatLon(surface), gainDb: 0, q: 1 };
      bands.push(band);
      // The new band's own bump lifts its node; re-pick on the lifted surface so
      // the node lands under the cursor.
      const lifted = pickSphere(px, py, true, handleRadius(band));
      if (lifted) Object.assign(band, toLatLon(lifted));
      hoveredBand = band;
      selectedBand = band;
      notify();
    };

    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      if (overHelpButton(e)) return;
      if (presetTarget(e)) {
        const layout = presetLayout(), rect = canvas.getBoundingClientRect();
        if (presetMenuOpen && presetMenuContains(layout, e.clientX - rect.left, e.clientY - rect.top)) {
          presetMenuScroll = clamp(layout.firstVisible + (e.deltaY > 0 ? 1 : -1), 0, Math.max(0, layout.presetCount - layout.visibleRows));
          presetHover = presetTarget(e);
        }
        return;
      }
      touch();
      // Some platforms turn shift+wheel into horizontal scrolling. Firefox can
      // report lines or pages instead of pixels; normalise to pixels (~100
      // per wheel notch) so steps feel the same everywhere.
      const unit = e.deltaMode === 1 ? 33 : e.deltaMode === 2 ? 800 : 1;
      const delta = (e.deltaY !== 0 ? e.deltaY : e.deltaX) * unit;

      const target = panelTarget(e);
      if (target) {
        if (target.kind === "knob" && selectedBand && knobEnabled(selectedBand, target.index)) {
          let change: number;
          if (knobIsStepped(selectedBand, target.index)) {
            // One step per notch; small trackpad deltas add up.
            slopeAccumulator += delta;
            const steps = Math.trunc(slopeAccumulator / 100);
            slopeAccumulator -= steps * 100;
            change = -steps / (CUT_SLOPES.length - 1);
          } else {
            change = (-delta / 100 / 120) * (e.shiftKey ? KNOB_FINE_FACTOR : 1);
          }
          setKnobPosition(selectedBand, target.index, knobPosition(selectedBand, target.index) + change);
          notify();
        }
        return;
      }
      const [px, py] = eventPos(e);

      // Gain changes move the node, so keep a scroll gesture locked to the band
      // it started on until the wheel has been still for a moment.
      const now = performance.now();
      const interval = now - lastWheelTime;
      if (!wheelBand || interval > 300 || bands.indexOf(wheelBand) < 0) wheelBand = hitBand(px, py);
      lastWheelTime = now;
      const band = wheelBand;

      // dB steps: 0.25 dB per wheel notch when scrolling slowly, for fine
      // adjustment, speeding up to 1.25 dB per notch when spinning the wheel
      // fast. Trackpads (small, frequent deltas) stay linear and fine.
      const notchy = Math.abs(delta) >= 50;
      const accel = notchy && interval < 250 ? clamp(90 / Math.max(interval, 1), 1, 5) : 1;
      const dbStep = -delta * 0.0025 * accel;
      const snap = (v: number) => Math.round(v * 100) / 100; // keep values tidy (0.01 dB)

      if (band && isCut(band)) {
        // Cuts: Shift+scroll steps the slope, one step per wheel notch (small
        // trackpad deltas add up). Gain, range and threshold don't apply.
        if (e.shiftKey) {
          slopeAccumulator += delta;
          const steps = Math.trunc(slopeAccumulator / 100);
          if (steps !== 0) {
            slopeAccumulator -= steps * 100;
            const i = CUT_SLOPES.indexOf(band.slope ?? DEFAULT_CUT_SLOPE);
            band.slope = CUT_SLOPES[clamp((i < 0 ? 3 : i) - steps, 0, CUT_SLOPES.length - 1)];
            notify();
          }
        }
        hoveredBand = band;
        return;
      }

      if (band) {
        // Alt+scroll: dynamic range. Ctrl (Cmd on macOS)+scroll: threshold.
        // Some systems turn Alt+wheel into horizontal scrolling; delta covers both.
        if (e.altKey) band.range = snap(clamp((band.range ?? 0) + dbStep, -MAX_GAIN_DB, MAX_GAIN_DB));
        else if (e.ctrlKey || e.metaKey)
          band.threshold = snap(clamp((band.threshold ?? DEFAULT_THRESHOLD_DB) + dbStep, MIN_THRESHOLD_DB, MAX_THRESHOLD_DB));
        else if (e.shiftKey) band.q = clamp(band.q * Math.exp(-delta * 0.002), MIN_Q, isShelf(band) ? MAX_SHELF_Q : MAX_Q);
        else band.gainDb = snap(clamp(band.gainDb + dbStep, -MAX_GAIN_DB, MAX_GAIN_DB));
        if (band.range !== undefined && Math.abs(band.range) < 0.05) band.range = 0;
        hoveredBand = band;
        notify();
        return;
      }

      view.zoom = clamp(view.zoom * Math.exp(-delta * 0.001), 0.25, 2.5);
    };

    // Stops the browser's middle-click autoscroll from taking over a pan.
    const onMouseDown = (e: MouseEvent) => { if (e.button === 1) e.preventDefault(); };
    canvas.addEventListener("mousedown", onMouseDown);
    canvas.addEventListener("pointerdown", onPointerDown);
    canvas.addEventListener("pointermove", onPointerMove);
    canvas.addEventListener("pointerup", onPointerUp);
    canvas.addEventListener("pointercancel", onPointerUp);
    canvas.addEventListener("pointerleave", onPointerLeave);
    canvas.addEventListener("dblclick", onDoubleClick);
    canvas.addEventListener("contextmenu", onContextMenu);
    canvas.addEventListener("wheel", onWheel, { passive: false });

    // ---- drawing ---------------------------------------------------------
    // Hover / drag emphasis per band id, eased so nodes grow and shrink smoothly.
    const nodeEmphasis = new Map<number, number>();

    // A band node, styled like a small hardware control:
    //   core      solid ball in the band's colour (orange boost, red cut, grey at 0 dB)
    //   gain arc  a ring around it filling clockwise for boost, anticlockwise for
    //             cut, in proportion to the gain (full ring = ±MAX_GAIN_DB)
    //   muted     hollow grey core, dimmed track, no arc
    const drawNode = (b: Band, dirs: Vec3[], dt: number) => {
      const [sx, sy, z] = bandScreenPos(b, dirs);
      const selected = b === selectedBand;
      // Fade smoothly as the node turns away round the back; the selected band stays visible.
      const facing = Math.max(selected ? 0.75 : 0, 0.22 + 0.78 * clamp((z + 0.3) / 0.45, 0, 1));

      const target = b === draggedBand ? 1 : b === hoveredBand ? 0.7 : selected ? 0.85 : 0;
      const prev = nodeEmphasis.get(b.id) ?? 0;
      const emphasis = prev + (target - prev) * (1 - Math.exp(-dt / 0.08));
      nodeEmphasis.set(b.id, emphasis);

      const core = (5.5 + 2 * emphasis) * dpr;
      const ringR = core + (5 + 1.5 * emphasis) * dpr;
      const cut = isCut(b);
      const live = displayGain(b);
      // Arc: gain for peaks; for cuts, the slope (a full ring = 48 dB/oct).
      const amount = cut ? (b.slope ?? DEFAULT_CUT_SLOPE) / 48 : clamp(live / MAX_GAIN_DB, -1, 1);
      const neutral = !cut && Math.abs(live) < 0.05;
      const colour: RGB = b.muted || neutral ? WARM_GREY : cut || live < 0 ? CUT_COLOUR : BOOST_COLOUR;
      const range = cut ? 0 : b.range ?? 0;

      ctx.globalAlpha = 1;

      // Selected: a soft pale halo.
      if (selected) {
        const glowR = ringR * 2.8;
        const glow = ctx.createRadialGradient(sx, sy, 0, sx, sy, glowR);
        glow.addColorStop(0, `rgba(255,252,244,${0.3 * facing})`);
        glow.addColorStop(1, "rgba(255,252,244,0)");
        ctx.globalCompositeOperation = "source-over";
        ctx.fillStyle = glow;
        ctx.beginPath();
        ctx.arc(sx, sy, glowR, 0, Math.PI * 2);
        ctx.fill();
      }

      // Soft glow (skipped when muted).
      if (!b.muted) {
        const glowR = ringR * 2.2;
        const glow = ctx.createRadialGradient(sx, sy, core * 0.5, sx, sy, glowR);
        glow.addColorStop(0, rgbString(colour, (0.28 + 0.2 * emphasis) * facing));
        glow.addColorStop(1, rgbString(colour, 0));
        ctx.globalCompositeOperation = "source-over";
        ctx.fillStyle = glow;
        ctx.beginPath();
        ctx.arc(sx, sy, glowR, 0, Math.PI * 2);
        ctx.fill();
      }

      ctx.globalCompositeOperation = "source-over";
      ctx.globalAlpha = facing;
      ctx.lineCap = "round";

      // Backing disc so the ring reads over bright particles.
      ctx.beginPath();
      ctx.arc(sx, sy, ringR + 2.5 * dpr, 0, Math.PI * 2);
      ctx.fillStyle = "rgba(17,17,16,0.55)";
      ctx.fill();

      // Track.
      ctx.beginPath();
      ctx.arc(sx, sy, ringR, 0, Math.PI * 2);
      ctx.lineWidth = (selected ? 1.8 : 1.5) * dpr;
      ctx.strokeStyle = selected ? "rgba(255,252,244,0.9)" : b.muted ? "rgba(138,134,124,0.25)" : "rgba(232,228,218,0.22)";
      ctx.stroke();

      // Gain arc from 12 o'clock, showing the band's gain right now (it moves
      // with the music on a dynamic band).
      const start = -Math.PI / 2;
      const angleFor = (db: number) => start + clamp(db / MAX_GAIN_DB, -1, 1) * Math.PI * 2;
      if (!b.muted && !neutral) {
        const sweep = amount * Math.PI * 2;
        ctx.beginPath();
        ctx.arc(sx, sy, ringR, start, start + sweep, sweep < 0);
        ctx.lineWidth = 2.2 * dpr;
        ctx.strokeStyle = rgbString(colour);
        ctx.stroke();
      }

      // Dynamic band: a thin outer arc spanning how far the gain can move
      // (static gain -> static + range), in the colour of the movement, with a
      // tick at the static gain.
      if (range !== 0) {
        const outer = ringR + 3.5 * dpr;
        const from = angleFor(b.gainDb), to = angleFor(b.gainDb + range);
        const rangeColour = range > 0 ? BOOST_COLOUR : CUT_COLOUR;
        ctx.beginPath();
        ctx.arc(sx, sy, outer, from, to, to < from);
        ctx.lineWidth = 1.4 * dpr;
        ctx.setLineDash([2 * dpr, 2 * dpr]);
        ctx.strokeStyle = rgbString(rangeColour, b.muted ? 0.3 : 0.75);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.beginPath();
        ctx.moveTo(sx + Math.cos(from) * (outer - 2 * dpr), sy + Math.sin(from) * (outer - 2 * dpr));
        ctx.lineTo(sx + Math.cos(from) * (outer + 2 * dpr), sy + Math.sin(from) * (outer + 2 * dpr));
        ctx.lineWidth = 1.4 * dpr;
        ctx.strokeStyle = "rgba(232,228,218,0.8)";
        ctx.stroke();
      }

      // Core ball.
      ctx.beginPath();
      ctx.arc(sx, sy, core, 0, Math.PI * 2);
      if (b.muted) {
        ctx.fillStyle = "#1a1a18";
        ctx.fill();
        ctx.lineWidth = 1.5 * dpr;
        ctx.strokeStyle = "#6e6b64";
        ctx.stroke();
        const k = core * 0.5;
        ctx.beginPath();
        ctx.moveTo(sx - k, sy + k);
        ctx.lineTo(sx + k, sy - k);
        ctx.stroke();
      } else {
        // Flat colour with a soft highlight toward the top-left, so it reads
        // as a ball without heavy shading.
        const hx = sx - core * 0.35, hy = sy - core * 0.4;
        const shade = ctx.createRadialGradient(hx, hy, 0, hx, hy, core * 1.53);
        shade.addColorStop(0, rgbString(lerpRGB(colour, [255, 255, 255], 0.45)));
        shade.addColorStop(0.5, rgbString(colour));
        shade.addColorStop(1, rgbString(lerpRGB(colour, [17, 17, 16], 0.3)));
        ctx.fillStyle = shade;
        ctx.fill();
        if (emphasis > 0.01) {
          ctx.lineWidth = 1.2 * dpr;
          ctx.strokeStyle = `rgba(255,252,244,${0.85 * emphasis})`;
          ctx.stroke();
        }
      }

      // Stereo band: a dot either side of the ring, for the two channels it acts on.
      if (b.stereo) {
        ctx.fillStyle = `rgba(235,232,224,${(b.muted ? 0.35 : 0.85) * facing})`;
        for (const side of [-1, 1]) {
          ctx.beginPath();
          ctx.arc(sx + side * (ringR + 4.5 * dpr), sy, 1.7 * dpr, 0, Math.PI * 2);
          ctx.fill();
        }
      }

      // Cut glyph on the core: a slope rising to a flat top (low cut), or
      // falling from one (high cut).
      if (cut) {
        const k = core * 0.55;
        const dir = b.type === "lowcut" ? 1 : -1;
        ctx.beginPath();
        ctx.moveTo(sx - dir * k, sy + k * 0.7);
        ctx.lineTo(sx - dir * k * 0.1, sy - k * 0.45);
        ctx.lineTo(sx + dir * k, sy - k * 0.45);
        ctx.lineWidth = 1.4 * dpr;
        ctx.lineJoin = "round";
        ctx.strokeStyle = b.muted ? "#6e6b64" : "rgba(17,17,16,0.85)";
        ctx.stroke();
      }

      // Shelf glyph: a step, raised on the shelf's side when boosting and
      // lowered when cutting.
      if (isShelf(b)) {
        const k = core * 0.55;
        const dir = b.type === "lowshelf" ? 1 : -1;
        const rise = (live < 0 ? -1 : 1) * k * 0.45;
        ctx.beginPath();
        ctx.moveTo(sx - dir * k, sy - rise);
        ctx.lineTo(sx - dir * k * 0.25, sy - rise);
        ctx.lineTo(sx + dir * k * 0.25, sy + rise);
        ctx.lineTo(sx + dir * k, sy + rise);
        ctx.lineWidth = 1.4 * dpr;
        ctx.lineJoin = "round";
        ctx.strokeStyle = b.muted ? "#6e6b64" : neutral ? "rgba(235,232,224,0.8)" : "rgba(17,17,16,0.85)";
        ctx.stroke();
      }

      ctx.globalAlpha = 1;
    };

    const drawLabel = (b: Band, dirs: Vec3[]) => {
      const [sx, sy] = bandScreenPos(b, dirs);
      const lines = isCut(b)
        ? [
            `${b.type === "lowcut" ? "Low cut" : "High cut"}  ${formatFreq(latToFreq(b.lat))}`,
            `${b.slope ?? DEFAULT_CUT_SLOPE} dB/oct  ·  ${b.stereo ? "Stereo" : panLabel(b.lon)}`,
            midSideLabel(b.lon),
          ]
        : [
            `${b.type === "lowshelf" ? "Low shelf  " : b.type === "highshelf" ? "High shelf  " : ""}${formatFreq(latToFreq(b.lat))}  ${formatGain(b.gainDb)}`,
            `Q ${b.q.toFixed(2)}  ·  ${b.stereo ? "Stereo" : panLabel(b.lon)}`,
            midSideLabel(b.lon),
          ];
      if (b.stereo) lines.splice(2, 1); // Mid / Side share doesn't apply
      if (b.range && !isCut(b)) {
        lines.push(`Dyn ${formatGain(b.range)}  ·  Thresh ${formatDb(b.threshold ?? DEFAULT_THRESHOLD_DB)} dB`);
        const dyn = liveDynamics?.get(b.id);
        if (dyn) lines.push(`Now ${formatGain(dyn.gainDb)}  ·  Level ${Math.max(-99, dyn.levelDb).toFixed(1)} dB`);
      }
      if (b.muted) lines.push("Muted · right-click to unmute");
      const fontPx = 12 * dpr;
      ctx.font = `${fontPx}px ${UI_FONT}`;
      const padding = 6 * dpr;
      const lineHeight = Math.round(fontPx * 1.35); // whole pixels, so the lines space evenly
      const width = Math.max(...lines.map((l) => ctx.measureText(l).width)) + padding * 2;
      const height = lineHeight * lines.length + padding * 2 - (lineHeight - fontPx);
      let x = sx + 16 * dpr;
      let y = sy - height - 10 * dpr;
      if (x + width > canvas.width - 8 * dpr) x = sx - width - 16 * dpr;
      if (y < 8 * dpr) y = sy + 14 * dpr;

      ctx.globalCompositeOperation = "source-over";
      ctx.globalAlpha = 1;
      ctx.fillStyle = PANEL_FILL;
      ctx.strokeStyle = PANEL_STROKE;
      ctx.lineWidth = 1 * dpr;
      ctx.beginPath();
      ctx.roundRect(x, y, width, height, 6 * dpr);
      ctx.fill();
      ctx.stroke();
      ctx.fillStyle = TEXT_COLOUR;
      ctx.textBaseline = "top";
      lines.forEach((l, i) => ctx.fillText(l, x + padding, y + padding + i * lineHeight));
    };

    // The ? button, and the list of controls that appears while hovering over it.
    const drawHelp = (dt: number) => {
      helpAmount += ((helpHovered ? 1 : 0) - helpAmount) * (1 - Math.exp(-dt / 0.08));

      const unit = dpr * panelScale(canvas.width / dpr, canvas.height / dpr); // one scaled point, in device pixels
      const cx = canvas.width - HELP_BUTTON_INSET * unit;
      const cy = canvas.height - HELP_BUTTON_INSET * unit;
      const radius = HELP_BUTTON_RADIUS * unit;
      const graphite: RGB = [17, 17, 16];
      const text: RGB = [232, 228, 218];

      ctx.globalCompositeOperation = "source-over";
      ctx.globalAlpha = 1;
      ctx.beginPath();
      ctx.arc(cx, cy, radius, 0, Math.PI * 2);
      ctx.fillStyle = rgbString(graphite, 0.7);
      ctx.fill();
      ctx.lineWidth = unit;
      ctx.strokeStyle = rgbString(WARM_GREY, 0.45 + 0.4 * helpAmount);
      ctx.stroke();
      ctx.font = `bold ${12 * unit}px ${UI_FONT}`;
      ctx.fillStyle = rgbString(text, 0.55 + 0.45 * helpAmount);
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText("?", cx, cy);
      ctx.textAlign = "left";

      if (helpAmount < 0.01) return;

      ctx.font = `${12 * unit}px ${UI_FONT}`;
      const padding = 10 * unit, gap = 18 * unit, lineHeight = Math.round(12 * unit * 1.45);
      const gestureWidth = Math.max(...HELP_ROWS.map(([gesture]) => ctx.measureText(gesture).width));
      const actionWidth = Math.max(...HELP_ROWS.map(([, action]) => ctx.measureText(action).width));
      const width = padding * 2 + gestureWidth + gap + actionWidth;
      const height = padding * 2 + lineHeight * HELP_ROWS.length;
      const x = Math.max(8 * unit, cx + radius - width);
      const y = Math.max(8 * unit, cy - radius - 8 * unit - height);

      ctx.fillStyle = rgbString(graphite, 0.92 * helpAmount);
      ctx.strokeStyle = rgbString(WARM_GREY, 0.45 * helpAmount);
      ctx.lineWidth = unit;
      ctx.beginPath();
      ctx.roundRect(x, y, width, height, 6 * unit);
      ctx.fill();
      ctx.stroke();

      ctx.textBaseline = "top";
      HELP_ROWS.forEach(([gesture, action], i) => {
        const rowY = y + padding + i * lineHeight;
        ctx.fillStyle = rgbString(text, 0.6 * helpAmount);
        ctx.fillText(gesture, x + padding, rowY);
        ctx.fillStyle = rgbString(text, helpAmount);
        ctx.fillText(action, x + padding + gestureWidth + gap, rowY);
      });
    };

    // A band's vortex: rings of particles lying on the (bumped or dented)
    // surface around the band, swirling slowly. Around a cut they sink into the
    // hole, around a boost they ride up the hill; the motion makes the depth read.
    const VORTEX_RINGS = 6;
    const VORTEX_POINTS = 56;
    const CUT_CONTOURS_DB = [1.5, 3, 6, 12, 18, 24, 30];
    const CUT_FLOOR_RING_SPACING = 0.26; // radians of latitude
    const CUT_FLOOR_RING_ALPHA = 0.3; // quieter than the contours on the slope
    const CUT_RING_POLE_LIMIT = Math.PI / 2 - 0.08; // rings shrink to nothing at the pole
    const RADIANS_PER_OCTAVE = Math.PI / Math.log2(MAX_FREQ / MIN_FREQ);

    // Vortex visibility per band id, eased so muting fades the rings out.
    const vortexAmount = new Map<number, number>();

    // A cut's vortex: swirling rings at constant latitude down the carved cap.
    // The first ones are contours at fixed depths, so they bunch up on a steep
    // slope and spread on a gentle one; past the last contour the response is
    // off the display scale, so evenly spaced rings carry on across the floor.
    const drawCutRings = (b: Band, dirs: Vec3[], time: number, amount: number) => {
      if (amount < 0.01) return;
      const type = b.type!;
      const slope = b.slope ?? DEFAULT_CUT_SLOPE;
      const towardPole = type === "lowcut" ? -1 : 1;
      const count = CUT_CONTOURS_DB.length;
      let lat = b.lat;
      for (let k = 0; ; ++k) {
        if (k < count) {
          // Butterworth inverted: octaves past the cutoff at which it reaches this depth.
          const octaves = Math.log2(Math.pow(10, CUT_CONTOURS_DB[k] / 10) - 1) / (2 * (slope / 6));
          lat = b.lat + towardPole * octaves * RADIANS_PER_OCTAVE;
        } else {
          lat += towardPole * CUT_FLOOR_RING_SPACING;
        }
        if (towardPole * lat > CUT_RING_POLE_LIMIT) break;
        if (towardPole * lat < -CUT_RING_POLE_LIMIT) continue;

        const ringDb = cutResponseDb(type, latToFreq(b.lat), slope, latToFreq(lat));
        const style = ringStyle(ringDb);
        const shape = -gainShape(ringDb);
        const ringFade = amount * (k < count ? 0.35 + 0.55 * shape : CUT_FLOOR_RING_ALPHA);
        const t = (k + 1) / VORTEX_RINGS;
        const phase = -time * 0.5 / (0.35 + t) + t * 2.4; // same drift as a peak's rings
        drawLatitudeRing(b, dirs, lat, phase, style, ringFade);
      }
    };

    // Particles round a circle of constant latitude, only where the band acts.
    const drawLatitudeRing = (b: Band, dirs: Vec3[], lat: number, phase: number, style: ReturnType<typeof ringStyle>, fade: number) => {
      const baseSize = radiusPx() * 0.05;
      const sinLat = Math.sin(lat), cosLat = Math.cos(lat);
      const points = Math.max(12, Math.round(76 * cosLat)); // even spacing as rings shrink
      for (let j = 0; j < points; ++j) {
        const lon = (j / points) * Math.PI * 2 + phase;
        const overlap = bandOverlap(b, lon);
        if (overlap < 0.02) continue;
        const dir: Vec3 = [cosLat * Math.sin(lon), sinLat, cosLat * Math.cos(lon)];
        const r = 1 + surfaceDisplacement(bands, dirs, dir);
        const [sx, sy, z] = toScreen(toView([dir[0] * r, dir[1] * r, dir[2] * r]));
        const facing = 0.2 + 0.8 * clamp((z + 0.5) / 1.0, 0, 1);
        const size = baseSize * 1.2 * (CAMERA_DISTANCE / (CAMERA_DISTANCE - z));
        sink.add(style, sx, sy, size, facing * fade * overlap);
      }
    };

    // A stereo peak's vortex: rings round the sphere on either side of its latitude.
    const BELT_RINGS_PER_SIDE = 3;
    const drawBeltRings = (b: Band, dirs: Vec3[], time: number, strength: number) => {
      const gain = displayGain(b);
      const w = bandWidth(b);
      const spin = gain > 0 ? 0.5 : -0.5;
      for (let k = -BELT_RINGS_PER_SIDE; k <= BELT_RINGS_PER_SIDE; ++k) {
        const offset = k / BELT_RINGS_PER_SIDE;
        const angle = offset * w * 1.5;
        const lat = b.lat + angle;
        if (Math.abs(lat) > CUT_RING_POLE_LIMIT) continue;
        const t = Math.abs(offset);
        const phase = time * spin / (0.35 + t) + offset * 2.4;
        const ringFade = strength * (1.15 - t * 0.75);
        drawLatitudeRing(b, dirs, lat, phase, ringStyle(gain * Math.exp(-2 * (1 - Math.cos(angle)) / (w * w))), ringFade);
      }
    };

    // A shelf's vortex: evenly spaced rings from the shelf frequency out to its
    // pole, lying on the raised or lowered surface and fading with its height.
    const drawShelfRings = (b: Band, dirs: Vec3[], time: number, amount: number) => {
      const gain = displayGain(b);
      if (Math.abs(gainShape(gain)) * amount < 0.01) return;
      const towardPole = b.type === "lowshelf" ? -1 : 1;
      const spin = gain > 0 ? 0.5 : -0.5;
      for (let k = 0; ; ++k) {
        const lat = b.lat + towardPole * k * CUT_FLOOR_RING_SPACING;
        if (towardPole * lat > CUT_RING_POLE_LIMIT) break;
        if (towardPole * lat < -CUT_RING_POLE_LIMIT) continue;
        const ringDb = shelfResponseDb(b, gain, latToFreq(lat));
        const style = ringStyle(ringDb);
        const ringFade = amount * Math.min(1, Math.abs(gainShape(ringDb)) * 1.4);
        const t = (k + 1) / VORTEX_RINGS;
        const phase = time * spin / (0.35 + t) + t * 2.4;
        drawLatitudeRing(b, dirs, lat, phase, style, ringFade);
      }
    };

    const drawVortex = (b: Band, dirs: Vec3[], time: number, dt: number) => {
      const previous = vortexAmount.get(b.id) ?? (b.muted ? 0 : 1);
      const amount = previous + ((b.muted ? 0 : 1) - previous) * (1 - Math.exp(-dt / 0.12));
      vortexAmount.set(b.id, amount);
      if (isCut(b)) { drawCutRings(b, dirs, time, amount); return; }
      if (isShelf(b)) { drawShelfRings(b, dirs, time, amount); return; }
      const strength = Math.abs(gainShape(displayGain(b))) * amount;
      if (strength < 0.01) return;

      if (b.stereo) { drawBeltRings(b, dirs, time, strength); return; }

      const gain = displayGain(b);
      const c = bandDirection(b);
      const ref: Vec3 = Math.abs(c[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
      const e1raw: Vec3 = [ref[1] * c[2] - ref[2] * c[1], ref[2] * c[0] - ref[0] * c[2], ref[0] * c[1] - ref[1] * c[0]];
      const e1len = Math.hypot(...e1raw);
      const e1: Vec3 = [e1raw[0] / e1len, e1raw[1] / e1len, e1raw[2] / e1len];
      const e2: Vec3 = [c[1] * e1[2] - c[2] * e1[1], c[2] * e1[0] - c[0] * e1[2], c[0] * e1[1] - c[1] * e1[0]];

      const w = bandWidth(b);
      const spin = displayGain(b) > 0 ? 0.5 : -0.5; // radians per second; boosts and cuts turn opposite ways
      const baseSize = radiusPx() * 0.05;

      for (let k = 0; k < VORTEX_RINGS; ++k) {
        const t = (k + 1) / VORTEX_RINGS;
        const alpha = t * w * 1.5; // angular radius of this ring
        const sa = Math.sin(alpha), ca = Math.cos(alpha);
        const phase = time * spin / (0.35 + t) + t * 2.4; // inner rings turn faster; offset gives a spiral
        const ringFade = strength * (1.15 - t * 0.75);
        const style = ringStyle(gain * Math.exp(-2 * (1 - ca) / (w * w))); // the bump's own dB at this ring

        for (let j = 0; j < VORTEX_POINTS; ++j) {
          const beta = (j / VORTEX_POINTS) * Math.PI * 2 + phase;
          const cb = Math.cos(beta) * sa, sb = Math.sin(beta) * sa;
          const dir: Vec3 = [c[0] * ca + e1[0] * cb + e2[0] * sb,
                             c[1] * ca + e1[1] * cb + e2[1] * sb,
                             c[2] * ca + e1[2] * cb + e2[2] * sb];
          const r = 1 + surfaceDisplacement(bands, dirs, dir);
          const [sx, sy, z] = toScreen(toView([dir[0] * r, dir[1] * r, dir[2] * r]));
          const facing = 0.2 + 0.8 * clamp((z + 0.5) / 1.0, 0, 1);
          const size = baseSize * 1.2 * (CAMERA_DISTANCE / (CAMERA_DISTANCE - z));
          sink.add(style, sx, sy, size, facing * ringFade);
        }
      }
    };

    let frame = 0;
    let lastTime = performance.now() / 1000;
    let statsTime = lastTime;
    let frameCount = 0;
    let cpuTotal = 0; // ms of JS work per frame, summed over the stats window
    let specTotal = 0, loopTotal = 0; // spectrum update / particle loop parts of it
    const render = () => {
      const cpuStart = performance.now();
      frame = requestAnimationFrame(render);
      const time = performance.now() / 1000;
      const dt = Math.min(0.1, Math.max(0.001, time - lastTime));
      lastTime = time;
      const frameScale = dt * 60; // per-frame constants below were tuned at 60 fps

      // Inertia after a drag (a fling keeps spinning briefly), then a slow
      // auto-rotation that only runs while the pointer is away from the sphere.
      // It stops quickly when the pointer comes in and eases back in slowly.
      const holding = rotating || draggedBand !== null || hoveredBand !== null;
      const autoTarget = !pointerInside && !holding && time - lastInteraction > AUTO_RESUME_DELAY ? 1 : 0;
      autoBlend += (autoTarget - autoBlend) * (1 - Math.exp(-dt / (autoTarget > autoBlend ? 1.2 : 0.12)));
      if (!rotating) {
        view.yaw += view.yawVelocity * frameScale;
        view.pitch += view.pitchVelocity * frameScale;
        // Fling inertia dies off faster over a node so it can be grabbed.
        const friction = hoveredBand ? 0.8 : 0.94;
        view.yawVelocity *= Math.pow(friction, frameScale);
        view.pitchVelocity *= Math.pow(friction, frameScale);
        view.yaw += AUTO_ROTATE * autoBlend * frameScale;
      }
      view.pitch = clamp(view.pitch, -1.4, 1.4);
      clampPan(); // the window may have shrunk

      const w = canvas.width, h = canvas.height;
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalCompositeOperation = "source-over";
      ctx.globalAlpha = 1;
      ctx.clearRect(0, 0, w, h);
      ctx.globalCompositeOperation = "lighter";


      // Dynamic bands: take their gain right now from the audio side.
      liveDynamics = getDynamicsRef.current?.() ?? null;
      for (const b of bands) {
        const dyn = b.range ? liveDynamics?.get(b.id) : undefined;
        b.liveGainDb = dyn && !b.muted ? dyn.gainDb : undefined;
      }

      const dirs = bandDirs();
      const baseSize = radiusPx() * 0.05;

      const tSpec0 = performance.now();
      updateSpectrum(dt);
      const reactive = audioMix > 0.001;
      if (reactive) updateLevelTable();
      const steps = Math.ceil(dt / SPRING_STEP);
      const stepDt = dt / steps;
      const kSpring = SPRING_OMEGA * SPRING_OMEGA;
      const cSpring = 2 * SPRING_DAMPING * SPRING_OMEGA;

      const useGl = glSink !== null && glSink.available;
      sink = bench ? nullSink : useGl ? glSink : canvasSink;
      canvas.dataset.renderer = useGl ? "webgl" : "canvas";

      // Dev-only readout (the element only exists in development builds).
      frameCount++;
      if (time - statsTime >= 0.5) {
        const el = document.getElementById("dev-stats");
        if (el) el.textContent = `${useGl ? "WebGL (three.js)" : "Canvas 2D fallback"} · ${points.length} particles · ` +
          `${Math.round(frameCount / (time - statsTime))} fps · ${(cpuTotal / frameCount).toFixed(1)} ms CPU ` +
          `(spectrum ${(specTotal / frameCount).toFixed(1)}, particles ${(loopTotal / frameCount).toFixed(1)})`;
        frameCount = 0;
        cpuTotal = specTotal = loopTotal = 0;
        statsTime = time;
      }
      sink.begin(w, h);

      const tLoop0 = performance.now();
      specTotal += tLoop0 - tSpec0;
      const panelGoal = panelOpen ? 1 : 0;
      panelAmount += (panelGoal - panelAmount) * (1 - Math.exp(-dt / 0.08));
      if (Math.abs(panelGoal - panelAmount) < 0.002) panelAmount = panelGoal;
      if (easingRestCentre && !recentring) {
        restCentre = recentreRest + (restCentre - recentreRest) * Math.exp(-dt / 0.12);
        if (Math.abs(restCentre - recentreRest) < 0.002) {
          restCentre = recentreRest;
          easingRestCentre = false;
        }
      }

      if (recentring) {
        const keep = Math.exp(-dt / 0.09);
        view.panX *= keep;
        view.panY *= keep;
        panelFit = {
          zoomScale: recentreFit.zoomScale + (panelFit.zoomScale - recentreFit.zoomScale) * keep,
          panDeltaY: recentreFit.panDeltaY + (panelFit.panDeltaY - recentreFit.panDeltaY) * keep,
        };
        restCentre = recentreRest + (restCentre - recentreRest) * keep;
        if (Math.hypot(view.panX, view.panY) < 0.5 && Math.abs(panelFit.panDeltaY - recentreFit.panDeltaY) < 0.5 && Math.abs(restCentre - recentreRest) < 0.002) {
          view.panX = view.panY = 0;
          panelFit = recentreFit;
          restCentre = recentreRest;
          recentring = false;
        }
      }
      if (panelOpen && (canvas.clientWidth !== panelFitWidth || canvas.clientHeight !== panelFitHeight)) updatePanelFit();

      // Bands, prepared once per frame. Muted bands drop out.
      let nb = 0, nc = 0, ns = 0, nr = 0;
      for (const b of bands) {
        if (b.muted) continue;
        if (isCut(b)) {
          cLow[nc] = b.type === "lowcut" ? 1 : 0;
          cFc[nc] = latToFreq(b.lat);
          cTwoN[nc] = (2 * (b.slope ?? DEFAULT_CUT_SLOPE)) / 6;
          cCos[nc] = b.stereo ? 0 : Math.cos(b.lon);
          cSin[nc] = b.stereo ? 0 : Math.sin(b.lon);
          cBias[nc] = b.stereo ? 2 : 1; // makes the overlap below 1 everywhere
          nc++;
          continue;
        }
        if (isShelf(b)) {
          sLow[ns] = b.type === "lowshelf" ? 1 : 0;
          sFc[ns] = latToFreq(b.lat);
          sA[ns] = shelfA(displayGain(b));
          sK[ns] = shelfK(sA[ns], b.q);
          sCos[ns] = b.stereo ? 0 : Math.cos(b.lon);
          sSin[ns] = b.stereo ? 0 : Math.sin(b.lon);
          sBias[ns] = b.stereo ? 2 : 1;
          ns++;
          continue;
        }
        if (b.stereo) {
          const w = bandWidth(b);
          rSin[nr] = Math.sin(b.lat);
          rCos[nr] = Math.cos(b.lat);
          rAmp[nr] = gainDisplacement(displayGain(b));
          rInvW2[nr] = 1 / (w * w);
          nr++;
          continue;
        }
        const bd = bandDirection(b);
        const w = bandWidth(b);
        bX[nb] = bd[0]; bY[nb] = bd[1]; bZ[nb] = bd[2];
        bAmp[nb] = gainDisplacement(displayGain(b));
        bInvW2[nb] = 1 / (w * w);
        nb++;
      }

      // View / projection constants (see toView and toScreen).
      syncTrig();
      const cx = centreX(), cyPx = centreY(), rPx = radiusPx();
      const sizeBase = PARTICLE_SIZE * baseSize;

      const extent = surfaceExtent(18, 36); // every frame, so a coarse grid
      if (!shownExtent) shownExtent = extent;
      const extentEase = 1 - Math.exp(-dt / 0.12);
      shownExtent.top += (extent.top - shownExtent.top) * extentEase;
      shownExtent.bottom += (extent.bottom - shownExtent.bottom) * extentEase;
      shownExtent.halfWidth += (extent.halfWidth - shownExtent.halfWidth) * extentEase;
      backdrop.draw(w, h, {
        centreX: cx, lightY: cyPx - ((shownExtent.top + shownExtent.bottom) / 2) * rPx,
        sphereY: cyPx, radius: rPx,
        shadowY: cyPx + (0.17 - shownExtent.bottom) * rPx, shadowHalfWidth: 0.9 * shownExtent.halfWidth * rPx,
        haze: hazeLevels,
      });

      // The far half first, so nearer balls cover it.
      let backCount = 0, frontStart = numBase, drawCount = numBase;
      for (let i = 0; i < numBase; ++i) drawOrder[pY[i] * sp + (pZ[i] * cy - pX[i] * sy) * cp < 0 ? backCount++ : --frontStart] = i;

      // Only the front half gets extras: that's the surface you see, at half the cost.
      const wantedExtras = numSurface * (Math.pow(rPx / EXTRA_START_RADIUS_PX, 1.6) - 1);
      for (let i = numBase; i < N; ++i) {
        if (i - numBase < wantedExtras && pY[i] * sp + (pZ[i] * cy - pX[i] * sy) * cp > 0) drawOrder[drawCount++] = i;
        else springOffset[i] = SPRING_UNSET;
      }

      for (let k = 0; k < drawCount; ++k) {
        const i = drawOrder[k];
        const dx = pX[i], dy = pY[i], dz = pZ[i];

        // EQ displacement: Gaussian bumps by squared chord distance; bands
        // whose bump is negligible here are skipped.
        let disp = 0;
        for (let k = 0; k < nb; ++k) {
          const e = 2 * (1 - (dx * bX[k] + dy * bY[k] + dz * bZ[k])) * bInvW2[k];
          if (e < 12) disp += bAmp[k] * Math.exp(-e);
        }

        // Stereo peaks: the same bump, measured in latitude only.
        if (nr > 0) {
          const cosLat = Math.sqrt(Math.max(0, 1 - dy * dy));
          for (let k = 0; k < nr; ++k) {
            const e = 2 * (1 - (cosLat * rCos[k] + dy * rSin[k])) * rInvW2[k];
            if (e < 12) disp += rAmp[k] * Math.exp(-e);
          }
        }

        // Cuts: Butterworth response at this particle's frequency (as in
        // cutDisplacement), weighted by stereo-axis overlap.
        for (let k = 0; k < nc; ++k) {
          const ratio = cLow[k] ? cFc[k] / pFreq[i] : pFreq[i] / cFc[k];
          const pw = Math.pow(ratio, cTwoN[k]);
          if (pw < 1e-4) continue; // < 0.0005 dB: nothing to show
          const db = -10 * Math.log10(1 + pw);
          const t = Math.min(1, -db / MAX_GAIN_DB);
          const overlap = (cBias[k] + pCosLon[i] * cCos[k] + pSinLon[i] * cSin[k]) / 2;
          disp -= Math.pow(t, 0.7) * MAX_CUT_DISPLACEMENT * overlap;
        }
        // Shelves: their response at this frequency, weighted by stereo overlap.
        for (let k = 0; k < ns; ++k) {
          const ratio = pFreq[i] / sFc[k];
          const overlap = (sBias[k] + pCosLon[i] * sCos[k] + pSinLon[i] * sSin[k]) / 2;
          disp += gainDisplacement(shelfDbAt(!!sLow[k], sA[k], sK[k], ratio * ratio)) * overlap;
        }
        if (disp > MAX_BOOST_DISPLACEMENT || disp < -MAX_CUT_DISPLACEMENT) disp = limitDisplacement(disp);

        const heardLevel = reactive ? audioLevelAt(i) : 0;
        let loud = (heardLevel - LOUD_FLOOR) / LOUD_WIDTH;
        loud = loud < 0 ? 0 : loud > 1 ? 1 : loud;

        // Boost or cut here in dB: the inverse of the shape curve. The analysis
        // hears the EQ's output, so its change is taken back out to find what
        // came in; a deep cut on a loud sound still counts.
        let tint = 0;
        if (reactive && disp !== 0) {
          const db = MAX_GAIN_DB * Math.pow(Math.min(1, Math.abs(disp) / (disp > 0 ? MAX_BOOST_DISPLACEMENT : MAX_CUT_DISPLACEMENT)), 1 / 0.7);
          let incoming = (heardLevel + (disp > 0 ? -db : db) / SPECTRUM_RANGE_DB - TINT_FLOOR) / TINT_WIDTH;
          incoming = incoming < 0 ? 0 : incoming > 1 ? 1 : incoming;
          tint = audioMix * incoming * incoming * (3 - 2 * incoming) * Math.pow(Math.min(1, db / TINT_FULL_DB), 0.6);
        }

        // An extra comes in at the rim, already in place.
        if (springOffset[i] === SPRING_UNSET) { springOffset[i] = disp; springVelocity[i] = 0; }

        let offset = springOffset[i];
        let velocity = springVelocity[i];
        if (offset !== disp || velocity !== 0) {
          for (let s = 0; s < steps; ++s) {
            velocity += (kSpring * (disp - offset) - cSpring * velocity) * stepDt;
            offset += velocity * stepDt;
          }
          // Settle exactly once the motion is invisible.
          if (Math.abs(offset - disp) < 1e-5 && Math.abs(velocity) < 1e-4) { offset = disp; velocity = 0; }
          springOffset[i] = offset;
          springVelocity[i] = velocity;
        }

        // Rotate into view space and project (inlined toView + toScreen).
        // (The floor stops a spring overshoot flipping a point through the centre.)
        const surface = surfaceRadius(pShell[i] + offset);
        const r = Math.max(0.05, surface);
        const mx = dx * r, my = dy * r, mz = dz * r;
        const x1 = mx * cy + mz * sy;
        const z1 = -mx * sy + mz * cy;
        const vy = my * cp - z1 * sp;
        const z = my * sp + z1 * cp;
        const persp = CAMERA_DISTANCE / (CAMERA_DISTANCE - z);
        const sx = cx + x1 * rPx * persp;
        const sy2 = cyPx - vy * rPx * persp;

        // Depth: the far side fades and shrinks so the front reads clearly
        // through it (smoothstep, so there's no visible edge where it changes).
        let front = (z + 0.7) / 1.4;
        front = front < 0 ? 0 : front > 1 ? 1 : front;
        const depth = front * front * (3 - 2 * front);
        const facing = 0.1 + 0.9 * depth;
        const lit = LIGHT_FLOOR + LIGHT_AMOUNT * Math.max(0, (x1 * LIGHT_X + vy * LIGHT_Y + z * LIGHT_Z) / r);
        const twinkle = 0.88 + 0.12 * Math.sin(pTwinkle[i] + time * pTwinkleRate[i]);
        const audioGain = 1 + audioMix * (QUIET_ALPHA + (LOUD_ALPHA - QUIET_ALPHA) * loud - 1);
        const audioSize = 1 + audioMix * (QUIET_SIZE + (LOUD_SIZE - QUIET_SIZE) * loud - 1);
        const size = sizeBase * pSize[i] * persp * audioSize * (0.75 + 0.25 * depth);
        const step = Math.round(tint * TINT_STEPS);
        const extraFade = i < numBase ? 1
          : Math.min(1, (wantedExtras - (i - numBase)) / EXTRA_FADE_COUNT)
            * Math.min(1, (dy * sp + (dz * cy - dx * sy) * cp) / EXTRA_EDGE_FADE);
        // A heavily cut dot has lost its audio but still needs to show its colour.
        sink.add(pColour[i] * NUM_TINTS + (step === 0 ? 0 : disp > 0 ? step : step + TINT_STEPS), sx, sy2, size,
                 extraFade * PARTICLE_ALPHA * facing * lit * twinkle * Math.max(audioGain, 0.35 + 0.6 * tint));

        // Light up the particles along the Mid (centre pan) meridian.
        const mid = pMid[i];
        if (mid > 0) sink.add(STYLE_MID, sx, sy2, size * 1.1, facing * mid * 0.5);
      }

      loopTotal += performance.now() - tLoop0;
      for (const b of bands) drawVortex(b, dirs, time, dt);
      sink.end();


      // Back nodes first so front ones draw on top.
      const ordered = [...bands].sort((a, b) => toView(bandDirection(a))[2] - toView(bandDirection(b))[2]);
      for (const b of ordered) drawNode(b, dirs, dt);

      const labelled = draggedBand ?? hoveredBand;
      if (labelled) drawLabel(labelled, dirs);

      // A closed panel leaves this alone, so only a clicked band gets highlighted.
      if (panelOpen) validateSelection();
      const soloed = soloedBand();
      drawBandPanel(ctx, panelLayout(panelAmount), dpr, panelAmount, selectedBand, !!soloed && soloed === selectedBand, panelHover, knobDrag ? knobDrag.knob : -1);
      // Solo changes what you hear, so it stays on show with the panel closed.
      if (soloed) {
        ctx.save();
        ctx.font = `${12 * dpr}px ${UI_FONT}`;
        ctx.fillStyle = "rgba(232,228,218,0.85)";
        ctx.textBaseline = "alphabetic";
        ctx.fillText(`Solo  ·  Band ${soloed.id + 1}`, 20 * dpr, 54 * dpr);
        ctx.restore();
      }
      drawHelp(dt);
      presetMenuAmount += ((presetMenuOpen ? 1 : 0) - presetMenuAmount) * (1 - Math.exp(-dt / 0.06));
      document.body.classList.toggle("preset-menu-open", presetMenuOpen);
      drawPresetBar(ctx, presetLayout(), dpr, presetInfo, presetHover, presetMenuAmount);

      ctx.globalCompositeOperation = "source-over";
      ctx.globalAlpha = 1;
      cpuTotal += performance.now() - cpuStart;
    };
    render();

    return () => {
      closePresetDialog();
      document.body.classList.remove("preset-menu-open");
      cancelAnimationFrame(frame);
      observer.disconnect();
      glSink?.dispose();
      canvas.removeEventListener("mousedown", onMouseDown);
      canvas.removeEventListener("pointerdown", onPointerDown);
      canvas.removeEventListener("pointermove", onPointerMove);
      canvas.removeEventListener("pointerup", onPointerUp);
      canvas.removeEventListener("pointercancel", onPointerUp);
      canvas.removeEventListener("pointerleave", onPointerLeave);
      canvas.removeEventListener("dblclick", onDoubleClick);
      canvas.removeEventListener("contextmenu", onContextMenu);
      canvas.removeEventListener("wheel", onWheel);
    };
  }, []);

  return (
    <div className="sphere">
      <canvas className="backdrop-layer" ref={backdropCanvasRef} />
      <canvas className="gl-layer" ref={glCanvasRef} />
      <canvas ref={canvasRef} />
    </div>
  );
}
