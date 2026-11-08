// Stereo spectrum used to make the sphere react to audio.
//
// For each of NUM_SPECTRUM_BANDS log-spaced frequency bands (20 Hz .. 20 kHz,
// matching the sphere's latitude) we keep three powers:
//   ll = |L|²,  rr = |R|²,  lr = Re(L · conj(R))
// From those, the energy along any stereo axis u = (cos φ, sin φ) is
//   |u·X|² = cos²φ·ll + sin²φ·rr + 2·cosφ·sinφ·lr
// which is what each particle (at its own longitude) shows.
//
// The plugin computes the same thing in C++ (Source/PluginEditor.cpp).

import { MAX_FREQ, MIN_FREQ } from "./bands";

export const NUM_SPECTRUM_BANDS = 48; // must match the C++ side

export interface Spectrum {
  ll: Float32Array;
  rr: Float32Array;
  lr: Float32Array;
}

export const emptySpectrum = (): Spectrum => ({
  ll: new Float32Array(NUM_SPECTRUM_BANDS),
  rr: new Float32Array(NUM_SPECTRUM_BANDS),
  lr: new Float32Array(NUM_SPECTRUM_BANDS),
});

const bandEdge = (i: number) => MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, i / NUM_SPECTRUM_BANDS);

// Integral of a per-bin power over [a, b] (in bins), treating the bins as
// samples of a continuous density joined by straight lines. Unlike summing
// whole bins, this changes smoothly as band edges move, so neighbouring bands
// don't come out identical (narrow low bands) or jump by a bin's worth.
function integrateBins(bin: (k: number) => number, numBins: number, a: number, b: number) {
  const at = (x: number) => {
    const k = Math.min(Math.floor(x), numBins - 2);
    const f = x - k;
    return bin(k) * (1 - f) + bin(k + 1) * f;
  };
  let sum = 0;
  let x0 = a;
  while (x0 < b) {
    const x1 = Math.min(b, Math.floor(x0) + 1);
    sum += (x1 - x0) * 0.5 * (at(x0) + at(x1)); // exact for a straight segment
    x0 = x1;
  }
  return sum;
}

// Integrates per-bin powers over each log band.
export function reduceToBands(
  numBins: number,
  binHz: number,
  binLL: (k: number) => number,
  binRR: (k: number) => number,
  binLR: (k: number) => number,
): Spectrum {
  const out = emptySpectrum();

  for (let b = 0; b < NUM_SPECTRUM_BANDS; ++b) {
    const a = Math.min(bandEdge(b) / binHz, numBins - 1);
    const z = Math.min(bandEdge(b + 1) / binHz, numBins - 1);
    out.ll[b] = integrateBins(binLL, numBins, a, z);
    out.rr[b] = integrateBins(binRR, numBins, a, z);
    out.lr[b] = integrateBins(binLR, numBins, a, z);
  }

  return out;
}

// 0..1 display level for a power.
export const SPECTRUM_FLOOR_DB = -80;
export const SPECTRUM_RANGE_DB = 60;

export function powerToLevel(power: number) {
  const db = 10 * Math.log10(power + 1e-20);
  return Math.max(0, Math.min(1, (db - SPECTRUM_FLOOR_DB) / SPECTRUM_RANGE_DB));
}
