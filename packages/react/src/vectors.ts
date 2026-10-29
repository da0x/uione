// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// SVG, as pictures come in: known by what it is, and sized from what it says, as the
// desktop knows and sizes one by the pinned nanosvg. It's never read as pixels.

// An SVG's text, when a file or what's fetched is one: said so by its type or its name,
// or starting with a tag and holding an <svg>.
export async function svgIn(file: Blob): Promise<string | undefined> {
  const named = file.type === "image/svg+xml" || (file instanceof File && file.name.toLowerCase().endsWith(".svg"));
  if (!named && file.type.startsWith("image/") && file.type !== "image/svg+xml") return undefined;
  // Its start first, so a large picture isn't read as words to learn it isn't one.
  const start = (await textOf(file.slice(0, 1024))).replace(/^\uFEFF/, "").trimStart();
  if (!start.startsWith("<")) return undefined;
  const text = await textOf(file);
  return text.includes("<svg") ? text : undefined;
}

// A file's text, as every browser reads it.
function textOf(file: Blob): Promise<string> {
  if (typeof file.text === "function") return file.text();
  return new Promise((done, failed) => {
    const reader = new FileReader();
    reader.onload = () => done(String(reader.result));
    reader.onerror = () => failed(reader.error);
    reader.readAsText(file);
  });
}

// An SVG's own size: its width and height, or its viewBox's when it says none.
export function svgSize(svg: string): { w: number; h: number } | undefined {
  const root = new DOMParser().parseFromString(svg, "image/svg+xml").documentElement;
  if (root.nodeName !== "svg") return undefined;
  const box = (root.getAttribute("viewBox") ?? "").split(/[\s,]+/).map(Number);
  // A length said in percent is of whatever it's in, so it says nothing of its own size.
  const length = (said: string | null) => (said && !said.trim().endsWith("%") ? parseFloat(said) : NaN);
  const w = length(root.getAttribute("width")) || box[2] || 0, h = length(root.getAttribute("height")) || box[3] || 0;
  return w > 0 && h > 0 ? { w, h } : undefined;
}

// A picture's own size, an SVG's as it says and any other's as it's read, the way up
// its camera says it was held; nothing when it isn't one that can be read.
export async function pictureSize(file: Blob): Promise<{ w: number; h: number } | undefined> {
  const svg = await svgIn(file);
  if (svg !== undefined) return svgSize(svg);
  try {
    const bitmap = await createImageBitmap(file, { imageOrientation: "from-image" });
    const size = { w: bitmap.width, h: bitmap.height };
    bitmap.close();
    return size;
  } catch {
    return undefined;
  }
}

// A path's commands, made absolute and only M, L, C and Z: lines across or down made
// lines, smooth and quadratic curves and arcs made cubic ones, each point given to
// `at`, which may move it, as a transform does.
type At = (x: number, y: number) => [number, number];
export function absolutePath(d: string, at: At = (x, y) => [x, y]): string {
  const tokens = d.match(/[a-zA-Z]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?/g) ?? [];
  const out: string[] = [];
  let i = 0, command = "", x = 0, y = 0, startX = 0, startY = 0, lastControlX = 0, lastControlY = 0, lastQuadX = 0, lastQuadY = 0, previous = "";
  const number = () => Number(tokens[i++]);
  const point = (px: number, py: number) => at(px, py).map((n) => String(Math.round(n * 1000) / 1000)).join(" ");
  const cubic = (x1: number, y1: number, x2: number, y2: number, ex: number, ey: number) => {
    out.push(`C ${point(x1, y1)} ${point(x2, y2)} ${point(ex, ey)}`);
    (lastControlX = x2), (lastControlY = y2), (x = ex), (y = ey);
  };
  while (i < tokens.length) {
    if (/^[a-zA-Z]$/.test(tokens[i]!)) command = tokens[i++]!;
    else if (!command) break;
    const relative = command === command.toLowerCase(), c = command.toUpperCase();
    const ox = relative ? x : 0, oy = relative ? y : 0;
    if (c === "M") {
      (x = ox + number()), (y = oy + number());
      (startX = x), (startY = y);
      out.push(`M ${point(x, y)}`);
      command = relative ? "l" : "L"; // more pairs after a move are lines
    } else if (c === "L") {
      (x = ox + number()), (y = oy + number());
      out.push(`L ${point(x, y)}`);
    } else if (c === "H") {
      x = ox + number();
      out.push(`L ${point(x, y)}`);
    } else if (c === "V") {
      y = (relative ? y : 0) + number();
      out.push(`L ${point(x, y)}`);
    } else if (c === "C") {
      const x1 = ox + number(), y1 = oy + number(), x2 = ox + number(), y2 = oy + number(), ex = ox + number(), ey = oy + number();
      cubic(x1, y1, x2, y2, ex, ey);
    } else if (c === "S") {
      const smooth = previous === "C" || previous === "S";
      const x1 = smooth ? 2 * x - lastControlX : x, y1 = smooth ? 2 * y - lastControlY : y;
      const x2 = ox + number(), y2 = oy + number(), ex = ox + number(), ey = oy + number();
      cubic(x1, y1, x2, y2, ex, ey);
    } else if (c === "Q" || c === "T") {
      let qx: number, qy: number;
      if (c === "Q") (qx = ox + number()), (qy = oy + number());
      else {
        const smooth = previous === "Q" || previous === "T";
        (qx = smooth ? 2 * x - lastQuadX : x), (qy = smooth ? 2 * y - lastQuadY : y);
      }
      const ex = ox + number(), ey = oy + number();
      (lastQuadX = qx), (lastQuadY = qy);
      cubic(x + (2 / 3) * (qx - x), y + (2 / 3) * (qy - y), ex + (2 / 3) * (qx - ex), ey + (2 / 3) * (qy - ey), ex, ey);
    } else if (c === "A") {
      const rx = Math.abs(number()), ry = Math.abs(number()), turn = (number() * Math.PI) / 180, large = number() !== 0, sweep = number() !== 0;
      const ex = ox + number(), ey = oy + number();
      for (const [x1, y1, x2, y2, px, py] of arcCubics(x, y, rx, ry, turn, large, sweep, ex, ey)) cubic(x1, y1, x2, y2, px, py);
      (x = ex), (y = ey);
    } else if (c === "Z") {
      out.push("Z");
      (x = startX), (y = startY);
      command = "";
    } else {
      break; // a command SVG doesn't have
    }
    previous = c;
  }
  return out.join(" ");
}

// An arc as the cubic curves that draw it, each [x1, y1, x2, y2, x, y], as SVG's
// implementation notes work an arc's center out from its ends.
function arcCubics(x0: number, y0: number, rx: number, ry: number, turn: number, large: boolean, sweep: boolean, x: number, y: number): number[][] {
  if ((x0 === x && y0 === y) || rx === 0 || ry === 0) return [[x0, y0, x, y, x, y]];
  const cos = Math.cos(turn), sin = Math.sin(turn);
  const dx = (x0 - x) / 2, dy = (y0 - y) / 2;
  const x1 = cos * dx + sin * dy, y1 = -sin * dx + cos * dy;
  const grown = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
  if (grown > 1) (rx *= Math.sqrt(grown)), (ry *= Math.sqrt(grown));
  const sign = large === sweep ? -1 : 1;
  const share = Math.max(0, (rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1) / (rx * rx * y1 * y1 + ry * ry * x1 * x1));
  const k = sign * Math.sqrt(share);
  const cx1 = (k * rx * y1) / ry, cy1 = (-k * ry * x1) / rx;
  const cx = cos * cx1 - sin * cy1 + (x0 + x) / 2, cy = sin * cx1 + cos * cy1 + (y0 + y) / 2;
  const angle = (ux: number, uy: number, vx: number, vy: number) => Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  const start = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
  let delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
  if (!sweep && delta > 0) delta -= 2 * Math.PI;
  if (sweep && delta < 0) delta += 2 * Math.PI;
  const pieces = Math.ceil(Math.abs(delta) / (Math.PI / 2));
  const step = delta / pieces, handle = (4 / 3) * Math.tan(step / 4);
  const on = (a: number): [number, number] => [cx + rx * Math.cos(a) * cos - ry * Math.sin(a) * sin, cy + rx * Math.cos(a) * sin + ry * Math.sin(a) * cos];
  const along = (a: number): [number, number] => [-rx * Math.sin(a) * cos - ry * Math.cos(a) * sin, -rx * Math.sin(a) * sin + ry * Math.cos(a) * cos];
  const out: number[][] = [];
  for (let n = 0; n < pieces; n++) {
    const a = start + n * step, b = a + step;
    const [ax, ay] = on(a), [bx, by] = on(b), [tax, tay] = along(a), [tbx, tby] = along(b);
    out.push([ax + handle * tax, ay + handle * tay, bx - handle * tbx, by - handle * tby, bx, by]);
  }
  return out;
}

// One shape of an SVG broken apart: its path, its points where they are in the SVG's
// own size, with its transforms applied, M, L, C and Z; its fill and stroke, each
// #rrggbb or none, a gradient's its first stop's color; its stroke's width; and how much
// of it shows. As the desktop's are by the pinned nanosvg.
export interface SvgShape {
  path: string;
  fill: string;
  stroke: string;
  strokeWidth: number;
  opacity: number;
}

// An element's outline as path data, as SVG draws each kind of shape.
function outlineOf(e: Element): string | undefined {
  const n = (name: string) => parseFloat(e.getAttribute(name) ?? "") || 0;
  switch (e.localName) {
    case "path":
      return e.getAttribute("d") ?? undefined;
    case "rect": {
      const x = n("x"), y = n("y"), w = n("width"), h = n("height");
      if (w <= 0 || h <= 0) return undefined;
      let rx = e.hasAttribute("rx") ? n("rx") : n("ry"), ry = e.hasAttribute("ry") ? n("ry") : rx;
      (rx = Math.min(rx, w / 2)), (ry = Math.min(ry, h / 2));
      if (rx <= 0 || ry <= 0) return `M ${x} ${y} H ${x + w} V ${y + h} H ${x} Z`;
      return `M ${x + rx} ${y} H ${x + w - rx} A ${rx} ${ry} 0 0 1 ${x + w} ${y + ry} V ${y + h - ry} A ${rx} ${ry} 0 0 1 ${x + w - rx} ${y + h} H ${x + rx} A ${rx} ${ry} 0 0 1 ${x} ${y + h - ry} V ${y + ry} A ${rx} ${ry} 0 0 1 ${x + rx} ${y} Z`;
    }
    case "circle":
    case "ellipse": {
      const cx = n("cx"), cy = n("cy"), rx = e.localName === "circle" ? n("r") : n("rx"), ry = e.localName === "circle" ? n("r") : n("ry");
      if (rx <= 0 || ry <= 0) return undefined;
      return `M ${cx - rx} ${cy} A ${rx} ${ry} 0 1 0 ${cx + rx} ${cy} A ${rx} ${ry} 0 1 0 ${cx - rx} ${cy} Z`;
    }
    case "line":
      return `M ${n("x1")} ${n("y1")} L ${n("x2")} ${n("y2")}`;
    case "polyline":
    case "polygon": {
      const points = (e.getAttribute("points") ?? "").trim().split(/[\s,]+/).map(Number);
      if (points.length < 4) return undefined;
      return "M " + points.join(" ") + (e.localName === "polygon" ? " Z" : "");
    }
  }
  return undefined;
}

// A color as the browser computes it, rgb(…) or a gradient's address, as #rrggbb, and how
// much of it shows; none and 0 for none.
function paintOf(said: string, root: Element): [string, number] {
  const gradient = said.match(/url\(["']?#([^"')]+)["']?\)/);
  if (gradient) {
    const stop = root.querySelector(`[id="${gradient[1]}"] stop`);
    if (!stop) return ["none", 0];
    const style = getComputedStyle(stop);
    const [color] = paintOf(style.stopColor || "black", root);
    return [color, Number(style.stopOpacity || 1)];
  }
  const rgb = said.match(/rgba?\(\s*(\d+)[\s,]+(\d+)[\s,]+(\d+)(?:[\s,/]+([\d.]+))?/);
  if (!rgb) return ["none", 0];
  const hex = (v: string) => Number(v).toString(16).padStart(2, "0");
  return [`#${hex(rgb[1]!)}${hex(rgb[2]!)}${hex(rgb[3]!)}`, rgb[4] === undefined ? 1 : Number(rgb[4])];
}

// An SVG's shapes, lowest first, those it says aren't seen, and what's only defined to be
// used elsewhere, left out: drawn where it is, unseen, so the browser works out each
// one's transforms and colors as it draws them.
export function svgShapes(svg: string): SvgShape[] {
  const size = svgSize(svg);
  const doc = new DOMParser().parseFromString(svg, "image/svg+xml");
  const root = doc.documentElement;
  if (root.nodeName !== "svg" || !size) return [];
  root.setAttribute("width", String(size.w));
  root.setAttribute("height", String(size.h));
  const holder = document.createElement("div");
  // Unseen without being hidden, as a shape's own visibility is read from it.
  holder.style.cssText = "position:fixed;left:0;top:0;opacity:0;pointer-events:none;z-index:-1";
  const shown = document.importNode(root, true) as unknown as SVGSVGElement;
  holder.appendChild(shown);
  document.body.appendChild(holder);
  try {
    const origin = shown.getBoundingClientRect();
    const out: SvgShape[] = [];
    for (const e of Array.from(shown.querySelectorAll("path, rect, circle, ellipse, line, polyline, polygon"))) {
      if (e.closest("defs, clipPath, mask, symbol, marker, pattern")) continue;
      const style = getComputedStyle(e);
      if (style.display === "none" || style.visibility === "hidden") continue;
      const outline = outlineOf(e);
      const matrix = (e as SVGGraphicsElement).getScreenCTM?.();
      if (!outline || !matrix) continue;
      const at: At = (x, y) => [matrix.a * x + matrix.c * y + matrix.e - origin.left, matrix.b * x + matrix.d * y + matrix.f - origin.top];
      const path = absolutePath(outline, at);
      if (!path) continue;
      const [fill, filled] = paintOf(style.fill, shown);
      const [stroke, stroked] = paintOf(style.stroke, shown);
      // How much shows: its own and its groups' opacity, by its fill's, or its stroke's when it has no fill.
      let opacity = 1;
      for (let up: Element | null = e; up && up !== shown; up = up.parentElement) opacity *= Number(getComputedStyle(up).opacity || 1);
      opacity *= fill !== "none" ? filled * Number(style.fillOpacity || 1) : stroked * Number(style.strokeOpacity || 1);
      const scale = Math.sqrt(Math.abs(matrix.a * matrix.d - matrix.b * matrix.c));
      out.push({ path, fill, stroke, strokeWidth: stroke === "none" ? 0 : (parseFloat(style.strokeWidth) || 1) * scale, opacity });
    }
    return out;
  } finally {
    holder.remove();
  }
}
