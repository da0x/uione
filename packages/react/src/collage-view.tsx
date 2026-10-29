// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A collage: a picture made of layers, as big as its view says, drawn scaled to fit
// with room around it, or at its own size on black filling the screen (decision
// 0021). Pictures are cropped, turned and mirrored as each layer says, what's around
// each drawn as it says, its edges drawn out or its edge color, or, harmonized, what's
// under them all filled from theirs together, each melting into it, and words drawn
// over a soft shadow. A click picks a
// layer, framed with its corners and the buttons over it; a picked layer is moved, or
// resized by a corner with its shape kept unless Shift is held, snapped unless Alt
// is. A picture is cropped in place, its whole picture faint around what's kept, and
// words are written where they are, with a caret, and resized by their size. Where
// everything goes is collage.ts, the model the desktop's is held to as well. It's drawn
// on a canvas, the same in every component set.

import { useContext, useEffect, useLayoutEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import type { ReactNode } from "react";
import { colorShifter, cornerAt, pathIn, cropEdgesAt, cropped, drawn, drawnLine, freehand, halo, penPath, haloInside, haloReach, harmonicFill, matched, matchingCast, matchingStrength, moved, resized, sitting, pathBounds, pathFractions, around, boardOf, carried, polygonPoints, zoomed, pathHandleMoved, pathHandles, pathMoved, pathNodes, pathReaches, snapLines, statsOf, svgDocument, wholeOf } from "./collage.js";
import type { Box, ColorShift, ColorStats, Guided, Lines, PenNode, Point, SavedLayer, ViewAt } from "./collage.js";
import { useFiles, useRunner } from "./data.js";
import { pictureSize, svgIn, svgShapes, svgSize } from "./vectors.js";
import type { ViewState } from "./data.js";
import { useUI } from "./ui.js";
import { useSaying } from "./said.js";
import { useSite } from "./app.js";
import { OverKind, OverLayer, OverRun, choosePictures, lastGiven, styleThese, hintOf, setChosen, setPicked, takeInTurn, typing, useKeys, useOff, usePickedId, useTool } from "./workspace.js";

type Row = Record<string, unknown>;

const handle = 10; // a corner's handle, in the window
const num = (v: unknown, otherwise = 0) => (typeof v === "number" && Number.isFinite(v) ? v : typeof v === "string" && v.trim() !== "" && Number.isFinite(Number(v)) ? Number(v) : otherwise);
const str = (v: unknown) => (typeof v === "string" ? v : "");
const boxOf = (l: Row): Box => ({ x: num(l["x"]), y: num(l["y"]), w: num(l["w"]), h: num(l["h"]) });
const cropOf = (l: Row): Box => ({ x: num(l["crop_x"], 0), y: num(l["crop_y"], 0), w: num(l["crop_w"], 1), h: num(l["crop_h"], 1) });
// What a layer is: a picture, when it has one; words, when it has those; or else a shape,
// as its parts say (decision 0025). The same as the desktop's.
const isPicture = (l: Row) => str(l["picture"]) !== "";
const isPath = (l: Row) => isShape(l) && str(l["shape"]) === "path";
// A path's outline where it is, in the collage's own units, its box `b`.
const outlineOf = (l: Row, b: Box) => pathIn(str(l["points"]), b.x, b.y, b.w, b.h);
// A path reshaped, as its points and its box: its box a pixel wide where it's straight
// across or down, as a line drawn is. The same as the desktop's.
function reshapedAs(outline: string): { points: string; box: Box } {
  const box = pathBounds(outline);
  if (box.w < 1) (box.x += box.w / 2 - 0.5), (box.w = 1);
  if (box.h < 1) (box.y += box.h / 2 - 0.5), (box.h = 1);
  return { points: pathFractions(outline, box), box };
}
// Whether a layer is hidden: not drawn, picked or saved.
const hiddenLayer = (l: Row) => l["hidden"] !== undefined && l["hidden"] !== null && num(l["hidden"]) !== 0;
const isWords = (l: Row) => !isPicture(l) && str(l["words"]) !== "";
const isShape = (l: Row) => !isPicture(l) && !isWords(l);

// How much of a layer shows: all of it, unless its opacity says less.
const opacityOf = (l: Row) => (l["opacity"] === undefined || l["opacity"] === null ? 1 : Math.min(1, Math.max(0, num(l["opacity"]))));

// A shape layer drawn at `b` on the canvas, its box in the collage's pixels `kept`: its
// path, rectangle or ellipse filled and stroked as it says, a color #rrggbb or none, its
// stroke as wide as it says in the collage's pixels, half outside its outline.
function paintShape(c: CanvasRenderingContext2D, l: Row, b: Box, kept: Box) {
  const per = kept.w > 0 ? b.w / kept.w : kept.h > 0 ? b.h / kept.h : 0;
  if (per <= 0) return;
  const color = (written: unknown) => (typeof written === "string" && /^#[0-9a-fA-F]{6}$/.test(written) ? written : undefined);
  const fill = color(l["fill"]), stroke = color(l["stroke"]);
  const width = num(l["stroke_width"]) * per;
  const kind = str(l["shape"]) || "rectangle";
  const outline = new Path2D();
  if (kind === "ellipse") outline.ellipse(b.x + b.w / 2, b.y + b.h / 2, b.w / 2, b.h / 2, 0, 0, 2 * Math.PI);
  else if (kind === "path") outline.addPath(new Path2D(pathIn(str(l["points"]), b.x, b.y, b.w, b.h)));
  else if (kind === "polygon" || kind === "star") outline.addPath(new Path2D(pathIn(polygonPoints(num(l["sides"], 5), num(l["spoke"], 0.5), kind === "star"), b.x, b.y, b.w, b.h)));
  else outline.roundRect(b.x, b.y, b.w, b.h, Math.min(Math.max(num(l["corners"]), 0), 0.5) * Math.min(b.w, b.h));
  c.save();
  c.globalAlpha = opacityOf(l);
  if (fill) {
    c.fillStyle = fill;
    c.fill(outline);
  }
  if (stroke && width > 0) {
    c.strokeStyle = stroke;
    c.lineWidth = width;
    c.stroke(outline);
  }
  c.restore();
}
const colorOf = (written: string, otherwise: string) => (/^#[0-9a-fA-F]{6}$/.test(written) ? written : otherwise);
const weightOf = (l: Row) => num(l["weight"], 700);
// Words' face, as a canvas's font says it: italic when they are, and their weight.
const faceOf = (l: Row) => `${num(l["italic"]) !== 0 ? "italic " : ""}${weightOf(l)}`;
const inside = (b: Box, p: Point) => p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h;
// A collage's view as its parts: its own values and each of its layers, with the
// fields its parts are kept in read as those parts.
function partsOf(data: Row, list: string, fields: Record<string, string>): Row {
  const asParts = (from: Row) => {
    const out: Row = { ...from };
    for (const part of Object.keys(fields)) delete out[part];
    for (const [part, field] of Object.entries(fields)) if (field in from) out[part] = from[field];
    return out;
  };
  const out = asParts(data);
  out[list] = ((data[list] as Row[] | undefined) ?? []).map(asParts);
  return out;
}

// And what it sends, its parts named as the fields they're kept in.
function fieldsOf(sent: Row, fields: Record<string, string>): Row {
  const out: Row = { ...sent };
  for (const part of Object.keys(fields)) delete out[part];
  for (const [part, field] of Object.entries(fields)) if (part in sent) out[field] = sent[part];
  return out;
}

// Words are in the theme's text font, as the desktop draws them, which an app can
// bring with it. A canvas only draws a web font once it's loaded, so it's asked for,
// and what's drawn is drawn again when it arrives.
function wordsFamily(): string {
  if (typeof document === "undefined" || typeof getComputedStyle !== "function") return "sans-serif";
  return getComputedStyle(document.documentElement).getPropertyValue("--font-sans").trim() || "sans-serif";
}

function wordsLoaded(): Promise<unknown> {
  const fonts = typeof document !== "undefined" ? document.fonts : undefined;
  if (!fonts?.load) return Promise.resolve();
  const family = wordsFamily();
  return Promise.all([fonts.load(`400 16px ${family}`), fonts.load(`700 16px ${family}`)]).catch(() => {});
}

// How big words are, in the collage's pixels: as wide as their widest line, each line
// a little more than their size.
let measurer: CanvasRenderingContext2D | null | undefined;
function measured(words: string, size: number, face: string): { w: number; h: number } {
  measurer ??= typeof document !== "undefined" ? document.createElement("canvas").getContext("2d") : null;
  const lines = (words || " ").split("\n");
  if (!measurer) return { w: Math.max(1, size * 0.6 * Math.max(...lines.map((l) => l.length))), h: Math.max(1, size * 1.2 * lines.length) };
  measurer.font = `${face} ${size}px ${wordsFamily()}`;
  return { w: Math.max(1, ...lines.map((l) => measurer!.measureText(l).width)), h: Math.max(1, size * 1.2 * lines.length) };
}

// Where a clear collage has nothing, a checkerboard, as drawing programs show what's
// clear: white and gray, or in the dark, the theme's own blue-grays; squares 8
// pixels on a side. The same as the desktop's.
function checkers(c: CanvasRenderingContext2D): CanvasPattern | string {
  const style = getComputedStyle(c.canvas);
  const light = style.getPropertyValue("--checker-light").trim() || "#fff";
  const dark = style.getPropertyValue("--checker-dark").trim() || "#ccc";
  const tile = document.createElement("canvas");
  tile.width = tile.height = 16;
  const t = tile.getContext("2d");
  if (!t) return light;
  t.fillStyle = light;
  t.fillRect(0, 0, 16, 16);
  t.fillStyle = dark;
  t.fillRect(8, 0, 8, 8);
  t.fillRect(0, 8, 8, 8);
  return c.createPattern(tile, "repeat") ?? light;
}

// Each picture read once, by what names it, and what's drawn from it: turned and
// mirrored as a layer says, and its edge color. An SVG is "vector", never read as
// pixels: it's drawn from its vectors, at the size it's shown.
const pictures = new Map<string, ImageBitmap | "reading" | "failed" | "vector">();
const vectors = new Map<string, HTMLImageElement>();
const orientedOnes = new Map<string, HTMLCanvasElement>();
const edges = new Map<string, string>();
const readListeners = new Set<() => void>();

function pictureOf(path: string, address: ((path: string) => Promise<string>) | undefined): ImageBitmap | undefined {
  const kept = pictures.get(path);
  if (kept !== undefined && kept !== "reading" && kept !== "failed" && kept !== "vector") return kept;
  if (kept || !address || typeof createImageBitmap !== "function") return undefined;
  pictures.set(path, "reading");
  void address(path)
    .then((url) => fetch(url))
    .then((r) => r.blob())
    .then(async (blob) => {
      const svg = await svgIn(blob);
      if (svg === undefined) return void pictures.set(path, await createImageBitmap(blob));
      vectors.set(path, await vectorImage(svg));
      pictures.set(path, "vector");
    })
    .catch(() => pictures.set(path, "failed"))
    .finally(() => readListeners.forEach((changed) => changed()));
  return undefined;
}

// An SVG as an image a canvas draws from its vectors at whatever size it's drawn, given
// its own size when it says only a viewBox, as a browser needs to draw it.
function vectorImage(svg: string): Promise<HTMLImageElement> {
  const doc = new DOMParser().parseFromString(svg, "image/svg+xml");
  const size = svgSize(svg);
  if (size) {
    doc.documentElement.setAttribute("width", String(size.w));
    doc.documentElement.setAttribute("height", String(size.h));
  }
  const url = URL.createObjectURL(new Blob([new XMLSerializer().serializeToString(doc)], { type: "image/svg+xml" }));
  return new Promise((done, failed) => {
    const image = new Image();
    image.onload = () => done(image);
    image.onerror = failed;
    image.src = url;
  });
}

function orientedOf(path: string, p: ImageBitmap, turns: number, mirrored: boolean): CanvasImageSource & { width: number; height: number } {
  if (turns === 0 && !mirrored) return p;
  const key = `${path} ${turns} ${mirrored}`;
  let made = orientedOnes.get(key);
  if (!made) {
    made = document.createElement("canvas");
    const sideways = turns % 2 === 1;
    made.width = sideways ? p.height : p.width;
    made.height = sideways ? p.width : p.height;
    const c = made.getContext("2d")!;
    c.translate(made.width / 2, made.height / 2);
    c.rotate((turns * Math.PI) / 2);
    if (mirrored) c.scale(-1, 1);
    c.drawImage(p, -p.width / 2, -p.height / 2);
    orientedOnes.set(key, made);
  }
  return made;
}

// A picture as it's shown: turned and mirrored, then only the part its crop keeps, so
// it's drawn, and fills what's around it, by that part alone.
const croppedOnes = new Map<string, HTMLCanvasElement>();
function keptOf(l: Row, shown: CanvasImageSource & { width: number; height: number }): CanvasImageSource & { width: number; height: number } {
  const cr = cropOf(l);
  if (cr.x <= 0 && cr.y <= 0 && cr.w >= 1 && cr.h >= 1) return shown;
  const named = edgeKeyOf(l);
  let made = croppedOnes.get(named);
  if (!made) {
    made = document.createElement("canvas");
    made.width = Math.max(1, Math.round(cr.w * shown.width));
    made.height = Math.max(1, Math.round(cr.h * shown.height));
    made.getContext("2d")?.drawImage(shown, cr.x * shown.width, cr.y * shown.height, cr.w * shown.width, cr.h * shown.height, 0, 0, made.width, made.height);
    croppedOnes.set(named, made);
  }
  return made;
}

// What names a picture's edges: its picture as turned, and its crop.
const edgeKeyOf = (l: Row) => {
  const cr = cropOf(l);
  return `${str(l["picture"])} ${num(l["turns"])} ${num(l["mirrored"])} ${cr.x} ${cr.y} ${cr.w} ${cr.h}`;
};

// What a picture is drawn from: what its crop keeps, its colors moved as the collage's
// matching says, and what names that, for what's worked out from it.
type Picture = CanvasImageSource & { width: number; height: number };
interface Look {
  picture: Picture;
  key: string;
}

// A picture as it's read, turned and mirrored, or nothing while it's being read.
function shownOf(l: Row, address: ((path: string) => Promise<string>) | undefined): Picture | undefined {
  const path = str(l["picture"]);
  const p = pictureOf(path, address);
  return p ? orientedOf(path, p, ((num(l["turns"]) % 4) + 4) % 4, num(l["mirrored"]) !== 0) : undefined;
}

// A picture's colors, measured once from a small copy of it.
const measuredColors = new Map<string, ColorStats>();
function statsKept(l: Row, kept: Picture, weight: number): ColorStats | undefined {
  const key = edgeKeyOf(l);
  let known = measuredColors.get(key);
  if (!known) {
    // Big enough that a busy picture's detail, and so its contrast, isn't averaged away.
    const side = 128;
    const small = document.createElement("canvas");
    small.width = side;
    small.height = side;
    const g = small.getContext("2d", { willReadFrequently: true });
    if (!g) return undefined;
    g.drawImage(kept, 0, 0, side, side);
    const read = g.getImageData(0, 0, side, side).data, colors = new Float64Array(side * side * 3);
    for (let i = 0; i < side * side; i++) for (let k = 0; k < 3; k++) colors[i * 3 + k] = read[i * 4 + k]!;
    known = statsOf(colors, 1);
    if (measuredColors.size > 256) measuredColors.clear();
    measuredColors.set(key, known);
  }
  return { ...known, weight };
}

// What each picture's colors are moved by while the collage matches them, so they look
// like one picture: each moved most of the way toward all of theirs, weighed by how much
// of the collage it covers where it's kept, so the larger lead.
function shiftsOf(layers: Row[], matching: boolean, address: ((path: string) => Promise<string>) | undefined): Map<string, ColorShift> {
  const out = new Map<string, ColorShift>();
  if (!matching) return out;
  const ids: string[] = [], stats: ColorStats[] = [];
  for (const l of layers) {
    if (!isPicture(l)) continue;
    const shown = shownOf(l, address), b = boxOf(l);
    if (!shown || b.w <= 0 || b.h <= 0) continue;
    const colors = statsKept(l, keptOf(l, shown), b.w * b.h);
    if (!colors) continue;
    ids.push(str(l["id"]));
    stats.push(colors);
  }
  matched(stats, matchingStrength, matchingCast).forEach((shift, i) => out.set(ids[i]!, shift));
  return out;
}

// On the page, a picture whose colors are moved is moved no larger than this, as it's
// never shown larger; saved, it's moved whole. The same as the desktop's.
const shownLargest = 2048;

// A picture as it's drawn: what its crop keeps, no more than `largest` on its longer
// side when that says, its colors moved as its shift says, worked out once.
const tonedOnes = new Map<string, HTMLCanvasElement>();
function lookOf(l: Row, shown: Picture, shifts: Map<string, ColorShift>, largest = 0): Look {
  const kept = keptOf(l, shown), named = edgeKeyOf(l);
  const shift = shifts.get(str(l["id"]));
  if (!shift) return { picture: kept, key: named };
  const s = largest > 0 ? Math.min(1, largest / Math.max(kept.width, kept.height)) : 1;
  const key = `${named} ${s < 1 ? `at most ${largest} ` : ""}${shift.gain.map((g) => g.toFixed(3)).join(" ")} ${shift.offset.map((o) => o.toFixed(1)).join(" ")} ${(shift.to ?? []).map((t) => t.toFixed(1)).join(" ")}`;
  let made = tonedOnes.get(key);
  if (!made) {
    made = document.createElement("canvas");
    made.width = Math.max(1, Math.round(kept.width * s));
    made.height = Math.max(1, Math.round(kept.height * s));
    const g = made.getContext("2d", { willReadFrequently: true });
    if (!g) return { picture: kept, key: named };
    g.drawImage(kept, 0, 0, made.width, made.height);
    const pixels = g.getImageData(0, 0, made.width, made.height);
    colorShifter(shift)(pixels.data);
    g.putImageData(pixels, 0, 0);
    if (tonedOnes.size > 32) tonedOnes.clear();
    tonedOnes.set(key, made);
  }
  return { picture: made, key };
}

function edgeColorOf(path: string, p: CanvasImageSource & { width: number; height: number }): string {
  const known = edges.get(path);
  if (known) return known;
  const c = document.createElement("canvas");
  c.width = p.width;
  c.height = p.height;
  const g = c.getContext("2d", { willReadFrequently: true })!;
  g.drawImage(p, 0, 0);
  const sum = [0, 0, 0];
  let count = 0;
  const add = (data: Uint8ClampedArray) => {
    for (let i = 0; i < data.length; i += 4) (sum[0]! += data[i]!), (sum[1]! += data[i + 1]!), (sum[2]! += data[i + 2]!), count++;
  };
  add(g.getImageData(0, 0, p.width, 1).data);
  add(g.getImageData(0, p.height - 1, p.width, 1).data);
  add(g.getImageData(0, 0, 1, p.height).data);
  add(g.getImageData(p.width - 1, 0, 1, p.height).data);
  const color = `rgb(${sum.map((s) => Math.round(s / Math.max(1, count))).join(", ")})`;
  edges.set(path, color);
  return color;
}

// What a picture leaves around it on the board: its outermost pixels drawn out to the
// edges, or its edge color.
function paintAround(c: CanvasRenderingContext2D, path: string, shown: CanvasImageSource & { width: number; height: number }, board: Box, b: Box, around: string) {
  const covers = b.x <= board.x && b.y <= board.y && b.x + b.w >= board.x + board.w && b.y + b.h >= board.y + board.h;
  if (covers) return;
  c.fillStyle = edgeColorOf(path, shown);
  c.fillRect(board.x, board.y, board.w, board.h);
  if (around === "color") return;
  const sw = shown.width, sh = shown.height;
  const strip = (sx: number, sy: number, sW: number, sH: number, dx: number, dy: number, dW: number, dH: number) => {
    if (dW > 0 && dH > 0) c.drawImage(shown, sx, sy, sW, sH, dx, dy, dW, dH);
  };
  const left = b.x - board.x, right = board.x + board.w - (b.x + b.w), top = b.y - board.y, bottom = board.y + board.h - (b.y + b.h);
  strip(0, 0, 1, sh, board.x, b.y, left + 1, b.h);
  strip(sw - 1, 0, 1, sh, b.x + b.w - 1, b.y, right + 1, b.h);
  strip(0, 0, sw, 1, b.x, board.y, b.w, top + 1);
  strip(0, sh - 1, sw, 1, b.x, b.y + b.h - 1, b.w, bottom + 1);
  strip(0, 0, 1, 1, board.x, board.y, left + 1, top + 1);
  strip(sw - 1, 0, 1, 1, b.x + b.w - 1, board.y, right + 1, top + 1);
  strip(0, sh - 1, 1, 1, board.x, b.y + b.h - 1, left + 1, bottom + 1);
  strip(sw - 1, sh - 1, 1, 1, b.x + b.w - 1, b.y + b.h - 1, right + 1, bottom + 1);
}

// Words drawn at x, y, `size` pixels tall, over a soft shadow.
function paintWords(c: CanvasRenderingContext2D, l: Row, said: string, size: number, x: number, y: number) {
  c.font = `${faceOf(l)} ${size}px ${wordsFamily()}`;
  c.textBaseline = "top";
  const off = Math.max(1, size * 0.04);
  said.split("\n").forEach((line, i) => {
    c.fillStyle = "rgba(0, 0, 0, 0.45)";
    c.fillText(line, x + off / 2, y + i * size * 1.2 + off);
    c.fillStyle = colorOf(str(l["color"]), "#ffffff");
    c.fillText(line, x, y + i * size * 1.2);
  });
}

// Whether a layer is harmonized: blend, as kept before the two were one, is too.
const harmonizes = (l: Row) => str(l["around"]) === "harmonize" || str(l["around"]) === "blend";

// With around harmonize, what's under the collage's pictures filled from all the
// harmonized ones together (harmonic fill): each drawn small where it is, the one on top
// where they overlap, and every gap settled between the colors around it, so past the
// outermost each one's colors carry on to the edge, and between two they cross from one
// to the other. Drawn as big as the collage, smooth as it is, under all of them; worked
// out again only when one of them, or where it is, changes. The same as the desktop's.
const fills = new Map<string, HTMLCanvasElement>();
function harmonizedOf(under: { l: Row; seen: Look }[], W: number, H: number): HTMLCanvasElement | undefined {
  if (under.length === 0 || W <= 0 || H <= 0) return undefined;
  const key = `${W} ${H} ` + under.map(({ l, seen }) => { const b = boxOf(l); return `${seen.key} ${b.x} ${b.y} ${b.w} ${b.h}`; }).join(" ");
  const known = fills.get(key);
  if (known) return known;
  const gw = 320, gh = Math.max(1, Math.round((320 * H) / W)), s = gw / W;
  const small = document.createElement("canvas");
  small.width = gw;
  small.height = gh;
  const c = small.getContext("2d", { willReadFrequently: true });
  if (!c) return undefined;
  for (const { l, seen } of under) {
    const b = boxOf(l);
    c.drawImage(seen.picture, b.x * s, b.y * s, b.w * s, b.h * s);
  }
  const pixels = c.getImageData(0, 0, gw, gh);
  const cells = gw * gh, colors = new Float64Array(cells * 3), covered: boolean[] = new Array(cells);
  for (let i = 0; i < cells; i++) {
    // Only a cell a picture covers whole is its color; one an edge crosses is part
    // picture, part nothing, and would leave a seam of the two.
    covered[i] = pixels.data[i * 4 + 3]! >= 250;
    for (let k = 0; k < 3; k++) colors[i * 3 + k] = pixels.data[i * 4 + k]!;
  }
  const filled = harmonicFill(gw, gh, colors, covered);
  for (let i = 0; i < cells; i++) {
    for (let k = 0; k < 3; k++) pixels.data[i * 4 + k] = Math.max(0, Math.min(255, Math.round(filled[i * 3 + k]!)));
    pixels.data[i * 4 + 3] = 255;
  }
  c.putImageData(pixels, 0, 0);
  if (fills.size > 8) fills.clear();
  fills.set(key, small);
  return small;
}

// A harmonized picture's halo: worked out small, its own colors spread past its edges
// and fading into what's under it (halo), as far as haloReach of its shorter side, and
// drawn over it, as much bigger as `growX` and `growY` say, each a share of its width
// and height.
interface Halo {
  picture: HTMLCanvasElement;
  growX: number;
  growY: number;
}
const halos = new Map<string, Halo>();
function haloOf(l: Row, seen: Look): Halo | undefined {
  const b = boxOf(l), kept = seen.picture;
  if (b.w <= 0 || b.h <= 0) return undefined;
  const sw = b.w >= b.h ? 64 : Math.max(1, Math.round((64 * b.w) / b.h));
  const sh = b.w >= b.h ? Math.max(1, Math.round((64 * b.h) / b.w)) : 64;
  const reach = Math.max(4, Math.round(haloReach * Math.min(sw, sh)));
  const key = `${seen.key} ${sw} ${sh}`;
  const known = halos.get(key);
  if (known) return known;
  const small = document.createElement("canvas");
  small.width = sw;
  small.height = sh;
  const g = small.getContext("2d", { willReadFrequently: true });
  if (!g) return undefined;
  g.drawImage(kept, 0, 0, sw, sh);
  const read = g.getImageData(0, 0, sw, sh).data, colors = new Float64Array(sw * sh * 3);
  for (let i = 0; i < sw * sh; i++) for (let k = 0; k < 3; k++) colors[i * 3 + k] = read[i * 4 + k]!;
  const spread = halo(sw, sh, colors, reach, reach * haloInside);
  const made = document.createElement("canvas");
  made.width = sw + 2 * reach;
  made.height = sh + 2 * reach;
  const m = made.getContext("2d");
  if (!m) return undefined;
  const pixels = m.createImageData(made.width, made.height);
  for (let i = 0; i < spread.length; i++) pixels.data[i] = Math.max(0, Math.min(255, Math.round(spread[i]!)));
  m.putImageData(pixels, 0, 0);
  if (halos.size > 64) halos.clear();
  const drawn = { picture: made, growX: reach / sw, growY: reach / sh };
  halos.set(key, drawn);
  return drawn;
}

// A picture layer drawn at `b`, the collage's `board`: what's around it first, over
// what's under it, as its around says, then it, then its halo when it's harmonized.
// Without `effects`, as while something's being moved or cropped, it alone.
function paintPicture(c: CanvasRenderingContext2D, l: Row, seen: Look, board: Box, b: Box, effects = true) {
  const around = str(l["around"]), kept = seen.picture;
  c.save();
  c.globalAlpha = opacityOf(l);
  if (effects && (around === "edges" || around === "color")) paintAround(c, seen.key, kept, board, b, around);
  // Its corners trimmed round as far as its corners say, a share of its shorter side.
  const radius = Math.min(num(l["corners"]) * Math.min(b.w, b.h), Math.min(b.w, b.h) / 2);
  if (radius > 0) {
    c.save();
    c.beginPath();
    c.roundRect(b.x, b.y, b.w, b.h, radius);
    c.clip();
    c.drawImage(kept, b.x, b.y, b.w, b.h);
    c.restore();
  } else c.drawImage(kept, b.x, b.y, b.w, b.h);
  // So its edge melts into what's filled from it, as seamless.
  const h = effects && harmonizes(l) ? haloOf(l, seen) : undefined;
  if (h) {
    const gx = h.growX * b.w, gy = h.growY * b.h;
    c.drawImage(h.picture, b.x - gx, b.y - gy, b.w + 2 * gx, b.h + 2 * gy);
  }
  c.restore();
}

// The collage drawn on a canvas of its own, `scale` times its own size, as it's kept:
// all of it, or only one layer, as big as that layer, cropped as it is. Pictures not
// yet read are read first.
async function drawnWhole(layers: Row[], W: number, H: number, scale: number, address: ((path: string) => Promise<string>) | undefined, matching: boolean, only?: string, clear = false): Promise<HTMLCanvasElement> {
  const read = async (path: string) => {
    if (!pictures.has(path) || pictures.get(path) === "reading") {
      pictureOf(path, address);
      await new Promise<void>((done) => {
        const check = () => (pictures.get(path) !== "reading" ? (readListeners.delete(check), done()) : undefined);
        readListeners.add(check);
        check();
      });
    }
    const p = pictures.get(path);
    return p !== "reading" && p !== "failed" && p !== "vector" ? p : undefined;
  };
  const one = only ? layers.find((l) => l["id"] === only) : undefined;
  const area = one ? boxOf(one) : { x: 0, y: 0, w: W, h: H };
  const made = document.createElement("canvas");
  made.width = Math.max(1, Math.round(area.w * scale));
  made.height = Math.max(1, Math.round(area.h * scale));
  const c = made.getContext("2d")!;
  const at = (b: Box): Box => ({ x: (b.x - area.x) * scale, y: (b.y - area.y) * scale, w: b.w * scale, h: b.h * scale });
  const board = at({ x: 0, y: 0, w: W, h: H });
  if (!only && !clear) {
    c.fillStyle = "#fff";
    c.fillRect(board.x, board.y, board.w, board.h);
  }
  // Every picture read first, so matching measures them all.
  if (matching) for (const l of layers) if (isPicture(l)) await read(str(l["picture"]));
  const shifts = shiftsOf(layers, matching, address);
  const looks = new Map<Row, Look>();
  for (const l of layers) {
    if (!isPicture(l) || (only && l["id"] !== only)) continue;
    const path = str(l["picture"]);
    const p = await read(path);
    if (p) looks.set(l, lookOf(l, orientedOf(path, p, ((num(l["turns"]) % 4) + 4) % 4, num(l["mirrored"]) !== 0), shifts));
  }
  if (!only) {
    const fill = harmonizedOf(layers.filter((l) => harmonizes(l) && looks.has(l)).map((l) => ({ l, seen: looks.get(l)! })), W, H);
    if (fill) c.drawImage(fill, board.x, board.y, board.w, board.h);
  }
  for (const l of layers) {
    if (only && l["id"] !== only) continue;
    if (isWords(l)) {
      const b = at(boxOf(l));
      paintWords(c, l, str(l["words"]), num(l["size"]) * scale, b.x, b.y);
      continue;
    }
    if (isShape(l)) {
      const b = boxOf(l);
      paintShape(c, l, only ? { x: 0, y: 0, w: made.width, h: made.height } : at(b), b);
      continue;
    }
    const v = vectors.get(str(l["picture"]));
    if (v) {
      const b = only ? { x: 0, y: 0, w: made.width, h: made.height } : at(boxOf(l));
      c.save();
      c.globalAlpha = opacityOf(l);
      c.drawImage(v, b.x, b.y, b.w, b.h);
      c.restore();
      continue;
    }
    const seen = looks.get(l);
    if (!seen) continue;
    // A layer copied alone is drawn without what it would leave around it.
    if (only) c.drawImage(seen.picture, 0, 0, made.width, made.height);
    else paintPicture(c, l, seen, board, at(boxOf(l)));
  }
  return made;
}

// The drawing as a saved SVG, from its vectors: all of it, or only one layer, as big as
// that layer. Pictures are read first, and held in it as data: addresses. The same as
// the desktop's.
async function svgOf(layers: Row[], W: number, H: number, address: ((path: string) => Promise<string>) | undefined, only?: string[]): Promise<string> {
  const ones = only ? layers.filter((l) => only.includes(str(l["id"]))) : [];
  const area = ones.length > 0 ? around(ones.map(boxOf)) : { x: 0, y: 0, w: W, h: H };
  const saved: SavedLayer[] = [];
  const groupOf = groupsOf(layers);
  // A group's all it holds.
  for (const l of layers) {
    if (holdsOthers(l)) continue;
    const id = str(l["id"]);
    if (only && !only.some((o) => o === id || inGroup(groupOf, id, o))) continue;
    const b = boxOf(l);
    const at = { x: b.x - area.x, y: b.y - area.y, w: b.w, h: b.h };
    const opacity = opacityOf(l);
    if (isWords(l)) {
      saved.push({ kind: "words", at, opacity, words: str(l["words"]), color: str(l["color"]), family: wordsFamily(), size: num(l["size"]), weight: weightOf(l), italic: num(l["italic"]) !== 0 });
    } else if (isShape(l)) {
      const drawn = { kind: str(l["shape"]) || "rectangle", points: str(l["points"]), fill: str(l["fill"]), stroke: str(l["stroke"]), strokeWidth: str(l["stroke"]) ? num(l["stroke_width"]) : 0, corners: num(l["corners"]), sides: num(l["sides"], 5), spoke: num(l["spoke"], 0.5) };
      saved.push({ kind: "shape", at, opacity, drawn });
    } else if (address) {
      const path = str(l["picture"]);
      const blob = await address(path).then((url) => fetch(url)).then((r) => r.blob()).catch(() => undefined);
      if (!blob) continue;
      const svg = await svgIn(blob);
      if (svg !== undefined) {
        const natural = svgSize(svg);
        saved.push({ kind: "svg", at, opacity, svg, natural: natural && { x: 0, y: 0, w: natural.w, h: natural.h } });
      } else {
        const href = await new Promise<string>((done) => {
          const reader = new FileReader();
          reader.onload = () => done(String(reader.result));
          reader.readAsDataURL(blob);
        });
        saved.push({ kind: "picture", at, opacity, href, crop: cropOf(l), turns: num(l["turns"]), mirrored: num(l["mirrored"]) !== 0, corners: num(l["corners"]) });
      }
    }
  }
  return svgDocument(area.w, area.h, saved);
}

// What a collage's preview is drawn from, apart from the preview itself.
function fingerprint(data: Row, previewInto: string): string {
  const plain = { ...data };
  delete plain[previewInto];
  const text = JSON.stringify(plain);
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) h = Math.imul(h ^ text.charCodeAt(i), 0x01000193);
  return (h >>> 0).toString(16);
}

// A name as a file's: lowercase words joined by hyphens.
function fileNameOf(name: string): string {
  return name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "") || "picture";
}

function minuteNow(): string {
  const d = new Date(), two = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())}-${two(d.getHours())}${two(d.getMinutes())}`;
}

// What a collage's buttons ask of it, by what names it: crop, edit, add, paste, a
// screenshot, a copy or a save. Pictures chosen from files are handed over as a paste's are.
const asked = new Map<string, string>();
const askListeners = new Set<() => void>();
function ask(name: string, does: string) {
  asked.set(name, does);
  askListeners.forEach((changed) => changed());
}

// What's asked of something else a screen's buttons act on, like a picture alone zoomed,
// by the name they ask it by: told it once, as a collage is.
export function useAsked(name: string, run: (does: string) => void): void {
  const asking = useSyncExternalStore(
    (changed) => {
      askListeners.add(changed);
      return () => askListeners.delete(changed);
    },
    () => asked.get(name),
    () => undefined,
  );
  const latest = useRef(run);
  latest.current = run;
  useEffect(() => {
    if (!asking) return;
    asked.delete(name);
    latest.current(asking);
  }, [asking, name]);
}

// What a Paste line's ctrl+v pasted, or an add pictures button chose, handed to its
// collage as it came: a browser gives a paste's pictures with the paste itself,
// without asking to read the clipboard.
const pastes = new Map<string, { files: File[]; words: string }>();

// What was last copied or cut from a collage, to be pasted back as itself: its layers, a
// group's all it holds, and the SVG put on the clipboard with them.
let copiedLayers: { svg: string; rows: Row[] } = { svg: "", rows: [] };

// crop "Crop", edit words "Edit", add words "Text": what's asked of the screen's collage.
export function CollageButton({ collage, does, label, icon, keys }: { collage: string; does: "crop" | "edit" | "ungroup" | "group picked" | "raise" | "lower" | "zoom in" | "zoom out" | "zoom to fit" | "add" | "add pictures" | "paste" | "screenshot" | "copy" | "copy picked" | "save" | "save picked" | "copy svg" | "copy svg picked" | "cut svg picked" | "save svg" | "save svg picked"; label: string; icon?: string; keys?: string[] }) {
  const ui = useUI();
  const tool = useTool() ?? "small";
  const off = useOff();
  // Pasting with ctrl+v takes what's pasted; its other keys, and a press, read the clipboard.
  const byPaste = does === "paste" && (keys ?? []).includes("ctrl+v") && !off;
  const press = () =>
    does === "add pictures"
      ? choosePictures((files) => {
          pastes.set(collage, { files, words: "" });
          ask(collage, "pasted");
        })
      : ask(collage, does);
  useKeys(byPaste ? keys!.filter((k) => k !== "ctrl+v") : keys, press, !off);
  useEffect(() => {
    if (!byPaste) return;
    const pasted = (e: ClipboardEvent) => {
      if (typing(e.target)) return;
      const files = [...(e.clipboardData?.files ?? [])];
      const words = e.clipboardData?.getData("text/plain") ?? "";
      if (files.length === 0 && !words.trim()) return;
      e.preventDefault();
      pastes.set(collage, { files, words });
      ask(collage, "pasted");
    };
    document.addEventListener("paste", pasted);
    return () => document.removeEventListener("paste", pasted);
  }, [byPaste, collage]);
  // Over a picked layer, only what fits it: crop over a picture, edit over words, and
  // ungroup over a group.
  const over = useContext(OverKind);
  if (over && ((does === "crop" && !over.picture) || (does === "edit" && !over.words) || (does === "ungroup" && !over.group))) return null;
  // A screenshot is taken where the browser can share the screen, and only there.
  if (does === "screenshot" && typeof navigator !== "undefined" && typeof navigator.mediaDevices?.getDisplayMedia !== "function") return null;
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} disabled={off} hint={hintOf(label, keys)} onClick={press}>
      {label}
    </ui.Button>
  );
}

// What the pointer is doing to a layer, from where it was and where the pointer was.
interface Gesture {
  what: "moving" | "resizing" | "cropping" | "shifting" | "drawing" | "reshaping" | "selecting" | "panning";
  id: string;
  from: Box;
  at: Point;
  corner: number;
  edges: number;
  keptFrom?: Box;
  free?: boolean; // a shape, resized freely, its shape kept only with Shift
}

// Where the collage was last drawn, how much smaller than itself, what the pointer is
// doing, where a layer is going or was let go, the lines it snaps to, a crop being
// made, and words being written. Kept apart from React's state, so a drag repaints
// the canvas alone.
interface Placed {
  board: Box;
  scale: number;
  now?: Gesture;
  going?: { id: string; box: Box };
  lines: Lines;
  guide?: Guided;
  crop?: { id: string; whole: Box; kept: Box };
  // Words typed where a draw by typing was pressed are made only once they're kept:
  // what they'll be made of, by that draw, its parts by name.
  editing?: { id: string; words: string; caret: number; fresh: boolean; unmade?: Row; by?: CollageDraw };
  drawing?: Box; // a layer being drawn, as big as the drag so far
  line?: { at: Box; points: string }; // or a line, as far as the drag has gone
  hand?: Point[]; // or what a drag by hand has gone through
  pen?: PenNode[]; // or the nodes a pen has put down so far
  hover?: Point; // and where the pointer is, which the next segment follows
  // A path being reshaped: its outline as it was, and as it is now, by its id.
  reshapeFrom?: string;
  reshaped?: { id: string; outline: string };
  // The picked path's node last pressed, whose control points show, by its id.
  node?: { id: string; at: number };
  // While a group is moved or resized, where it and each it holds were as it began.
  carried?: { id: string; group: Box; from: Map<string, Box> }; // the group carried, or "chosen"
  // A group gone into by pressing it twice: what's pressed in it is picked from among
  // what it holds, until Esc or a press outside it.
  entered?: string;
  // Several picked at once, by a drag around them or with Shift, besides the screen's
  // pick of one; and a drag around what's to be, as far as it's gone.
  chosen: string[];
  marquee?: Box;
  // How it's looked at, zoomed and moved, and how far in from its edges it's fitted.
  view: ViewAt;
  margin: number;
}

// A path as a reshape has it so far: its node moved, or one of its control points, in
// edges 1 coming in and 2 going out. The same as the desktop's.
function reshapedBy(outline: string, g: Gesture, dx: number, dy: number): string {
  return g.edges ? pathHandleMoved(outline, g.corner, g.edges === 2, dx, dy) : pathMoved(outline, g.corner, dx, dy);
}

// draw layer::create { fill = "#2e86de" } when tool == tool::box: a drag makes a layer
// by `command`, as big as the drag, given these values, while `when` holds; then each
// choice it read, by its key, goes back to its first.
export interface CollageDraw {
  command: string;
  given?: Record<string, unknown>;
  when?: boolean;
  // By line, a line from where the drag began to where it ended; by hand, what it went
  // through; by points, a path node by node, as a pen draws; each kept as a path. By
  // typing, words typed where it's pressed, made once they're kept.
  by?: "line" | "hand" | "points" | "typing";
  back?: [string, string][];
  // with stroke from layer::paint: these as that command's buttons last gave them.
  with?: { fields: string[]; from: string }[];
}

export function Collage({
  view,
  list,
  width,
  height,
  fields,
  place,
  reshape,
  group,
  order,
  snap = false,
  clear = false,
  matchColors = false,
  empty,
  pick,
  subject,
  fills = false,
  add,
  draws = [],
  crop,
  write,
  remove,
  parent,
  name,
  layers: layersList,
  layer: layerField,
  layerCreate,
  layerPick,
  layerParent,
  preview,
  previewInto,
  previewWide = 480,
  paste = false,
  update,
  children,
}: {
  view: ViewState;
  list: string;
  width: string;
  height: string;
  fields: Record<string, string>; // the field each of its parts is kept in, like words: "caption"
  place?: string; // the command a layer moved or resized is sent to, with x y w h
  reshape?: string; // and the one a path's nodes moved are, with its points and x y w h
  group?: string; // and the one that puts a layer in a group, or takes it out, by its group
  order?: string; // and the one a layer's depth is sent to, raised or lowered
  snap?: boolean;
  clear?: boolean; // nothing behind its layers: shown over a checkerboard, saved clear
  matchColors?: boolean; // its pictures' colors moved toward one another's, so they look like one picture
  empty?: string;
  pick?: string; // the screen's pick of its layers
  subject?: string; // the id of what the view is per, which its layers and preview are made on
  fills?: boolean; // in a workspace, as big as its region; elsewhere 560 tall
  add?: string; // the command that makes a layer, sent `parent` = subject
  draws?: CollageDraw[]; // what a drag on it draws, the first whose when holds
  crop?: string; // the one a picture cropped is sent to, its crop and its place
  write?: string; // words written, or resized: their words, size and place
  remove?: string; // a layer deleted, like words written away to nothing
  parent?: string; // the field a layer names what the view is per by, like poster
  name?: string; // what its buttons, like crop and add words, name it by
  // Held in layers: the view's list of them, the field each object names its layer by,
  // the command a new layer is made by, the screen's pick of a layer, the one new
  // objects go in, and the field a layer names what the view is per by.
  layers?: string;
  layer?: string;
  layerCreate?: string;
  layerPick?: string;
  layerParent?: string;
  preview?: string; // the command a small picture of it is sent to, once changes settle
  previewInto?: string; // in this file field of what the view is per
  previewWide?: number; // this many pixels wide
  paste?: boolean; // a file dropped or pasted made a picture, by add
  update?: string; // the command a picture's own size is filled in by, when it isn't kept yet
  children?: ReactNode; // the buttons over the picked layer
}) {
  const files = useFiles();
  const runner = useRunner();
  const picked = usePickedId(pick ?? "") ?? "";
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const placed = useRef<Placed>({ board: { x: 0, y: 0, w: 0, h: 0 }, scale: 0, lines: { across: [], down: [] }, view: { zoom: 1, panX: 0, panY: 0 }, margin: 32, chosen: [] });
  const [, setRead] = useState(0);
  const [, redraw] = useState(0);
  const [frame, setFrame] = useState<Box | undefined>();
  const [full, setFull] = useState(() => typeof document !== "undefined" && Boolean(document.fullscreenElement));
  const [dropping, setDropping] = useState(false);

  // Read as its parts, whichever of the app's fields keep them.
  const kept = JSON.stringify(fields);
  const data = useMemo(() => (view.status === "live" ? partsOf((view.data ?? {}) as Row, list, fields) : {}), [view.status, view.data, list, kept]);
  const send = (command: string, input: Row) => runner.reply(command, fieldsOf(input, fields));
  // Those hidden aren't drawn, picked, snapped to or saved; held in layers, they're in
  // their layers' order, and those of a hidden layer aren't either.
  const groups = layersList ? ((data[layersList] as Row[] | undefined) ?? []) : [];
  const shownOnes = ((data[list] as Row[] | undefined) ?? []).filter((l) => !hiddenLayer(l));
  // A group is drawn as what it holds, its place the box around them.
  const layers = grouped(layersList && layerField ? inLayers(shownOnes, groups, layerField) : shownOnes);
  const groupOf = groupsOf(layers);
  const pickedLayer = usePickedId(layerPick ?? "") ?? "";
  // Whether it keeps shapes, so an SVG brought in can be its own.
  const keepsShapes = ["shape", "points", "fill", "stroke", "stroke_width"].every((part) => part in fields);
  // The layer new objects go in, made first, as Layer 1, when there's none yet.
  const layerFor = async (): Promise<string | undefined> => {
    if (!layersList || !layerField) return undefined;
    const now = currentLayer(groups, pickedLayer);
    if (now || !layerCreate) return now || undefined;
    const made: Row = { name: "Layer 1", depth: 0 };
    if (layerParent) made[layerParent] = subject;
    const reply = await runner.reply(layerCreate, made);
    if (reply?.id && layerPick) setPicked(layerPick, reply.id);
    return reply?.id;
  };
  const W = num(data[width]), H = num(data[height]);
  const address = files ? (path: string) => files.address(path) : undefined;
  const chosen = layers.find((l) => l["id"] === picked);
  const w = placed.current;
  // Several chosen are let go as one's picked, as the layers' list may; and a group gone
  // into is left once it's gone.
  if (picked && w.chosen.length > 1) w.chosen = [];
  w.chosen = w.chosen.filter((id) => layers.some((l) => str(l["id"]) === id));
  if (w.entered && !layers.some((l) => str(l["id"]) === w.entered)) w.entered = undefined;

  useEffect(() => {
    const changed = () => setRead((n) => n + 1);
    readListeners.add(changed);
    void wordsLoaded().then(changed);
    const screen = () => setFull(Boolean(document.fullscreenElement));
    document.addEventListener("fullscreenchange", screen);
    return () => {
      readListeners.delete(changed);
      document.removeEventListener("fullscreenchange", screen);
    };
  }, []);

  const { say } = useSaying();
  const site = useSite();
  // A picture whose own size isn't kept yet, like one kept before its size was, has
  // it filled in from the picture as it's turned, once, never a step undo takes back.
  const sizedAsked = useRef(new Set<string>());
  useEffect(() => {
    if (!update) return;
    for (const l of layers) {
      const id = str(l["id"]);
      if (!("picture_width" in l) || num(l["picture_width"]) > 0 || !str(l["picture"]) || sizedAsked.current.has(id)) continue;
      const p = pictureOf(str(l["picture"]), address);
      if (!p) continue;
      sizedAsked.current.add(id);
      const sideways = (((num(l["turns"]) % 4) + 4) % 4) % 2 === 1;
      void send(update, { id, picture_width: sideways ? p.height : p.width, picture_height: sideways ? p.width : p.height, "one:history": "apart" });
    }
  });
  // Pictures dropped or pasted: the first on a collage with nothing on it fills it, and
  // the rest, and every SVG, are as big as they are, smaller where they wouldn't fit,
  // centered where they were let go, each a little further along; all one step to
  // undo. The last of them is picked once it's made, so it's ready to be worked on;
  // the same as the desktop's.
  const addPictures = async (given: File[], on?: Point) => {
    if (!add || !paste || !("picture" in fields) || !files) return;
    // The layer pictures go in, worked out once one comes; an SVG makes its own.
    let inLayer: string | undefined, asked = false;
    const top = layers[layers.length - 1];
    let depth = top && top["depth"] !== undefined ? num(top["depth"]) + 1 : undefined;
    let covered = layers.length > 0, at = 0, first = true;
    await takeInTurn(given, say, async (file) => {
      // An SVG, where the collage keeps shapes, comes in as its own shapes.
      const svg = keepsShapes ? await svgIn(file) : undefined;
      if (svg !== undefined) {
        const where = on && at ? { x: on.x + 40 * at, y: on.y + 40 * at } : on;
        at++;
        await addShapes(svg, file.name.replace(/\.svg$/i, "") || "Pasted", where);
        return;
      }
      const size = await pictureSize(file);
      if (!size) return `${file.name} isn't a picture this can read.`;
      if (!asked) (inLayer = await layerFor()), (asked = true);
      let b: Box;
      // An SVG is drawing, not a photograph: it comes in at its own size, fitted, never
      // covering the collage.
      const vector = (await svgIn(file)) !== undefined;
      if (!covered && !vector) {
        b = sitting("fill", size.w, size.h, W, H);
        covered = true;
      } else {
        const s = Math.min(1, W / size.w, H / size.h);
        const cx = on ? on.x : W / 2, cy = on ? on.y : H / 2;
        b = { x: cx - (size.w * s) / 2 + 40 * at, y: cy - (size.h * s) / 2 + 40 * at, w: size.w * s, h: size.h * s };
        at++;
      }
      const path = await files.upload(file, () => {});
      const made: Record<string, unknown> = { picture: path, x: b.x, y: b.y, w: b.w, h: b.h };
      if (layerField && inLayer) made[layerField] = inLayer;
      if (parent) made[parent] = subject;
      if (depth !== undefined) made["depth"] = depth++;
      if ("picture_width" in fields) (made["picture_width"] = size.w), (made["picture_height"] = size.h);
      if (!first) made["one:history"] = "joined";
      first = false;
      const reply = await send(add, made);
      if (reply?.id && pick) setPicked(pick, reply.id);
    });
  };
  // An SVG brought in as its own shapes and curves, each an object, where it would sit
  // as a picture: as big as it is, smaller where it wouldn't fit, centered on `on` or the
  // middle. Held in layers, they're a new layer, named for it, on top; all one step to
  // undo, and the layer picked. The same as the desktop's.
  const addShapes = async (svg: string, name: string, on?: Point) => {
    const size = svgSize(svg), shapes = svgShapes(svg);
    if (!add || !size || shapes.length === 0) return say({ text: `${name} has no shapes to bring in.` });
    const s = Math.min(1, W / size.w, H / size.h);
    const cx = on ? on.x : W / 2, cy = on ? on.y : H / 2;
    const at = { x: cx - (size.w * s) / 2, y: cy - (size.h * s) / 2 };
    let inLayer: string | undefined, joined = false, inGroupOf: string | undefined;
    const last = layers[layers.length - 1];
    let depth = last && last["depth"] !== undefined ? num(last["depth"]) + 1 : 0;
    if ("group" in fields) {
      // Where layers are grouped, one group in the layer it's brought into, named for
      // it, as big as it is; its shapes made once it is, all one step to undo.
      inLayer = await layerFor();
      const made: Row = { ...at, w: size.w * s, h: size.h * s, depth: depth++ };
      if ("name" in fields) made["name"] = name;
      if (parent) made[parent] = subject;
      if (layerField && inLayer) made[layerField] = inLayer;
      inGroupOf = (await send(add, made))?.id;
      if (!inGroupOf) return;
      joined = true;
    } else if (layersList && layerField && layerCreate) {
      const top = groups.reduce((most, l) => Math.max(most, num(l["depth"])), 0);
      const made: Row = { name, depth: top + 1 };
      if (layerParent) made[layerParent] = subject;
      inLayer = (await runner.reply(layerCreate, made))?.id;
      if (!inLayer) return;
      if (layerPick) setPicked(layerPick, inLayer);
      joined = true;
    } else {
      inLayer = await layerFor();
    }
    for (const shape of shapes) {
      const b = pathBounds(shape.path);
      const placed = { x: at.x + b.x * s, y: at.y + b.y * s, w: b.w * s, h: b.h * s };
      if (placed.w < 1) (placed.x += placed.w / 2 - 0.5), (placed.w = 1);
      if (placed.h < 1) (placed.y += placed.h / 2 - 0.5), (placed.h = 1);
      const object: Row = { shape: "path", points: pathFractions(shape.path, b), fill: shape.fill, stroke: shape.stroke, stroke_width: shape.strokeWidth * s, ...placed, depth: depth++ };
      if ("opacity" in fields) object["opacity"] = shape.opacity;
      if (parent) object[parent] = subject;
      if (layerField && inLayer) object[layerField] = inLayer;
      if (inGroupOf) object["group"] = inGroupOf;
      if (joined) object["one:history"] = "joined";
      joined = true;
      await send(add, object);
    }
    if (inGroupOf && pick) setPicked(pick, inGroupOf);
  };
  // Words pasted: a layer of them in the middle, a twelfth of its height; or, when
  // they're an SVG's, as a drawing program copies one, that SVG, as one dropped is.
  const addWords = async (words: string) => {
    // What this copied, still on the clipboard, comes back as itself.
    if (copiedLayers.rows.length > 0 && words === copiedLayers.svg) return pasteLayers();
    const start = words.replace(/^\uFEFF/, "").trimStart();
    if (paste && start.startsWith("<") && words.includes("<svg")) return void addPictures([new File([words], "pasted.svg", { type: "image/svg+xml" })]);
    if (!add || !("words" in fields)) return;
    const inLayer = await layerFor();
    const size = Math.max(8, Math.round(H / 12));
    const m = measured(words, size, "700");
    const made: Record<string, unknown> = { words, size, x: (W - m.w) / 2, y: (H - m.h) / 2, w: m.w, h: m.h };
    if (parent) made[parent] = subject;
    if (layerField && inLayer) made[layerField] = inLayer;
    const top = layers[layers.length - 1];
    if (top && top["depth"] !== undefined) made["depth"] = num(top["depth"]) + 1;
    void send(add, made);
  };
  // What was copied, made again where it was, in the current layer, above what's there:
  // each group before what it holds, in it again; all one step to undo; then it's
  // picked. The same as the desktop's.
  const pasteLayers = async () => {
    if (!add) return;
    const rows = [...copiedLayers.rows];
    const ids = new Set(rows.map((r) => str(r["id"])));
    // Groups first, outermost first, then the rest, as they were stacked.
    const nesting = (r: Row) => {
      let depth = 0;
      for (let at = str(r["group"]); ids.has(at) && depth < 64; depth++) at = str(rows.find((o) => str(o["id"]) === at)?.["group"]);
      return depth;
    };
    rows.sort((a, b) => {
      const ga = holdsOthers(a), gb = holdsOthers(b);
      if (ga !== gb) return ga ? -1 : 1;
      return ga ? nesting(a) - nesting(b) : 0;
    });
    const top = layers.reduce((most, l) => Math.max(most, num(l["depth"])), 0);
    const inLayer = await layerFor();
    const made = new Map<string, string>();
    for (const [i, r] of rows.entries()) {
      const again: Row = {};
      for (const part of Object.keys(fields)) if (part !== "group" && part !== "hidden" && part in r) again[part] = r[part];
      again["depth"] = top + 1 + i;
      const group = str(r["group"]);
      if ("group" in fields) again["group"] = ids.has(group) ? (made.get(group) ?? null) : null;
      if (parent) again[parent] = subject;
      if (layerField && inLayer) again[layerField] = inLayer;
      if (i > 0) again["one:history"] = "joined";
      const reply = await send(add, again);
      if (reply?.id) made.set(str(r["id"]), reply.id);
    }
    // The ones not in another pasted with them, picked.
    const outer = rows.filter((r) => !ids.has(str(r["group"])));
    if (outer.length === 1) pickRow(made.get(str(outer[0]!["id"])) ?? "");
  };
  // What's on the clipboard, pasted: pictures, or words.
  const pasteNow = async () => {
    try {
      const items = await navigator.clipboard.read();
      const pictures: File[] = [];
      for (const item of items) {
        const kind = item.types.find((t) => t.startsWith("image/"));
        if (kind) pictures.push(new File([await item.getType(kind)], `Pasted.${kind.slice(6)}`, { type: kind }));
      }
      if (pictures.length > 0) return void (await addPictures(pictures));
      const words = await navigator.clipboard.readText();
      if (words.trim()) return addWords(words);
      say({ text: "There's nothing to paste. Copy a picture or some words first." });
    } catch {
      say({ text: "This browser didn't let the page read the clipboard. Press Ctrl+V instead.", problem: true });
    }
  };
  // The screen as the browser shares it, one frame of it, added like any picture.
  const screenshot = async () => {
    try {
      const stream = await navigator.mediaDevices.getDisplayMedia({ video: true });
      const video = document.createElement("video");
      video.muted = true;
      video.srcObject = stream;
      await video.play();
      const frame = document.createElement("canvas");
      frame.width = video.videoWidth;
      frame.height = video.videoHeight;
      frame.getContext("2d")!.drawImage(video, 0, 0);
      stream.getTracks().forEach((t) => t.stop());
      const blob = await new Promise<Blob | null>((done) => frame.toBlob(done, "image/png"));
      if (blob) await addPictures([new File([blob], "Screenshot.png", { type: "image/png" })]);
    } catch {
      say({ text: "No screenshot was taken." });
    }
  };
  // Its preview, drawn once changes have settled for a moment and nothing's being moved,
  // cropped or written, and sent never to be undone. One already there when it's first
  // shown is what it looks like now.
  const drawnFrom = useRef<string | undefined>(undefined);
  const now = preview && previewInto && view.status === "live" ? fingerprint(data, previewInto) : undefined;
  if (now !== undefined && drawnFrom.current === undefined && (str(data[previewInto!]) !== "" || layers.length === 0)) drawnFrom.current = now;
  useEffect(() => {
    if (now === undefined || now === drawnFrom.current || !preview || !previewInto || !files || W <= 0 || H <= 0) return;
    let waiting: ReturnType<typeof setTimeout>;
    const settle = () => {
      waiting = setTimeout(() => {
        if (w.now || w.crop || w.editing) return settle();
        drawnFrom.current = now;
        void drawnWhole(layers, W, H, previewWide / W, address, Boolean(matchColors), undefined, clear)
          .then((made) => new Promise<Blob | null>((done) => made.toBlob(done, "image/png")))
          .then(async (blob) => {
            if (!blob) return;
            const path = await files.upload(new File([blob], "preview.png", { type: "image/png" }), () => {});
            await runner.reply(preview, { id: subject, [previewInto]: path, "one:history": "apart" });
          });
      }, 800);
    };
    settle();
    return () => clearTimeout(waiting);
  }, [now]);

  // A crop or words are only of the picked layer; picking another puts them away.
  if (w.crop && w.crop.id !== picked) w.crop = undefined;

  // Words written kept, as big as they now are; written away to nothing, gone.
  const finishWriting = () => {
    const e = w.editing;
    if (!e) return;
    w.editing = undefined;
    if (e.unmade && e.by) {
      // Typed: made now, as big as they are; nothing typed, nothing made.
      if (!e.words.trim()) return;
      const m = measured(e.words, num(e.unmade["size"]), faceOf(e.unmade));
      return void makeDrawn(e.by, { ...e.unmade, words: e.words, w: m.w, h: m.h });
    }
    const l = layers.find((r) => r["id"] === e.id);
    if (!l) return;
    if (!e.words.trim()) {
      if (remove) void send(remove, { id: e.id });
      if (pick) setPicked(pick, undefined);
      return;
    }
    if (e.words === str(l["words"]) || !write) return;
    const m = measured(e.words, num(l["size"]), faceOf(l));
    void send(write, { id: e.id, words: e.words, size: l["size"], x: l["x"], y: l["y"], w: m.w, h: m.h });
  };
  if (w.editing && !w.editing.unmade && w.editing.id !== picked) finishWriting();

  // What its buttons asked of it: a crop begun, words written, or words added.
  const asking = useSyncExternalStore(
    (changed) => {
      askListeners.add(changed);
      return () => askListeners.delete(changed);
    },
    () => (name ? asked.get(name) : undefined),
    () => undefined,
  );
  useEffect(() => {
    if (!asking || !name) return;
    asked.delete(name);
    if (asking === "crop" && chosen && isPicture(chosen) && crop) {
      w.crop = cropBegun(chosen);
    } else if ((asking === "raise" || asking === "lower") && chosen && order) {
      // Past the one above it, or below, among those beside it, in one step.
      const moved = restacked(layers, picked, asking === "raise", layerField ?? "");
      void (async () => {
        for (const [i, [id, depth]] of moved.entries()) await send(order, { id, depth, ...(i > 0 ? { "one:history": "joined" } : {}) });
      })();
    } else if ((asking === "zoom in" || asking === "zoom out") && canvasRef.current) {
      // About the middle of where it's shown.
      const area = { x: 0, y: 0, w: canvasRef.current.clientWidth, h: canvasRef.current.clientHeight };
      w.view = zoomed(w.view, area, W, H, w.margin, asking === "zoom in" ? 1.25 : 0.8, { x: area.w / 2, y: area.h / 2 });
    } else if (asking === "zoom to fit") {
      w.view = { zoom: 1, panX: 0, panY: 0 };
    } else if (asking === "group picked" && w.chosen.length > 1 && add && group) {
      // A new group, as big as what's chosen, where the topmost of them is, and each of
      // them in it, all one step to undo; then it's picked.
      const members = layers.filter((l) => w.chosen.includes(str(l["id"])));
      const top = members[members.length - 1]!;
      const made: Row = { ...chosenBox(), depth: num(top["depth"]) };
      if ("name" in fields) made["name"] = "Group";
      if (parent) made[parent] = subject;
      if (layerField && top[layerField] !== undefined) made[layerField] = top[layerField];
      if (w.entered) made["group"] = w.entered;
      w.chosen = [];
      void (async () => {
        const reply = await send(add, made);
        if (!reply?.id) return;
        for (const m of members) await send(group, { id: str(m["id"]), group: reply.id, "one:history": "joined" });
        pickRow(reply.id);
      })();
    } else if (asking === "ungroup" && chosen && holdsOthers(chosen) && group) {
      // What it holds, each in no group, and it gone, all one step to undo.
      const held = layers.filter((l) => str(l["group"]) === picked);
      void (async () => {
        for (const [i, l] of held.entries()) await send(group, { id: str(l["id"]), group: null, ...(i > 0 ? { "one:history": "joined" } : {}) });
        if (remove) await send(remove, { id: picked, "one:history": "joined" });
      })();
      pickRow("");
    } else if (asking === "edit" && chosen && isWords(chosen) && write) {
      w.editing = { id: picked, words: str(chosen["words"]), caret: str(chosen["words"]).length, fresh: false };
    } else if (asking.startsWith("copy svg") || asking.startsWith("cut svg") || asking.startsWith("save svg")) {
      // As SVG, from its vectors: copied as its text, which drawing programs paste, or
      // saved. What's picked, several or one, is kept too, to be pasted back as itself.
      const one = asking.endsWith("picked"), copying = !asking.startsWith("save"), cutting = asking.startsWith("cut");
      const picks = w.chosen.length > 1 ? [...w.chosen] : picked ? [picked] : [];
      if (one && picks.length === 0) say({ text: "Pick something first." });
      else if (layers.length === 0 || W <= 0 || H <= 0) say({ text: copying ? "There's nothing to copy yet." : "There's nothing to save yet." });
      else {
        const svg = svgOf(layers, W, H, address, one ? picks : undefined);
        if (copying) {
          const rows = one ? layers.filter((l) => picks.some((o) => o === str(l["id"]) || inGroup(groupOf, str(l["id"]), o))) : [];
          copiedLayers = { svg: "", rows: [] };
          if (cutting && remove) {
            // Taken away, all of it, in one step.
            void (async () => {
              for (const [i, l] of rows.entries()) await send(remove, { id: str(l["id"]), ...(i > 0 ? { "one:history": "joined" } : {}) });
            })();
            w.chosen = [];
            pickRow("");
          }
          void svg
            .then((text) => {
              if (one) copiedLayers = { svg: text, rows };
              return navigator.clipboard.writeText(text);
            })
            .then(() => say({ text: !one ? "Copied the drawing as SVG. Paste it anywhere." : cutting ? "Cut. Paste it into any layer, or anywhere as SVG." : "Copied. Paste it into any layer, or anywhere as SVG." }))
            .catch(() => say({ text: "This browser didn't let the page copy.", problem: true }));
        } else {
          const title = str(data["title"]) || str(data["name"]) || site.name;
          void svg.then((text) => {
            const link = document.createElement("a");
            link.href = URL.createObjectURL(new Blob([text], { type: "image/svg+xml" }));
            link.download = `${fileNameOf(one ? `${title} layer` : title)}-${minuteNow()}.svg`;
            link.click();
            setTimeout(() => URL.revokeObjectURL(link.href), 1000);
            say({ text: `Saved ${link.download} in your downloads.` });
          });
        }
      }
    } else if (asking.startsWith("copy") || asking.startsWith("save")) {
      // Copied for another program to paste, or saved: all of it, or the picked layer.
      const one = asking.endsWith("picked"), copying = asking.startsWith("copy");
      if (one && !chosen) say({ text: "Pick a layer first." });
      else if (layers.length === 0 || W <= 0 || H <= 0) say({ text: copying ? "There's nothing to copy yet." : "There's nothing to save yet." });
      else {
        const png = drawnWhole(layers, W, H, 1, address, Boolean(matchColors), one ? picked : undefined, clear).then(
          (made) => new Promise<Blob>((done, failed) => made.toBlob((b) => (b ? done(b) : failed(new Error("not drawn"))), "image/png")),
        );
        if (copying) {
          navigator.clipboard
            .write([new ClipboardItem({ "image/png": png })])
            .then(() => say({ text: one ? "Copied the layer. Paste it anywhere." : "Copied the picture. Paste it anywhere." }))
            .catch(() => say({ text: "This browser didn't let the page copy a picture.", problem: true }));
        } else {
          // Named for what the view calls it, or for the app when it calls it nothing.
          const title = str(data["title"]) || str(data["name"]) || site.name;
          void png.then((blob) => {
            const link = document.createElement("a");
            link.href = URL.createObjectURL(blob);
            link.download = `${fileNameOf(one ? `${title} layer` : title)}-${minuteNow()}.png`;
            link.click();
            setTimeout(() => URL.revokeObjectURL(link.href), 1000);
            say({ text: `Saved ${link.download} in your downloads.` });
          });
        }
      }
    } else if (asking === "pasted") {
      const given = pastes.get(name);
      pastes.delete(name);
      if (given && given.files.length > 0) void addPictures(given.files);
      else if (given) addWords(given.words);
    } else if (asking === "paste") {
      void pasteNow();
    } else if (asking === "screenshot") {
      void screenshot();
    } else if (asking === "add" && add && "words" in fields) {
      // Words in the middle, a twelfth of its height, all chosen so the first key
      // typed replaces them, on top of the rest.
      const size = Math.max(8, Math.round(H / 12));
      const words = "Your text";
      const m = measured(words, size, "700");
      const made: Record<string, unknown> = { words, size, x: (W - m.w) / 2, y: (H - m.h) / 2, w: m.w, h: m.h };
      if (parent) made[parent] = subject;
      const top = layers[layers.length - 1];
      if (top && top["depth"] !== undefined) made["depth"] = num(top["depth"]) + 1;
      void layerFor()
        .then((inLayer) => {
          if (layerField && inLayer) made[layerField] = inLayer;
          return send(add, made);
        })
        .then((reply) => {
        if (!reply?.id) return;
        if (pick) setPicked(pick, reply.id);
        w.editing = { id: reply.id, words, caret: words.length, fresh: true };
        redraw((n) => n + 1);
      });
    }
    redraw((n) => n + 1);
  });

  // Where each layer is on the collage: words as big as they are, the rest as they
  // say, and one being moved or let go where it's going.
  const whereIs = (l: Row): Box => {
    let b = boxOf(l);
    const g = w.going;
    if (g && g.id === l["id"]) {
      const near = (a: number, z: number) => Math.abs(a - z) < 0.01;
      if (near(b.x, g.box.x) && near(b.y, g.box.y) && near(b.w, g.box.w) && near(b.h, g.box.h) && !w.now) (w.going = undefined), (w.carried = undefined);
      else b = g.box;
    } else if (g && w.carried?.from.has(str(l["id"])) && w.carried.id === g.id) {
      // One a group holds, carried with it.
      b = carried(w.carried.from.get(str(l["id"]))!, w.carried.group, g.box);
    }
    if (isWords(l) && !(w.going && w.going.id === l["id"] && w.now)) {
      const writing = w.editing?.id === l["id"];
      const m = measured(writing ? w.editing!.words : str(l["words"]), num(l["size"]), faceOf(l));
      b = { x: b.x, y: b.y, w: m.w, h: m.h };
    }
    return b;
  };
  // A crop begun on a picture where it is, and the whole picture behind it.
  const cropBegun = (l: Row): NonNullable<Placed["crop"]> => {
    const kept = boxOf(l);
    return { id: str(l["id"]), whole: wholeOf(kept, cropOf(l)), kept };
  };
  const onBoard = (b: Box): Box => ({ x: w.board.x + b.x * w.scale, y: w.board.y + b.y * w.scale, w: b.w * w.scale, h: b.h * w.scale });

  const paint = () => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ratio = window.devicePixelRatio || 1;
    const cw = canvas.clientWidth, ch = canvas.clientHeight;
    if (canvas.width !== Math.round(cw * ratio) || canvas.height !== Math.round(ch * ratio)) {
      canvas.width = Math.round(cw * ratio);
      canvas.height = Math.round(ch * ratio);
    }
    const c = canvas.getContext("2d");
    if (!c) return;
    c.setTransform(ratio, 0, 0, ratio, 0, 0);
    const style = getComputedStyle(canvas);
    const accent = style.getPropertyValue("--color-accent").trim() || "#2457d6";
    c.clearRect(0, 0, cw, ch);
    if (full) {
      c.fillStyle = "#000";
      c.fillRect(0, 0, cw, ch);
    }
    if (W <= 0 || H <= 0) return;
    // Fitted in its area, then zoomed and moved as it's looked at.
    const margin = full ? 0 : 32;
    w.margin = margin;
    const board = boardOf(w.view, { x: 0, y: 0, w: cw, h: ch }, W, H, margin);
    if (board.w <= 0) return;
    const scale = board.w / W, bw = board.w, bh = board.h;
    w.board = board;
    w.scale = scale;
    c.save();
    if (!full) {
      c.shadowColor = "rgba(0, 0, 0, 0.18)";
      c.shadowBlur = 14;
      c.shadowOffsetY = 2;
    }
    c.fillStyle = clear ? checkers(c) : "#fff";
    c.fillRect(board.x, board.y, bw, bh);
    c.restore();
    const shifts = shiftsOf(layers, matchColors, address);
    let framed: Box | undefined;
    let framedRadius = 0;
    let nodes: Box[] = []; // a picked path's, drawn instead of its frame
    let levers: Box[] = []; // its chosen node's control points
    let chosenNode: Box | undefined;
    c.save();
    c.beginPath();
    c.rect(board.x, board.y, bw, bh);
    c.clip();
    // While something's moved, resized or cropped, its pictures alone, nothing worked out
    // from where they are drawn around them, so what's being done is plain to see.
    const effects = !w.now && !w.crop;
    const lookHere = (l: Row) => {
      const path = str(l["picture"]);
      const p = pictureOf(path, address);
      return p ? lookOf(l, orientedOf(path, p, ((num(l["turns"]) % 4) + 4) % 4, num(l["mirrored"]) !== 0), shifts, shownLargest) : undefined;
    };
    if (effects) {
      const under = layers.filter((l) => isPicture(l) && harmonizes(l)).flatMap((l) => {
        const seen = lookHere(l);
        return seen ? [{ l, seen }] : [];
      });
      const fill = harmonizedOf(under, W, H);
      if (fill) c.drawImage(fill, board.x, board.y, bw, bh);
    }
    for (const l of layers) {
      let b = onBoard(whereIs(l));
      // A group draws nothing of its own; picked, it's framed around all it holds.
      if (holdsOthers(l)) {
        if (l["id"] === picked && !full) framed = b;
        continue;
      }
      if (isWords(l)) {
        // Being written: as typed so far, with its caret, all of it marked while the
        // first key would replace it; being resized, its letters as large.
        const writing = w.editing?.id === l["id"] ? w.editing : undefined;
        const kept = measured(str(l["words"]), num(l["size"]), faceOf(l));
        const size = num(l["size"]) * (w.going?.id === l["id"] && w.now && kept.h > 0 ? b.h / w.scale / kept.h : 1) * scale;
        c.font = `${faceOf(l)} ${size}px ${wordsFamily()}`;
        c.textBaseline = "top";
        if (writing?.fresh) {
          c.fillStyle = accent;
          c.globalAlpha = 0.35;
          c.fillRect(b.x, b.y, b.w, b.h);
          c.globalAlpha = 1;
        }
        const said = writing ? writing.words : str(l["words"]);
        paintWords(c, l, said, size, b.x, b.y);
        if (writing && !writing.fresh) {
          const before = said.slice(0, writing.caret).split("\n");
          const line = before.length - 1;
          c.fillStyle = accent;
          c.fillRect(b.x + c.measureText(before[line]!).width, b.y + line * size * 1.2, 2, size * 1.2);
        }
      } else if (isShape(l)) {
        // Being reshaped: as its nodes are now.
        let shown = l, kept = whereIs(l);
        const reshaped = w.reshaped;
        if (reshaped && reshaped.id === l["id"]) {
          const now = reshapedAs(reshaped.outline);
          (shown = { ...l, points: now.points }), (kept = now.box), (b = onBoard(kept));
        }
        paintShape(c, shown, b, kept);
        // A path picked shows its nodes, not a box.
        if (l["id"] === picked && !full && isPath(shown) && reshape) {
          const outline = outlineOf(shown, kept);
          nodes = pathNodes(outline).map((n) => onBoard({ x: n.x, y: n.y, w: 0, h: 0 }));
          if (w.node?.id === l["id"] && nodes[w.node.at]) {
            chosenNode = nodes[w.node.at];
            levers = pathHandles(outline, w.node.at).map((h) => onBoard({ x: h.at.x, y: h.at.y, w: 0, h: 0 }));
          }
        }
      } else {
        const path = str(l["picture"]);
        const p = pictureOf(path, address);
        const v = vectors.get(path);
        if (v) {
          c.save();
          c.globalAlpha = opacityOf(l);
          c.drawImage(v, b.x, b.y, b.w, b.h);
          c.restore();
        } else if (p) {
          const shown = orientedOf(path, p, ((num(l["turns"]) % 4) + 4) % 4, num(l["mirrored"]) !== 0);
          if (w.crop && w.crop.id === l["id"]) {
            // Being cropped: the whole picture faint, what's kept as it will be.
            const whole = onBoard(w.crop.whole), kept = onBoard(w.crop.kept);
            c.globalAlpha = 0.35;
            c.drawImage(shown, whole.x, whole.y, whole.w, whole.h);
            c.globalAlpha = 1;
            const fx = (w.crop.kept.x - w.crop.whole.x) / w.crop.whole.w, fy = (w.crop.kept.y - w.crop.whole.y) / w.crop.whole.h;
            c.drawImage(shown, fx * shown.width, fy * shown.height, (w.crop.kept.w / w.crop.whole.w) * shown.width, (w.crop.kept.h / w.crop.whole.h) * shown.height, kept.x, kept.y, kept.w, kept.h);
            b = kept;
          } else {
            // Only what its crop keeps, its colors as matched, and what's around it as it says.
            paintPicture(c, l, lookOf(l, shown, shifts, shownLargest), board, b, effects);
          }
        }
      }
      if (l["id"] === picked && !full) {
        framed = b;
        // A picture's frame follows its corners, round as they are.
        framedRadius = isWords(l) || (w.crop && w.crop.id === l["id"]) ? 0 : Math.min(num(l["corners"]) * Math.min(b.w, b.h), Math.min(b.w, b.h) / 2);
      }
    }
    c.restore();
    // The lines a moved layer snapped to, across the whole collage.
    const guide = w.guide;
    if (w.now && guide && w.now.what !== "cropping" && w.now.what !== "shifting") {
      c.strokeStyle = c.fillStyle = accent;
      if (guide.corner) {
        c.lineWidth = 1.5;
        c.beginPath();
        c.moveTo(board.x + guide.corner[0].x * scale, board.y + guide.corner[0].y * scale);
        c.lineTo(board.x + guide.corner[1].x * scale, board.y + guide.corner[1].y * scale);
        c.stroke();
      }
      if (guide.guideX !== undefined) c.fillRect(board.x + guide.guideX * scale - 0.5, board.y, 1, bh);
      if (guide.guideY !== undefined) c.fillRect(board.x, board.y + guide.guideY * scale - 0.5, bw, 1);
    }
    // Words being typed, not made yet, with their caret.
    const unmade = w.editing?.unmade;
    if (unmade) {
      const at = onBoard({ x: num(unmade["x"]), y: num(unmade["y"]), w: 0, h: 0 });
      const size = num(unmade["size"]) * scale, said = w.editing!.words;
      c.font = `${faceOf(unmade)} ${size}px ${wordsFamily()}`;
      c.textBaseline = "top";
      paintWords(c, unmade, said, size, at.x, at.y);
      const before = said.slice(0, w.editing!.caret).split("\n");
      c.fillStyle = accent;
      c.fillRect(at.x + c.measureText(before[before.length - 1]!).width, at.y + (before.length - 1) * size * 1.2, 2, size * 1.2);
    }
    // A layer being drawn, as big as the drag so far.
    if (w.drawing) {
      const d = onBoard(w.drawing);
      c.strokeStyle = accent;
      c.lineWidth = 1.5;
      c.strokeRect(d.x, d.y, d.w, d.h);
    }
    // A pen's path so far, the next segment following the pointer, and its nodes.
    if (w.pen?.length) {
      const shown = [...w.pen];
      if (w.hover && w.now?.what !== "drawing") shown.push({ x: w.hover.x, y: w.hover.y, handleX: 0, handleY: 0 });
      if (shown.length > 1) {
        const { points, box } = reshapedAs(penPath(shown, false));
        paintShape(c, { shape: "path", points, stroke: "#2457d6", stroke_width: 1.5 / scale }, onBoard(box), box);
      }
      c.strokeStyle = accent;
      c.lineWidth = 1.5;
      for (const n of w.pen) {
        const at = onBoard({ x: n.x, y: n.y, w: 0, h: 0 });
        c.fillStyle = "#fff";
        c.fillRect(at.x - handle / 2, at.y - handle / 2, handle, handle);
        c.strokeRect(at.x - handle / 2, at.y - handle / 2, handle, handle);
      }
    }
    // What a drag by hand has gone through so far.
    if (w.hand && w.now?.what === "drawing" && w.hand.length > 1) {
      c.strokeStyle = accent;
      c.lineWidth = 1.5;
      c.beginPath();
      w.hand.forEach((h, i) => {
        const at = onBoard({ x: h.x, y: h.y, w: 0, h: 0 });
        if (i === 0) c.moveTo(at.x, at.y);
        else c.lineTo(at.x, at.y);
      });
      c.stroke();
    }
    if (w.line) {
      const d = onBoard(w.line.at);
      const [, x0, y0, , x1, y1] = w.line.points.split(" ").map(Number);
      c.strokeStyle = accent;
      c.lineWidth = 1.5;
      c.beginPath();
      c.moveTo(d.x + x0! * d.w, d.y + y0! * d.h);
      c.lineTo(d.x + x1! * d.w, d.y + y1! * d.h);
      c.stroke();
    }
    if (layers.length === 0 && !full && empty && !w.editing) {
      c.fillStyle = style.getPropertyValue("--color-muted").trim() || "#5b6577";
      c.font = `13px ${wordsFamily()}`;
      c.textAlign = "center";
      c.textBaseline = "middle";
      c.fillText(empty, board.x + bw / 2, board.y + bh / 2);
      c.textAlign = "start";
    }
    // A group gone into, faintly framed; several chosen, each framed finely and all of
    // them together, moving as one; and a drag around what's to be chosen.
    c.save();
    c.strokeStyle = accent;
    c.globalAlpha = 0.45;
    c.lineWidth = 1;
    const entered = w.entered ? layers.find((l) => str(l["id"]) === w.entered) : undefined;
    if (entered) {
      const e = onBoard(whereIs(entered));
      c.strokeRect(e.x - 4, e.y - 4, e.w + 8, e.h + 8);
    }
    if (w.chosen.length > 1) {
      const moving = w.going?.id === "chosen";
      if (!moving) for (const l of layers) if (w.chosen.includes(str(l["id"]))) {
        const o = onBoard(whereIs(l));
        c.strokeRect(o.x, o.y, o.w, o.h);
      }
      c.globalAlpha = 1;
      c.lineWidth = 1.5;
      const all = onBoard(moving ? w.going!.box : chosenBox());
      c.strokeRect(all.x - 3, all.y - 3, all.w + 6, all.h + 6);
    }
    if (w.marquee) {
      const m = onBoard(w.marquee);
      c.globalAlpha = 0.08;
      c.fillStyle = accent;
      c.fillRect(m.x, m.y, m.w, m.h);
      c.globalAlpha = 1;
      c.strokeRect(m.x, m.y, m.w, m.h);
    }
    c.restore();
    // The picked layer's frame and corners, over everything, a crop's edges too; a
    // picked path's nodes instead.
    if (framed && nodes.length) {
      c.strokeStyle = accent;
      c.lineWidth = 1;
      for (const h of levers) {
        c.beginPath();
        c.moveTo(chosenNode!.x, chosenNode!.y);
        c.lineTo(h.x, h.y);
        c.stroke();
      }
      c.lineWidth = 2;
      for (const n of nodes) {
        c.fillStyle = n === chosenNode ? accent : "#fff";
        c.fillRect(n.x - handle / 2, n.y - handle / 2, handle, handle);
        c.strokeRect(n.x - handle / 2, n.y - handle / 2, handle, handle);
      }
      // A control point is round, as a node is square.
      c.lineWidth = 1.5;
      c.fillStyle = "#fff";
      for (const h of levers) {
        c.beginPath();
        c.arc(h.x, h.y, handle / 2 - 1, 0, 2 * Math.PI);
        c.fill();
        c.stroke();
      }
    } else if (framed) {
      c.strokeStyle = accent;
      c.lineWidth = 2;
      if (framedRadius > 0) {
        c.beginPath();
        c.roundRect(framed.x, framed.y, framed.w, framed.h, framedRadius);
        c.stroke();
      } else c.strokeRect(framed.x, framed.y, framed.w, framed.h);
      const corners = w.crop ? true : chosen && isWords(chosen) ? Boolean(write) : chosen && Boolean(place);
      const points: [number, number][] = [];
      if (corners && !w.editing) points.push([framed.x, framed.y], [framed.x + framed.w, framed.y], [framed.x + framed.w, framed.y + framed.h], [framed.x, framed.y + framed.h]);
      if (w.crop) {
        const mx = framed.x + framed.w / 2, my = framed.y + framed.h / 2;
        points.push([mx, framed.y], [framed.x + framed.w, my], [mx, framed.y + framed.h], [framed.x, my]);
      }
      for (const [x, y] of points) {
        c.fillStyle = "#fff";
        c.fillRect(x - handle / 2, y - handle / 2, handle, handle);
        c.strokeRect(x - handle / 2, y - handle / 2, handle, handle);
      }
    }
    setFrame((was) => (was && framed && was.x === framed.x && was.y === framed.y && was.w === framed.w && was.h === framed.h ? was : framed));
  };

  useLayoutEffect(() => paint());
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || typeof ResizeObserver === "undefined") return;
    const watching = new ResizeObserver(() => paint());
    watching.observe(canvas);
    return () => watching.disconnect();
  });
  // The wheel, with Ctrl, zooms about the pointer, as a trackpad's pinch does; without,
  // it moves what's shown, across with Shift. Heard as it's turned, so the page doesn't
  // scroll or zoom instead.
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const turned = (e: WheelEvent) => {
      if (W <= 0 || H <= 0) return;
      e.preventDefault();
      const r = canvas.getBoundingClientRect();
      const lines = e.deltaMode === 1 ? 16 : 1;
      let dx = e.deltaX * lines, dy = e.deltaY * lines;
      if (e.ctrlKey) w.view = zoomed(w.view, { x: 0, y: 0, w: r.width, h: r.height }, W, H, w.margin, Math.exp(-dy * 0.002), { x: e.clientX - r.left, y: e.clientY - r.top });
      else {
        if (e.shiftKey) [dx, dy] = [dy, dx];
        w.view = { ...w.view, panX: w.view.panX - dx, panY: w.view.panY - dy };
      }
      paint();
    };
    canvas.addEventListener("wheel", turned, { passive: false });
    return () => canvas.removeEventListener("wheel", turned);
  });
  // What a style applies to: each of several chosen, or all a picked group holds.
  useEffect(() => {
    if (!pick) return;
    const allIn = (top: string) => layers.filter((l) => !holdsOthers(l) && (str(l["id"]) === top || inGroup(groupOf, str(l["id"]), top))).map((l) => str(l["id"]));
    styleThese(pick, w.chosen.length > 1 ? w.chosen.flatMap(allIn) : chosen && holdsOthers(chosen) ? allIn(picked) : []);
  });
  // Space held, for a drag to move what's shown, and the hand that says so.
  const spaceHeld = useRef(false);
  useEffect(() => {
    const canvas = canvasRef.current;
    const down = (k: KeyboardEvent) => {
      if (k.key !== " " || typing(k.target) || spaceHeld.current) return;
      spaceHeld.current = true;
      if (canvas && !w.now) canvas.style.cursor = "grab";
      k.preventDefault();
    };
    const up = (k: KeyboardEvent) => {
      if (k.key !== " ") return;
      spaceHeld.current = false;
      if (canvas && w.now?.what !== "panning") canvas.style.cursor = "default";
    };
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    return () => {
      window.removeEventListener("keydown", down);
      window.removeEventListener("keyup", up);
    };
  });
  // Drawn again as light and dark trade places, by the page's switch or the system's,
  // as its colors come from the theme.
  useEffect(() => {
    if (typeof MutationObserver === "undefined") return;
    const switched = new MutationObserver(() => paint());
    switched.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme", "data-palette", "class"] });
    const system = globalThis.matchMedia?.("(prefers-color-scheme: dark)");
    const changed = () => paint();
    system?.addEventListener?.("change", changed);
    return () => {
      switched.disconnect();
      system?.removeEventListener?.("change", changed);
    };
  });

  // A path being drawn by a pen: Enter ends it, Esc drops it, Backspace takes back its
  // last node; put away when the pen is.
  if (w.pen?.length && draws.find((one) => one.when !== false)?.by !== "points") w.pen = w.hover = undefined;
  useEffect(() => {
    const down = (k: KeyboardEvent) => {
      if (!w.pen?.length) return;
      if (k.key === "Enter") penDone(false);
      else if (k.key === "Escape") (w.pen = w.hover = undefined), paint();
      else if (k.key === "Backspace") w.pen.pop(), paint();
      else return;
      k.preventDefault();
      k.stopPropagation();
    };
    window.addEventListener("keydown", down, true);
    return () => window.removeEventListener("keydown", down, true);
  });

  // Words being written take every key: Esc keeps them, Enter starts a new line, the
  // arrows, Home and End move the caret, and the rest is typed.
  useEffect(() => {
    if (!w.editing) return;
    const down = (k: KeyboardEvent) => {
      const e = w.editing;
      if (!e) return;
      k.preventDefault();
      k.stopPropagation();
      if (k.key === "Escape") {
        finishWriting();
        redraw((n) => n + 1);
        return;
      }
      const typed = k.key.length === 1 && !k.ctrlKey && !k.metaKey && !k.altKey;
      const newLine = k.key === "Enter";
      if (e.fresh && (typed || newLine || k.key === "Backspace" || k.key === "Delete")) (e.words = ""), (e.caret = 0), (e.fresh = false);
      if (["ArrowLeft", "ArrowRight", "Home", "End"].includes(k.key)) e.fresh = false;
      const chars = [...e.words];
      let at = [...e.words.slice(0, e.caret)].length;
      if (newLine) chars.splice(at++, 0, "\n");
      else if (k.key === "Backspace" && at > 0) chars.splice(--at, 1);
      else if (k.key === "Delete" && at < chars.length) chars.splice(at, 1);
      else if (k.key === "ArrowLeft" && at > 0) at--;
      else if (k.key === "ArrowRight" && at < chars.length) at++;
      else if (k.key === "Home") while (at > 0 && chars[at - 1] !== "\n") at--;
      else if (k.key === "End") while (at < chars.length && chars[at] !== "\n") at++;
      else if (typed) chars.splice(at++, 0, k.key);
      e.words = chars.join("");
      e.caret = chars.slice(0, at).join("").length;
      paint();
    };
    window.addEventListener("keydown", down, true);
    return () => window.removeEventListener("keydown", down, true);
  });

  const at = (e: { clientX: number; clientY: number }): Point => {
    const r = canvasRef.current!.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  };
  const linesFor = (moving: string) =>
    snapLines(
      W,
      H,
      layers.filter((l) => l["id"] !== moving && !inGroup(groupOf, str(l["id"]), moving) && !w.carried?.from.has(str(l["id"])) && !holdsOthers(l)).map((l) => whereIs(l)),
    );
  // The collage's delete of a group, run by a button over it, deletes all it holds with
  // it, in one step.
  const deleteGroup = (name: string, sent: Record<string, unknown>) => {
    const id = str(sent["id"]);
    const group = layers.find((l) => str(l["id"]) === id);
    if (name !== remove || !group || !holdsOthers(group)) return false;
    void (async () => {
      let joined = false;
      for (const l of layers) if (inGroup(groupOf, str(l["id"]), id)) await runner.reply(name, { id: str(l["id"]), ...(joined ? { "one:history": "joined" } : {}) }), (joined = true);
      await runner.reply(name, { ...sent, "one:history": "joined" });
    })();
    return true;
  };
  // A group's gesture begun, or what's chosen: where it and each it holds are.
  const carryFrom = (group: string, from: Box) => {
    const carriedOne = (id: string) => (group === "chosen" ? w.chosen.some((c) => c === id || inGroup(groupOf, id, c)) : inGroup(groupOf, id, group));
    w.carried = { id: group, group: from, from: new Map(layers.filter((l) => !holdsOthers(l) && carriedOne(str(l["id"]))).map((l) => [str(l["id"]), boxOf(l)])) };
  };
  // The box around what's chosen, where each is.
  const chosenBox = () => around(layers.filter((l) => w.chosen.includes(str(l["id"]))).map(whereIs));
  // What's chosen made the screen's pick: one alone is picked; several, none is, and
  // they're drawn chosen.
  const pickChosen = () => {
    if (w.chosen.length === 1) {
      pickRow(w.chosen[0]!);
      w.chosen = [];
    } else pickRow("");
    redraw((n) => n + 1);
  };
  // A group taken to `to`: each it holds carried with it, all one step to undo.
  const placeCarried = async (to: Box) => {
    const c = w.carried;
    if (!c) return;
    let i = 0;
    for (const [id, from] of c.from) {
      const b = carried(from, c.group, to);
      await send(place!, { id, x: b.x, y: b.y, w: b.w, h: b.h, ...(i++ > 0 ? { "one:history": "joined" } : {}) });
    }
    // Once the last is answered, it's drawn from the view again.
    if (c.id === "chosen") {
      w.going = w.carried = undefined;
      paint();
    }
  };
  const goingTo = (p: Point, e: { altKey: boolean; shiftKey: boolean }): Box => {
    const g = w.now!;
    const dx = (p.x - g.at.x) / w.scale, dy = (p.y - g.at.y) / w.scale, close = 8 / w.scale;
    if (g.what === "cropping" && w.crop) return cropped(g.from, g.edges, dx, dy, w.crop.whole);
    const snaps = snap && !e.altKey;
    const to =
      g.what === "moving"
        ? moved(g.from, dx, dy, w.lines, W, H, close, snaps)
        : resized(g.from, g.corner, dx, dy, g.free ? e.shiftKey : !e.shiftKey, w.lines, close, snaps);
    w.guide = to;
    return to.at;
  };
  const pickRow = (id: string) => pick && setPicked(pick, id || undefined);
  // Whether a press at `p`, in the window, reaches a layer, its box `b`: a path by its
  // stroke, 6 pixels around it and half its width, or inside it while it's filled;
  // anything else within its box. The same as the desktop's.
  const hits = (l: Row, b: Box, p: Point) => {
    if (!isPath(l)) return inside(onBoard(b), p);
    const reach = 6 / w.scale + (str(l["stroke"]) ? num(l["stroke_width"]) / 2 : 0);
    return pathReaches(outlineOf(l, b), onCollage(p), reach, /^#[0-9a-fA-F]{6}$/.test(str(l["fill"])));
  };
  const onCollage = (p: Point): Point => ({ x: (p.x - w.board.x) / w.scale, y: (p.y - w.board.y) / w.scale });
  // A layer drawn, on top of the rest, picked once it's made; then the choices its
  // when read go back to their first, as a tool goes back to Select. The same as the
  // desktop's.
  const drawingNow = () => draws.find((one) => one.when !== false);
  // The path a pen drew, ended where it is or closed: made a layer as a line drawn is,
  // and the pen put away; fewer than two nodes draw nothing. The same as the desktop's.
  const penDone = (closed: boolean) => {
    const nodes = w.pen ?? [];
    w.pen = w.hover = undefined;
    if (nodes.length >= 2) {
      const { points, box } = reshapedAs(penPath(nodes, closed));
      void drawLayer(box, points);
    }
    paint();
  };
  // What a draw gives what it makes, and what its buttons last gave, where it takes it,
  // by the fields they're kept in.
  const givenBy = (d: CollageDraw): Row => ({
    ...d.given,
    ...Object.fromEntries(
      (d.with ?? []).flatMap((t) => {
        const last = lastGiven(t.from);
        return t.fields.filter((field) => field in last).map((field) => [field, last[field]]);
      }),
    ),
  });
  // A layer made by a draw, of these parts: in the current layer, on top of the rest,
  // picked once it's made; then the choices its when read go back to their first.
  const makeDrawn = async (d: CollageDraw, made: Row) => {
    for (const [key, first] of d.back ?? []) setChosen(key, first);
    const inLayer = await layerFor();
    if (layerField && inLayer) made[layerField] = inLayer;
    if (parent) made[parent] = subject;
    const top = layers[layers.length - 1];
    if (top && top["depth"] !== undefined) made["depth"] = num(top["depth"]) + 1;
    const reply = await runner.reply(d.command, { ...fieldsOf(made, fields), ...givenBy(d) });
    if (reply?.id) pickRow(reply.id);
  };
  // A layer drawn, as big as the drag, or a line's points.
  const drawLayer = async (b: Box, points?: string) => {
    const d = drawingNow();
    if (!d) return;
    const made: Row = { x: b.x, y: b.y, w: b.w, h: b.h };
    if (points !== undefined) made["points"] = points;
    await makeDrawn(d, made);
  };
  // Words begun where a draw by typing was pressed, a line of them centered there: as
  // large as it gives, or a twelfth of the collage's height, in what it gives and its
  // buttons last gave, by their parts' names, so they're drawn as they'll be made.
  const typingAt = (d: CollageDraw, at: Point): NonNullable<Placed["editing"]> => {
    const given = givenBy(d), made: Row = {};
    for (const [part, field] of Object.entries(fields)) if (field in given) made[part] = given[field];
    if (!(num(made["size"]) > 0)) made["size"] = Math.max(8, Math.round(H / 12));
    const m = measured("", num(made["size"]), faceOf(made));
    made["x"] = at.x;
    made["y"] = at.y - m.h / 2;
    return { id: "", words: "", caret: 0, fresh: false, unmade: made, by: d };
  };
  // Words typed are kept when the tool they're typed by is put away, or another's picked.
  if (w.editing?.unmade && (picked || drawingNow()?.by !== "typing")) finishWriting();
  const keepCrop = () => {
    const e = w.crop;
    if (!e || !crop) return;
    w.crop = undefined;
    w.going = { id: e.id, box: e.kept };
    void send(crop, {
      id: e.id,
      crop_x: (e.kept.x - e.whole.x) / e.whole.w,
      crop_y: (e.kept.y - e.whole.y) / e.whole.h,
      crop_w: e.kept.w / e.whole.w,
      crop_h: e.kept.h / e.whole.h,
      x: e.kept.x,
      y: e.kept.y,
      w: e.kept.w,
      h: e.kept.h,
    });
    redraw((n) => n + 1);
  };

  const press = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (full) {
      void document.exitFullscreen();
      return;
    }
    if (w.scale <= 0) return;
    const p = at(e);
    // With Space held, or by the middle button, a drag moves what's shown, as a drawing
    // program's hand does.
    if (spaceHeld.current || e.button === 1) {
      e.preventDefault();
      w.now = { what: "panning", id: "", from: { x: w.view.panX, y: w.view.panY, w: 0, h: 0 }, at: p, corner: 0, edges: 0 };
      e.currentTarget.setPointerCapture(e.pointerId);
      e.currentTarget.style.cursor = "grabbing";
      return;
    }
    // Words being written are kept by a press anywhere but on them.
    if (w.editing) {
      const l = w.editing.unmade ?? layers.find((r) => r["id"] === w.editing!.id);
      const box = l && (w.editing.unmade ? { ...boxOf(l), ...measured(w.editing.words, num(l["size"]), faceOf(l)) } : whereIs(l));
      if (box && inside(onBoard(box), p)) return;
      finishWriting();
    }
    // Cropping: its edges and corners trim it, inside moves it, the rest of the
    // picture shifts with it, and a press anywhere else keeps it.
    if (w.crop) {
      const edgesAt = cropEdgesAt(onBoard(w.crop.kept), p, handle);
      if (edgesAt < 0 && inside(onBoard(w.crop.whole), p)) {
        w.now = { what: "shifting", id: w.crop.id, from: w.crop.whole, at: p, corner: 0, edges: 0, keptFrom: w.crop.kept };
        e.currentTarget.setPointerCapture(e.pointerId);
        return;
      }
      if (edgesAt < 0) return keepCrop();
      w.now = { what: "cropping", id: w.crop.id, from: w.crop.kept, at: p, corner: 0, edges: edgesAt };
      e.currentTarget.setPointerCapture(e.pointerId);
      return;
    }
    // Typing: words begun where it's pressed, nothing picked meanwhile.
    const typing = drawingNow();
    if (typing?.by === "typing" && !spaceHeld.current && e.button !== 1 && inside(onBoard({ x: 0, y: 0, w: W, h: H }), p)) {
      w.editing = typingAt(typing, onCollage(p));
      pickRow("");
      return redraw((n) => n + 1);
    }
    // A pen puts down a node: a double-click ends its path, and a press on its first
    // node closes it.
    if (drawingNow()?.by === "points" && inside(onBoard({ x: 0, y: 0, w: W, h: H }), p)) {
      const pen = w.pen ?? [];
      if (e.detail >= 2 && pen.length) return penDone(false);
      if (pen.length > 2) {
        const first = onBoard({ x: pen[0]!.x, y: pen[0]!.y, w: 0, h: 0 });
        if (Math.abs(first.x - p.x) <= handle && Math.abs(first.y - p.y) <= handle) return penDone(true);
      }
      const on = onCollage(p);
      w.pen = [...pen, { x: on.x, y: on.y, handleX: 0, handleY: 0 }];
      w.now = { what: "drawing", id: "points", from: { x: 0, y: 0, w: 0, h: 0 }, at: p, corner: 0, edges: 0 };
      e.currentTarget.setPointerCapture(e.pointerId);
      return paint();
    }
    // While something's to be drawn, a press on the collage starts drawing it, over
    // whatever's there.
    if (draws.some((d) => d.when !== false) && inside(onBoard({ x: 0, y: 0, w: W, h: H }), p)) {
      w.now = { what: "drawing", id: "", from: { x: 0, y: 0, w: 0, h: 0 }, at: p, corner: 0, edges: 0 };
      w.hand = [onCollage(p)];
      e.currentTarget.setPointerCapture(e.pointerId);
      return;
    }
    // A corner of the picked layer resizes it: a picture with its shape kept, words by their size.
    // A node of the picked path reshapes it, and shows its control points, which reshape
    // it too.
    if (chosen && isPath(chosen) && reshape) {
      const outline = outlineOf(chosen, boxOf(chosen));
      const lever = w.node?.id === picked ? pathHandles(outline, w.node.at).find((h) => {
        const at = onBoard({ x: h.at.x, y: h.at.y, w: 0, h: 0 });
        return Math.abs(at.x - p.x) <= handle && Math.abs(at.y - p.y) <= handle;
      }) : undefined;
      if (lever && w.node) {
        w.now = { what: "reshaping", id: picked, from: boxOf(chosen), at: p, corner: w.node.at, edges: lever.out ? 2 : 1 };
        w.reshapeFrom = outline;
        e.currentTarget.setPointerCapture(e.pointerId);
        return;
      }
      const node = pathNodes(outline).findIndex((n) => {
        const at = onBoard({ x: n.x, y: n.y, w: 0, h: 0 });
        return Math.abs(at.x - p.x) <= handle && Math.abs(at.y - p.y) <= handle;
      });
      if (node >= 0) {
        w.now = { what: "reshaping", id: picked, from: boxOf(chosen), at: p, corner: node, edges: 0 };
        w.reshapeFrom = outline;
        w.node = { id: picked, at: node };
        paint();
        e.currentTarget.setPointerCapture(e.pointerId);
        return;
      }
    }
    // A corner of the picked one resizes it; a path has nodes, not corners.
    if (chosen && (!isPath(chosen) || holdsOthers(chosen)) && (isWords(chosen) ? write : place)) {
      const b = whereIs(chosen);
      const corner = cornerAt(onBoard(b), p, handle);
      if (corner >= 0) {
        // A group keeps its shape unless Shift, as a picture does.
        w.now = { what: "resizing", id: picked, from: b, at: p, corner, edges: 0, free: isShape(chosen) && !holdsOthers(chosen) };
        if (holdsOthers(chosen)) carryFrom(picked, b);
        w.lines = linesFor(picked);
        e.currentTarget.setPointerCapture(e.pointerId);
        return;
      }
    }
    // The top layer under the pointer is picked and moves; pressed twice, a picture is
    // cropped and words written.
    for (let i = layers.length - 1; i >= 0; i--) {
      if (holdsOthers(layers[i]!)) continue;
      let l = layers[i]!;
      let b = whereIs(l);
      if (!hits(l, b, p)) continue;
      // One a group holds is picked as the group, the outermost, or the one just inside
      // the group gone into; a press outside that leaves it. A group pressed twice is
      // gone into, as a drawing program does.
      const hit = str(l["id"]);
      if (w.entered && !inGroup(groupOf, hit, w.entered)) w.entered = undefined;
      let id = pickedBelow(groupOf, hit, w.entered ?? "");
      if (e.detail >= 2 && id !== hit) {
        w.entered = id;
        id = pickedBelow(groupOf, hit, id);
      }
      if (id !== hit) {
        l = layers.find((r) => str(r["id"]) === id) ?? l;
        b = whereIs(l);
      }
      w.node = undefined;
      // With Shift, it's chosen or let go, beside what's chosen already.
      if (e.shiftKey) {
        if (w.chosen.length === 0 && picked) w.chosen = [picked];
        w.chosen = w.chosen.includes(id) ? w.chosen.filter((c) => c !== id) : [...w.chosen, id];
        pickChosen();
        return;
      }
      // One of several chosen moves them all.
      if (w.chosen.length > 1 && w.chosen.includes(id)) {
        if (!place) return;
        const all = chosenBox();
        w.now = { what: "moving", id: "chosen", from: all, at: p, corner: 0, edges: 0 };
        carryFrom("chosen", all);
        w.lines = linesFor("chosen");
        e.currentTarget.setPointerCapture(e.pointerId);
        return;
      }
      w.chosen = [];
      pickRow(id);
      // Its layer is the current one, as new objects go in it.
      if (layerPick && layerField && str(l[layerField])) setPicked(layerPick, str(l[layerField]));
      if (e.detail >= 2 && id === hit) {
        if (isWords(l) && write) w.editing = { id, words: str(l["words"]), caret: str(l["words"]).length, fresh: false };
        else if (isPicture(l) && !vectors.has(str(l["picture"])) && crop) w.crop = cropBegun(l);
        redraw((n) => n + 1);
        return;
      }
      if (place) {
        w.now = { what: "moving", id, from: b, at: p, corner: 0, edges: 0 };
        if (holdsOthers(l)) carryFrom(id, b);
        w.lines = linesFor(id);
        e.currentTarget.setPointerCapture(e.pointerId);
      }
      return;
    }
    // On nothing, nothing is picked, and a group gone into is left; a drag from there
    // picks what it goes all the way around.
    w.node = undefined;
    w.entered = undefined;
    if (!e.shiftKey) {
      w.chosen = [];
      pickRow("");
    }
    if (!pick || !inside(onBoard({ x: 0, y: 0, w: W, h: H }), p)) return;
    const from = onCollage(p);
    w.now = { what: "selecting", id: "", from: { ...from, w: 0, h: 0 }, at: p, corner: 0, edges: 0 };
    w.marquee = { ...from, w: 0, h: 0 };
    e.currentTarget.setPointerCapture(e.pointerId);
  };
  const move = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const g = w.now;
    if (!g) return;
    const p = at(e);
    if (g.what === "panning") {
      w.view = { ...w.view, panX: g.from.x + (p.x - g.at.x), panY: g.from.y + (p.y - g.at.y) };
      paint();
      return;
    }
    if (g.what === "selecting") {
      const z = onCollage(p);
      w.marquee = { x: Math.min(g.from.x, z.x), y: Math.min(g.from.y, z.y), w: Math.abs(z.x - g.from.x), h: Math.abs(z.y - g.from.y) };
      paint();
      return;
    }
    if (g.what === "reshaping") {
      w.reshaped = { id: g.id, outline: reshapedBy(w.reshapeFrom ?? "", g, (p.x - g.at.x) / w.scale, (p.y - g.at.y) / w.scale) };
      paint();
      return;
    }
    if (g.what === "drawing") {
      if (g.id === "points") {
        // A pen's node dragged from: its handle, the way the curve leaves it.
        const node = w.pen?.[w.pen.length - 1], on = onCollage(p);
        if (node) (node.handleX = on.x - node.x), (node.handleY = on.y - node.y);
        w.hover = on;
      } else if (drawingNow()?.by === "hand") w.hand?.push(onCollage(p));
      else if (drawingNow()?.by === "line") w.line = drawnLine(onCollage(g.at), onCollage(p), e.shiftKey, 4 / w.scale, W, H);
      else w.drawing = drawn(onCollage(g.at), onCollage(p), e.shiftKey, 4 / w.scale, W, H);
    } else if (g.what === "shifting" && w.crop) {
      const dx = (p.x - g.at.x) / w.scale, dy = (p.y - g.at.y) / w.scale;
      w.crop.whole = { ...g.from, x: g.from.x + dx, y: g.from.y + dy };
      w.crop.kept = { ...g.keptFrom!, x: g.keptFrom!.x + dx, y: g.keptFrom!.y + dy };
    } else if (g.what === "cropping" && w.crop) {
      w.crop.kept = goingTo(p, e);
    } else {
      w.going = { id: g.id, box: goingTo(p, e) };
    }
    paint();
  };
  const release = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const g = w.now;
    if (!g) return;
    if (g.what === "panning") {
      w.now = undefined;
      e.currentTarget.style.cursor = spaceHeld.current ? "grab" : "default";
      return;
    }
    // What the drag went all the way around is chosen, as the groups they're in are, or
    // what's just inside the group gone into.
    if (g.what === "selecting") {
      move(e);
      w.now = undefined;
      const m = w.marquee;
      w.marquee = undefined;
      if (!m || (m.w * w.scale < 3 && m.h * w.scale < 3)) return paint();
      for (const l of layers) {
        const id = str(l["id"]), b = whereIs(l);
        if (str(l["group"]) !== (w.entered ?? "")) continue;
        if (b.x >= m.x && b.y >= m.y && b.x + b.w <= m.x + m.w && b.y + b.h <= m.y + m.h && !w.chosen.includes(id)) w.chosen = [...w.chosen, id];
      }
      return pickChosen();
    }
    if (g.what === "shifting" || g.what === "cropping") {
      move(e);
      w.now = undefined;
      return;
    }
    if (g.what === "reshaping") {
      w.now = undefined;
      const p = at(e);
      if (p.x === g.at.x && p.y === g.at.y) {
        w.reshaped = undefined;
        return paint();
      }
      const outline = reshapedBy(w.reshapeFrom ?? "", g, (p.x - g.at.x) / w.scale, (p.y - g.at.y) / w.scale);
      w.reshaped = { id: g.id, outline };
      const { points, box } = reshapedAs(outline);
      // Kept, it's drawn from the view again; turned down, as it was.
      void send(reshape!, { id: g.id, points, x: box.x, y: box.y, w: box.w, h: box.h }).finally(() => {
        w.reshaped = undefined;
        paint();
      });
      return paint();
    }
    if (g.what === "drawing" && g.id === "points") {
      // A pen's node is put down; its path goes on until it's ended.
      w.now = undefined;
      return paint();
    }
    if (g.what === "drawing") {
      w.now = undefined;
      w.drawing = w.line = undefined;
      if (drawingNow()?.by === "hand") {
        // What it went through, only as much as keeps its shape, as one curve.
        const hand = [...(w.hand ?? []), onCollage(at(e))];
        w.hand = undefined;
        const outline = freehand(hand, 4 / w.scale);
        if (outline) {
          const { points, box } = reshapedAs(outline);
          void drawLayer(box, points);
        }
      } else if (drawingNow()?.by === "line") {
        const line = drawnLine(onCollage(g.at), onCollage(at(e)), e.shiftKey, 4 / w.scale, W, H);
        drawLayer(line.at, line.points);
      } else drawLayer(drawn(onCollage(g.at), onCollage(at(e)), e.shiftKey, 4 / w.scale, W, H));
      paint();
      return;
    }
    const to = goingTo(at(e), e);
    w.now = undefined;
    w.guide = undefined;
    if (to.x === g.from.x && to.y === g.from.y && to.w === g.from.w && to.h === g.from.h) {
      w.going = undefined;
      paint();
      return;
    }
    w.going = { id: g.id, box: to };
    // Where it's taken, kept; turned down, it goes back where it was.
    const back = (made: unknown) => {
      if (made === undefined) {
        w.going = w.carried = undefined;
        paint();
      }
    };
    if (w.carried && w.carried.id === g.id) {
      void placeCarried(to).catch(() => back(undefined));
      paint();
      return;
    }
    const l = layers.find((r) => r["id"] === g.id);
    if (l && isWords(l) && g.what === "resizing" && g.from.h > 0 && write) {
      // Words resized are larger or smaller words, not stretched ones.
      const size = num(l["size"]) * (to.h / g.from.h);
      const m = measured(str(l["words"]), size, faceOf(l));
      void send(write, { id: g.id, words: l["words"], size, x: to.x, y: to.y, w: m.w, h: m.h }).then(back);
    } else {
      void send(place!, { id: g.id, x: to.x, y: to.y, w: to.w, h: to.h }).then(back);
    }
    paint();
  };
  const cursorAt = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (w.now) return;
    const p = at(e);
    let cursor = spaceHeld.current ? "grab" : "default";
    if (spaceHeld.current) return void (e.currentTarget.style.cursor = cursor);
    if (w.crop) {
      const edgesAt = cropEdgesAt(onBoard(w.crop.kept), p, handle);
      cursor =
        edgesAt < 0
          ? inside(onBoard(w.crop.whole), p)
            ? "grab"
            : "default"
          : edgesAt === 0
            ? "all-scroll"
            : edgesAt === 3 || edgesAt === 12
              ? "nwse-resize"
              : edgesAt === 6 || edgesAt === 9
                ? "nesw-resize"
                : edgesAt & 5
                  ? "ew-resize"
                  : "ns-resize";
    } else if (w.editing) cursor = "text";
    else if (draws.some((d) => d.when !== false)) cursor = drawingNow()?.by === "typing" ? "text" : "crosshair";
    else {
      const corner = chosen && !isPath(chosen) && (isWords(chosen) ? write : place) ? cornerAt(onBoard(whereIs(chosen)), p, handle) : -1;
      if (corner === 0 || corner === 2) cursor = "nwse-resize";
      else if (corner === 1 || corner === 3) cursor = "nesw-resize";
      else if (layers.some((l) => hits(l, whereIs(l), p))) cursor = place ? "all-scroll" : "pointer"; // four arrows; many themes draw move as a hand
    }
    e.currentTarget.style.cursor = cursor;
  };

  // A crop is kept with Enter and put away with Esc; otherwise Esc lets the picked
  // layer go, and the arrows nudge it, by ten with Shift.
  const busy = Boolean(w.crop || w.editing);
  useKeys(w.crop ? ["enter"] : undefined, keepCrop);
  useKeys(w.crop ? ["escape"] : undefined, () => {
    w.crop = undefined;
    redraw((n) => n + 1);
  });
  // Esc in a group gone into picks the group again.
  useKeys(picked && !busy ? ["escape"] : undefined, () => {
    const back = w.entered ?? "";
    w.entered = undefined;
    pickRow(back);
  });
  // Several chosen: Esc lets them go, and Delete deletes them, a group with all it
  // holds, in one step.
  const several = w.chosen.length > 1 && !busy;
  // While something's to be drawn and nothing's picked or under way, Esc sets the
  // choices its when reads back to their first, as a drawing program goes back to its
  // pointer.
  const backToFirst = drawingNow();
  useKeys(backToFirst && !picked && !w.pen?.length && w.chosen.length < 2 && !busy ? ["escape"] : undefined, () => {
    for (const [key, first] of backToFirst?.back ?? []) setChosen(key, first);
  });
  useKeys(several ? ["escape"] : undefined, () => {
    w.chosen = [];
    redraw((n) => n + 1);
  });
  useKeys(several && remove ? ["delete"] : undefined, () => {
    const gone = layers.filter((l) => w.chosen.some((c) => c === str(l["id"]) || inGroup(groupOf, str(l["id"]), c))).map((l) => str(l["id"]));
    w.chosen = [];
    void (async () => {
      for (const [i, id] of gone.entries()) await send(remove!, { id, ...(i > 0 ? { "one:history": "joined" } : {}) });
    })();
    redraw((n) => n + 1);
  });
  const nudge = (dx: number, dy: number) => {
    if (!chosen || !place) return;
    const b = boxOf(chosen);
    const to = { x: b.x + dx, y: b.y + dy, w: b.w, h: b.h };
    w.going = { id: picked, box: to };
    if (holdsOthers(chosen)) {
      carryFrom(picked, b);
      void placeCarried(to);
    } else void send(place, { id: picked, ...to });
    paint();
  };
  const nudging = picked && place && !busy;
  useKeys(nudging ? ["left"] : undefined, () => nudge(-1, 0));
  useKeys(nudging ? ["right"] : undefined, () => nudge(1, 0));
  useKeys(nudging ? ["up"] : undefined, () => nudge(0, -1));
  useKeys(nudging ? ["down"] : undefined, () => nudge(0, 1));
  useKeys(nudging ? ["shift+left"] : undefined, () => nudge(-10, 0));
  useKeys(nudging ? ["shift+right"] : undefined, () => nudge(10, 0));
  useKeys(nudging ? ["shift+up"] : undefined, () => nudge(0, -10));
  useKeys(nudging ? ["shift+down"] : undefined, () => nudge(0, 10));

  // The buttons over the picked layer, just above it, or below when there's no room;
  // while cropping, keep and put away.
  const overAt = frame && !full && !w.editing && (children || w.crop) ? { left: frame.x + frame.w / 2, top: frame.y - 12 } : undefined;
  const below = overAt !== undefined && overAt.top < 52;
  return (
    <div
      className="one-collage"
      style={{ position: "relative", flex: 1, minHeight: fills ? 0 : 560, width: "100%", height: fills ? "100%" : 560, outline: dropping ? "3px solid var(--color-accent, #2457d6)" : "none", outlineOffset: -3 }}
      onDragOver={
        paste
          ? (e) => {
              e.preventDefault();
              setDropping(true);
            }
          : undefined
      }
      onDragLeave={() => setDropping(false)}
      onDrop={
        paste
          ? (e) => {
              e.preventDefault();
              setDropping(false);
              const given = [...e.dataTransfer.files];
              if (given.length === 0) {
                if (e.dataTransfer.getData("text/uri-list")) say({ text: "That picture is on the web. Save it first, then drop it here." });
                return;
              }
              const p = at(e);
              const on = w.scale > 0 && inside(w.board, p) ? { x: (p.x - w.board.x) / w.scale, y: (p.y - w.board.y) / w.scale } : undefined;
              void addPictures(given, on);
            }
          : undefined
      }
    >
      <canvas
        ref={canvasRef}
        role="img"
        aria-label="A picture made of layers"
        style={{ position: "absolute", inset: 0, width: "100%", height: "100%", touchAction: "none" }}
        onPointerDown={press}
        onPointerMove={(e) => {
          if (w.now) return move(e);
          // The next segment of a pen's path follows the pointer.
          if (w.pen?.length) (w.hover = onCollage(at(e))), paint();
          cursorAt(e);
        }}
        onPointerUp={release}
        onPointerCancel={release}
      />
      {overAt && picked && (
        <div
          role="toolbar"
          aria-label="The picked layer"
          style={{
            position: "absolute",
            left: overAt.left,
            top: below ? frame!.y + frame!.h + 12 : overAt.top,
            transform: below ? "translateX(-50%)" : "translate(-50%, -100%)",
            display: "flex",
            gap: 2,
            padding: 4,
            borderRadius: 8,
            background: "var(--color-surface, #fff)",
            border: "1px solid var(--color-line, #dde2ea)",
            boxShadow: "0 2px 12px rgba(0, 0, 0, 0.22)",
          }}
        >
          <OverLayer.Provider value>
            <OverRun.Provider value={deleteGroup}>
            <OverKind.Provider value={chosen ? { picture: isPicture(chosen) && !vectors.has(str(chosen["picture"])), words: isWords(chosen), group: holdsOthers(chosen) } : undefined}>
            {w.crop ? (
              <CropTools
                keep={keepCrop}
                away={() => {
                  w.crop = undefined;
                  redraw((n) => n + 1);
                }}
              />
            ) : (
              children
            )}
            </OverKind.Provider>
            </OverRun.Provider>
          </OverLayer.Provider>
        </div>
      )}
    </div>
  );
}

// While cropping: keep it, or put it away.
function CropTools({ keep, away }: { keep: () => void; away: () => void }) {
  const ui = useUI();
  return (
    <>
      <ui.Button kind="secondary" icon="check" tool="small" hint="Keep (Enter)" onClick={keep}>
        Keep
      </ui.Button>
      <ui.Button kind="secondary" icon="close" tool="small" hint="Put away (Esc)" onClick={away}>
        Put away
      </ui.Button>
    </>
  );
}

// The depths its layers, lowest first, take after the one at `from` moves to `to`: each
// its place in the order, so no two are ever at one depth. Only those that change, by
// id. The same as the desktop's.
export function depthsMoved(layers: Row[], from: number, to: number): [string, number][] {
  if (from < 0 || to < 0 || from >= layers.length || to >= layers.length) return [];
  const order = [...layers];
  const [moving] = order.splice(from, 1);
  order.splice(to, 0, moving!);
  return order.flatMap((l, i) => (l["depth"] === undefined || l["depth"] === null || num(l["depth"]) !== i ? [[str(l["id"]), i] as [string, number]] : []));
}

// A collage's layer, or a group with all it holds, raised past the one just above it
// among those beside it, in its layer and its group, or lowered past the one just below,
// as `layers` are drawn, lowest first, their groups worked out: each that moves, by its
// id, and its new depth, its layer's renumbered from 0. Nothing at the top or the
// bottom. The same as the desktop's.
export function restacked(layers: Row[], id: string, up: boolean, layerField: string): [string, number][] {
  const row = layers.find((l) => str(l["id"]) === id);
  if (!row) return [];
  const inLayer = layerField ? str(row[layerField]) : "", inGroup = str(row["group"]);
  const groups = groupsOf(layers);
  // Each of the layer's drawn ones, and which of those beside it it's part of.
  const order: [string, string][] = [];
  const beside: string[] = [];
  for (const l of layers) {
    if (holdsOthers(l) || (layerField && str(l[layerField]) !== inLayer)) continue;
    let at = str(l["id"]), part = "";
    for (let depth = 0; depth < 64; depth++) {
      const over = groups.get(at) ?? "";
      if (over === inGroup) {
        part = at;
        break;
      }
      if (!over) break;
      at = over;
    }
    order.push([str(l["id"]), part]);
    if (part && !beside.includes(part)) beside.push(part);
  }
  const here = beside.indexOf(id);
  if (here < 0 || (up && here + 1 === beside.length) || (!up && here === 0)) return [];
  const past = beside[up ? here + 1 : here - 1]!;
  const moving = order.filter((o) => o[1] === id), rest = order.filter((o) => o[1] !== id);
  // Just after the last of the one above, or just before the first of the one below.
  const at = up ? rest.map((o) => o[1]).lastIndexOf(past) + 1 : rest.findIndex((o) => o[1] === past);
  rest.splice(at, 0, ...moving);
  const depthOf = new Map(layers.map((l) => [str(l["id"]), num(l["depth"])]));
  return rest.flatMap(([each], i) => (depthOf.get(each) !== i ? [[each, i] as [string, number]] : []));
}

// A layer alone, drawn where it is on its collage, in the collage's proportions, over a
// checkerboard when it's clear: what a list of its layers shows of each.
function LayerPreview({ objects, W, H, clear, hidden, address }: { objects: Row[]; W: number; H: number; clear: boolean; hidden: boolean; address?: (path: string) => Promise<string> }) {
  const ref = useRef<HTMLCanvasElement>(null);
  const wide = 40, tall = W > 0 && H > 0 ? Math.min(40, Math.max(12, (wide * H) / W)) : 23;
  const key = JSON.stringify(objects);
  useEffect(() => {
    const canvas = ref.current;
    const c = canvas?.getContext("2d");
    if (!canvas || !c || W <= 0 || H <= 0) return;
    let live = true;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(wide * ratio);
    canvas.height = Math.round(tall * ratio);
    const shown = objects.map((o) => {
      const one = { ...o };
      delete one["hidden"];
      return one;
    });
    void drawnWhole(shown, W, H, (wide * ratio) / W, address, false, undefined, true).then((made) => {
      if (!live) return;
      c.setTransform(1, 0, 0, 1, 0, 0);
      c.fillStyle = clear ? checkers(c) : "#fff";
      c.fillRect(0, 0, canvas.width, canvas.height);
      c.globalAlpha = hidden ? 0.35 : 1;
      c.drawImage(made, 0, 0, canvas.width, canvas.height);
      c.globalAlpha = 1;
    });
    return () => {
      live = false;
    };
  }, [key, W, H, clear, hidden]);
  return <canvas ref={ref} aria-hidden="true" style={{ width: wide, height: tall, flex: "none", border: "1px solid var(--color-line, #d8dce3)" }} />;
}

// layers drawing_page.layers { ... }: the layers of the screen's collage, topmost first,
// each an eye that hides or shows it, its preview, its name, up and down, and delete. A
// press on one picks it, as the collage does. The same as the desktop's.
export function Layers({
  view,
  list,
  width,
  height,
  fields,
  clear = false,
  pick,
  hide,
  order,
  remove,
  objects,
  objectLayer,
  objectRemove,
  objectPick,
  rename,
  objectRename,
  create,
  createParent,
  subject,
}: {
  view: ViewState;
  list: string;
  width: string;
  height: string;
  fields: Record<string, string>;
  clear?: boolean;
  pick?: string;
  hide?: string;
  order?: string;
  remove?: string;
  // Its rows are the layers the collage's objects are held in: their list, the field
  // each names its layer by, how one's deleted, a layer's going with it, and the
  // screen's pick of one.
  objects?: string;
  objectLayer?: string;
  objectRemove?: string;
  objectPick?: string;
  // The commands a name written in place is sent to: a layer's, and an object's.
  rename?: string;
  objectRename?: string;
  // A new layer made from its header's +, by the layers' create, named what the view is
  // per by `createParent`.
  create?: string;
  createParent?: string;
  subject?: string;
}) {
  const ui = useUI();
  const files = useFiles();
  const runner = useRunner();
  const picked = usePickedId(pick ?? "") ?? "";
  const pickedObject = usePickedId(objectPick ?? "") ?? "";
  const [open, setOpen] = useState<Set<string>>(() => new Set());
  // The row pointed at, whose delete shows; and the name being written in place.
  const [hovered, setHovered] = useState("");
  const [renaming, setRenaming] = useState<{ id: string; words: string; command: string; mapped: boolean } | undefined>();
  // A row held and dragged: where it began, which row it is, and the row it's over.
  const [dragging, setDragging] = useState<{ id: string; y: number; moving: boolean; over?: string } | undefined>();
  const data = view.status === "live" ? partsOf((view.data ?? {}) as Row, objects ?? list, fields) : {};
  const layers = (data[list] as Row[] | undefined) ?? [];
  const W = num(data[width]), H = num(data[height]);
  const address = files ? (path: string) => files.address(path) : undefined;
  const send = (command: string, input: Row) => runner.reply(command, fieldsOf(input, fields));
  if (view.status !== "live") return <p>Loading…</p>;
  if (layers.length === 0) return <p style={{ color: "var(--color-muted)" }}>No layers yet.</p>;
  const nameOf = (l: Row) => {
    if (holdsOthers(l)) return "Group";
    if (isWords(l)) return str(l["words"]).split("\n")[0]!;
    if (isPicture(l)) return pictures.get(str(l["picture"])) === "vector" ? "SVG" : "Picture";
    // A path of two points is a line.
    const shape = str(l["shape"]), points = str(l["points"]);
    const line = (points.match(/L/g) ?? []).length === 1 && !points.includes("C");
    return shape === "ellipse" ? "Ellipse" : shape === "path" ? (line ? "Line" : "Path") : "Rectangle";
  };
  const rowStyle = (isPicked: boolean, indent: number): React.CSSProperties => ({
    display: "flex",
    alignItems: "center",
    gap: 4,
    padding: 4,
    paddingLeft: 4 + indent,
    borderRadius: 6,
    cursor: "pointer",
    border: `1px solid ${isPicked ? "var(--color-accent, #2457d6)" : "transparent"}`,
    background: isPicked ? "var(--color-sunken, #f1f3f6)" : undefined,
  });
  const quiet = (hidden: boolean, soft = false) => ({
    flex: 1,
    minWidth: 0,
    overflow: "hidden",
    textOverflow: "ellipsis",
    whiteSpace: "nowrap" as const,
    fontSize: soft ? "0.6875rem" : "0.75rem",
    fontWeight: 400,
    color: hidden || soft ? "var(--color-muted)" : undefined,
  });
  // A name in the list: a double-click, given a command to send it to, writes it in
  // place, Enter or leaving it keeping it and Esc putting it away. The same as the
  // desktop's.
  const nameField = (id: string, name: string, style: React.CSSProperties, command: string | undefined, mapped: boolean) => {
    if (renaming && renaming.id === id) {
      const keep = () => {
        const sent: Row = { id, name: renaming.words };
        void runner.reply(renaming.command, renaming.mapped ? fieldsOf(sent, fields) : sent);
        setRenaming(undefined);
      };
      return (
        <input
          autoFocus
          aria-label="Name"
          value={renaming.words}
          onClick={(e) => e.stopPropagation()}
          onChange={(e) => setRenaming({ ...renaming, words: e.target.value })}
          onKeyDown={(e) => {
            e.stopPropagation();
            if (e.key === "Enter") keep();
            else if (e.key === "Escape") setRenaming(undefined);
          }}
          onBlur={keep}
          style={{ ...style, font: "inherit", fontSize: style.fontSize, padding: "0 2px", border: "1px solid var(--color-accent, #2457d6)", borderRadius: 4, background: "var(--color-surface, #fff)" }}
        />
      );
    }
    return (
      <span style={style} onDoubleClick={command ? () => setRenaming({ id, words: name, command, mapped }) : undefined}>
        {name}
      </span>
    );
  };
  const hover = (id: string) => ({ onMouseEnter: () => setHovered(id), onMouseLeave: () => setHovered((was) => (was === id ? "" : was)) });
  // A row held and dragged goes where it's let go, in one step: past a few pixels it
  // moves, a line showing where; a press let go where it began is a click. `ids` are the
  // movable rows' ids, topmost first, and `move` takes their places in that order. The
  // same as the desktop's.
  const draggable = (id: string, ids: string[], move: (from: number, to: number) => void) => ({
    onPointerDown: (e: React.PointerEvent) => {
      if (!order || e.button !== 0) return;
      setDragging({ id, y: e.clientY, moving: false });
    },
    onPointerMove: (e: React.PointerEvent) => {
      if (!dragging) return;
      const moving = dragging.moving || Math.abs(e.clientY - dragging.y) > 4;
      // The row under the pointer, of those that move.
      const under = (document.elementsFromPoint?.(e.clientX, e.clientY) ?? []).map((n) => n.closest("[data-row]")?.getAttribute("data-row")).find((one) => one && ids.includes(one));
      if (moving !== dragging.moving || under !== dragging.over) setDragging({ ...dragging, moving, over: under ?? dragging.over });
    },
    onPointerUp: () => {
      const was = dragging;
      setDragging(undefined);
      if (!was?.moving || !was.over || was.over === was.id) return;
      move(ids.indexOf(was.id), ids.indexOf(was.over));
    },
    "data-row": id,
    style: dragging?.moving && dragging.over === id && dragging.id !== id ? { boxShadow: `inset 0 ${ids.indexOf(id) > ids.indexOf(dragging.id) ? -2 : 2}px 0 var(--color-accent, #2457d6)` } : undefined,
  });
  const merged = (base: React.CSSProperties, extra?: React.CSSProperties): React.CSSProperties => ({ ...base, ...extra });
  // A row of the list: its eye in a column of its own, a thin line, then the row itself.
  const withEye = (eye: React.ReactNode, row: React.ReactNode) => (
    <div style={{ display: "flex", alignItems: "center", gap: 4 }}>
      <span style={{ width: 20, flex: "none", display: "flex", justifyContent: "center" }}>{eye}</span>
      <span aria-hidden="true" style={{ width: 1, height: 20, flex: "none", background: "var(--color-line, #d8dce3)" }} />
      <div style={{ flex: 1, minWidth: 0 }}>{row}</div>
    </div>
  );
  // Its header, thin: its name, and a + that makes a new layer on top.
  const header = (top: number, count: number) => (
    <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "0 4px 4px", fontSize: "0.75rem", fontWeight: 600, color: "var(--color-muted)" }}>
      <span>Layers</span>
      {create && (
        <ui.Button
          kind="secondary"
          icon="add"
          tool="tiny"
          hint="New layer"
          onClick={() => {
            const made: Row = { name: `Layer ${count + 1}`, depth: top + 1 };
            if (createParent) made[createParent] = subject;
            void runner.reply(create, made).then((reply) => reply?.id && pick && setPicked(pick, reply.id));
          }}
        >
          New layer
        </ui.Button>
      )}
    </div>
  );
  // Shown only while its row is pointed at, keeping its place so nothing moves.
  const onHover = (id: string): React.CSSProperties => ({ display: "flex", visibility: hovered === id ? "visible" : "hidden" });
  // The layers objects are held in, topmost first: each its eye, a preview of its
  // objects, its name, up, down and delete, and, opened, its objects under it. The same
  // as the desktop's.
  if (objects && objectLayer) {
    const groups = [...layers].sort((a, b) => num(a["depth"]) - num(b["depth"]));
    const held = ((data[objects] as Row[] | undefined) ?? []).filter((o) => !hiddenLayer(o));
    const send = (command: string, input: Row) => runner.reply(command, input);
    const ids = [...groups].reverse().map((g) => str(g["id"]));
    const moveLayer = (from: number, to: number) =>
      depthsMoved(groups, groups.length - 1 - from, groups.length - 1 - to).forEach(([moved, depth], n) => void send(order!, n === 0 ? { id: moved, depth } : { id: moved, depth, "one:history": "joined" }));
    return (
      <div>
      {header(groups.reduce((most, g) => Math.max(most, num(g["depth"])), 0), groups.length)}
      <div role="list" aria-label="Layers" style={{ display: "flex", flexDirection: "column", gap: 2, userSelect: "none" }}>
        {groups
          .map((g) => ({ g }))
          .reverse()
          .flatMap(({ g }) => {
            const id = str(g["id"]), hidden = hiddenLayer(g), opened = open.has(id);
            const mine = grouped(held.filter((o) => str(o[objectLayer]) === id));
            const flip = () => setOpen((was) => (was.has(id) ? new Set([...was].filter((one) => one !== id)) : new Set([...was, id])));
            const rows = [
              <div key={id}>
                {withEye(
                  hide && (
                    <ui.Button kind="secondary" icon={hidden ? "eye_off" : "eye"} tool="tiny" hint={hidden ? "Show" : "Hide"} onClick={() => void send(hide, { id, hidden: hidden ? 0 : 1 })}>
                      {hidden ? "Show" : "Hide"}
                    </ui.Button>
                  ),
                  <div role="listitem" aria-current={id === picked || undefined} onClick={() => pick && setPicked(pick, id)} {...hover(id)} {...draggable(id, ids, moveLayer)} style={merged(rowStyle(id === picked, 0), draggable(id, ids, moveLayer).style)}>
                    <span onClick={(e) => e.stopPropagation()} style={{ display: "flex" }}>
                      {/* Opened or closed by a small caret, so its name keeps its room. */}
                      <button type="button" aria-label={opened ? "Close" : "Open"} title={opened ? "Close" : "Open"} onClick={flip} style={{ width: 16, padding: 0, border: 0, background: "none", color: "inherit", cursor: "pointer" }}>
                        <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
                          <path d={opened ? "M4 6l4 4 4-4" : "M6 4l4 4-4 4"} />
                        </svg>
                      </button>
                    </span>
                    <LayerPreview objects={mine} W={W} H={H} clear={clear} hidden={hidden} address={address} />
                    {nameField(id, str(g["name"]) || "Layer", quiet(hidden), rename, false)}
                    {remove && (
                      <span onClick={(e) => e.stopPropagation()} style={onHover(id)}>
                        <ui.Button
                          kind="secondary"
                          icon="delete"
                          tool="tiny"
                          hint="Delete"
                          onClick={() => {
                            // Its objects go with it, in one step.
                            const gone = objectRemove ? mine : [];
                            gone.forEach((o, n) => void send(objectRemove!, n === 0 ? { id: str(o["id"]) } : { id: str(o["id"]), "one:history": "joined" }));
                            void send(remove, gone.length ? { id, "one:history": "joined" } : { id });
                          }}
                        >
                          Delete
                        </ui.Button>
                      </span>
                    )}
                  </div>,
                )}
              </div>,
            ];
            // Its objects, topmost first, a group's opened under it, each further in.
            const mineGroups = groupsOf(mine);
            const objectRows = (within: string, indent: number, depth: number) => {
              if (depth > 32) return;
              for (const o of [...mine].reverse()) {
                if (str(o["group"]) !== within) continue;
                const object = str(o["id"]), group = holdsOthers(o), shown = open.has(object);
                const flipGroup = () => setOpen((was) => (was.has(object) ? new Set([...was].filter((one) => one !== object)) : new Set([...was, object])));
                // A group's all it holds go with it, in one step.
                const gone = [object, ...mine.filter((h) => inGroup(mineGroups, str(h["id"]), object)).map((h) => str(h["id"]))];
                rows.push(
                  <div key={object}>
                  {withEye(
                  null,
                  <div
                    role="listitem"
                    aria-current={object === pickedObject || undefined}
                    onClick={() => {
                      if (objectPick) setPicked(objectPick, object);
                      if (pick) setPicked(pick, id);
                    }}
                    style={rowStyle(object === pickedObject, indent)}
                    {...hover(object)}
                  >
                    {group && (
                      <span onClick={(e) => e.stopPropagation()} style={{ display: "flex" }}>
                        <button type="button" aria-label={shown ? "Close" : "Open"} title={shown ? "Close" : "Open"} onClick={flipGroup} style={{ width: 16, padding: 0, border: 0, background: "none", color: "inherit", cursor: "pointer" }}>
                          <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
                            <path d={shown ? "M4 6l4 4 4-4" : "M6 4l4 4-4 4"} />
                          </svg>
                        </button>
                      </span>
                    )}
                    {nameField(object, str(o["name"]) || nameOf(o), quiet(hidden, true), objectRename, true)}
                    {objectRemove && (
                      <span onClick={(e) => e.stopPropagation()} style={onHover(object)}>
                        <ui.Button
                          kind="secondary"
                          icon="delete"
                          tool="tiny"
                          hint="Delete"
                          onClick={() => gone.forEach((each, n) => void runner.reply(objectRemove, n === 0 ? { id: each } : { id: each, "one:history": "joined" }))}
                        >
                          Delete
                        </ui.Button>
                      </span>
                    )}
                  </div>,
                  )}
                  </div>,
                );
                if (group && shown) objectRows(object, indent + 16, depth + 1);
              }
            };
            if (opened) objectRows("", 20, 0);
            return rows;
          })}
      </div>
      </div>
    );
  }
  const move = (from: number, to: number) => {
    depthsMoved(layers, from, to).forEach(([id, depth], i) => void send(order!, i === 0 ? { id, depth } : { id, depth, "one:history": "joined" }));
  };

  const ids = [...layers].reverse().map((l) => str(l["id"]));
  return (
    <div>
    {header(0, 0)}
    <div role="list" aria-label="Layers" style={{ display: "flex", flexDirection: "column", gap: 2, userSelect: "none" }}>
      {layers
        .map((l) => ({ l }))
        .reverse()
        .map(({ l }) => {
          const id = str(l["id"]), hidden = hiddenLayer(l);
          const isPicked = id === picked;
          return (
            <div key={id}>
              {withEye(
                hide && (
                  <ui.Button kind="secondary" icon={hidden ? "eye_off" : "eye"} tool="tiny" hint={hidden ? "Show" : "Hide"} onClick={() => void send(hide, { id, hidden: hidden ? 0 : 1 })}>
                    {hidden ? "Show" : "Hide"}
                  </ui.Button>
                ),
                <div
                  role="listitem"
                  aria-current={isPicked || undefined}
                  onClick={() => pick && setPicked(pick, id)}
                  {...hover(id)}
                  {...draggable(id, ids, (from, to) => move(layers.length - 1 - from, layers.length - 1 - to))}
                  style={merged(rowStyle(isPicked, 0), draggable(id, ids, () => {}).style)}
                >
                  <LayerPreview objects={[l]} W={W} H={H} clear={clear} hidden={hidden} address={address} />
                  {nameField(id, str(l["name"]) || nameOf(l), quiet(hidden), rename, true)}
                  {remove && (
                    <span onClick={(e) => e.stopPropagation()} style={onHover(id)}>
                      <ui.Button kind="secondary" icon="delete" tool="tiny" hint="Delete" onClick={() => void send(remove, { id })}>
                        Delete
                      </ui.Button>
                    </span>
                  )}
                </div>,
              )}
            </div>
          );
        })}
    </div>
    </div>
  );
}

// A collage's objects held in layers, as it draws them: in their layers' order, the
// lowest layer's first, each layer's in their own; those of a hidden layer left out,
// and those of none, or of one that isn't there, first. The same as the desktop's.
export function inLayers(objects: Row[], layers: Row[], field: string): Row[] {
  const order = [...layers].sort((a, b) => num(a["depth"]) - num(b["depth"]));
  const place = new Map(order.map((l, i) => [str(l["id"]), { at: i + 1, hidden: hiddenLayer(l) }]));
  return objects
    .map((o) => ({ o, found: place.get(str(o[field])) }))
    .filter(({ found }) => !found?.hidden)
    .map(({ o, found }, i) => ({ o, at: found?.at ?? 0, i }))
    .sort((a, b) => a.at - b.at || a.i - b.i)
    .map(({ o }) => o);
}

// A collage's layers with their groups worked out: one that others name as their group
// holds them, "holds" saying how many, and its place is the box around all it holds,
// however deep; one naming a group that isn't there, or itself, is in none. The same as
// the desktop's.
export function grouped(layers: Row[]): Row[] {
  const ids = new Set(layers.map((l) => str(l["id"])));
  const rows = layers.map((l) => {
    const group = str(l["group"]);
    return group && (!ids.has(group) || group === str(l["id"])) ? { ...l, group: "" } : l;
  });
  const held = new Map<string, Row[]>();
  for (const r of rows) if (str(r["group"])) held.set(str(r["group"]), [...(held.get(str(r["group"])) ?? []), r]);
  if (held.size === 0) return rows;
  const working = new Set<string>();
  const boxOfRow = (r: Row): Box => {
    const id = str(r["id"]);
    const inside = held.get(id);
    if (!inside || working.has(id)) return { x: num(r["x"]), y: num(r["y"]), w: num(r["w"]), h: num(r["h"]) };
    working.add(id);
    const b = around(inside.map(boxOfRow));
    working.delete(id);
    return b;
  };
  return rows.map((r) => {
    const inside = held.get(str(r["id"]));
    if (!inside) return r;
    const b = boxOfRow(r);
    return { ...r, x: b.x, y: b.y, w: b.w, h: b.h, holds: inside.length };
  });
}
// Whether a layer is a group, holding others.
export const holdsOthers = (l: Row) => num(l["holds"]) > 0;
// The group each is in, by its id.
const groupsOf = (rows: Row[]) => new Map(rows.filter((r) => str(r["group"])).map((r) => [str(r["id"]), str(r["group"])]));
// Whether one is in a group, however deep; and the outermost it's in, or itself.
function inGroup(groups: Map<string, string>, id: string, group: string): boolean {
  for (let depth = 0, at = groups.get(id); at !== undefined && depth < 64; at = groups.get(at), depth++) if (at === group) return true;
  return false;
}
// What's picked by a press on `id`: the group it's in just inside `top`, or the
// outermost when `top` is none, or itself. The same as the desktop's.
function pickedBelow(groups: Map<string, string>, id: string, top: string): string {
  for (let depth = 0; depth < 64; depth++) {
    const up = groups.get(id);
    if (up === undefined || up === top) break;
    id = up;
  }
  return id;
}

// The layer new objects go in: the one picked, while it's there, or the topmost.
export function currentLayer(layers: Row[], picked: string): string {
  if (layers.some((l) => str(l["id"]) === picked)) return picked;
  let top = "", highest = 0;
  for (const l of layers) if (!top || num(l["depth"]) >= highest) (top = str(l["id"])), (highest = num(l["depth"]));
  return top;
}
