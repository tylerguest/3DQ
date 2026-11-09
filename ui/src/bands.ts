// EQ band model for the 3DQ sphere.
//
//   latitude  -> frequency (log scale, 20 Hz at the bottom pole, 20 kHz at the top)
//   longitude -> stereo position (0 = front = Mid, +90° = R, -90° = L, ±180° = back = Side)
//   gain      -> bump out of (boost) or dent into (cut) the sphere surface
//   q         -> how wide the bump is

// Peak = bell (gain, Q, dynamics). Cuts are Butterworth filters on the band's
// stereo axis: low cut = high-pass, high cut = low-pass. Shelves take gain, Q
// and dynamics like a peak, with the band's frequency at the half-gain point.
export type FilterType = "peak" | "lowcut" | "highcut" | "lowshelf" | "highshelf";
export const FILTER_TYPES: FilterType[] = ["peak", "lowcut", "highcut", "lowshelf", "highshelf"];
export const MAX_SHELF_Q = 2; // must match threedq::maxShelfQ
export const CUT_SLOPES = [6, 12, 18, 24, 36, 48]; // dB/oct, must match threedq::cutSlopes
export const DEFAULT_CUT_SLOPE = 24;
export const isCut = (b: Band) => b.type === "lowcut" || b.type === "highcut";
export const isShelf = (b: Band) => b.type === "lowshelf" || b.type === "highshelf";

export interface Band {
  id: number; // parameter slot in the plugin, 0 .. MAX_BANDS-1
  lat: number; // radians, -π/2 .. π/2
  lon: number; // radians, -π .. π
  gainDb: number;
  q: number;
  muted?: boolean; // kept on the sphere (settings remembered) but not applied
  soloed?: boolean; // play only what this band works on (at most one band)
  // Dynamics: when the band's own signal goes over `threshold` (dBFS), its gain
  // moves from gainDb toward gainDb + range. range 0 = a static band.
  range?: number;
  threshold?: number;
  type?: FilterType; // default "peak"
  slope?: number; // dB/oct, cuts only
  stereo?: boolean; // filters L and R alike: a full belt round the sphere
  // Display only (never sent to the plugin): the gain the band has right now,
  // including dynamic movement. Undefined = gainDb.
  liveGainDb?: number;
}

export const MAX_BANDS = 12; // must match threedq::maxBands in Source/SphereEq.h
export const MIN_FREQ = 20;
export const MAX_FREQ = 20000;
export const MAX_GAIN_DB = 30; // must match threedq::maxGainDb in Source/SphereEq.h
export const MIN_Q = 0.3;
export const MAX_Q = 10;
export const MIN_THRESHOLD_DB = -60; // must match threedq::minThresholdDb
export const MAX_THRESHOLD_DB = 0;
export const DEFAULT_THRESHOLD_DB = -30;

// Gain movement (dB) of a dynamic band for a detector level. Must match
// SphereEq::dynamicGainFor in Source/SphereEq.h: soft 6 dB knee, 2:1 slope,
// never past the range.
export function dynamicGainFor(levelDb: number, thresholdDb: number, rangeDb: number) {
  const knee = 6, slope = 0.5;
  const over = levelDb - thresholdDb;
  const soft = over <= -knee / 2 ? 0 : over >= knee / 2 ? over : ((over + knee / 2) ** 2) / (2 * knee);
  const amount = Math.min(Math.abs(rangeDb), soft * slope);
  return rangeDb < 0 ? -amount : amount;
}

export const displayGain = (b: Band) => b.liveGainDb ?? b.gainDb;

const HALF_PI = Math.PI / 2;

export const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

export function latToFreq(lat: number) {
  const t = (lat / HALF_PI + 1) / 2;
  return MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, t);
}

export function freqToLat(freq: number) {
  const t = Math.log(freq / MIN_FREQ) / Math.log(MAX_FREQ / MIN_FREQ);
  return (t * 2 - 1) * HALF_PI;
}

export function formatFreq(freq: number) {
  return freq >= 1000 ? `${(freq / 1000).toFixed(freq >= 10000 ? 1 : 2)} kHz` : `${Math.round(freq)} Hz`;
}

// dB value with one decimal, or two when needed (wheel steps are 0.25 dB):
// 6 -> "6.0", 6.5 -> "6.5", 6.25 -> "6.25".
export function formatDb(db: number) {
  const s = db.toFixed(2);
  return s.endsWith("0") ? s.slice(0, -1) : s;
}

export function formatGain(gainDb: number) {
  return `${gainDb >= 0 ? "+" : ""}${formatDb(gainDb)} dB`;
}

// Exact stereo readouts for a band's axis u = (cos φ, sin φ), φ = π/4 + lon/2:
//   pan (L/R balance)  = sin²φ − cos²φ   = sin(lon)        -1 = L .. +1 = R
//   mid share          = (1 + sin 2φ)/2  = (1 + cos lon)/2  1 = pure Mid, 0 = pure Side
export function panLabel(lon: number) {
  const pan = Math.round(Math.sin(lon) * 100);
  return pan === 0 ? "Pan C" : `Pan ${pan < 0 ? "L" : "R"} ${Math.abs(pan)}%`;
}

export function midSideLabel(lon: number) {
  const mid = Math.round(((1 + Math.cos(lon)) / 2) * 100);
  return `Mid ${mid}% · Side ${100 - mid}%`;
}

export function bandDirection(b: Band): [number, number, number] {
  const c = Math.cos(b.lat);
  return [c * Math.sin(b.lon), Math.sin(b.lat), c * Math.cos(b.lon)];
}

// Angular radius of a band's bump on the sphere: narrower as Q goes up.
export function bandWidth(b: Band) {
  return 0.55 / Math.sqrt(b.q);
}

// How far the surface is pushed out (+) or in (-) along unit direction d.
// Full-scale ±30 dB: a cut sinks most of the way to the centre; a boost stops
// where it still fits in the window.
export const MAX_BOOST_DISPLACEMENT = 0.55;
export const MAX_CUT_DISPLACEMENT = 0.82;

// How much of the full bump a gain shows (-1..1). A gentle curve rather than
// linear, so everyday gains (±3..12 dB) stay clearly visible while the full
// ±30 dB range still fits on the sphere.
export const gainShape = (gainDb: number) => {
  const t = Math.min(1, Math.abs(gainDb) / MAX_GAIN_DB);
  return Math.sign(gainDb) * Math.pow(t, 0.7);
};

export const gainDisplacement = (gainDb: number) =>
  gainShape(gainDb) * (gainDb > 0 ? MAX_BOOST_DISPLACEMENT : MAX_CUT_DISPLACEMENT);

// Stacked bands can add up past full scale: ease them into a little extra room
// so the surface never passes through the centre of the sphere.
const STACK_ROOM = 0.1;
export function limitDisplacement(d: number) {
  const max = d > 0 ? MAX_BOOST_DISPLACEMENT : MAX_CUT_DISPLACEMENT;
  const x = Math.abs(d);
  return x <= max ? d : Math.sign(d) * (max + STACK_ROOM * (1 - Math.exp(-(x - max) / STACK_ROOM)));
}

// A cut's magnitude (dB, <= 0) at frequency f: ideal Butterworth.
export function cutResponseDb(type: FilterType, cutoff: number, slope: number, f: number) {
  const n = slope / 6;
  const ratio = type === "lowcut" ? cutoff / f : f / cutoff;
  return -10 * Math.log10(1 + Math.pow(ratio, 2 * n));
}

// A shelf's magnitude (dB) at (frequency / shelf frequency)^2: the analogue RBJ
// shelf. shelfA is 10^(gain / 40), shelfK is A / Q^2 (the per-frame parts).
export function shelfDbAt(low: boolean, shelfA: number, shelfK: number, ratioSquared: number) {
  const damping = shelfK * ratioSquared;
  const a = shelfA - ratioSquared, c = 1 - shelfA * ratioSquared;
  const lowSide = a * a + damping, highSide = c * c + damping;
  return 10 * Math.log10(shelfA * shelfA * (low ? lowSide / highSide : highSide / lowSide));
}
export const shelfA = (gainDb: number) => Math.pow(10, gainDb / 40);
export const shelfK = (a: number, q: number) => a / Math.pow(Math.min(q, MAX_SHELF_Q), 2);
export function shelfResponseDb(b: Band, gainDb: number, f: number) {
  const a = shelfA(gainDb), ratio = f / latToFreq(b.lat);
  return shelfDbAt(b.type === "lowshelf", a, shelfK(a, b.q), ratio * ratio);
}

// How much a band on axis lonBand affects a point at longitude lon: the
// squared overlap of their stereo axes, cos²(Δlon / 2) = (1 + cos Δlon) / 2.
// 1 on its own axis, 0 on the opposite one (e.g. a Mid cut never touches Side).
export const axisOverlap = (lonBand: number, lon: number) => (1 + Math.cos(lon - lonBand)) / 2;
export const bandOverlap = (b: Band, lon: number) => (b.stereo ? 1 : axisOverlap(b.lon, lon));

// Display depth (<= 0) of a cut at a point: its response in dB, shaped like
// gains, weighted by stereo overlap.
export function cutDisplacement(b: Band, d: [number, number, number]) {
  const lat = Math.asin(clamp(d[1], -1, 1));
  const lon = Math.atan2(d[0], d[2]);
  const db = cutResponseDb(b.type!, latToFreq(b.lat), b.slope ?? DEFAULT_CUT_SLOPE, latToFreq(lat));
  return gainShape(db) * MAX_CUT_DISPLACEMENT * bandOverlap(b, lon);
}

// The radius a surface is drawn at: past the knee it eases towards a limit, so
// peaks stay in the window without a crease.
export function surfaceRadius(r: number) {
  const KNEE = 1.3, ROOM = 0.3;
  return r > KNEE ? KNEE + ROOM * (1 - Math.exp((KNEE - r) / ROOM)) : r;
}

export function surfaceDisplacement(bands: Band[], dirs: [number, number, number][], d: [number, number, number]) {
  let r = 0;
  for (let i = 0; i < bands.length; ++i) {
    if (bands[i].muted) continue; // a muted band leaves the surface flat
    if (isCut(bands[i])) { r += cutDisplacement(bands[i], d); continue; }
    if (isShelf(bands[i])) {
      const lat = Math.asin(clamp(d[1], -1, 1)), lon = Math.atan2(d[0], d[2]);
      r += gainDisplacement(shelfResponseDb(bands[i], displayGain(bands[i]), latToFreq(lat))) * bandOverlap(bands[i], lon);
      continue;
    }
    const bd = dirs[i];
    // A stereo peak is a belt: only the distance in latitude counts.
    const dot = bands[i].stereo
      ? clamp(Math.sqrt(Math.max(0, 1 - d[1] * d[1])) * Math.cos(bands[i].lat) + d[1] * Math.sin(bands[i].lat), -1, 1)
      : clamp(d[0] * bd[0] + d[1] * bd[1] + d[2] * bd[2], -1, 1);
    // Squared chord length 2(1 - cos θ) ≈ θ² near the band, without an acos.
    // (Sphere.tsx inlines this same formula in its per-particle loop.)
    const dist2 = 2 * (1 - dot);
    const w = bandWidth(bands[i]);
    r += gainDisplacement(displayGain(bands[i])) * Math.exp(-dist2 / (w * w));
  }
  return limitDisplacement(r);
}

// Bands as exchanged with the plugin: audio units instead of sphere angles.
export interface WireBand {
  id: number;
  freq: number;
  gainDb: number;
  q: number;
  pos: number; // stereo position (longitude) in degrees
  muted: boolean;
  range: number;
  threshold: number;
  type: FilterType;
  slope: number;
  stereo: boolean;
}

export const toWire = (b: Band): WireBand => ({
  id: b.id, freq: latToFreq(b.lat), gainDb: b.gainDb, q: b.q, pos: (b.lon * 180) / Math.PI, muted: !!b.muted,
  range: b.range ?? 0, threshold: b.threshold ?? DEFAULT_THRESHOLD_DB,
  type: b.type ?? "peak", slope: b.slope ?? DEFAULT_CUT_SLOPE, stereo: !!b.stereo,
});

export const fromWire = (w: WireBand): Band => ({
  id: w.id, lat: freqToLat(w.freq), lon: (w.pos * Math.PI) / 180, gainDb: w.gainDb, q: w.q, muted: !!w.muted,
  range: w.range ?? 0, threshold: w.threshold ?? DEFAULT_THRESHOLD_DB,
  type: w.type ?? "peak", slope: w.slope ?? DEFAULT_CUT_SLOPE, stereo: !!w.stereo,
});

