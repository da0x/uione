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

import { useEffect, useLayoutEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import type { ReactNode } from "react";
import { colorShifter, cornerAt, pathIn, cropEdgesAt, cropped, drawn, drawnLine, halo, haloInside, haloReach, harmonicFill, matched, matchingCast, matchingStrength, moved, resized, sitting, snapLines, statsOf, svgDocument, wholeOf } from "./collage.js";
import type { Box, ColorShift, ColorStats, Guided, Lines, Point, SavedLayer } from "./collage.js";
import { useFiles, useRunner } from "./data.js";
import { pictureSize, svgIn, svgSize } from "./vectors.js";
import type { ViewState } from "./data.js";
import { useUI } from "./ui.js";
import { useSaying } from "./said.js";
import { useSite } from "./app.js";
import { OverLayer, choosePictures, hintOf, setChosen, setPicked, typing, useKeys, useOff, usePickedId, useTool } from "./workspace.js";

type Row = Record<string, unknown>;

const handle = 10; // a corner's handle, in the window
const num = (v: unknown, otherwise = 0) => (typeof v === "number" && Number.isFinite(v) ? v : typeof v === "string" && v.trim() !== "" && Number.isFinite(Number(v)) ? Number(v) : otherwise);
const str = (v: unknown) => (typeof v === "string" ? v : "");
const boxOf = (l: Row): Box => ({ x: num(l["x"]), y: num(l["y"]), w: num(l["w"]), h: num(l["h"]) });
const cropOf = (l: Row): Box => ({ x: num(l["crop_x"], 0), y: num(l["crop_y"], 0), w: num(l["crop_w"], 1), h: num(l["crop_h"], 1) });
// What a layer is: a picture, when it has one; words, when it has those; or else a shape,
// as its parts say (decision 0025). The same as the desktop's.
const isPicture = (l: Row) => str(l["picture"]) !== "";
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
async function drawnWhole(layers: Row[], W: number, H: number, scale: number, address: ((path: string) => Promise<string>) | undefined, matching: boolean, only?: string): Promise<HTMLCanvasElement> {
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
  if (!only) {
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
async function svgOf(layers: Row[], W: number, H: number, address: ((path: string) => Promise<string>) | undefined, only?: string): Promise<string> {
  const one = only ? layers.find((l) => l["id"] === only) : undefined;
  const area = one ? boxOf(one) : { x: 0, y: 0, w: W, h: H };
  const saved: SavedLayer[] = [];
  for (const l of layers) {
    if (only && l["id"] !== only) continue;
    const b = boxOf(l);
    const at = { x: b.x - area.x, y: b.y - area.y, w: b.w, h: b.h };
    const opacity = opacityOf(l);
    if (isWords(l)) {
      saved.push({ kind: "words", at, opacity, words: str(l["words"]), color: str(l["color"]), family: wordsFamily(), size: num(l["size"]), weight: weightOf(l), italic: num(l["italic"]) !== 0 });
    } else if (isShape(l)) {
      const drawn = { kind: str(l["shape"]) || "rectangle", points: str(l["points"]), fill: str(l["fill"]), stroke: str(l["stroke"]), strokeWidth: str(l["stroke"]) ? num(l["stroke_width"]) : 0, corners: num(l["corners"]) };
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

// What a Paste line's ctrl+v pasted, or an add pictures button chose, handed to its
// collage as it came: a browser gives a paste's pictures with the paste itself,
// without asking to read the clipboard.
const pastes = new Map<string, { files: File[]; words: string }>();

// crop "Crop", edit words "Edit", add words "Text": what's asked of the screen's collage.
export function CollageButton({ collage, does, label, icon, keys }: { collage: string; does: "crop" | "edit" | "add" | "add pictures" | "paste" | "screenshot" | "copy" | "copy picked" | "save" | "save picked" | "copy svg" | "copy svg picked" | "save svg" | "save svg picked"; label: string; icon?: string; keys?: string[] }) {
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
  what: "moving" | "resizing" | "cropping" | "shifting" | "drawing";
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
  editing?: { id: string; words: string; caret: number; fresh: boolean };
  drawing?: Box; // a layer being drawn, as big as the drag so far
  line?: { at: Box; points: string }; // or a line, as far as the drag has gone
}

// draw layer::create { fill = "#2e86de" } when tool == tool::box: a drag makes a layer
// by `command`, as big as the drag, given these values, while `when` holds; then each
// choice it read, by its key, goes back to its first.
export interface CollageDraw {
  command: string;
  given?: Record<string, unknown>;
  when?: boolean;
  by?: "line"; // a line from where the drag began to where it ended, as a path
  back?: [string, string][];
}

export function Collage({
  view,
  list,
  width,
  height,
  fields,
  place,
  snap = false,
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
  snap?: boolean;
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
  const placed = useRef<Placed>({ board: { x: 0, y: 0, w: 0, h: 0 }, scale: 0, lines: { across: [], down: [] } });
  const [, setRead] = useState(0);
  const [, redraw] = useState(0);
  const [frame, setFrame] = useState<Box | undefined>();
  const [full, setFull] = useState(() => typeof document !== "undefined" && Boolean(document.fullscreenElement));
  const [dropping, setDropping] = useState(false);

  // Read as its parts, whichever of the app's fields keep them.
  const kept = JSON.stringify(fields);
  const data = useMemo(() => (view.status === "live" ? partsOf((view.data ?? {}) as Row, list, fields) : {}), [view.status, view.data, list, kept]);
  const send = (command: string, input: Row) => runner.reply(command, fieldsOf(input, fields));
  const layers = (data[list] as Row[] | undefined) ?? [];
  const W = num(data[width]), H = num(data[height]);
  const address = files ? (path: string) => files.address(path) : undefined;
  const chosen = layers.find((l) => l["id"] === picked);
  const w = placed.current;

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
    const top = layers[layers.length - 1];
    let depth = top && top["depth"] !== undefined ? num(top["depth"]) + 1 : undefined;
    let covered = layers.length > 0, at = 0, first = true;
    for (const file of given) {
      const size = await pictureSize(file);
      if (!size) {
        say({ text: `${file.name} isn't a picture this can read.`, problem: true });
        continue;
      }
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
      if (parent) made[parent] = subject;
      if (depth !== undefined) made["depth"] = depth++;
      if ("picture_width" in fields) (made["picture_width"] = size.w), (made["picture_height"] = size.h);
      if (!first) made["one:history"] = "joined";
      first = false;
      const reply = await send(add, made);
      if (reply?.id && pick) setPicked(pick, reply.id);
    }
  };
  // Words pasted: a layer of them in the middle, a twelfth of its height; or, when
  // they're an SVG's, as a drawing program copies one, that SVG, as one dropped is.
  const addWords = (words: string) => {
    const start = words.replace(/^\uFEFF/, "").trimStart();
    if (paste && start.startsWith("<") && words.includes("<svg")) return void addPictures([new File([words], "pasted.svg", { type: "image/svg+xml" })]);
    if (!add || !("words" in fields)) return;
    const size = Math.max(8, Math.round(H / 12));
    const m = measured(words, size, "700");
    const made: Record<string, unknown> = { words, size, x: (W - m.w) / 2, y: (H - m.h) / 2, w: m.w, h: m.h };
    if (parent) made[parent] = subject;
    const top = layers[layers.length - 1];
    if (top && top["depth"] !== undefined) made["depth"] = num(top["depth"]) + 1;
    void send(add, made);
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
        void drawnWhole(layers, W, H, previewWide / W, address, Boolean(matchColors))
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
  if (w.editing && w.editing.id !== picked) finishWriting();

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
    } else if (asking === "edit" && chosen && isWords(chosen) && write) {
      w.editing = { id: picked, words: str(chosen["words"]), caret: str(chosen["words"]).length, fresh: false };
    } else if (asking.startsWith("copy svg") || asking.startsWith("save svg")) {
      // As SVG, from its vectors: copied as its text, which drawing programs paste, or saved.
      const one = asking.endsWith("picked"), copying = asking.startsWith("copy");
      if (one && !chosen) say({ text: "Pick a layer first." });
      else if (layers.length === 0 || W <= 0 || H <= 0) say({ text: copying ? "There's nothing to copy yet." : "There's nothing to save yet." });
      else {
        const svg = svgOf(layers, W, H, address, one ? picked : undefined);
        if (copying) {
          void svg
            .then((text) => navigator.clipboard.writeText(text))
            .then(() => say({ text: one ? "Copied the layer as SVG. Paste it anywhere." : "Copied the drawing as SVG. Paste it anywhere." }))
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
        const png = drawnWhole(layers, W, H, 1, address, Boolean(matchColors), one ? picked : undefined).then(
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
      void send(add, made).then((reply) => {
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
      if (near(b.x, g.box.x) && near(b.y, g.box.y) && near(b.w, g.box.w) && near(b.h, g.box.h) && !w.now) w.going = undefined;
      else b = g.box;
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
    const margin = full ? 0 : 32;
    const scale = Math.min((cw - 2 * margin) / W, (ch - 2 * margin) / H);
    if (scale <= 0) return;
    const bw = W * scale, bh = H * scale;
    const board = { x: (cw - bw) / 2, y: (ch - bh) / 2, w: bw, h: bh };
    w.board = board;
    w.scale = scale;
    c.save();
    if (!full) {
      c.shadowColor = "rgba(0, 0, 0, 0.18)";
      c.shadowBlur = 14;
      c.shadowOffsetY = 2;
    }
    c.fillStyle = "#fff";
    c.fillRect(board.x, board.y, bw, bh);
    c.restore();
    const shifts = shiftsOf(layers, matchColors, address);
    let framed: Box | undefined;
    let framedRadius = 0;
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
        paintShape(c, l, b, whereIs(l));
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
    // A layer being drawn, as big as the drag so far.
    if (w.drawing) {
      const d = onBoard(w.drawing);
      c.strokeStyle = accent;
      c.lineWidth = 1.5;
      c.strokeRect(d.x, d.y, d.w, d.h);
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
    if (layers.length === 0 && !full && empty) {
      c.fillStyle = style.getPropertyValue("--color-muted").trim() || "#5b6577";
      c.font = `13px ${wordsFamily()}`;
      c.textAlign = "center";
      c.textBaseline = "middle";
      c.fillText(empty, board.x + bw / 2, board.y + bh / 2);
      c.textAlign = "start";
    }
    // The picked layer's frame and corners, over everything, a crop's edges too.
    if (framed) {
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

  // Words being written take every key: Enter or Esc keeps them, Shift+Enter starts a
  // new line, the arrows, Home and End move the caret, and the rest is typed.
  useEffect(() => {
    if (!w.editing) return;
    const down = (k: KeyboardEvent) => {
      const e = w.editing;
      if (!e) return;
      k.preventDefault();
      k.stopPropagation();
      if (k.key === "Escape" || (k.key === "Enter" && !k.shiftKey)) {
        finishWriting();
        redraw((n) => n + 1);
        return;
      }
      const typed = k.key.length === 1 && !k.ctrlKey && !k.metaKey && !k.altKey;
      const newLine = k.key === "Enter" && k.shiftKey;
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
      layers.filter((l) => l["id"] !== moving).map((l) => whereIs(l)),
    );
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
  // Where a press reaches a layer, as drawn: its box, or a path's 6 pixels around it,
  // so a thin line can be picked. The same as the desktop's.
  const reached = (l: Row, b: Box): Box => (isShape(l) && str(l["shape"]) === "path" ? { x: b.x - 6, y: b.y - 6, w: b.w + 12, h: b.h + 12 } : b);
  const onCollage = (p: Point): Point => ({ x: (p.x - w.board.x) / w.scale, y: (p.y - w.board.y) / w.scale });
  // A layer drawn, on top of the rest, picked once it's made; then the choices its
  // when read go back to their first, as a tool goes back to Select. The same as the
  // desktop's.
  const drawingNow = () => draws.find((one) => one.when !== false);
  const drawLayer = (b: Box, points?: string) => {
    const d = drawingNow();
    if (!d) return;
    const made: Row = { x: b.x, y: b.y, w: b.w, h: b.h };
    if (points !== undefined) made["points"] = points;
    if (parent) made[parent] = subject;
    const top = layers[layers.length - 1];
    if (top && top["depth"] !== undefined) made["depth"] = num(top["depth"]) + 1;
    void runner.reply(d.command, { ...fieldsOf(made, fields), ...d.given }).then((reply) => {
      if (reply?.id) pickRow(reply.id);
    });
    for (const [key, first] of d.back ?? []) setChosen(key, first);
  };
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
    // Words being written are kept by a press anywhere but on them.
    if (w.editing) {
      const l = layers.find((r) => r["id"] === w.editing!.id);
      if (l && inside(onBoard(whereIs(l)), p)) return;
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
    // While something's to be drawn, a press on the collage starts drawing it, over
    // whatever's there.
    if (draws.some((d) => d.when !== false) && inside(onBoard({ x: 0, y: 0, w: W, h: H }), p)) {
      w.now = { what: "drawing", id: "", from: { x: 0, y: 0, w: 0, h: 0 }, at: p, corner: 0, edges: 0 };
      e.currentTarget.setPointerCapture(e.pointerId);
      return;
    }
    // A corner of the picked layer resizes it: a picture with its shape kept, words by their size.
    if (chosen && (isWords(chosen) ? write : place)) {
      const b = whereIs(chosen);
      const corner = cornerAt(onBoard(b), p, handle);
      if (corner >= 0) {
        w.now = { what: "resizing", id: picked, from: b, at: p, corner, edges: 0, free: isShape(chosen) };
        w.lines = linesFor(picked);
        e.currentTarget.setPointerCapture(e.pointerId);
        return;
      }
    }
    // The top layer under the pointer is picked and moves; pressed twice, a picture is
    // cropped and words written.
    for (let i = layers.length - 1; i >= 0; i--) {
      const l = layers[i]!;
      const b = whereIs(l);
      if (!inside(reached(l, onBoard(b)), p)) continue;
      const id = str(l["id"]);
      pickRow(id);
      if (e.detail >= 2) {
        if (isWords(l) && write) w.editing = { id, words: str(l["words"]), caret: str(l["words"]).length, fresh: false };
        else if (isPicture(l) && !vectors.has(str(l["picture"])) && crop) w.crop = cropBegun(l);
        redraw((n) => n + 1);
        return;
      }
      if (place) {
        w.now = { what: "moving", id, from: b, at: p, corner: 0, edges: 0 };
        w.lines = linesFor(id);
        e.currentTarget.setPointerCapture(e.pointerId);
      }
      return;
    }
    // On nothing, nothing is picked.
    pickRow("");
  };
  const move = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const g = w.now;
    if (!g) return;
    const p = at(e);
    if (g.what === "drawing") {
      if (drawingNow()?.by === "line") w.line = drawnLine(onCollage(g.at), onCollage(p), e.shiftKey, 4 / w.scale, W, H);
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
    if (g.what === "shifting" || g.what === "cropping") {
      move(e);
      w.now = undefined;
      return;
    }
    if (g.what === "drawing") {
      w.now = undefined;
      w.drawing = w.line = undefined;
      if (drawingNow()?.by === "line") {
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
        w.going = undefined;
        paint();
      }
    };
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
    let cursor = "default";
    if (w.crop) {
      const edgesAt = cropEdgesAt(onBoard(w.crop.kept), p, handle);
      cursor =
        edgesAt < 0
          ? inside(onBoard(w.crop.whole), p)
            ? "grab"
            : "default"
          : edgesAt === 0
            ? "move"
            : edgesAt === 3 || edgesAt === 12
              ? "nwse-resize"
              : edgesAt === 6 || edgesAt === 9
                ? "nesw-resize"
                : edgesAt & 5
                  ? "ew-resize"
                  : "ns-resize";
    } else if (w.editing) cursor = "text";
    else if (draws.some((d) => d.when !== false)) cursor = "crosshair";
    else {
      const corner = chosen && (isWords(chosen) ? write : place) ? cornerAt(onBoard(whereIs(chosen)), p, handle) : -1;
      if (corner === 0 || corner === 2) cursor = "nwse-resize";
      else if (corner === 1 || corner === 3) cursor = "nesw-resize";
      else if (layers.some((l) => inside(reached(l, onBoard(whereIs(l))), p))) cursor = place ? "move" : "pointer";
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
  useKeys(picked && !busy ? ["escape"] : undefined, () => pickRow(""));
  const nudge = (dx: number, dy: number) => {
    if (!chosen || !place) return;
    const b = boxOf(chosen);
    const to = { x: b.x + dx, y: b.y + dy, w: b.w, h: b.h };
    w.going = { id: picked, box: to };
    void send(place, { id: picked, ...to });
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
        onPointerMove={(e) => (w.now ? move(e) : cursorAt(e))}
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
