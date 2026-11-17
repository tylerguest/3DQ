// The band model for the web UI: ranges, value formats and the filter shapes,
// matching source/dsp/eq_params.h and source/model/bands.h. For now the state
// is mock data; later it comes from the plugin over the web view's bridge.
"use strict";

const EQ = {
  minFreq: 20, maxFreq: 20000, maxGain: 30,
  minQ: 0.025, maxQ: 40, maxShelfQ: 2,
  minThreshold: -60, maxThreshold: 0,
  minRatio: 1, maxRatio: 20,
  minAttack: 0.1, maxAttack: 200,
  minRelease: 5, maxRelease: 3000,
  slopes: [6, 12, 18, 24, 36, 48],
  maxTrim: 24,
  types: ["peak", "lowCut", "highCut", "lowShelf", "highShelf"],
  typeNames: { peak: "Bell", lowCut: "High Pass", highCut: "Low Pass", lowShelf: "Low Shelf", highShelf: "High Shelf" },
  boost: "#ff8a2a", cut: "#e5484d",
};

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const toLog = (v, lo, hi) => Math.log(v / lo) / Math.log(hi / lo);
const fromLog = (t, lo, hi) => lo * Math.pow(hi / lo, clamp(t, 0, 1));
const isCut = (b) => b.type === "lowCut" || b.type === "highCut";
const isShelf = (b) => b.type === "lowShelf" || b.type === "highShelf";
const qCeiling = (b) => (isShelf(b) ? EQ.maxShelfQ : EQ.maxQ);

// Text formats, as the plugin writes them.
const fmt = {
  db(v) { const s = (Math.round(v * 100) / 100).toFixed(2); return s.endsWith("0") ? s.slice(0, -1) : s; },
  gain(v) { return (v >= 0 ? "+" : "") + fmt.db(v) + " dB"; },
  freq(f) { return f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 1 : 2) + " kHz" : Math.round(f) + " Hz"; },
  q(q) { return q.toFixed(q < 0.1 ? 3 : q < 10 ? 2 : 1); },
  ms(ms) { return ms >= 1000 ? (ms / 1000).toFixed(2) + " s" : ms.toFixed(ms < 10 ? 1 : 0) + " ms"; },
  ratio(r) { return r.toFixed(r < 10 ? 1 : 0) + ":1"; },
  pos(deg) {
    const d = Math.round(deg);
    if (d === 0) return "Mid";
    if (Math.abs(d) === 180) return "Side";
    if (Math.abs(d) === 90) return d < 0 ? "L" : "R";
    return (d > 0 ? "R " : "L ") + Math.abs(d) + "°";
  },
  trim(v) { const r = Math.round(v * 10) / 10; return (r > 0 ? "+" : "") + (r === 0 ? 0 : r).toFixed(1) + " dB"; },
};

// A typed number: "1.5k" and "2k5" are thousands; with seconds, a trailing s is too.
function parseTyped(text, seconds = false) {
  let t = String(text).toLowerCase().replace(/[\s]/g, "").replace(",", ".");
  let scale = 1;
  const k = t.match(/^([+-]?\d+)k(\d+)/);
  if (k) { t = k[1] + "." + k[2] + t.slice(k[0].length); scale = 1000; }
  const m = t.match(/^[+-]?(\d+\.?\d*|\.\d+)/);
  if (!m) return null;
  const unit = t.slice(m[0].length);
  if (unit.startsWith("k") || (seconds && unit.startsWith("s"))) scale = 1000;
  return parseFloat(m[0]) * scale;
}

// The controls a band has. Each maps the band to 0..1 and back, formats and
// parses its value, and knows its default (double-click resets to it).
const KNOBS = {
  freq: {
    label: "FREQ",
    get: (b) => toLog(b.freq, EQ.minFreq, EQ.maxFreq),
    set: (b, t) => { b.freq = fromLog(t, EQ.minFreq, EQ.maxFreq); },
    text: (b) => fmt.freq(b.freq),
    type: (b, s) => { const v = parseTyped(s); if (v == null) return false; b.freq = clamp(v, EQ.minFreq, EQ.maxFreq); return true; },
    reset: (b) => { b.freq = 1000; },
  },
  gain: {
    label: "GAIN", bipolar: true,
    enabled: (b) => !isCut(b),
    get: (b) => (b.gain + EQ.maxGain) / (2 * EQ.maxGain),
    set: (b, t) => { b.gain = Math.round((t * 2 - 1) * EQ.maxGain * 20) / 20; },
    text: (b) => fmt.gain(b.gain),
    type: (b, s) => { const v = parseTyped(s); if (v == null) return false; b.gain = clamp(v, -EQ.maxGain, EQ.maxGain); return true; },
    reset: (b) => { b.gain = 0; },
  },
  q: {
    label: "Q",
    labelFor: (b) => (isCut(b) ? "SLOPE" : "Q"),
    steps: (b) => (isCut(b) ? EQ.slopes.length : 0),
    get: (b) => (isCut(b) ? EQ.slopes.indexOf(b.slope) / (EQ.slopes.length - 1) : toLog(b.q, EQ.minQ, qCeiling(b))),
    set: (b, t) => {
      if (isCut(b)) b.slope = EQ.slopes[Math.round(clamp(t, 0, 1) * (EQ.slopes.length - 1))];
      else b.q = fromLog(t, EQ.minQ, qCeiling(b));
    },
    text: (b) => (isCut(b) ? b.slope + " dB/oct" : fmt.q(b.q)),
    type: (b, s) => {
      const v = parseTyped(s); if (v == null) return false;
      if (isCut(b)) b.slope = EQ.slopes.reduce((a, x) => (Math.abs(x - v) < Math.abs(a - v) ? x : a));
      else b.q = clamp(v, EQ.minQ, qCeiling(b));
      return true;
    },
    reset: (b) => { if (isCut(b)) b.slope = 24; else b.q = 1; },
  },
  pos: {
    label: "POSITION", bipolar: true,
    enabled: (b) => !b.stereo,
    get: (b) => (b.pos + 180) / 360,
    set: (b, t) => { b.pos = Math.round((t * 360 - 180) * 2) / 2; },
    text: (b) => fmt.pos(b.pos),
    type: (b, s) => {
      const word = { m: 0, mid: 0, c: 0, centre: 0, center: 0, l: -90, left: -90, r: 90, right: 90, s: 180, side: 180 }[s.trim().toLowerCase()];
      if (word !== undefined) { b.pos = word; return true; }
      const v = parseTyped(s); if (v == null) return false;
      b.pos = clamp(v, -180, 180); return true;
    },
    reset: (b) => { b.pos = 0; },
  },
  range: {
    label: "RANGE", bipolar: true,
    enabled: (b) => !isCut(b),
    get: (b) => (b.range + EQ.maxGain) / (2 * EQ.maxGain),
    set: (b, t) => { const r = Math.round((t * 2 - 1) * EQ.maxGain * 20) / 20; b.range = Math.abs(r) < 0.05 ? 0 : r; },
    text: (b) => (b.range === 0 ? "Off" : fmt.gain(b.range)),
    type: (b, s) => {
      if (s.trim().toLowerCase() === "off") { b.range = 0; return true; }
      const v = parseTyped(s); if (v == null) return false;
      b.range = clamp(v, -EQ.maxGain, EQ.maxGain); return true;
    },
    reset: (b) => { b.range = 0; },
  },
  threshold: {
    label: "THRESHOLD",
    enabled: (b) => !isCut(b),
    get: (b) => (b.threshold - EQ.minThreshold) / (EQ.maxThreshold - EQ.minThreshold),
    set: (b, t) => { b.threshold = Math.round((EQ.minThreshold + t * (EQ.maxThreshold - EQ.minThreshold)) * 10) / 10; },
    text: (b) => fmt.db(b.threshold) + " dB",
    type: (b, s) => { const v = parseTyped(s); if (v == null) return false; b.threshold = clamp(v, EQ.minThreshold, EQ.maxThreshold); return true; },
    reset: (b) => { b.threshold = -30; },
  },
  ratio: {
    label: "RATIO",
    enabled: (b) => !isCut(b),
    get: (b) => toLog(b.ratio, EQ.minRatio, EQ.maxRatio),
    set: (b, t) => { b.ratio = Math.round(fromLog(t, EQ.minRatio, EQ.maxRatio) * 10) / 10; },
    text: (b) => fmt.ratio(b.ratio),
    type: (b, s) => { const v = parseTyped(s); if (v == null) return false; b.ratio = clamp(v, EQ.minRatio, EQ.maxRatio); return true; },
    reset: (b) => { b.ratio = 2; },
  },
  attack: {
    label: "ATTACK",
    enabled: (b) => !isCut(b),
    get: (b) => toLog(b.attack, EQ.minAttack, EQ.maxAttack),
    set: (b, t) => { b.attack = fromLog(t, EQ.minAttack, EQ.maxAttack); },
    text: (b) => fmt.ms(b.attack),
    type: (b, s) => { const v = parseTyped(s, true); if (v == null) return false; b.attack = clamp(v, EQ.minAttack, EQ.maxAttack); return true; },
    reset: (b) => { b.attack = 2; },
  },
  release: {
    label: "RELEASE",
    enabled: (b) => !isCut(b),
    get: (b) => toLog(b.release, EQ.minRelease, EQ.maxRelease),
    set: (b, t) => { b.release = fromLog(t, EQ.minRelease, EQ.maxRelease); },
    text: (b) => fmt.ms(b.release),
    type: (b, s) => { const v = parseTyped(s, true); if (v == null) return false; b.release = clamp(v, EQ.minRelease, EQ.maxRelease); return true; },
    reset: (b) => { b.release = 90; },
  },
};

// Filter shapes in dB, as the sphere draws them.
const response = {
  peak(gain, q, x) {
    const a = Math.pow(10, gain / 40), x2 = x * x, u = (1 - x2) ** 2;
    return 10 * Math.log10((u + x2 * a * a / (q * q)) / (u + x2 / (a * a * q * q)));
  },
  shelf(low, gain, q, x) {
    const a = Math.pow(10, gain / 40), k = a / Math.min(q, EQ.maxShelfQ) ** 2, r2 = x * x;
    const lo = (a - r2) ** 2 + k * r2, hi = (1 - a * r2) ** 2 + k * r2;
    return 10 * Math.log10(a * a * (low ? lo / hi : hi / lo));
  },
  cut(low, slope, x) {
    const n = slope / 6, ratio = low ? 1 / x : x;
    return -10 * Math.log10(1 + Math.pow(ratio, 2 * n));
  },
  of(b, f) {
    const x = f / b.freq;
    if (isCut(b)) return response.cut(b.type === "lowCut", b.slope, x);
    if (isShelf(b)) return response.shelf(b.type === "lowShelf", b.gain, b.q, x);
    return response.peak(b.gain, Math.min(b.q, 8), x);
  },
};

// The state: mock bands for now, and listeners for any change.
const store = {
  bands: [
    { id: 0, type: "lowCut", freq: 60, gain: 0, q: 0.71, slope: 24, stereo: true, pos: 0, muted: false, range: 0, threshold: -30, ratio: 2, attack: 2, release: 90, sc: false },
    { id: 1, type: "peak", freq: 320, gain: -4.5, q: 1.4, slope: 24, stereo: false, pos: 0, muted: false, range: 0, threshold: -30, ratio: 2, attack: 2, release: 90, sc: false },
    { id: 2, type: "peak", freq: 2400, gain: 3.5, q: 0.9, slope: 24, stereo: false, pos: 0, muted: false, range: -6, threshold: -24, ratio: 3, attack: 5, release: 120, sc: false },
    { id: 3, type: "highShelf", freq: 9000, gain: 4, q: 0.71, slope: 24, stereo: false, pos: 180, muted: false, range: 0, threshold: -30, ratio: 2, attack: 2, release: 90, sc: false },
  ],
  selected: 2,
  soloed: -1,
  sidechainConnected: false,
  trim: { in: 0, out: 0 },
  track: { name: "vox", colour: "#6aa7d8" },
  listeners: [],
  band() { return this.bands.find((b) => b.id === this.selected) || null; },
  changed() { this.listeners.forEach((f) => f()); },
  on(f) { this.listeners.push(f); },
};
