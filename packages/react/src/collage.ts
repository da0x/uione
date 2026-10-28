// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a collage's layers go: a picture made of layers, as big as it says, in its
// own pixels (decision 0021). Where a picture sits when it's placed, the whole picture
// behind a crop, and where a layer moved, resized or cropped ends up, snapped
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

export function contains(r: Box, at: Point): boolean {
  return at.x >= r.x && at.x <= r.x + r.w && at.y >= r.y && at.y <= r.y + r.h;
}

// Where a picture of its own size sits on a collage, as `sits` says: covering it
// (fill), all of it showing (fit), stretched to it, or at its own size in the middle.
export function sitting(sits: string, pictureWidth: number, pictureHeight: number, width: number, height: number): Box {
  if (sits === "stretch" || pictureWidth <= 0 || pictureHeight <= 0) return { x: 0, y: 0, w: width, h: height };
  const s = sits === "fill" ? Math.max(width / pictureWidth, height / pictureHeight) : sits === "center" ? 1 : Math.min(width / pictureWidth, height / pictureHeight);
  const w = pictureWidth * s, h = pictureHeight * s;
  return { x: (width - w) / 2, y: (height - h) / 2, w, h };
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

// The box a drag draws from `from` to `to`, whichever way it went: square when
// `square`, as far as the longer side. A drag no further than `click` is a click, which
// draws a square a fifth of the collage's shorter side, centered on it.
export function drawn(from: Point, to: Point, square: boolean, click: number, width: number, height: number): Box {
  let dx = to.x - from.x, dy = to.y - from.y;
  if (Math.abs(dx) <= click && Math.abs(dy) <= click) {
    const side = Math.min(width, height) / 5;
    return { x: from.x - side / 2, y: from.y - side / 2, w: side, h: side };
  }
  if (square) {
    const side = Math.max(Math.abs(dx), Math.abs(dy));
    dx = dx < 0 ? -side : side;
    dy = dy < 0 ? -side : side;
  }
  return { x: Math.min(from.x, from.x + dx), y: Math.min(from.y, from.y + dy), w: Math.abs(dx), h: Math.abs(dy) };
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

// A picture's gaps, the cells not `covered`, filled from what's around them (harmonic
// fill): each settles to the average of its neighbors, so the colors at every edge
// blend across the gap, like a film stretched between them. `colors` is three
// channels a cell, row by row; the covered ones stay as they are. Worked out on
// smaller grids first, then each larger one, so it settles in a few passes.
export function harmonicFill(width: number, height: number, given: ArrayLike<number>, covered: ArrayLike<boolean>): Float64Array {
  const colors = Float64Array.from(given);
  const cell = (x: number, y: number) => y * width + x;
  if (width <= 0 || height <= 0 || !Array.prototype.some.call(covered, (c: boolean) => c)) return colors;
  // Each gap starts from a grid half as big, or from the average of what's covered.
  if (width > 2 && height > 2) {
    const cw = Math.floor((width + 1) / 2), ch = Math.floor((height + 1) / 2);
    const small = new Float64Array(cw * ch * 3);
    const held: boolean[] = new Array(cw * ch).fill(false);
    for (let y = 0; y < ch; y++) {
      for (let x = 0; x < cw; x++) {
        const sum = [0, 0, 0];
        let count = 0;
        for (let fy = y * 2; fy < Math.min(height, y * 2 + 2); fy++) {
          for (let fx = x * 2; fx < Math.min(width, x * 2 + 2); fx++) {
            if (!covered[cell(fx, fy)]) continue;
            for (let c = 0; c < 3; c++) sum[c]! += colors[cell(fx, fy) * 3 + c]!;
            count++;
          }
        }
        const at = y * cw + x;
        held[at] = count > 0;
        for (let c = 0; c < 3; c++) small[at * 3 + c] = count > 0 ? sum[c]! / count : 0;
      }
    }
    const filled = harmonicFill(cw, ch, small, held);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        if (covered[cell(x, y)]) continue;
        const from = (Math.floor(y / 2) * cw + Math.floor(x / 2)) * 3;
        for (let c = 0; c < 3; c++) colors[cell(x, y) * 3 + c] = filled[from + c]!;
      }
    }
  } else {
    const sum = [0, 0, 0];
    let count = 0;
    for (let i = 0; i < covered.length; i++) {
      if (!covered[i]) continue;
      for (let c = 0; c < 3; c++) sum[c]! += colors[i * 3 + c]!;
      count++;
    }
    for (let i = 0; i < covered.length; i++) {
      if (!covered[i]) for (let c = 0; c < 3; c++) colors[i * 3 + c] = sum[c]! / count;
    }
  }
  // Then each gap is the average of its neighbors, row by row, again and again.
  const passes = width > 2 && height > 2 ? 40 : 200;
  for (let pass = 0; pass < passes; pass++) {
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        if (covered[cell(x, y)]) continue;
        const sum = [0, 0, 0];
        let count = 0;
        const add = (nx: number, ny: number) => {
          if (nx < 0 || ny < 0 || nx >= width || ny >= height) return;
          for (let c = 0; c < 3; c++) sum[c]! += colors[cell(nx, ny) * 3 + c]!;
          count++;
        };
        add(x - 1, y), add(x + 1, y), add(x, y - 1), add(x, y + 1);
        for (let c = 0; c < 3; c++) colors[cell(x, y) * 3 + c] = sum[c]! / count;
      }
    }
  }
  return colors;
}

// What a picture lays over itself to melt into what's under it: its own colors, spread
// `reach` cells past each edge by harmonic fill, clear over its middle, whole a cell
// either side of its edge, and gone a cell before the end of the reach, smoothly each
// way. `inner` is how far in from that it starts to show. Four channels a cell, its
// alpha 0 to 255, over a grid `reach` bigger than the picture on every side.
// How far a harmonized or blended picture's halo reaches past its edges, as a share of
// its shorter side, and how much of that it starts in from them: a little, so the picture
// stays sharp nearly to its edge.
export const haloReach = 0.15;
export const haloInside = 0.25;

export function halo(width: number, height: number, colors: ArrayLike<number>, reach: number, inner: number): Float64Array {
  const ow = width + 2 * reach, oh = height + 2 * reach;
  if (width <= 0 || height <= 0 || reach < 0) return new Float64Array(0);
  const spread = new Float64Array(ow * oh * 3);
  const covered: boolean[] = new Array(ow * oh).fill(false);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const at = (y + reach) * ow + (x + reach);
      covered[at] = true;
      for (let c = 0; c < 3; c++) spread[at * 3 + c] = colors[(y * width + x) * 3 + c]!;
    }
  }
  const filled = harmonicFill(ow, oh, spread, covered);
  const smooth = (from: number, to: number, v: number) => {
    const t = to > from ? Math.min(1, Math.max(0, (v - from) / (to - from))) : v >= to ? 1 : 0;
    return t * t * (3 - 2 * t);
  };
  const out = new Float64Array(ow * oh * 4);
  for (let y = 0; y < oh; y++) {
    for (let x = 0; x < ow; x++) {
      // How far the cell's middle is outside the picture, or, less than none, inside it.
      const px = x + 0.5 - reach, py = y + 0.5 - reach;
      const ox = Math.max(0, -px, px - width), oy = Math.max(0, -py, py - height);
      const d = ox > 0 || oy > 0 ? Math.hypot(ox, oy) : -Math.min(px, width - px, py, height - py);
      // Whole a cell either side of its edge, so drawn bigger, smoothly between cells, it's
      // whole on the edge itself and the picture's own edge never shows; and gone a cell
      // before the reach's end, so it fades to nothing before the grid ends.
      const alpha = d <= 0 ? smooth(-inner - 1, -1, d) : 1 - smooth(1, reach - 1, d);
      const at = y * ow + x;
      for (let c = 0; c < 3; c++) out[at * 4 + c] = filled[at * 3 + c]!;
      out[at * 4 + 3] = alpha * 255;
    }
  }
  return out;
}

// How many of a picture's brightnesses its tones are told by: its darkest, its
// brightest, and evenly between.
export const tonePoints = 21;

// A picture's colors, as matching them needs, each as brightness and two colors (YCbCr:
// Y, then blue against yellow and red against green, 0 where it's gray): its brightness's
// average and how far its cells spread from that (their standard deviation); its cast,
// the two colors' average over its less colorful half alone, its grays, whites and
// shadows, so a red hat doesn't read as a red light; how strong its colors are, the
// middle of its cells' (their median), for both colors alike, so a few vivid things
// don't make it all seem so; its tones, its
// cells' brightness from the darkest to the brightest at each of the tone points (its
// quantiles); from three channels a cell, red, green and blue; and how much of the
// collage it covers, which counts for that much.
export interface ColorStats {
  mean: [number, number, number];
  spread: [number, number, number];
  tones?: number[];
  weight: number;
}

// Red, green and blue as brightness and two colors, and back (YCbCr, as JPEG has it).
const toYcc = (r: number, g: number, b: number): [number, number, number] => [
  0.299 * r + 0.587 * g + 0.114 * b,
  -0.168736 * r - 0.331264 * g + 0.5 * b,
  0.5 * r - 0.418688 * g - 0.081312 * b,
];
const toRgb = (y: number, cb: number, cr: number): [number, number, number] => [y + 1.402 * cr, y - 0.344136 * cb - 0.714136 * cr, y + 1.772 * cb];

export function statsOf(colors: ArrayLike<number>, weight: number): ColorStats {
  const out: ColorStats = { mean: [0, 0, 0], spread: [0, 0, 0], weight };
  const cells = Math.floor(colors.length / 3);
  if (cells === 0) return out;
  const ycc = new Float64Array(cells * 3);
  for (let i = 0; i < cells; i++) {
    const [y, cb, cr] = toYcc(colors[i * 3]!, colors[i * 3 + 1]!, colors[i * 3 + 2]!);
    ycc[i * 3] = y, ycc[i * 3 + 1] = cb, ycc[i * 3 + 2] = cr;
    for (let c = 0; c < 3; c++) out.mean[c] += ycc[i * 3 + c]!;
  }
  out.mean[0] /= cells;
  for (let i = 0; i < cells; i++) out.spread[0] += (ycc[i * 3]! - out.mean[0]) ** 2;
  out.spread[0] = Math.sqrt(out.spread[0] / cells);
  // How colorful each cell is, and the middle of them.
  const colorful = Array.from({ length: cells }, (_, i) => Math.hypot(ycc[i * 3 + 1]!, ycc[i * 3 + 2]!));
  const sorted = [...colorful].sort((a, b) => a - b);
  const middle = cells % 2 ? sorted[(cells - 1) / 2]! : (sorted[cells / 2 - 1]! + sorted[cells / 2]!) / 2;
  out.spread[1] = out.spread[2] = middle;
  // Its cast, from the cells no more colorful than that.
  let cb = 0, cr = 0, plain = 0;
  for (let i = 0; i < cells; i++) {
    if (colorful[i]! > middle) continue;
    cb += ycc[i * 3 + 1]!;
    cr += ycc[i * 3 + 2]!;
    plain++;
  }
  out.mean[1] = cb / plain;
  out.mean[2] = cr / plain;
  const bright = Array.from({ length: cells }, (_, i) => ycc[i * 3]!).sort((a, b) => a - b);
  out.tones = [];
  for (let q = 0; q < tonePoints; q++) {
    const at = ((cells - 1) * q) / (tonePoints - 1), low = Math.floor(at), part = at - low;
    out.tones.push(low + 1 < cells ? bright[low]! + (bright[low + 1]! - bright[low]!) * part : bright[low]!);
  }
  return out;
}

// How far a collage matching its colors moves each picture's: its cast all the way, so
// the same face in two lights looks alike, and the rest most of the way.
export const matchingStrength = 0.85;
export const matchingCast = 1;

// What a picture's colors become, as brightness and two colors: its brightness along a
// tone curve, through each of `from` to the one in `to` and straight between, and each of
// the two colors times its gain and plus its offset.
export interface ColorShift {
  gain: [number, number, number];
  offset: [number, number, number];
  from?: number[];
  to?: number[];
}

// A color, red, green and blue in 0 to 255, as a shift makes it.
export function shifted(s: ColorShift, r: number, g: number, b: number): [number, number, number] {
  const ycc = toYcc(r, g, b);
  let y = ycc[0] * s.gain[0] + s.offset[0];
  // Its brightness along the tone curve, straight between its points, and as far past its
  // ends as it is from them.
  const from = s.from, to = s.to;
  if (from && to && from.length >= 2 && from.length === to.length) {
    const v = ycc[0];
    let i = 0;
    while (i + 2 < from.length && v > from[i + 1]!) i++;
    const span = from[i + 1]! - from[i]!;
    y = span > 1e-9 ? to[i]! + ((to[i + 1]! - to[i]!) * (v - from[i]!)) / span : to[i]! + (v - from[i]!);
    if (v < from[0]!) y = to[0]! + (v - from[0]!);
    if (v > from[from.length - 1]!) y = to[to.length - 1]! + (v - from[from.length - 1]!);
    // Its colors as much stronger or weaker as it's brighter or darker, so it looks as
    // colorful as it was: darker with the same colors looks more so.
    const follow = Math.min(2, Math.max(0.5, (y + 16) / (v + 16)));
    ycc[1] *= follow;
    ycc[2] *= follow;
  }
  return toRgb(y, ycc[1] * s.gain[1] + s.offset[1], ycc[2] * s.gain[2] + s.offset[2]);
}

// A shift made ready to move many pixels' colors, as a large picture's are: each
// channel's share of the brightness and two colors, and the tone curve and how much the
// colors follow it, looked up rather than worked out again for every pixel. Its colors
// are within a hundredth of shifted's. The same as the desktop's.
export function colorShifter(s: ColorShift): (pixels: Uint8ClampedArray) => void {
  const steps = 16;
  const share = [0, 1, 2].map((c) => [0, 1, 2].map((k) => Float64Array.from({ length: 256 }, (_, v) => toYcc(c === 0 ? v : 0, c === 1 ? v : 0, c === 2 ? v : 0)[k]!)));
  // Brightness reaches past 0 to 255 only by rounding, so the tables cover that and a
  // step either side.
  const places = 257 * steps + 1;
  const bright = new Float64Array(places), follow = new Float64Array(places);
  const toned = s.from && s.to && s.from.length >= 2 && s.from.length === s.to.length;
  for (let i = 0; i < places; i++) {
    const y = i / steps - 1;
    if (!toned) {
      bright[i] = y * s.gain[0] + s.offset[0];
      follow[i] = 1;
      continue;
    }
    const from = s.from!, to = s.to!;
    let at = 0;
    while (at + 2 < from.length && y > from[at + 1]!) at++;
    const span = from[at + 1]! - from[at]!;
    let made = span > 1e-9 ? to[at]! + ((to[at + 1]! - to[at]!) * (y - from[at]!)) / span : to[at]! + (y - from[at]!);
    if (y < from[0]!) made = to[0]! + (y - from[0]!);
    if (y > from[from.length - 1]!) made = to[to.length - 1]! + (y - from[from.length - 1]!);
    bright[i] = made;
    follow[i] = Math.min(2, Math.max(0.5, (made + 16) / (y + 16)));
  }
  const [r0, r1, r2] = share[0]!, [g0, g1, g2] = share[1]!, [b0, b1, b2] = share[2]!;
  const gb = s.gain[1], gr = s.gain[2], ob = s.offset[1], or = s.offset[2];
  return (pixels) => {
    for (let i = 0; i + 3 < pixels.length; i += 4) {
      const r = pixels[i]!, g = pixels[i + 1]!, b = pixels[i + 2]!;
      const y = r0![r]! + g0![g]! + b0![b]!;
      // The curve's places either side of this brightness, and straight between.
      const at = Math.min(Math.max((y + 1) * steps, 0), places - 2);
      const below = Math.floor(at), part = at - below;
      const yy = bright[below]! + (bright[below + 1]! - bright[below]!) * part;
      const f = follow[below]! + (follow[below + 1]! - follow[below]!) * part;
      const cb = (r1![r]! + g1![g]! + b1![b]!) * f * gb + ob;
      const cr = (r2![r]! + g2![g]! + b2![b]!) * f * gr + or;
      pixels[i] = Math.round(yy + 1.402 * cr);
      pixels[i + 1] = Math.round(yy - 0.344136 * cb - 0.714136 * cr);
      pixels[i + 2] = Math.round(yy + 1.772 * cb);
    }
  };
}

// Pictures' colors matched, so they look like one picture, by matching their statistics
// (color transfer): each one's cast, the average of its two colors, moved `cast` of the
// way to all of theirs, as a white balance would; its tones, its darkest to its
// brightest, along a curve to all of theirs averaged, point by point, so its contrast is
// theirs; and how strong its colors are; those `strength` of the way; each 0 to 1,
// weighed by how much each covers. Its contrast is never made more than three times or a
// third; its tones are moved half as far, none more than 24 of 255; how strong its colors
// are is moved half as far, never past a quarter more or a fifth less; one too even to
// tell, or nearly gray, is left as it is.
export function matched(pictures: ColorStats[], strength: number, cast: number): ColorShift[] {
  let total = 0;
  const mean = [0, 0, 0], spread = [0, 0, 0];
  for (const p of pictures) {
    total += p.weight;
    for (let c = 0; c < 3; c++) (mean[c]! += p.mean[c] * p.weight), (spread[c]! += p.spread[c] * p.weight);
  }
  const out: ColorShift[] = pictures.map(() => ({ gain: [1, 1, 1], offset: [0, 0, 0] }));
  if (total <= 0) return out;
  for (let c = 0; c < 3; c++) (mean[c]! /= total), (spread[c]! /= total);
  // Their tones together: each point the average of theirs there, weighed alike.
  const toned = pictures.every((p) => p.tones?.length === tonePoints);
  const tones = new Array<number>(tonePoints).fill(0);
  if (toned) for (const p of pictures) for (let q = 0; q < tonePoints; q++) tones[q]! += (p.tones![q]! * p.weight) / total;
  const k = Math.min(1, Math.max(0, strength)), towardCast = Math.min(1, Math.max(0, cast));
  pictures.forEach((p, i) => {
    for (let c = 0; c < 3; c++) {
      // How strong its colors are changes only half as far, and only so far, so a colorful
      // picture keeps most of it; and one nearly gray is left so, never colorized.
      const g =
        c === 0
          ? p.spread[c] < 1 ? 1 : Math.min(3, Math.max(1 / 3, spread[c]! / p.spread[c]))
          : p.spread[c] < 4 ? 1 : Math.min(1.25, Math.max(0.8, spread[c]! / p.spread[c]));
      const toward = c === 0 ? k : k / 2;
      // Spread about its own average, part of the way; then that average moved toward all
      // of theirs, the two colors' as far as their cast is.
      out[i]!.gain[c] = 1 - toward + toward * g;
      out[i]!.offset[c] = p.mean[c] * (1 - out[i]!.gain[c]) + (c === 0 ? k : towardCast) * (mean[c]! - p.mean[c]);
    }
    // Its brightness along a curve from its tones to theirs, part of the way.
    if (toned) {
      out[i]!.from = [...p.tones!];
      // Half as far as the rest, and each tone no more than 24 of 255, so a bright picture
      // comes nearer darker ones without turning dark.
      out[i]!.to = p.tones!.map((own, q) => own + Math.min(24, Math.max(-24, (k / 2) * (tones[q]! - own))));
    }
  });
  return out;
}

// A layer that's neither a picture nor words is a shape, as its parts say (decision
// 0025): a rectangle, its corners rounded as far as corners says, a share of its
// shorter side; an ellipse; or a path through its points, in SVG path notation, M, L, C
// and Z, each point a fraction of its box from its top left. Each is filled and stroked
// as it says, a color written #rrggbb or none, its stroke as wide as it says in the
// collage's pixels, half of it outside its outline as SVG draws it. The same as the
// desktop's.
export interface Shape {
  kind: string; // rectangle, ellipse or path
  points: string;
  fill: string;
  stroke: string;
  strokeWidth: number;
  corners: number;
}

// A number as SVG writes it: no more decimals than it needs, up to three.
function said(n: number): string {
  const out = (Math.abs(n) < 0.0005 ? 0 : n).toFixed(3).replace(/0+$/, "").replace(/\.$/, "");
  return out === "-0" ? "0" : out;
}

// A color as it may be written into SVG: #rrggbb, or none for anything else.
const paint = (written: string) => (/^#[0-9a-fA-F]{6}$/.test(written) ? written : "none");

// A path's points in the box they're fractions of, at x y, w by h: M 0 0 L 1 1 in a 100
// by 50 box at 10 20 is M 10 20 L 110 70. What isn't a command and a number is left out.
export function pathIn(points: string, x: number, y: number, w: number, h: number): string {
  const out: string[] = [];
  let across = true;
  for (const token of points.match(/[MLCZ]|-?[0-9.]+(?:e-?[0-9]+)?/g) ?? []) {
    if (/[MLCZ]/.test(token)) {
      out.push(token);
      across = true;
      continue;
    }
    const n = Number(token) || 0;
    out.push(said(across ? x + n * w : y + n * h));
    across = !across;
  }
  return out.join(" ");
}

// The shape as one SVG element in the box at x y, w by h: <rect>, <ellipse> or <path>.
export function svgElement(s: Shape, x: number, y: number, w: number, h: number): string {
  const drawn = `fill="${paint(s.fill)}" stroke="${paint(s.stroke)}" stroke-width="${said(s.strokeWidth)}"`;
  if (s.kind === "ellipse") return `<ellipse cx="${said(x + w / 2)}" cy="${said(y + h / 2)}" rx="${said(w / 2)}" ry="${said(h / 2)}" ${drawn}/>`;
  if (s.kind === "path") return `<path d="${pathIn(s.points, x, y, w, h)}" ${drawn}/>`;
  const round = Math.min(Math.max(s.corners, 0), 0.5) * Math.min(w, h);
  return `<rect x="${said(x)}" y="${said(y)}" width="${said(w)}" height="${said(h)}" rx="${said(round)}" ${drawn}/>`;
}

// As a whole SVG document, w by h and as much more around as its stroke reaches,
// `reach` on each side, so none of it is cut off.
export function svgAlone(s: Shape, w: number, h: number, reach: number): string {
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${said(w + 2 * reach)}" height="${said(h + 2 * reach)}" viewBox="${said(-reach)} ${said(-reach)} ${said(w + 2 * reach)} ${said(h + 2 * reach)}">${svgElement(s, 0, 0, w, h)}</svg>`;
}
