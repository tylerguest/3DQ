// The presets in the browser's storage, the A and B setups, and the undo
// history of each. Same behaviour as Source/PresetBank.cpp.

import { Band } from "./bands";
import { PresetBarInfo } from "./presetBar";

const STORAGE_KEY = "3dq.presets";
const DEFAULT_NAME = "Default";
const EDIT_RUN_MS = 500;
const MAX_UNDO_STEPS = 200;

interface Setup {
  bands: Band[];
  name: string;
  saved: Band[] | null; // what the preset holds, to tell if it's been changed
}

interface Slot {
  now: Setup;
  undo: Setup[];
  redo: Setup[];
}

const near = (x: number, y: number, tolerance: number) => Math.abs(x - y) <= tolerance;

function sameBands(a: Band[], b: Band[]) {
  if (a.length !== b.length) return false;
  return a.every((x) => {
    const y = b.find((other) => other.id === x.id);
    return !!y && !!x.muted === !!y.muted && (x.type ?? "peak") === (y.type ?? "peak") && x.slope === y.slope && !!x.stereo === !!y.stereo
      && near(x.lat, y.lat, 0.002) && near(x.lon, y.lon, 0.003) && near(x.gainDb, y.gainDb, 0.02) && near(x.q, y.q, x.q * 0.005)
      && near(x.range ?? 0, y.range ?? 0, 0.02) && near(x.threshold ?? 0, y.threshold ?? 0, 0.06);
  });
}

const copyBands = (bands: Band[]): Band[] => bands.map((b) => {
  const { liveGainDb: _live, soloed: _soloed, ...settings } = b;
  return settings;
});

function readStore(): Record<string, Band[]> {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "{}");
    return parsed && typeof parsed === "object" ? parsed : {};
  } catch {
    return {};
  }
}

function writeStore(store: Record<string, Band[]>) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(store));
    return true;
  } catch {
    return false;
  }
}

export const legalPresetName = (name: string) => name.trim().replace(/[\\/:*?"<>|]/g, "");
export const isReservedPresetName = (name: string) => legalPresetName(name).toLowerCase() === DEFAULT_NAME.toLowerCase();

const newSlot = (): Slot => ({ now: { bands: [], name: DEFAULT_NAME, saved: [] }, undo: [], redo: [] });
const copySetup = (s: Setup): Setup => ({ bands: copyBands(s.bands), name: s.name, saved: s.saved && copyBands(s.saved) });

export class PresetBank {
  private readonly slots = [newSlot(), newSlot()];
  private active = 0;
  private names: string[] = [DEFAULT_NAME];
  private lastEdit = 0;

  constructor() { this.rescan(); }

  private get slot() { return this.slots[this.active]; }
  private get now() { return this.slot.now; }

  bands() { return copyBands(this.now.bands); }
  name() { return this.now.name; }

  rescan() {
    const found = Object.keys(readStore()).sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" }));
    this.names = [DEFAULT_NAME, ...found];
  }

  exists(name: string) { return legalPresetName(name) in readStore(); }

  private remember() {
    const slot = this.slot;
    slot.undo.push(copySetup(slot.now));
    if (slot.undo.length > MAX_UNDO_STEPS) slot.undo.shift();
    slot.redo.length = 0;
  }

  // A change the user made on the sphere. A run of quick changes, such as a
  // drag, is one undo step.
  edited(bands: Band[]) {
    if (sameBands(this.now.bands, bands)) {
      this.now.bands = copyBands(bands);
      return;
    }
    const time = performance.now();
    if (time - this.lastEdit > EDIT_RUN_MS) this.remember();
    this.lastEdit = time;
    this.now.bands = copyBands(bands);
  }

  undo() {
    const slot = this.slot, previous = slot.undo.pop();
    if (!previous) return false;
    slot.redo.push(slot.now);
    slot.now = previous;
    this.lastEdit = 0;
    return true;
  }

  redo() {
    const slot = this.slot, next = slot.redo.pop();
    if (!next) return false;
    slot.undo.push(slot.now);
    slot.now = next;
    this.lastEdit = 0;
    return true;
  }

  toggleAB() {
    this.active = 1 - this.active;
    this.lastEdit = 0;
  }

  copyToOtherSlot() {
    const other = this.slots[1 - this.active];
    other.undo.push(other.now);
    other.redo.length = 0;
    other.now = copySetup(this.now);
  }

  load(index: number) {
    if (index < 0 || index >= this.names.length) return false;
    const name = this.names[index];
    const bands = index === 0 ? [] : readStore()[name];
    if (!Array.isArray(bands)) return false;
    this.remember();
    this.slot.now = { bands: copyBands(bands), name, saved: copyBands(bands) };
    this.lastEdit = 0;
    return true;
  }

  step(direction: number) {
    this.rescan();
    const count = this.names.length, current = this.names.indexOf(this.name());
    return this.load(current < 0 ? (direction > 0 ? 0 : count - 1) : (current + direction + count) % count);
  }

  saveAs(newName: string) {
    const name = legalPresetName(newName);
    const store = readStore();
    store[name] = copyBands(this.now.bands);
    if (!writeStore(store)) return false;
    this.now.name = name;
    this.now.saved = copyBands(this.now.bands);
    this.rescan();
    return true;
  }

  save() { return this.saveAs(this.name()); }

  remove(index: number) {
    if (index <= 0 || index >= this.names.length) return false;
    const removed = this.names[index];
    const store = readStore();
    delete store[removed];
    if (!writeStore(store)) return false;
    for (const slot of this.slots) if (slot.now.name === removed) slot.now.saved = null;
    this.rescan();
    return true;
  }

  info(): PresetBarInfo {
    const { now, undo, redo } = this.slot;
    const dirty = !now.saved || !sameBands(now.bands, now.saved);
    const current = this.names.indexOf(now.name);
    return { name: now.name, dirty, canUndo: undo.length > 0, canRedo: redo.length > 0, showingB: this.active === 1,
             presets: this.names, current, canSave: current > 0 && dirty };
  }
}
