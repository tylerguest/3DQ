// A short synthesized demo track for the browser preview, so the sphere has
// real musical material to react to without needing a file. It's laid out to
// exercise the whole stereo sphere:
//   kick, snare, bass         -> centre (Mid), low and mid frequencies
//   hi-hats                   -> alternating left / right, high frequencies
//   pad (detuned pairs)       -> wide, so it fills the sides and back (Side)
//   arpeggio                  -> sweeps slowly across the stereo field
// 120 bpm, 8 bars of Am - F - C - G, rendered once offline.

const BPM = 120;
const BEAT = 60 / BPM;
const BARS = 8;
const LENGTH = BARS * 4 * BEAT;

const CHORDS = [
  { bass: 33, notes: [57, 60, 64] }, // Am
  { bass: 29, notes: [53, 57, 60] }, // F
  { bass: 36, notes: [55, 60, 64] }, // C
  { bass: 31, notes: [55, 59, 62] }, // G
];

const hz = (midi: number) => 440 * Math.pow(2, (midi - 69) / 12);

export async function renderDemoLoop(sampleRate: number): Promise<AudioBuffer> {
  const ctx = new OfflineAudioContext(2, Math.ceil(LENGTH * sampleRate), sampleRate);
  const master = new GainNode(ctx, { gain: 0.5 });
  master.connect(ctx.destination);

  const noise = ctx.createBuffer(1, sampleRate, sampleRate);
  const nd = noise.getChannelData(0);
  for (let i = 0; i < nd.length; ++i) nd[i] = Math.random() * 2 - 1;

  // Simple envelope: attack, then exponential-ish decay to silence.
  const envelope = (g: GainNode, t: number, peak: number, attack: number, decay: number) => {
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(peak, t + attack);
    g.gain.setTargetAtTime(0, t + attack, decay / 4);
  };

  const panned = (pan: number) => {
    const p = new StereoPannerNode(ctx, { pan });
    p.connect(master);
    return p;
  };

  const centre = panned(0);

  const kick = (t: number) => {
    const osc = new OscillatorNode(ctx, { type: "sine", frequency: 150 });
    osc.frequency.setValueAtTime(150, t);
    osc.frequency.exponentialRampToValueAtTime(45, t + 0.12);
    const g = new GainNode(ctx);
    envelope(g, t, 1.0, 0.002, 0.35);
    osc.connect(g).connect(centre);
    osc.start(t);
    osc.stop(t + 0.5);
  };

  const snare = (t: number) => {
    const src = new AudioBufferSourceNode(ctx, { buffer: noise });
    const hp = new BiquadFilterNode(ctx, { type: "highpass", frequency: 1200 });
    const g = new GainNode(ctx);
    envelope(g, t, 0.45, 0.001, 0.18);
    src.connect(hp).connect(g).connect(centre);
    src.start(t, Math.random() * 0.5);
    src.stop(t + 0.3);

    const body = new OscillatorNode(ctx, { type: "triangle", frequency: 190 });
    const bg = new GainNode(ctx);
    envelope(bg, t, 0.35, 0.001, 0.08);
    body.connect(bg).connect(centre);
    body.start(t);
    body.stop(t + 0.15);
  };

  const hat = (t: number, pan: number, open: boolean) => {
    const src = new AudioBufferSourceNode(ctx, { buffer: noise });
    const hp = new BiquadFilterNode(ctx, { type: "highpass", frequency: 7000 });
    const g = new GainNode(ctx);
    envelope(g, t, open ? 0.16 : 0.12, 0.001, open ? 0.2 : 0.05);
    src.connect(hp).connect(g).connect(panned(pan));
    src.start(t, Math.random() * 0.5);
    src.stop(t + 0.3);
  };

  const bass = (t: number, midi: number, dur: number) => {
    const osc = new OscillatorNode(ctx, { type: "sawtooth", frequency: hz(midi) });
    const lp = new BiquadFilterNode(ctx, { type: "lowpass", frequency: 420, Q: 4 });
    const g = new GainNode(ctx);
    envelope(g, t, 0.32, 0.005, dur);
    osc.connect(lp).connect(g).connect(centre);
    osc.start(t);
    osc.stop(t + dur + 0.1);
  };

  // Each pad note is two detuned voices spread hard left and right: wide,
  // partly decorrelated, so it lights up the sides and back of the sphere.
  const pad = (t: number, midi: number, dur: number) => {
    for (const [pan, cents] of [[-0.9, -9], [0.9, 9]] as const) {
      const osc = new OscillatorNode(ctx, { type: "sawtooth", frequency: hz(midi), detune: cents });
      const lp = new BiquadFilterNode(ctx, { type: "lowpass", frequency: 1600 });
      const g = new GainNode(ctx);
      g.gain.setValueAtTime(0, t);
      g.gain.linearRampToValueAtTime(0.045, t + 0.4);
      g.gain.setValueAtTime(0.045, t + dur - 0.3);
      g.gain.linearRampToValueAtTime(0, t + dur);
      osc.connect(lp).connect(g).connect(panned(pan));
      osc.start(t);
      osc.stop(t + dur + 0.05);
    }
  };

  const arp = (t: number, midi: number, pan: number) => {
    const osc = new OscillatorNode(ctx, { type: "square", frequency: hz(midi) });
    const lp = new BiquadFilterNode(ctx, { type: "lowpass", frequency: 3500 });
    const g = new GainNode(ctx);
    envelope(g, t, 0.07, 0.003, 0.14);
    osc.connect(lp).connect(g).connect(panned(pan));
    osc.start(t);
    osc.stop(t + 0.25);
  };

  for (let bar = 0; bar < BARS; ++bar) {
    const chord = CHORDS[Math.floor(bar / 2) % CHORDS.length];
    const barStart = bar * 4 * BEAT;

    if (bar % 2 === 0) for (const n of chord.notes) pad(barStart, n, 8 * BEAT);

    for (let beat = 0; beat < 4; ++beat) {
      const t = barStart + beat * BEAT;
      kick(t);
      if (beat % 2 === 1) snare(t);
    }

    for (let eighth = 0; eighth < 8; ++eighth) {
      const t = barStart + eighth * BEAT / 2;
      hat(t, eighth % 2 === 0 ? -0.7 : 0.7, eighth === 7);
      bass(t, chord.bass + (eighth % 4 === 3 ? 12 : 0), BEAT / 2 * 0.9);
    }

    for (let sixteenth = 0; sixteenth < 16; ++sixteenth) {
      const t = barStart + sixteenth * BEAT / 4;
      const tones = [...chord.notes, chord.notes[0] + 12];
      const midi = tones[sixteenth % tones.length] + 12;
      const pan = Math.sin((t / LENGTH) * Math.PI * 4) * 0.9; // sweeps across the field
      if (sixteenth % 4 !== 3) arp(t, midi, pan);
    }
  }

  const buffer = await ctx.startRendering();

  // Normalise to -1 dBFS.
  let peak = 0;
  for (let ch = 0; ch < buffer.numberOfChannels; ++ch)
    for (const v of buffer.getChannelData(ch)) peak = Math.max(peak, Math.abs(v));
  const gain = peak > 0 ? 0.89 / peak : 1;
  for (let ch = 0; ch < buffer.numberOfChannels; ++ch) {
    const d = buffer.getChannelData(ch);
    for (let i = 0; i < d.length; ++i) d[i] *= gain;
  }

  return buffer;
}
