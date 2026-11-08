// Browser-only audio preview of the 3DQ EQ, built on Web Audio.
//
// Used when the GUI runs outside the plugin (dev server in a browser) so the
// sphere can be heard without rebuilding C++. Inside the plugin the processor
// does the audio instead, with the same maths.
//
// Stereo position: a band's longitude picks an axis u = (cos φ, sin φ) in the
// (L, R) plane, with φ = 45° + lon/2:
//   front (lon 0)    -> φ 45°   -> Mid  (L+R)/√2
//   right (lon 90°)  -> φ 90°   -> R
//   back  (lon 180°) -> φ 135°  -> Side (R-L)/√2
//   left  (lon -90°) -> φ 0°    -> L
// Each band filters only the signal along its axis:
//   c = u·x,   x' = x + u · (H(c) - c)
// so a Mid band leaves the side signal untouched, an R band leaves L untouched, etc.
// Bands run in series.

import { Band, DEFAULT_CUT_SLOPE, DEFAULT_THRESHOLD_DB, MAX_SHELF_Q, dynamicGainFor, isCut, isShelf, latToFreq } from "./bands";
import type { BandDynamics } from "./pluginBridge";
import { Spectrum, reduceToBands } from "./spectrum";

export function stereoAxis(lon: number): [number, number] {
  const phi = Math.PI / 4 + lon / 2;
  return [Math.cos(phi), Math.sin(phi)];
}

const SMOOTHING = 0.015; // seconds
const DYN_ATTACK = 0.002; // detector time constants (s), as in SphereEq.h
const DYN_RELEASE = 0.09;

function stereoGain(ctx: BaseAudioContext, gain = 1) {
  return new GainNode(ctx, { gain, channelCount: 2, channelCountMode: "explicit", channelInterpretation: "speakers" });
}

function monoGain(ctx: BaseAudioContext, gain = 1) {
  return new GainNode(ctx, { gain, channelCount: 1, channelCountMode: "explicit", channelInterpretation: "discrete" });
}

class BandStage {
  readonly input: GainNode;
  readonly output: GainNode;
  private readonly projL: GainNode;
  private readonly projR: GainNode;
  private readonly backL: GainNode;
  private readonly backR: GainNode;
  private readonly filter: BiquadFilterNode; // peaking (peak bands)
  private readonly filterIn: GainNode; // component -> [filter chain] -> filterOut
  private readonly filterOut: GainNode;
  private readonly wet: GainNode; // 0..1, mutes / fades the band's effect
  private readonly nodes: AudioNode[];
  // Cut bands: Butterworth second-order sections, plus a first-order one for
  // odd orders (Web Audio has no first-order type, so it's an IIRFilterNode
  // rebuilt when the cutoff moves).
  private cutSections: BiquadFilterNode[] = [];
  private firstOrder: IIRFilterNode | null = null;
  private firstOrderKey = "";
  private chainKey = "";

  // Dynamics side-chain: this band's component, band-passed at its frequency.
  // The main thread reads it (see updateDynamics) and moves the filter gain.
  private readonly sidechain: BiquadFilterNode;
  // Shelves: 1 + (A²-1)·(low/high-pass) + (A-1)·band-pass, the RBJ shelf split
  // into parts, because Web Audio's own shelf filters ignore Q.
  private readonly shelfPass: BiquadFilterNode;
  private readonly shelfBand: BiquadFilterNode;
  private readonly shelfPassGain: GainNode;
  private readonly shelfBandGain: GainNode;
  private readonly detector: AnalyserNode;
  private readonly detectorData = new Float32Array(256);
  private envelope = 0;
  private band: Band | null = null;
  liveGainDb = 0;
  levelDb = -120;

  constructor(private readonly ctx: BaseAudioContext) {
    this.input = stereoGain(ctx);
    this.output = stereoGain(ctx);

    const splitter = ctx.createChannelSplitter(2);
    this.projL = monoGain(ctx);
    this.projR = monoGain(ctx);
    const component = monoGain(ctx);
    this.filter = new BiquadFilterNode(ctx, { type: "peaking", channelCount: 1, channelCountMode: "explicit" });
    const minusDry = monoGain(ctx, -1);
    const delta = monoGain(ctx);
    this.filterIn = monoGain(ctx);
    this.filterOut = monoGain(ctx);
    this.wet = monoGain(ctx, 1);
    this.backL = monoGain(ctx);
    this.backR = monoGain(ctx);
    const merger = ctx.createChannelMerger(2);
    this.sidechain = new BiquadFilterNode(ctx, { type: "bandpass", channelCount: 1, channelCountMode: "explicit" });
    this.detector = new AnalyserNode(ctx, { fftSize: 256, channelCount: 1, channelCountMode: "explicit" });

    // Dry path: x
    this.input.connect(this.output);

    // c = u·x
    this.input.connect(splitter);
    splitter.connect(this.projL, 0);
    splitter.connect(this.projR, 1);
    this.projL.connect(component);
    this.projR.connect(component);

    // d = H(c) - c
    component.connect(this.filterIn);
    component.connect(minusDry);
    component.connect(this.sidechain);
    this.sidechain.connect(this.detector);
    this.filterIn.connect(this.filter);
    this.filter.connect(this.filterOut);
    this.chainKey = "peak";
    this.shelfPass = new BiquadFilterNode(ctx, { type: "lowpass", channelCount: 1, channelCountMode: "explicit" });
    this.shelfBand = new BiquadFilterNode(ctx, { type: "bandpass", channelCount: 1, channelCountMode: "explicit" });
    this.shelfPassGain = monoGain(ctx, 0);
    this.shelfBandGain = monoGain(ctx, 0);
    this.shelfPass.connect(this.shelfPassGain);
    this.shelfBand.connect(this.shelfBandGain);
    this.shelfPassGain.connect(this.filterOut);
    this.shelfBandGain.connect(this.filterOut);
    this.filterOut.connect(delta);
    minusDry.connect(delta);

    // x' = x + u·(wet·d)
    delta.connect(this.wet);
    this.wet.connect(this.backL);
    this.wet.connect(this.backR);
    this.backL.connect(merger, 0, 0);
    this.backR.connect(merger, 0, 1);
    merger.connect(this.output);

    this.nodes = [this.input, this.output, splitter, this.projL, this.projR, component, this.filter,
                  minusDry, delta, this.backL, this.backR, merger, this.sidechain, this.detector,
                  this.filterIn, this.filterOut, this.wet, this.shelfPass, this.shelfBand,
                  this.shelfPassGain, this.shelfBandGain];
  }

  set(band: Band, immediate: boolean) {
    const [uL, uR] = stereoAxis(band.lon);
    const t = this.ctx.currentTime;
    const apply = (param: AudioParam, value: number) =>
      immediate ? param.setValueAtTime(value, t) : param.setTargetAtTime(value, t, SMOOTHING);

    apply(this.projL.gain, uL);
    apply(this.projR.gain, uR);
    apply(this.backL.gain, uL);
    apply(this.backR.gain, uR);
    apply(this.filter.frequency, latToFreq(band.lat));
    this.band = band;
    this.setChain(band);
    apply(this.wet.gain, band.muted ? 0 : 1);
    if (!band.range) apply(this.filter.gain, band.muted ? 0 : band.gainDb); // muted: flat, i.e. no effect
    apply(this.filter.Q, band.q);
    if (isShelf(band) && (!band.range || immediate)) this.applyShelf(band, band.muted ? 0 : band.gainDb, immediate ? 0 : SMOOTHING);
    // A shelf's detector covers the side of the spectrum it moves.
    const shelf = isShelf(band);
    this.sidechain.type = !shelf ? "bandpass" : band.type === "lowshelf" ? "lowpass" : "highpass";
    apply(this.sidechain.frequency, latToFreq(band.lat));
    apply(this.sidechain.Q, shelf ? -3.01 : band.q); // low/high-pass Q is in dB: Butterworth
    if (!band.range) {
      this.envelope = 0;
      this.liveGainDb = band.muted ? 0 : band.gainDb;
    }
  }

  // Moves the shelf's parts to a gain. Each part needs its corner at f / √A
  // (low shelf) or f · √A (high shelf). timeConstant 0 = jump there now.
  private applyShelf(band: Band, gainDb: number, timeConstant: number) {
    const t = this.ctx.currentTime;
    const apply = (param: AudioParam, value: number) =>
      timeConstant === 0 ? param.setValueAtTime(value, t) : param.setTargetAtTime(value, t, timeConstant);
    const A = Math.pow(10, gainDb / 40);
    const q = Math.min(band.q, MAX_SHELF_Q);
    const corner = latToFreq(band.lat) * (band.type === "lowshelf" ? 1 / Math.sqrt(A) : Math.sqrt(A));
    apply(this.shelfPass.frequency, corner);
    apply(this.shelfBand.frequency, corner);
    apply(this.shelfPass.Q, 20 * Math.log10(q)); // low/high-pass Q is in dB
    apply(this.shelfBand.Q, q);
    apply(this.shelfPassGain.gain, A * A - 1);
    apply(this.shelfBandGain.gain, A - 1);
  }

  // Builds / updates the filter chain between filterIn and filterOut.
  private setChain(band: Band) {
    const cut = isCut(band);
    const slope = band.slope ?? DEFAULT_CUT_SLOPE;
    const order = cut ? slope / 6 : 0;
    const shelf = isShelf(band);
    const key = cut ? `${band.type}/${order}` : shelf ? band.type! : "peak";
    const freq = latToFreq(band.lat);
    const high = band.type === "lowcut"; // low cut = high-pass

    const layoutChanged = key !== this.chainKey;
    if (layoutChanged) {
      this.wired = false;
      this.filterIn.disconnect();
      this.filter.disconnect();
      for (const f of this.cutSections) f.disconnect();
      this.firstOrder?.disconnect();
      this.cutSections = [];
      this.firstOrder = null;
      this.firstOrderKey = "";
      this.chainKey = key;

      if (shelf) {
        this.shelfPass.type = band.type === "lowshelf" ? "lowpass" : "highpass";
        this.filterIn.connect(this.filterOut);
        this.filterIn.connect(this.shelfPass);
        this.filterIn.connect(this.shelfBand);
      } else if (!cut) {
        this.filterIn.connect(this.filter);
        this.filter.connect(this.filterOut);
      } else {
        for (let i = 0; i < Math.floor(order / 2); ++i) {
          const theta = order % 2 === 0 ? ((2 * i + 1) * Math.PI) / (2 * order) : ((i + 1) * Math.PI) / order;
          const q = 1 / (2 * Math.cos(theta));
          // Web Audio's highpass / lowpass Q is a resonance in dB.
          this.cutSections.push(new BiquadFilterNode(this.ctx, {
            type: high ? "highpass" : "lowpass", frequency: freq, Q: 20 * Math.log10(q),
            channelCount: 1, channelCountMode: "explicit",
          }));
        }
      }
    }

    if (!cut) return;

    for (const f of this.cutSections) f.frequency.setTargetAtTime(freq, this.ctx.currentTime, SMOOTHING);

    let rebuilt = layoutChanged;
    if (order % 2 === 1) {
      // First-order section (bilinear, prewarped), as in SphereEq.h.
      const fKey = freq.toFixed(1);
      if (fKey !== this.firstOrderKey) {
        const K = Math.tan((Math.PI * Math.min(freq, 0.49 * this.ctx.sampleRate)) / this.ctx.sampleRate);
        const b0 = high ? 1 / (1 + K) : K / (1 + K);
        this.firstOrder?.disconnect();
        this.firstOrder = new IIRFilterNode(this.ctx, {
          feedforward: [b0, high ? -b0 : b0], feedback: [1, (K - 1) / (K + 1)],
          channelCount: 1, channelCountMode: "explicit",
        });
        this.firstOrderKey = fKey;
        rebuilt = true;
      }
    }
    if (!rebuilt && this.wired) return;

    // (Re)connect: filterIn -> sections... -> [first order] -> filterOut.
    this.filterIn.disconnect();
    for (const f of this.cutSections) f.disconnect();
    const chain: AudioNode[] = [...this.cutSections, ...(this.firstOrder ? [this.firstOrder] : [])];
    let node: AudioNode = this.filterIn;
    for (const f of chain) { node.connect(f); node = f; }
    node.connect(this.filterOut);
    this.wired = true;
  }

  private wired = false;

  // The side-chain's peak level over the last few ms (0 for a static band).
  readPeak() {
    if (!this.band?.range || isCut(this.band)) return 0;
    this.detector.getFloatTimeDomainData(this.detectorData);
    let peak = 0;
    for (const v of this.detectorData) peak = Math.max(peak, Math.abs(v));
    return peak;
  }

  // Dynamic bands: follow the side-chain level and move the gain.
  // Called often from the main thread (every few ms) while audio plays.
  updateDynamics(dt: number, peak: number) {
    const band = this.band;
    if (!band || !band.range || isCut(band)) return;

    const k = peak > this.envelope ? 1 - Math.exp(-dt / DYN_ATTACK) : 1 - Math.exp(-dt / DYN_RELEASE);
    this.envelope += (peak - this.envelope) * k;
    this.levelDb = 20 * Math.log10(this.envelope + 1e-9);

    const dyn = band.muted ? 0 : dynamicGainFor(this.levelDb, band.threshold ?? DEFAULT_THRESHOLD_DB, band.range);
    this.liveGainDb = band.muted ? 0 : band.gainDb + dyn;
    if (isShelf(band)) this.applyShelf(band, this.liveGainDb, 0.003);
    else this.filter.gain.setTargetAtTime(this.liveGainDb, this.ctx.currentTime, 0.003);
  }

  dispose() {
    for (const n of this.nodes) n.disconnect();
    for (const f of this.cutSections) f.disconnect();
    this.firstOrder?.disconnect();
  }
}

// The EQ itself: input -> band stages in series -> output, with a bypass.
export class EqChain {
  readonly input: GainNode;
  readonly output: GainNode;
  private readonly wet: GainNode;
  private readonly dry: GainNode;
  private stages: BandStage[] = [];
  private bandIds: number[] = [];

  constructor(private readonly ctx: BaseAudioContext) {
    this.input = stereoGain(ctx);
    this.output = stereoGain(ctx);
    this.wet = stereoGain(ctx, 1);
    this.dry = stereoGain(ctx, 0);
    this.input.connect(this.dry);
    this.dry.connect(this.output);
    this.wet.connect(this.output);
    this.rewire();
  }

  setBands(requested: Band[]) {
    // A stereo band is two stages in series, one on the L axis and one on the
    // R axis: the same filter on each channel.
    const bands: Band[] = [];
    for (const b of requested) {
      if (b.stereo) bands.push({ ...b, lon: -Math.PI / 2 }, { ...b, lon: Math.PI / 2 });
      else bands.push(b);
    }
    const ids = bands.map((b) => b.id);
    const sameLayout = ids.length === this.bandIds.length && ids.every((id, i) => id === this.bandIds[i]);

    if (!sameLayout) {
      for (const s of this.stages) s.dispose();
      this.stages = bands.map(() => new BandStage(this.ctx));
      this.bandIds = ids;
      this.rewire();
    }

    bands.forEach((b, i) => this.stages[i].set(b, !sameLayout));
  }

  updateDynamics(dt: number) {
    // The two halves of a stereo band share the louder channel's level.
    const peaks = this.stages.map((s) => s.readPeak());
    this.stages.forEach((s, i) => {
      const id = this.bandIds[i];
      s.updateDynamics(dt, Math.max(peaks[i], this.bandIds[i - 1] === id ? peaks[i - 1] : 0, this.bandIds[i + 1] === id ? peaks[i + 1] : 0));
    });
  }

  // Live gain / detector level per band id.
  readDynamics(): Map<number, BandDynamics> {
    const out = new Map<number, BandDynamics>();
    this.stages.forEach((s, i) => out.set(this.bandIds[i], { gainDb: s.liveGainDb, levelDb: s.levelDb }));
    return out;
  }

  setBypassed(bypassed: boolean) {
    const t = this.ctx.currentTime;
    this.wet.gain.setTargetAtTime(bypassed ? 0 : 1, t, SMOOTHING);
    this.dry.gain.setTargetAtTime(bypassed ? 1 : 0, t, SMOOTHING);
  }

  private rewire() {
    this.input.disconnect();
    this.input.connect(this.dry);
    let node: AudioNode = this.input;
    for (const s of this.stages) {
      node.connect(s.input);
      node = s.output;
    }
    node.connect(this.wet);
  }
}

// Stereo pink noise (independent per channel), Paul Kellet's filter.
export function makePinkNoise(ctx: BaseAudioContext, seconds = 6): AudioBuffer {
  const length = Math.floor(ctx.sampleRate * seconds);
  const buffer = ctx.createBuffer(2, length, ctx.sampleRate);

  for (let ch = 0; ch < 2; ++ch) {
    const data = buffer.getChannelData(ch);
    let b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (let i = 0; i < length; ++i) {
      const white = Math.random() * 2 - 1;
      b0 = 0.99886 * b0 + white * 0.0555179;
      b1 = 0.99332 * b1 + white * 0.0750759;
      b2 = 0.96900 * b2 + white * 0.1538520;
      b3 = 0.86650 * b3 + white * 0.3104856;
      b4 = 0.55000 * b4 + white * 0.5329522;
      b5 = -0.7616 * b5 - white * 0.0168980;
      data[i] = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.06;
      b6 = white * 0.115926;
    }
  }

  return buffer;
}

// Plays a looping buffer through the EQ to the speakers, and analyses the
// EQ's output so the sphere can react to it.
export class AudioPreview {
  readonly ctx = new AudioContext();
  readonly eq = new EqChain(this.ctx);
  private source: AudioBufferSourceNode | null = null;
  private readonly master = stereoGain(this.ctx, 0.8);

  // Analysers on L, R and M = (L+R)/2. |M|² lets us recover Re(L·conj R).
  private static readonly FFT_SIZE = 4096;
  private readonly analyserL: AnalyserNode;
  private readonly analyserR: AnalyserNode;
  private readonly analyserM: AnalyserNode;
  private readonly binsL: Float32Array<ArrayBuffer>;
  private readonly binsR: Float32Array<ArrayBuffer>;
  private readonly binsM: Float32Array<ArrayBuffer>;

  constructor() {
    this.eq.output.connect(this.master);
    this.master.connect(this.ctx.destination);

    const makeAnalyser = () =>
      new AnalyserNode(this.ctx, { fftSize: AudioPreview.FFT_SIZE, smoothingTimeConstant: 0,
                                   channelCount: 1, channelCountMode: "explicit" });
    this.analyserL = makeAnalyser();
    this.analyserR = makeAnalyser();
    this.analyserM = makeAnalyser();

    const splitter = this.ctx.createChannelSplitter(2);
    const halfL = monoGain(this.ctx, 0.5);
    const halfR = monoGain(this.ctx, 0.5);
    this.eq.output.connect(splitter);
    splitter.connect(this.analyserL, 0);
    splitter.connect(this.analyserR, 1);
    splitter.connect(halfL, 0);
    splitter.connect(halfR, 1);
    halfL.connect(this.analyserM);
    halfR.connect(this.analyserM);

    const bins = AudioPreview.FFT_SIZE / 2;
    this.binsL = new Float32Array(bins);
    this.binsR = new Float32Array(bins);
    this.binsM = new Float32Array(bins);
  }

  get playing() {
    return this.source !== null;
  }

  async play(buffer: AudioBuffer) {
    await this.ctx.resume();
    this.stop();
    const source = new AudioBufferSourceNode(this.ctx, { buffer, loop: true });
    source.connect(this.eq.input);
    source.start();
    this.source = source;

    // Drive the dynamic bands from the main thread while playing.
    let last = performance.now();
    this.dynamicsTimer ??= window.setInterval(() => {
      const now = performance.now();
      this.eq.updateDynamics(Math.min(0.05, (now - last) / 1000));
      last = now;
    }, 5);
  }

  private dynamicsTimer: number | undefined;

  readDynamics(): Map<number, BandDynamics> | null {
    return this.playing ? this.eq.readDynamics() : null;
  }

  stop() {
    if (!this.source) return;
    this.source.stop();
    this.source.disconnect();
    this.source = null;
    window.clearInterval(this.dynamicsTimer);
    this.dynamicsTimer = undefined;
  }

  async decode(file: File) {
    return this.ctx.decodeAudioData(await file.arrayBuffer());
  }

  // Current stereo spectrum of the EQ output, or null when nothing is playing.
  readSpectrum(): Spectrum | null {
    if (!this.playing) return null;

    this.analyserL.getFloatFrequencyData(this.binsL);
    this.analyserR.getFloatFrequencyData(this.binsR);
    this.analyserM.getFloatFrequencyData(this.binsM);

    // dB -> power, in place (the arrays are refilled every read).
    for (const bins of [this.binsL, this.binsR, this.binsM])
      for (let k = 0; k < bins.length; ++k) bins[k] = Math.pow(10, bins[k] / 10);
    const spec = reduceToBands(this.binsL.length, this.ctx.sampleRate / AudioPreview.FFT_SIZE,
                               (k) => this.binsL[k],
                               (k) => this.binsR[k],
                               (k) => this.binsM[k]);

    // |M|² = (ll + rr + 2·lr) / 4  ->  lr = 2|M|² - (ll + rr) / 2, kept physically possible.
    for (let b = 0; b < spec.lr.length; ++b) {
      const limit = Math.sqrt(spec.ll[b] * spec.rr[b]);
      spec.lr[b] = Math.max(-limit, Math.min(limit, 2 * spec.lr[b] - (spec.ll[b] + spec.rr[b]) / 2));
    }

    return spec;
  }
}

// One shared preview per page, created on first use (after a user gesture).
let sharedPreview: AudioPreview | null = null;

export const getPreview = () => (sharedPreview ??= new AudioPreview());
export const peekPreview = () => sharedPreview;
