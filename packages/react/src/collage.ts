// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a collage's layers go: a picture made of layers, as big as it says, in its
// own pixels (decision 0019). How its background sits and slides, the whole picture
// behind a crop, and where a layer moved, resized, slid or cropped ends up, snapped
// to the collage's edges and middle and every other layer's. It's the same model as
// one::screens's collage.hpp, and both are held to screens/tests/collage-cases.json,
// so the web, the desktop and the terminal put a layer in the same place.

export interface Point {
  x: number;
  y: number;
}

export interface Box {
  x: number;
  y: number;
  w: number;
  h: number;
}

// The smallest a layer, or a crop of one, is made by a drag, in the collage's pixels.
export const smallest = 8;

// How the background sits on a collage: covering it (fill), all of it showing (fit,
// the default), stretched to it, or at its own size in the middle; zoomed about its
// middle; and how far along each side it's slid, from 0 to 1.
export interface Backdrop {
  sits: string;
  width: number;
  height: number;
  zoom: number;
  slideX: number;
  slideY: number;
}

// Where a layer went, and the lines it snapped to: one across, one down, and a corner
// it sits in evenly, the collage's corner and its own.
export interface Guided {
  at: Box;
  guideX?: number;
  guideY?: number;
  corner?: [Point, Point];
}

// The lines a layer moved or resized snaps to, across and down.
export interface Lines {
  across: number[];
  down: number[];
}

const clamp = (v: number, least: number, most: number) => Math.min(Math.max(v, least), most);
const slidesSitting = (sits: string) => sits === "" || sits === "fill" || sits === "fit";

export function contains(r: Box, at: Point): boolean {
  return at.x >= r.x && at.x <= r.x + r.w && at.y >= r.y && at.y <= r.y + r.h;
}

// Where a picture of its own size sits on a collage, as `sits` says.
export function sitting(sits: string, pictureWidth: number, pictureHeight: number, width: number, height: number): Box {
  if (sits === "stretch" || pictureWidth <= 0 || pictureHeight <= 0) return { x: 0, y: 0, w: width, h: height };
  const s = sits === "fill" ? Math.max(width / pictureWidth, height / pictureHeight) : sits === "center" ? 1 : Math.min(width / pictureWidth, height / pictureHeight);
  const w = pictureWidth * s, h = pictureHeight * s;
  return { x: (width - w) / 2, y: (height - h) / 2, w, h };
}

// How far something `length` long can go along a side `side` long.
export function slideRange(side: number, length: number): [number, number] {
  return [Math.min(0, side - length), Math.max(0, side - length)];
}

// Where the background is: sitting as it says, zoomed, and slid as far as it says.
export function backgroundAt(b: Backdrop, pictureWidth: number, pictureHeight: number): Box {
  let at = sitting(b.sits, pictureWidth, pictureHeight, b.width, b.height);
  const zoom = b.zoom > 0 ? b.zoom : 1;
  at = { x: at.x + (at.w * (1 - zoom)) / 2, y: at.y + (at.h * (1 - zoom)) / 2, w: at.w * zoom, h: at.h * zoom };
  if (slidesSitting(b.sits) || zoom !== 1) {
    const [left, right] = slideRange(b.width, at.w);
    const [top, bottom] = slideRange(b.height, at.h);
    at.x = left + (right - left) * clamp(b.slideX, 0, 1);
    at.y = top + (bottom - top) * clamp(b.slideY, 0, 1);
  }
  return at;
}

// Whether a background where it is has room to slide.
export function slides(b: Backdrop, at: Box): boolean {
  return (slidesSitting(b.sits) || (b.zoom > 0 && b.zoom !== 1)) && (Math.abs(at.w - b.width) > 0.5 || Math.abs(at.h - b.height) > 0.5);
}

// The whole picture a layer shows part of, `crop` in fractions of it.
export function wholeOf(shown: Box, crop: Box): Box {
  const w = shown.w / crop.w, h = shown.h / crop.h;
  return { x: shown.x - crop.x * w, y: shown.y - crop.y * h, w, h };
}

// The collage's edges and middle, and each other layer's.
export function snapLines(width: number, height: number, others: Box[]): Lines {
  const out: Lines = { across: [0, width / 2, width], down: [0, height / 2, height] };
  for (const o of others) {
    out.across.push(o.x, o.x + o.w / 2, o.x + o.w);
    out.down.push(o.y, o.y + o.h / 2, o.y + o.h);
  }
  return out;
}

// Moved by dx and dy, its edges or middle drawn to a line `close` or nearer, and into
// a corner evenly when it's as far from both edges there.
export function moved(from: Box, dx: number, dy: number, to: Lines, width: number, height: number, close: number, snapping: boolean): Guided {
  const out: Guided = { at: { x: from.x + dx, y: from.y + dy, w: from.w, h: from.h } };
  if (!snapping) return out;
  const b = out.at;
  const snap = (start: number, size: number, lines: number[]): [number, number | undefined] => {
    let best = close + 1, by = 0;
    for (const line of lines)
      for (const edge of [start, start + size / 2, start + size])
        if (Math.abs(line - edge) < Math.abs(best)) (best = line - edge), (by = line);
    if (Math.abs(best) > close) return [start, undefined];
    return [start + best, by];
  };
  [b.x, out.guideX] = snap(b.x, b.w, to.across);
  [b.y, out.guideY] = snap(b.y, b.h, to.down);
  if (out.guideX === undefined) delete out.guideX;
  if (out.guideY === undefined) delete out.guideY;
  const east = b.x + b.w / 2 > width / 2, south = b.y + b.h / 2 > height / 2;
  const marginX = east ? width - (b.x + b.w) : b.x, marginY = south ? height - (b.y + b.h) : b.y;
  const snappedX = out.guideX !== undefined, snappedY = out.guideY !== undefined;
  if (marginX > close && marginY > close && Math.abs(marginX - marginY) <= close && !(snappedX && snappedY)) {
    if (!snappedY) b.y = south ? height - b.h - marginX : marginX;
    else b.x = east ? width - b.w - marginY : marginY;
    const margin = snappedY ? marginY : marginX;
    out.corner = [
      { x: east ? width : 0, y: south ? height : 0 },
      { x: east ? width - margin : margin, y: south ? height - margin : margin },
    ];
  }
  return out;
}

// Resized by a corner, 0 to 3 clockwise from the top left, the opposite one staying
// put: its shape kept unless not `keepShape`, and the edges it's dragged by snapped.
export function resized(from: Box, corner: number, dx: number, dy: number, keepShape: boolean, to: Lines, close: number, snapping: boolean): Guided {
  const left = from.x, top = from.y, right = from.x + from.w, bottom = from.y + from.h;
  const west = corner === 0 || corner === 3, north = corner === 0 || corner === 1;
  let w = Math.max(smallest, west ? right - (left + dx) : right + dx - left);
  let h = Math.max(smallest, north ? bottom - (top + dy) : bottom + dy - top);
  const kept = keepShape && from.w > 0 && from.h > 0;
  if (kept) {
    const grown = Math.max(w / from.w, h / from.h);
    w = from.w * grown;
    h = from.h * grown;
  }
  const out: Guided = { at: { x: 0, y: 0, w: 0, h: 0 } };
  if (snapping) {
    const toLine = (edge: number, lines: number[]): number | undefined => {
      let best: number | undefined;
      for (const line of lines) if (Math.abs(line - edge) <= close && (best === undefined || Math.abs(line - edge) < Math.abs(best - edge))) best = line;
      return best;
    };
    const across = toLine(west ? right - w : left + w, to.across), down = toLine(north ? bottom - h : top + h, to.down);
    const widthAt = (line: number) => Math.max(smallest, west ? right - line : line - left);
    const heightAt = (line: number) => Math.max(smallest, north ? bottom - line : line - top);
    if (kept) {
      const offAcross = across !== undefined ? Math.abs(widthAt(across) - w) : 1e9, offDown = down !== undefined ? Math.abs(heightAt(down) - h) : 1e9;
      if (across !== undefined && offAcross <= offDown) (w = widthAt(across)), (h = (w * from.h) / from.w), (out.guideX = across);
      else if (down !== undefined) (h = heightAt(down)), (w = (h * from.w) / from.h), (out.guideY = down);
    } else {
      if (across !== undefined) (w = widthAt(across)), (out.guideX = across);
      if (down !== undefined) (h = heightAt(down)), (out.guideY = down);
    }
  }
  out.at = { x: west ? right - w : left, y: north ? bottom - h : top, w, h };
  return out;
}

// A background slid by dx and dy, within how far it goes each way, and to the middle
// of a collage `width` by `height` when it's `close` to it.
export function slid(from: Box, dx: number, dy: number, across: [number, number], down: [number, number], width: number, height: number, close: number, snapping: boolean): Guided {
  const out: Guided = { at: { ...from } };
  const b = out.at;
  b.x = clamp(b.x + dx, across[0], across[1]);
  b.y = clamp(b.y + dy, down[0], down[1]);
  if (!snapping) return out;
  const middleX = (across[0] + across[1]) / 2, middleY = (down[0] + down[1]) / 2;
  if (across[1] > across[0] && Math.abs(b.x - middleX) <= close) (b.x = middleX), (out.guideX = width / 2);
  if (down[1] > down[0] && Math.abs(b.y - middleY) <= close) (b.y = middleY), (out.guideY = height / 2);
  return out;
}

// A crop's kept part dragged: moved within the whole picture when `edges` is 0, or by
// its edges, 1 left, 2 top, 4 right and 8 bottom.
export function cropped(kept: Box, edges: number, dx: number, dy: number, whole: Box): Box {
  if (edges === 0) {
    return { x: clamp(kept.x + dx, whole.x, whole.x + whole.w - kept.w), y: clamp(kept.y + dy, whole.y, whole.y + whole.h - kept.h), w: kept.w, h: kept.h };
  }
  let left = kept.x, top = kept.y, right = kept.x + kept.w, bottom = kept.y + kept.h;
  if (edges & 1) left = clamp(left + dx, whole.x, right - smallest);
  if (edges & 4) right = clamp(right + dx, left + smallest, whole.x + whole.w);
  if (edges & 2) top = clamp(top + dy, whole.y, bottom - smallest);
  if (edges & 8) bottom = clamp(bottom + dy, top + smallest, whole.y + whole.h);
  return { x: left, y: top, w: right - left, h: bottom - top };
}

// The corner of a box a point is within `handle` of, 0 to 3 clockwise from the top
// left, or -1.
export function cornerAt(r: Box, at: Point, handle: number): number {
  const corners = [
    [r.x, r.y],
    [r.x + r.w, r.y],
    [r.x + r.w, r.y + r.h],
    [r.x, r.y + r.h],
  ];
  return corners.findIndex(([x, y]) => Math.abs(at.x - x!) <= handle && Math.abs(at.y - y!) <= handle);
}

// The edges of a crop a point is on: 1 left, 2 top, 4 right and 8 bottom; 0 inside
// it; or -1 outside.
export function cropEdgesAt(r: Box, at: Point, handle: number): number {
  const across = at.x >= r.x - handle && at.x <= r.x + r.w + handle, down = at.y >= r.y - handle && at.y <= r.y + r.h + handle;
  if (!across || !down) return -1;
  let edges = 0;
  if (Math.abs(at.x - r.x) <= handle) edges |= 1;
  else if (Math.abs(at.x - (r.x + r.w)) <= handle) edges |= 4;
  if (Math.abs(at.y - r.y) <= handle) edges |= 2;
  else if (Math.abs(at.y - (r.y + r.h)) <= handle) edges |= 8;
  return edges || contains(r, at) ? edges : -1;
}
