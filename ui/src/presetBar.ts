// The controls along the top of the window: the current preset, centred, with
// its menu for saving and loading, and undo, redo, A/B and copy on the right. Same layout and drawing
// as Source/PresetBar.cpp. Rectangles are in unscaled units, as in bandPanel.ts.

import { clamp } from "./bands";
import { panelScale } from "./bandPanel";

export interface PresetBarInfo {
  name: string;
  dirty: boolean;
  canUndo: boolean;
  canRedo: boolean;
  showingB: boolean;
  canSave: boolean;
  presets: string[]; // the built-in default first, then the saved ones
  current: number; // index in presets; -1 = not in the list
}

export type PresetKind = "undo" | "redo" | "ab" | "copy" | "previous" | "name" | "next"
  | "menuBackground" | "save" | "saveAs" | "item" | "remove";
export interface PresetTarget { kind: PresetKind; index: number }

interface Rect { x: number; y: number; w: number; h: number }

export interface PresetBarLayout {
  undo: Rect; redo: Rect; ab: Rect; copy: Rect; previous: Rect; name: Rect; next: Rect;
  menu: Rect; save: Rect; saveAs: Rect;
  listTop: number; rowHeight: number; scale: number;
  presetCount: number; firstVisible: number; visibleRows: number;
}

const BAR_TOP = 12, BAR_HEIGHT = 28, BUTTON_HEIGHT = 24;
const UNDO_WIDTH = 24, AB_WIDTH = 36, COPY_WIDTH = 38, ARROW_WIDTH = 20, GROUP_GAP = 4;
const TOOLS_WIDTH = UNDO_WIDTH * 2 + GROUP_GAP + AB_WIDTH + COPY_WIDTH;
const TOOLS_RIGHT_INSET = 14, TOOLS_CLEARANCE = 8; // from the window's edge, and from the preset selector
const MIN_NAME_WIDTH = 90, MAX_NAME_WIDTH = 170;
const MENU_WIDTH = 240, MENU_GAP = 6, MENU_INSET = 4, MENU_DIVIDER = 9, MENU_MARGIN = 12, MAX_MENU_ROWS = 12;
const ROW_HEIGHT = 24;

const UI_FONT = '"Liberation Sans", system-ui, sans-serif';
const TEXT = "232,228,218", WARM_GREY = "138,134,124", GRAPHITE = "17,17,16";

const contains = (r: Rect, x: number, y: number) => x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;

export function makePresetBarLayout(windowWidth: number, windowHeight: number, presetCount: number, scroll: number): PresetBarLayout {
  const scale = panelScale(windowWidth, windowHeight);
  const width = windowWidth / scale, height = windowHeight / scale;
  // The preset selector is centred on the window, with the tools to its right.
  const nameWidth = clamp(width - 2 * (TOOLS_RIGHT_INSET + TOOLS_WIDTH + TOOLS_CLEARANCE + ARROW_WIDTH), MIN_NAME_WIDTH, MAX_NAME_WIDTH);
  const y = BAR_TOP + (BAR_HEIGHT - BUTTON_HEIGHT) / 2;
  let x = width / 2 - nameWidth / 2 - ARROW_WIDTH;
  const take = (w: number): Rect => {
    const r = { x, y, w, h: BUTTON_HEIGHT };
    x += w;
    return r;
  };
  const previous = take(ARROW_WIDTH), name = take(nameWidth), next = take(ARROW_WIDTH);
  x = width - TOOLS_RIGHT_INSET - TOOLS_WIDTH;
  const undo = take(UNDO_WIDTH), redo = take(UNDO_WIDTH);
  x += GROUP_GAP;
  const ab = take(AB_WIDTH), copy = take(COPY_WIDTH);

  const shownMenuWidth = Math.min(MENU_WIDTH, width - 16);
  const menuLeft = (width - shownMenuWidth) / 2;
  const menuTop = BAR_TOP + BAR_HEIGHT + MENU_GAP;
  const save = { x: menuLeft + MENU_INSET, y: menuTop + MENU_INSET, w: shownMenuWidth - MENU_INSET * 2, h: ROW_HEIGHT };
  const saveAs = { ...save, y: save.y + ROW_HEIGHT };
  const listTop = saveAs.y + ROW_HEIGHT + MENU_DIVIDER;
  const room = Math.max(1, Math.floor((height - MENU_MARGIN - MENU_INSET - listTop) / ROW_HEIGHT));
  const visibleRows = Math.min(presetCount, room, MAX_MENU_ROWS);
  const firstVisible = clamp(scroll, 0, presetCount - visibleRows);
  const menu = { x: menuLeft, y: menuTop, w: shownMenuWidth, h: listTop + ROW_HEIGHT * visibleRows + MENU_INSET - menuTop };

  return { undo, redo, ab, copy, previous, name, next,
           menu, save, saveAs, listTop, rowHeight: ROW_HEIGHT, scale, presetCount, firstVisible, visibleRows };
}

const rowRect = (l: PresetBarLayout, preset: number): Rect => (
  { x: l.save.x, y: l.listTop + l.rowHeight * (preset - l.firstVisible), w: l.save.w, h: l.rowHeight });

const removeRect = (l: PresetBarLayout, preset: number): Rect => {
  const r = rowRect(l, preset);
  return { ...r, x: r.x + r.w - 24, w: 24 };
};

export const presetMenuContains = (l: PresetBarLayout, cssX: number, cssY: number) => contains(l.menu, cssX / l.scale, cssY / l.scale);

export function hitPresetBar(l: PresetBarLayout, cssX: number, cssY: number, menuOpen: boolean): PresetTarget | null {
  const x = cssX / l.scale, y = cssY / l.scale;

  if (menuOpen && contains(l.menu, x, y)) {
    if (contains(l.save, x, y)) return { kind: "save", index: 0 };
    if (contains(l.saveAs, x, y)) return { kind: "saveAs", index: 0 };
    for (let i = l.firstVisible; i < l.firstVisible + l.visibleRows; ++i)
      if (contains(rowRect(l, i), x, y))
        return { kind: i > 0 && contains(removeRect(l, i), x, y) ? "remove" : "item", index: i };
    return { kind: "menuBackground", index: 0 };
  }

  const buttons: [Rect, PresetKind][] = [[l.undo, "undo"], [l.redo, "redo"], [l.ab, "ab"], [l.copy, "copy"],
                                         [l.previous, "previous"], [l.name, "name"], [l.next, "next"]];
  for (const [r, kind] of buttons) if (contains(r, x, y)) return { kind, index: 0 };
  return null;
}

export function drawPresetBar(ctx: CanvasRenderingContext2D, l: PresetBarLayout, dpr: number, info: PresetBarInfo,
                              hover: PresetTarget | null, menuAmount: number) {
  ctx.save();
  ctx.scale(dpr * l.scale, dpr * l.scale);
  ctx.globalCompositeOperation = "source-over";
  ctx.globalAlpha = 1;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.font = `12px ${UI_FONT}`;
  ctx.textBaseline = "middle";

  const fit = (s: string, width: number) => {
    if (ctx.measureText(s).width <= width) return s;
    let shown = s;
    while (shown.length > 0 && ctx.measureText(shown + "…").width > width) shown = shown.slice(0, -1);
    return shown + "…";
  };
  const text = (s: string, r: Rect, alpha: number) => {
    ctx.fillStyle = `rgba(${TEXT},${alpha})`;
    ctx.textAlign = "center";
    ctx.fillText(fit(s, r.w), r.x + r.w / 2, r.y + r.h / 2);
  };
  const leftText = (s: string, r: Rect, alpha: number, rightInset = 8) => {
    ctx.fillStyle = `rgba(${TEXT},${alpha})`;
    ctx.textAlign = "left";
    ctx.fillText(fit(s, r.w - 16 - rightInset), r.x + 16, r.y + r.h / 2);
  };
  const hoverBack = (r: Rect, alpha: number) => {
    ctx.fillStyle = `rgba(${TEXT},${alpha})`;
    ctx.beginPath();
    ctx.roundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4);
    ctx.fill();
  };
  const hovered = (kind: PresetKind) => hover?.kind === kind;
  const alphaOf = (kind: PresetKind, enabled = true) => (!enabled ? 0.22 : hovered(kind) ? 1 : 0.7);

  const undoArrow = (r: Rect, direction: number, alpha: number) => {
    const cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const X = (v: number) => cx + v * direction;
    ctx.strokeStyle = `rgba(${TEXT},${alpha})`;
    ctx.lineWidth = 1.4;
    ctx.beginPath();
    ctx.moveTo(X(-1.5), cy - 5);
    ctx.lineTo(X(-5), cy - 1.8);
    ctx.lineTo(X(-1.5), cy + 1.4);
    ctx.moveTo(X(-5), cy - 1.8);
    ctx.lineTo(X(1), cy - 1.8);
    ctx.bezierCurveTo(X(6.5), cy - 1.8, X(6.5), cy + 5, X(1), cy + 5);
    ctx.lineTo(X(-2.5), cy + 5);
    ctx.stroke();
  };
  const chevron = (r: Rect, direction: number, alpha: number) => {
    const cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    ctx.strokeStyle = `rgba(${TEXT},${alpha})`;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(cx - 2 * direction, cy - 4);
    ctx.lineTo(cx + 2 * direction, cy);
    ctx.lineTo(cx - 2 * direction, cy + 4);
    ctx.stroke();
  };

  const buttons: [Rect, PresetKind, boolean][] = [[l.undo, "undo", info.canUndo], [l.redo, "redo", info.canRedo], [l.ab, "ab", true], [l.copy, "copy", true],
                                                  [l.previous, "previous", true], [l.name, "name", true], [l.next, "next", true]];
  for (const [r, kind, enabled] of buttons) if (enabled && hovered(kind)) hoverBack(r, 0.08);

  undoArrow(l.undo, 1, alphaOf("undo", info.canUndo));
  undoArrow(l.redo, -1, alphaOf("redo", info.canRedo));

  const letter = (s: string, offset: number, lit: boolean) => {
    text(s, { x: l.ab.x + l.ab.w / 2 + offset - 8, y: l.ab.y, w: 16, h: l.ab.h }, lit ? (hovered("ab") ? 1 : 0.9) : 0.3);
  };
  letter("A", -9, !info.showingB);
  letter("/", 0, false);
  letter("B", 9, info.showingB);

  text("Copy", l.copy, alphaOf("copy"));
  chevron(l.previous, -1, alphaOf("previous"));
  chevron(l.next, 1, alphaOf("next"));
  text(info.dirty ? `${info.name} *` : info.name, { ...l.name, x: l.name.x + 4, w: l.name.w - 8 }, hovered("name") ? 1 : 0.9);

  if (menuAmount >= 0.01) {
    ctx.fillStyle = `rgba(${GRAPHITE},${0.94 * menuAmount})`;
    ctx.beginPath();
    ctx.roundRect(l.menu.x, l.menu.y, l.menu.w, l.menu.h, 8);
    ctx.fill();

    if (hovered("save") && info.canSave) hoverBack(l.save, 0.08 * menuAmount);
    if (hovered("saveAs")) hoverBack(l.saveAs, 0.08 * menuAmount);
    leftText("Save", l.save, (info.canSave ? 0.9 : 0.3) * menuAmount);
    leftText("Save As…", l.saveAs, 0.9 * menuAmount);

    ctx.fillStyle = `rgba(${WARM_GREY},${0.3 * menuAmount})`;
    ctx.fillRect(l.save.x + 4, l.listTop - MENU_DIVIDER / 2, l.save.w - 8, 1);

    for (let i = l.firstVisible; i < l.firstVisible + l.visibleRows; ++i) {
      const r = rowRect(l, i);
      const overRow = (hover?.kind === "item" || hover?.kind === "remove") && hover.index === i;
      if (overRow) hoverBack(r, 0.08 * menuAmount);

      if (i === info.current) {
        ctx.fillStyle = `rgba(${TEXT},${0.9 * menuAmount})`;
        ctx.beginPath();
        ctx.arc(r.x + 8, r.y + r.h / 2, 2, 0, Math.PI * 2);
        ctx.fill();
      }

      leftText(info.presets[i], r, (i === info.current ? 1 : 0.72) * menuAmount, 26);

      if (overRow && i > 0) {
        const b = removeRect(l, i), cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        ctx.strokeStyle = `rgba(${TEXT},${(hover?.kind === "remove" ? 1 : 0.45) * menuAmount})`;
        ctx.lineWidth = 1.3;
        ctx.beginPath();
        ctx.moveTo(cx - 3.5, cy - 3.5);
        ctx.lineTo(cx + 3.5, cy + 3.5);
        ctx.moveTo(cx + 3.5, cy - 3.5);
        ctx.lineTo(cx - 3.5, cy + 3.5);
        ctx.stroke();
      }
    }

    if (l.visibleRows < l.presetCount) {
      const track = l.rowHeight * l.visibleRows;
      const thumb = track * l.visibleRows / l.presetCount;
      const top = l.listTop + (track - thumb) * l.firstVisible / (l.presetCount - l.visibleRows);
      ctx.fillStyle = `rgba(${TEXT},${0.3 * menuAmount})`;
      ctx.beginPath();
      ctx.roundRect(l.menu.x + l.menu.w - 4, top, 2, thumb, 1);
      ctx.fill();
    }
  }

  ctx.restore();
}
