// Talks to the 3DQ plugin's editor (Source/PluginEditor.cpp) through JUCE's
// WebView interop. Only used when the GUI runs inside the plugin.

import * as Juce from "@juce-framework/webview";
import { Band, WireBand, fromWire, toWire } from "./bands";
import { Spectrum } from "./spectrum";

interface JuceBackend {
  addEventListener(eventId: string, fn: (payload: unknown) => void): unknown;
  removeEventListener(handle: unknown): void;
}

const juceWindow = window as {
  __JUCE__?: { backend: JuceBackend; initialisationData?: { __juce__functions?: string[] } };
};

// The interop library creates a stub window.__JUCE__ even in a plain browser,
// so check for one of our own native functions instead.
export const inPlugin = (juceWindow.__JUCE__?.initialisationData?.__juce__functions ?? []).indexOf("setBands") >= 0;

const setBandsNative = inPlugin ? Juce.getNativeFunction("setBands") : null;
const getBandsNative = inPlugin ? Juce.getNativeFunction("getBands") : null;

export function sendBands(bands: Band[]) {
  void setBandsNative?.(bands.map(toWire));
}

export async function fetchBands(): Promise<Band[]> {
  const wire = ((await getBandsNative?.()) ?? []) as WireBand[];
  return wire.map(fromWire);
}

// Called when the host changes the bands (automation, preset load, ...).
// Returns an unsubscribe function.
export function onHostBandsChanged(fn: (bands: Band[]) => void): () => void {
  const backend = juceWindow.__JUCE__?.backend;
  if (!backend) return () => {};
  const handle = backend.addEventListener("bandsChanged", (payload) => fn((payload as WireBand[]).map(fromWire)));
  return () => backend.removeEventListener(handle);
}

// Latest stereo spectrum sent by the plugin ("spectrum" event, ~60 Hz), with
// each band slot's live dynamic gain and detector level.
interface WireSpectrum {
  ll: number[];
  rr: number[];
  lr: number[];
  gains?: number[];
  levels?: number[];
}

export interface BandDynamics {
  gainDb: number; // current total gain
  levelDb: number; // detector level the threshold compares against
}

let latestDynamics: Map<number, BandDynamics> | null = null;

let latestSpectrum: Spectrum | null = null;
let latestSpectrumTime = 0;

if (inPlugin) juceWindow.__JUCE__?.backend.addEventListener("spectrum", (payload) => {
  const w = payload as WireSpectrum;
  latestSpectrum = { ll: Float32Array.from(w.ll), rr: Float32Array.from(w.rr), lr: Float32Array.from(w.lr) };
  if (w.gains && w.levels) {
    const dyn = new Map<number, BandDynamics>();
    w.gains.forEach((g, slot) => dyn.set(slot, { gainDb: g, levelDb: w.levels![slot] }));
    latestDynamics = dyn;
  }
  latestSpectrumTime = performance.now();
});

export function getPluginDynamics(): Map<number, BandDynamics> | null {
  return performance.now() - latestSpectrumTime < 250 ? latestDynamics : null;
}

export function getPluginSpectrum(): Spectrum | null {
  return performance.now() - latestSpectrumTime < 250 ? latestSpectrum : null;
}
