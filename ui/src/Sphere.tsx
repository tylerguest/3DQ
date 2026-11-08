import { useEffect, useRef } from "react";
import { NUM_SPECTRUM_BANDS, SPECTRUM_RANGE_DB, Spectrum, emptySpectrum, powerToLevel } from "./spectrum";
import type { BandDynamics } from "./pluginBridge";
import { CanvasSpriteSink, GlSpriteSink, SpriteProfile, SpriteSink, SpriteStyle } from "./sprites";
import {
  Band, MAX_BANDS, MAX_FREQ, MIN_FREQ, axisOverlap, cutResponseDb, MAX_GAIN_DB, MAX_Q, MIN_Q, bandDirection, bandWidth, clamp, formatDb, formatFreq, formatGain,
  CUT_SLOPES, DEFAULT_CUT_SLOPE, DEFAULT_THRESHOLD_DB, FILTER_TYPES, MAX_BOOST_DISPLACEMENT, MAX_CUT_DISPLACEMENT, gainDisplacement, limitDisplacement, isCut, MAX_THRESHOLD_DB, MIN_THRESHOLD_DB, displayGain, gainShape, latToFreq, midSideLabel, panLabel, surfaceDisplacement,
} from "./bands";

// Rotating particle sphere drawn with Canvas 2D (no WebGL needed, so it runs in
// any WebView). Points sit on a unit sphere, coloured by frequency (blue lows
// -> green mids -> pale highs) and drawn additively so they glow. EQ bands are
// nodes on the surface; their gain bulges or dents the sphere around them.
//
// Gestures:
//   drag empty space       rotate
//   scroll empty space     zoom
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
const NUM_COLOUR_BUCKETS = 32;
const SPRITE_SIZE = 64; // Canvas 2D fallback sprites; big enough not to blur when scaled up
const CAMERA_DISTANCE = 3.2;
const NODE_HIT_RADIUS = 16; // CSS px

type RGB = [number, number, number];
type Vec3 = [number, number, number];

// Theme: graphite, with muted particle colours running through the frequency
// range. They stay clear of orange and red, which only ever mean boost and cut.
// Stops are positions on the log frequency axis (0 = 20 Hz, 1 = 20 kHz).
const FREQUENCY_COLOURS: { t: number; rgb: RGB }[] = [
  { t: 0, rgb: [104, 128, 232] },    // sub: blue
  { t: 0.3, rgb: [74, 182, 182] },   // ~160 Hz, bass: teal
  { t: 0.52, rgb: [140, 202, 118] }, // ~700 Hz, mids: green
  { t: 0.75, rgb: [232, 208, 122] }, // ~3.5 kHz, presence: straw
  { t: 1, rgb: [238, 232, 220] },    // air: bone
];
const POLE_DIM = 0.82; // brightness at the poles relative to the equator
// Near-white particles add up fast with additive blending; keep each one
// fainter and smaller so the sphere reads as fine grain, not white blobs.
const PARTICLE_ALPHA = 0.5;
const PARTICLE_SIZE = 0.78;
const WARM_GREY: RGB = [138, 134, 124];
const PANEL_FILL = "rgba(17,17,16,0.88)";
const PANEL_STROKE = "rgba(138,134,124,0.45)";
const TEXT_COLOUR = "#e8e4da";

interface Point {
  dir: Vec3;   // unit direction
  shell: number; // base radius (1 on the surface, ~0.85-1.15 for haze)
  size: number;
  bucket: number;
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
  const [r, g, bl] = lerpRGB(a.rgb, b.rgb, clamp((t - a.t) / (b.t - a.t), 0, 1));
  const y = Math.sin((t * 2 - 1) * (Math.PI / 2));
  const k = 1 - (1 - POLE_DIM) * y * y;
  return [r * k, g * k, bl * k];
}

const bucketColour = (i: number) => colourForFrequency(i / (NUM_COLOUR_BUCKETS - 1));

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
    const grad = ctx.createRadialGradient(half, half, 0, half, half, half);
    // Crisp core inside a faint halo (matches the WebGL shader in sprites.ts).
    const hot = lerpRGB(rgb, [255, 255, 255], 0.35);
    grad.addColorStop(0, rgbString(hot));
    grad.addColorStop(0.27, rgbString(rgb));
    grad.addColorStop(0.32, rgbString(rgb, 0.12));
    grad.addColorStop(0.6, rgbString(rgb, 0.04));
    grad.addColorStop(1, rgbString(rgb, 0));
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

// Colour bucket for a unit direction's y: latitude is the log frequency axis.
const bucketOf = (y: number) =>
  Math.round((Math.asin(clamp(y, -1, 1)) / Math.PI + 0.5) * (NUM_COLOUR_BUCKETS - 1));

function buildPoints(numSurface: number, numHaze: number): Point[] {
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
                  size: (0.5 + Math.random() * 1.0) * sizeScale, bucket: bucketOf(y),
                  twinkle: Math.random() * Math.PI * 2, twinkleRate: 0.6 + Math.random() * 1.4 });
  }

  // Sparse haze just inside / outside the shell.
  for (let i = 0; i < numHaze; ++i) {
    const dir = randomDirection();
    const shell = 0.85 + Math.random() * 0.3;
    points.push({ dir, shell, size: (0.3 + Math.random() * 0.6) * sizeScale, bucket: bucketOf(dir[1]),
                  twinkle: Math.random() * Math.PI * 2, twinkleRate: 0.4 + Math.random() * 0.8 });
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
    const canvasSink = new CanvasSpriteSink(ctx, styles);
    let sink: SpriteSink = canvasSink;
    // Dev benchmark (?bench): WebGL particle count, nothing drawn, so the CPU
    // side can be timed even without a GPU.
    const bench = process.env.NODE_ENV !== "production" && new URLSearchParams(location.search).has("bench");
    const nullSink: SpriteSink = { begin() {}, add() {}, end() {} };
    const points = glSink || bench ? buildPoints(GL_SURFACE_POINTS, GL_HAZE_POINTS)
                          : buildPoints(CANVAS_SURFACE_POINTS, CANVAS_HAZE_POINTS);

    // Flat per-particle arrays for the hot render loop (no objects or
    // allocations per particle per frame).
    const N = points.length;
    const pX = new Float32Array(N), pY = new Float32Array(N), pZ = new Float32Array(N);
    const pShell = new Float32Array(N), pSize = new Float32Array(N), pBucket = new Uint8Array(N);
    const pTwinkle = new Float32Array(N), pTwinkleRate = new Float32Array(N);
    const pMid = new Float32Array(N); // Mid-seam weight (0 = not on the seam)
    points.forEach((p, i) => {
      [pX[i], pY[i], pZ[i]] = p.dir;
      pShell[i] = p.shell;
      pSize[i] = p.size;
      pBucket[i] = p.bucket;
      pTwinkle[i] = p.twinkle;
      pTwinkleRate[i] = p.twinkleRate;
      // Mid (centre pan) meridian: lon 0 is the x = 0, z > 0 half of the sphere.
      const w = Math.exp(-(p.dir[0] * p.dir[0]) / (MID_LINE_WIDTH * MID_LINE_WIDTH));
      pMid[i] = p.dir[2] > 0 && p.shell > 0.99 && p.shell < 1.01 && w > 0.05 ? w : 0;
    });

    // For cuts: each particle's frequency and longitude (as cos / sin).
    const pFreq = new Float32Array(N), pCosLon = new Float32Array(N), pSinLon = new Float32Array(N);
    points.forEach((p, i) => {
      pFreq[i] = latToFreq(Math.asin(clamp(p.dir[1], -1, 1)));
      const lon = Math.atan2(p.dir[0], p.dir[2]);
      pCosLon[i] = Math.cos(lon);
      pSinLon[i] = Math.sin(lon);
    });
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

    // Bass onsets (kicks) give the whole sphere a small "breath".
    const BASS_BANDS = 12; // 20 Hz .. ~110 Hz
    let bassAverage = 0;
    let bassOnset = 0;

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

      let bass = 0;
      for (let b = 0; b < BASS_BANDS; ++b) bass += (held.ll[b] + held.rr[b]) / 2;
      const bassLevel = powerToLevel(bass);
      bassAverage += (bassLevel - bassAverage) * (1 - Math.exp(-dt / 0.35));
      bassOnset = Math.max(0, bassLevel - bassAverage) * audioMix;
    };

    // Each particle's radius rides a damped spring toward its target (EQ shape
    // + audio). Slightly under-damped, so the surface gives a soft, jelly-like
    // bounce on hits and when bands are added, moved or removed, instead of
    // snapping or following the analysis frame by frame.
    const springOffset = new Float32Array(points.length); // current radius offset
    const springVelocity = new Float32Array(points.length);
    const SPRING_OMEGA = 2 * Math.PI * 4.5; // natural frequency (rad/s)
    const SPRING_DAMPING = 0.42; // damping ratio (< 1 = a little overshoot)
    const SPRING_STEP = 1 / 120; // max integration step (s), keeps the spring stable
    let breath = 0, breathVelocity = 0; // whole-sphere pulse on kicks

    // The audio part of the motion has its own spring, critically damped (no
    // overshoot): it glides with the music instead of wobbling on every
    // analysis frame. The bouncier spring above is for EQ edits only.
    const audioOffset = new Float32Array(points.length);
    const audioVelocity = new Float32Array(points.length);
    const AUDIO_OMEGA = 2 * Math.PI * 4.2;
    const AUDIO_HEIGHT = 0.28; // radius added at the loudest spot in the mix
    const AUDIO_WINDOW = 36 / SPECTRUM_RANGE_DB; // moves over the top 36 dB, in level units
    const AUDIO_CENTRING = 0.7; // how much of the average lift is taken back out
    const AUDIO_SURGE = 0.8; // extra radius per level unit above a spot's recent average
    const AUDIO_SURGE_MAX = 0.2; // ...up to this many level units (12 dB)
    const SURFACE_KNEE = 1.3; // past this radius the surface eases toward...
    const SURFACE_ROOM = 0.3; // ...this much further, so peaks stay in the window without a crease
    let audioPeakRef = 0; // loudest level lately: jumps up at once, falls over seconds
    let audioMeanLift = 0; // average of the shaped levels in the previous frame

    // Energy map: each particle's running average of the level at its own
    // frequency and stereo position. Drawn as size and brightness relative to
    // the hottest spot, so the sphere reads as a heat map of the recent mix.
    const energy = new Float32Array(points.length);
    const ENERGY_MEMORY = 6; // seconds averaged while audio plays
    const ENERGY_FADE = 20; // seconds for the map to fade out after audio stops
    const ENERGY_RANGE = 24 / SPECTRUM_RANGE_DB; // shows the top 24 dB, in level units
    let energyAge = 0; // seconds of audio averaged so far (capped at ENERGY_MEMORY)
    let energyMapAmount = 0; // 0 = no map drawn, 1 = fully drawn
    let energyPeak = 0; // hottest particle in the previous frame
    let energyMeanHeat = 0; // average heat in the previous frame: only differences from it show

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
      if (draggedBand) return;
      const keep = (b: Band | null) => (b ? bands.find((x) => x.id === b.id) ?? null : null);
      bands.length = 0;
      for (const b of incoming) bands.push({ ...b });
      hoveredBand = keep(hoveredBand);
      wheelBand = keep(wheelBand);
    };
    let hoveredBand: Band | null = null;
    let draggedBand: Band | null = null;
    let wheelBand: Band | null = null;
    let lastWheelTime = 0;
    let slopeAccumulator = 0; // wheel delta toward the next cut-slope step
    let liveDynamics: Map<number, BandDynamics> | null = null;
    const notify = () => onBandsChangeRef.current?.(bands.map((b) => ({ ...b })));

    // View state: yaw spins around the vertical axis, pitch tilts toward the viewer.
    const view = { yaw: 0, pitch: -0.35, zoom: 1, yawVelocity: 0, pitchVelocity: 0 };
    const AUTO_ROTATE = 0.0035; // radians per frame
    let rotating = false;
    let lastX = 0;
    let lastY = 0;
    // A press only becomes a drag (band move or rotation) once the pointer has
    // moved a few pixels, so clicks and double-clicks don't nudge anything.
    const DRAG_THRESHOLD = 4; // CSS px
    let pressed: { x: number; y: number; band: Band | null; offsetX: number; offsetY: number; radius: number } | null = null;
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
      // With WebGL, render above the screen's resolution (supersampling) so
      // the fine particles get smooth, sharp edges instead of stair-stepped
      // pixels. The Canvas 2D fallback can't afford it.
      const supersample = glSink ? 1.5 : 1;
      dpr = Math.min((window.devicePixelRatio || 1) * supersample, 3);
      canvas.width = Math.round(canvas.clientWidth * dpr);
      canvas.height = Math.round(canvas.clientHeight * dpr);
    };
    const observer = new ResizeObserver(resize);
    observer.observe(canvas);
    resize();

    // ---- view maths ------------------------------------------------------
    const radiusPx = () => Math.min(canvas.width, canvas.height) * 0.32 * view.zoom;

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
      return [canvas.width / 2 + x * r * scale, canvas.height / 2 - y * r * scale, z];
    };

    // Canvas pixel -> unit direction (model space) of the point on a sphere of
    // the given radius under the pixel. When `clampToEdge` is set, positions
    // outside the silhouette snap to the sphere's edge.
    const pickSphere = (px: number, py: number, clampToEdge: boolean, radius = 1): Vec3 | null => {
      const r = radiusPx();
      const u = (px - canvas.width / 2) / r;
      const v = -(py - canvas.height / 2) / r;
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

    const updateCursor = () => {
      canvas.style.cursor = gainDrag ? "ns-resize" : draggedBand || rotating ? "grabbing" : hoveredBand ? "pointer" : "grab";
    };

    const onPointerDown = (e: PointerEvent) => {
      touch();
      if (e.button !== 0) return; // right button is mute (contextmenu), not drag
      const [px, py] = eventPos(e);
      canvas.setPointerCapture(e.pointerId);
      const band = hitBand(px, py);
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
      }
      pressed = { x: e.clientX, y: e.clientY, band, offsetX, offsetY, radius };
      lastX = e.clientX;
      lastY = e.clientY;
      rotateSamples = [];
      view.yawVelocity = view.pitchVelocity = 0; // catching the sphere stops it
    };

    const onPointerMove = (e: PointerEvent) => {
      pointerInside = true;
      touch();
      const [px, py] = eventPos(e);

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
        hoveredBand = hitBand(px, py);
      }

      updateCursor();
    };

    const onPointerUp = (e: PointerEvent) => {
      if (rotating) {
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
      rotating = false;
      draggedBand = null;
      gainDrag = null;
      pressed = null;
      touch();
      if (canvas.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
      const [px, py] = eventPos(e);
      hoveredBand = hitBand(px, py);
      updateCursor();
    };

    const onPointerLeave = () => {
      pointerInside = false;
      touch();
      if (!draggedBand) hoveredBand = null;
    };

    // Right-click a band to mute / unmute it. A muted band keeps its settings
    // but flattens out and stops affecting the sound.
    const onContextMenu = (e: MouseEvent) => {
      e.preventDefault();
      touch();
      if (e.ctrlKey) return; // macOS reports Ctrl+click as a right-click; that's the gain drag
      const [px, py] = eventPos(e);
      const hit = hitBand(px, py);
      if (!hit) return;
      hit.muted = !hit.muted;
      notify();
    };

    const onDoubleClick = (e: MouseEvent) => {
      touch();
      if (e.ctrlKey || e.metaKey) return; // two quick gain drags shouldn't remove the band
      const [px, py] = eventPos(e);
      const hit = hitBand(px, py);

      // Shift+double-click a band: cycle Peak -> Low cut -> High cut.
      if (hit && e.shiftKey) {
        const next = FILTER_TYPES[(FILTER_TYPES.indexOf(hit.type ?? "peak") + 1) % FILTER_TYPES.length];
        hit.type = next;
        hit.slope ??= DEFAULT_CUT_SLOPE;
        notify();
        return;
      }

      if (hit) {
        bands.splice(bands.indexOf(hit), 1);
        hoveredBand = null;
        notify();
        return;
      }

      const slot = freeSlot();
      if (slot < 0) return;
      const surface = pickSphere(px, py, false);
      if (!surface) return;
      const band: Band = { id: slot, ...toLatLon(surface), gainDb: 0, q: 1 };
      bands.push(band);
      // The new band's own bump lifts its node; re-pick on the lifted surface so
      // the node lands under the cursor.
      const lifted = pickSphere(px, py, true, handleRadius(band));
      if (lifted) Object.assign(band, toLatLon(lifted));
      hoveredBand = band;
      notify();
    };

    const onWheel = (e: WheelEvent) => {
      touch();
      e.preventDefault();
      // Some platforms turn shift+wheel into horizontal scrolling. Firefox can
      // report lines or pages instead of pixels; normalise to pixels (~100
      // per wheel notch) so steps feel the same everywhere.
      const unit = e.deltaMode === 1 ? 33 : e.deltaMode === 2 ? 800 : 1;
      const delta = (e.deltaY !== 0 ? e.deltaY : e.deltaX) * unit;
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
        else if (e.shiftKey) band.q = clamp(band.q * Math.exp(-delta * 0.002), MIN_Q, MAX_Q);
        else band.gainDb = snap(clamp(band.gainDb + dbStep, -MAX_GAIN_DB, MAX_GAIN_DB));
        if (band.range !== undefined && Math.abs(band.range) < 0.05) band.range = 0;
        hoveredBand = band;
        notify();
        return;
      }

      view.zoom = clamp(view.zoom * Math.exp(-delta * 0.001), 0.25, 2.5);
    };

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
      // Fade smoothly as the node turns away round the back.
      const facing = 0.22 + 0.78 * clamp((z + 0.3) / 0.45, 0, 1);

      const target = b === draggedBand ? 1 : b === hoveredBand ? 0.7 : 0;
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

      // Soft glow (skipped when muted).
      if (!b.muted) {
        ctx.globalCompositeOperation = "lighter";
        const glowR = ringR * 2.2;
        const glow = ctx.createRadialGradient(sx, sy, core * 0.5, sx, sy, glowR);
        glow.addColorStop(0, rgbString(colour, (0.28 + 0.2 * emphasis) * facing));
        glow.addColorStop(1, rgbString(colour, 0));
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
      ctx.lineWidth = 1.5 * dpr;
      ctx.strokeStyle = b.muted ? "rgba(138,134,124,0.25)" : "rgba(232,228,218,0.22)";
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
        const shade = ctx.createRadialGradient(sx - core * 0.35, sy - core * 0.4, core * 0.1, sx, sy, core);
        shade.addColorStop(0, rgbString(lerpRGB(colour, [255, 255, 255], 0.45)));
        shade.addColorStop(0.6, rgbString(colour));
        shade.addColorStop(1, rgbString(lerpRGB(colour, [17, 17, 16], 0.3)));
        ctx.fillStyle = shade;
        ctx.fill();
        if (emphasis > 0.01) {
          ctx.lineWidth = 1.2 * dpr;
          ctx.strokeStyle = `rgba(255,252,244,${0.85 * emphasis})`;
          ctx.stroke();
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

      ctx.globalAlpha = 1;
    };

    const drawLabel = (b: Band, dirs: Vec3[]) => {
      const [sx, sy] = bandScreenPos(b, dirs);
      const lines = isCut(b)
        ? [
            `${b.type === "lowcut" ? "Low cut" : "High cut"}  ${formatFreq(latToFreq(b.lat))}`,
            `${b.slope ?? DEFAULT_CUT_SLOPE} dB/oct  ·  ${panLabel(b.lon)}`,
            midSideLabel(b.lon),
          ]
        : [
            `${formatFreq(latToFreq(b.lat))}  ${formatGain(b.gainDb)}`,
            `Q ${b.q.toFixed(2)}  ·  ${panLabel(b.lon)}`,
            midSideLabel(b.lon),
          ];
      if (b.range && !isCut(b)) {
        lines.push(`Dyn ${formatGain(b.range)}  ·  Thresh ${formatDb(b.threshold ?? DEFAULT_THRESHOLD_DB)} dB`);
        const dyn = liveDynamics?.get(b.id);
        if (dyn) lines.push(`Now ${formatGain(dyn.gainDb)}  ·  Level ${Math.max(-99, dyn.levelDb).toFixed(1)} dB`);
      }
      if (b.muted) lines.push("Muted · right-click to unmute");
      const fontPx = 12 * dpr;
      ctx.font = `${fontPx}px system-ui, sans-serif`;
      const padding = 6 * dpr;
      const lineHeight = fontPx * 1.35;
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
      const baseSize = radiusPx() * 0.05;
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
        const sinLat = Math.sin(lat), cosLat = Math.cos(lat);
        const points = Math.max(12, Math.round(76 * cosLat)); // even spacing as rings shrink
        for (let j = 0; j < points; ++j) {
          const lon = (j / points) * Math.PI * 2 + phase;
          const overlap = axisOverlap(b.lon, lon); // only where the cut acts
          if (overlap < 0.02) continue;
          const dir: Vec3 = [cosLat * Math.sin(lon), sinLat, cosLat * Math.cos(lon)];
          const r = 1 + surfaceDisplacement(bands, dirs, dir);
          const [sx, sy, z] = toScreen(toView([dir[0] * r, dir[1] * r, dir[2] * r]));
          const facing = 0.2 + 0.8 * clamp((z + 0.5) / 1.0, 0, 1);
          const size = baseSize * 1.2 * (CAMERA_DISTANCE / (CAMERA_DISTANCE - z));
          sink.add(style, sx, sy, size, facing * ringFade * overlap);
        }
      }
    };

    const drawVortex = (b: Band, dirs: Vec3[], time: number, dt: number) => {
      const previous = vortexAmount.get(b.id) ?? (b.muted ? 0 : 1);
      const amount = previous + ((b.muted ? 0 : 1) - previous) * (1 - Math.exp(-dt / 0.12));
      vortexAmount.set(b.id, amount);
      if (isCut(b)) { drawCutRings(b, dirs, time, amount); return; }
      const strength = Math.abs(gainShape(displayGain(b))) * amount;
      if (strength < 0.01) return;

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
      const kAudio = AUDIO_OMEGA * AUDIO_OMEGA;
      const cAudio = 2 * AUDIO_OMEGA; // damping ratio 1

      // Whole-sphere breath: a slightly bouncier spring kicked by bass onsets.
      for (let s = 0; s < steps; ++s) {
        breathVelocity += (kSpring * 0.6 * (bassOnset * 0.09 - breath) - cSpring * 0.6 * breathVelocity) * stepDt;
        breath += breathVelocity * stepDt;
      }

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
      // Bands, prepared once per frame. Muted bands drop out.
      let nb = 0, nc = 0;
      for (const b of bands) {
        if (b.muted) continue;
        if (isCut(b)) {
          cLow[nc] = b.type === "lowcut" ? 1 : 0;
          cFc[nc] = latToFreq(b.lat);
          cTwoN[nc] = (2 * (b.slope ?? DEFAULT_CUT_SLOPE)) / 6;
          cCos[nc] = Math.cos(b.lon);
          cSin[nc] = Math.sin(b.lon);
          nc++;
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
      const cx = w / 2, cyPx = h / 2, rPx = radiusPx();
      const breathScale = 1 + breath;
      const sizeBase = PARTICLE_SIZE * baseSize;
      const audioDim = 1 - 0.3 * audioMix; // = 1 + (0.7 - 1) * audioMix
      const audioBright = 0.45 * audioMix;

      // Averages over everything heard so far until the memory is full, so a
      // usable map appears within a second instead of slowly filling up.
      const energyActive = audioMix > 0.5;
      if (energyMapAmount < 0.01 && !energyActive) energyAge = 0;
      if (energyActive) energyAge = Math.min(ENERGY_MEMORY, energyAge + dt);
      const energyRate = energyActive ? 1 - Math.exp(-dt / Math.max(0.1, energyAge)) : 0;
      energyMapAmount += energyActive
        ? (1 - energyMapAmount) * (1 - Math.exp(-dt / 0.8))
        : -energyMapAmount * (1 - Math.exp(-dt / ENERGY_FADE));
      const energyFloor = energyPeak - ENERGY_RANGE;
      let nextEnergyPeak = 0, heatSum = 0;
      const audioFloor = audioPeakRef - AUDIO_WINDOW;
      let framePeak = 0, shapedSum = 0;

      for (let i = 0; i < N; ++i) {
        const dx = pX[i], dy = pY[i], dz = pZ[i];

        // EQ displacement: Gaussian bumps by squared chord distance; bands
        // whose bump is negligible here are skipped.
        let disp = 0;
        for (let k = 0; k < nb; ++k) {
          const e = 2 * (1 - (dx * bX[k] + dy * bY[k] + dz * bZ[k])) * bInvW2[k];
          if (e < 12) disp += bAmp[k] * Math.exp(-e);
        }

        // Cuts: Butterworth response at this particle's frequency (as in
        // cutDisplacement), weighted by stereo-axis overlap.
        for (let k = 0; k < nc; ++k) {
          const ratio = cLow[k] ? cFc[k] / pFreq[i] : pFreq[i] / cFc[k];
          const pw = Math.pow(ratio, cTwoN[k]);
          if (pw < 1e-4) continue; // < 0.0005 dB: nothing to show
          const db = -10 * Math.log10(1 + pw);
          const t = Math.min(1, -db / MAX_GAIN_DB);
          const overlap = (1 + pCosLon[i] * cCos[k] + pSinLon[i] * cSin[k]) / 2;
          disp -= Math.pow(t, 0.7) * MAX_CUT_DISPLACEMENT * overlap;
        }
        if (disp > MAX_BOOST_DISPLACEMENT || disp < -MAX_CUT_DISPLACEMENT) disp = limitDisplacement(disp);

        // Audio: loud spots move outward with the music (the main effect),
        // with only a gentle change in brightness and size.
        const heardLevel = reactive ? audioLevelAt(i) : 0;
        if (heardLevel > framePeak) framePeak = heardLevel;
        let heard = energy[i];
        if (energyActive) energy[i] = heard += (heardLevel - heard) * energyRate;
        if (heard > nextEnergyPeak) nextEnergyPeak = heard;
        let heat = (heard - energyFloor) / ENERGY_RANGE;
        heat = heat < 0 ? 0 : heat > 1 ? 1 : heat;
        heatSum += heat;
        const heatDelta = energyMapAmount * (heat - energyMeanHeat);
        // Each spot moves by its level relative to the loudest part of the mix,
        // less most of the average so the sphere changes shape rather than just
        // inflating, plus a push whenever it jumps above its own recent average.
        let relative = (heardLevel - audioFloor) / AUDIO_WINDOW;
        relative = relative < 0 ? 0 : relative > 1 ? 1 : relative;
        const shaped = relative * Math.sqrt(relative);
        shapedSum += shaped;
        let surge = heardLevel - heard;
        surge = surge < 0 ? 0 : surge > AUDIO_SURGE_MAX ? AUDIO_SURGE_MAX : surge;
        const audioTarget = reactive
          ? audioMix * ((shaped - AUDIO_CENTRING * audioMeanLift) * AUDIO_HEIGHT + surge * AUDIO_SURGE)
          : 0;
        let lift = audioOffset[i];
        let liftVelocity = audioVelocity[i];
        if (lift !== audioTarget || liftVelocity !== 0) {
          for (let s = 0; s < steps; ++s) {
            liftVelocity += (kAudio * (audioTarget - lift) - cAudio * liftVelocity) * stepDt;
            lift += liftVelocity * stepDt;
          }
          if (Math.abs(lift) < 1e-5 && audioTarget === 0 && Math.abs(liftVelocity) < 1e-4) { lift = 0; liftVelocity = 0; }
          audioOffset[i] = lift;
          audioVelocity[i] = liftVelocity;
        }
        // Brightness / size follow the same smoothed motion.
        const level = lift > 0 ? Math.min(1, Math.sqrt(lift / AUDIO_HEIGHT)) : 0;

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
        let surface = pShell[i] + offset + lift;
        if (surface > SURFACE_KNEE) surface = SURFACE_KNEE + SURFACE_ROOM * (1 - Math.exp((SURFACE_KNEE - surface) / SURFACE_ROOM));
        const r = Math.max(0.05, surface) * breathScale;
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
        const twinkle = 0.88 + 0.12 * Math.sin(pTwinkle[i] + time * pTwinkleRate[i]);
        // Cuts sink into a darker hole; boosts lift a little brighter.
        let amount = disp / 0.2;
        amount = amount < -1 ? -1 : amount > 1 ? 1 : amount;
        const audioGain = audioDim + audioBright * level;
        const size = sizeBase * pSize[i] * persp * (1 + 0.15 * level) * (0.75 + 0.25 * depth)
                     * (1 + 0.8 * heatDelta);
        sink.add(pBucket[i], sx, sy2, size,
                 PARTICLE_ALPHA * facing * twinkle * audioGain * (amount < 0 ? 1 + amount * 0.8 : 1 + amount * 0.3)
                 * (1 + 1.3 * heatDelta));

        // Light up the particles along the Mid (centre pan) meridian.
        const mid = pMid[i];
        if (mid > 0) sink.add(STYLE_MID, sx, sy2, size * 1.1, facing * mid * 0.5);
      }

      energyPeak = nextEnergyPeak;
      audioPeakRef = framePeak > audioPeakRef ? framePeak : audioPeakRef + (framePeak - audioPeakRef) * (1 - Math.exp(-dt / 4));
      audioMeanLift = shapedSum / N;
      energyMeanHeat = heatSum / N;

      loopTotal += performance.now() - tLoop0;
      for (const b of bands) drawVortex(b, dirs, time, dt);
      sink.end();


      // Back nodes first so front ones draw on top.
      const ordered = [...bands].sort((a, b) => toView(bandDirection(a))[2] - toView(bandDirection(b))[2]);
      for (const b of ordered) drawNode(b, dirs, dt);

      const labelled = draggedBand ?? hoveredBand;
      if (labelled) drawLabel(labelled, dirs);

      ctx.globalCompositeOperation = "source-over";
      ctx.globalAlpha = 1;
      cpuTotal += performance.now() - cpuStart;
    };
    render();

    return () => {
      cancelAnimationFrame(frame);
      observer.disconnect();
      glSink?.dispose();
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
      <canvas className="gl-layer" ref={glCanvasRef} />
      <canvas ref={canvasRef} />
    </div>
  );
}
