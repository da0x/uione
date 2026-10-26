// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A collage: a picture made of layers, as big as its view says, drawn scaled to fit
// with room around it, or at its own size on black filling the screen (decision
// 0019). Its background sits as its view says, and what it leaves around it is the
// picture's edges drawn out, or its edge color; pictures are cropped, turned and
// mirrored as each layer says, and words drawn over a soft shadow. A click picks a
// layer, framed with its corners and the buttons over it; a picked layer is moved, or
// resized by a corner with its shape kept unless Shift is held, snapped unless Alt
// is, and the background slid where it can. Where everything goes is collage.ts, the
// model the desktop's is held to as well. It's drawn on a canvas, the same in every
// component set.

import { useEffect, useLayoutEffect, useRef, useState } from "react";
import type { ReactNode } from "react";
import { backgroundAt, cornerAt, moved, resized, slid, slideRange, slides, snapLines } from "./collage.js";
import type { Backdrop, Box, Guided, Lines, Point } from "./collage.js";
import { useFiles, useRunner } from "./data.js";
import type { ViewState } from "./data.js";
import { OverLayer, setPicked, useKeys, usePickedId } from "./workspace.js";

type Row = Record<string, unknown>;

const handle = 10; // a corner's handle, in the window
const num = (v: unknown, otherwise = 0) => (typeof v === "number" && Number.isFinite(v) ? v : typeof v === "string" && v.trim() !== "" && Number.isFinite(Number(v)) ? Number(v) : otherwise);
const str = (v: unknown) => (typeof v === "string" ? v : "");
const boxOf = (l: Row): Box => ({ x: num(l["x"]), y: num(l["y"]), w: num(l["w"]), h: num(l["h"]) });
const cropOf = (l: Row): Box => ({ x: num(l["crop_x"], 0), y: num(l["crop_y"], 0), w: num(l["crop_w"], 1), h: num(l["crop_h"], 1) });
const isWords = (l: Row) => str(l["picture"]) === "";
const isBackground = (layers: Row[], l: Row) => layers.length > 0 && !isWords(l) && layers[0]!["id"] === l["id"];
const colorOf = (written: string, otherwise: string) => (/^#[0-9a-fA-F]{6}$/.test(written) ? written : otherwise);
const wordsFamily = 'system-ui, -apple-system, "Segoe UI", Roboto, Cantarell, Ubuntu, sans-serif';

// Each picture read once, by what names it, and what's drawn from it: turned and
// mirrored as a layer says, and its edge color.
const pictures = new Map<string, ImageBitmap | "reading" | "failed">();
const orientedOnes = new Map<string, HTMLCanvasElement>();
const edges = new Map<string, string>();
const readListeners = new Set<() => void>();

function pictureOf(path: string, address: ((path: string) => Promise<string>) | undefined): ImageBitmap | undefined {
  const kept = pictures.get(path);
  if (kept instanceof ImageBitmap) return kept;
  if (kept || !address || typeof createImageBitmap !== "function") return undefined;
  pictures.set(path, "reading");
  void address(path)
    .then((url) => fetch(url))
    .then((r) => r.blob())
    .then((blob) => createImageBitmap(blob))
    .then((bitmap) => pictures.set(path, bitmap))
    .catch(() => pictures.set(path, "failed"))
    .finally(() => readListeners.forEach((changed) => changed()));
  return undefined;
}

function orientedOf(path: string, p: ImageBitmap, turns: number, mirrored: boolean): CanvasImageSource {
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

// What the pointer is doing to a layer, from where it was and where the pointer was.
interface Gesture {
  what: "moving" | "resizing" | "sliding";
  id: string;
  from: Box;
  at: Point;
  corner: number;
  across: [number, number];
  down: [number, number];
}

// Where the collage was last drawn, how much smaller than itself, what the pointer is
// doing, where a layer is going or was let go, and the lines it snaps to. Kept apart
// from React's state, so a drag repaints the canvas alone.
interface Placed {
  board: Box;
  scale: number;
  now?: Gesture;
  going?: { id: string; box: Box };
  lines: Lines;
  guide?: Guided;
}

export function Collage({
  view,
  list,
  width = "width",
  height = "height",
  place,
  slide,
  snap = false,
  empty,
  pick,
  subject,
  fills = false,
  children,
}: {
  view: ViewState;
  list: string;
  width?: string;
  height?: string;
  place?: string; // the command a layer moved or resized is sent to, with x y w h
  slide?: string; // and the one the background slid is, with slide_x and slide_y
  snap?: boolean;
  empty?: string;
  pick?: string; // the screen's pick of its layers
  subject?: string; // the id of what the view is per, which its background slides on
  fills?: boolean; // in a workspace, as big as its region; elsewhere 560 tall
  children?: ReactNode; // the buttons over the picked layer
}) {
  const files = useFiles();
  const runner = useRunner();
  const picked = usePickedId(pick ?? "") ?? "";
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const holder = useRef<HTMLDivElement>(null);
  const placed = useRef<Placed>({ board: { x: 0, y: 0, w: 0, h: 0 }, scale: 0, lines: { across: [], down: [] } });
  const [, setRead] = useState(0);
  const [frame, setFrame] = useState<Box | undefined>();
  const [full, setFull] = useState(() => typeof document !== "undefined" && document.fullscreenElement !== null);

  const data = view.status === "live" ? ((view.data ?? {}) as Row) : {};
  const layers = (data[list] as Row[] | undefined) ?? [];
  const W = num(data[width]), H = num(data[height]);
  const backdrop: Backdrop = { sits: str(data["background"]), width: W, height: H, zoom: num(data["zoom"], 1), slideX: num(data["slide_x"], 0.5), slideY: num(data["slide_y"], 0.5) };
  const address = files ? (path: string) => files.address(path) : undefined;

  useEffect(() => {
    const changed = () => setRead((n) => n + 1);
    readListeners.add(changed);
    const screen = () => setFull(document.fullscreenElement !== null);
    document.addEventListener("fullscreenchange", screen);
    return () => {
      readListeners.delete(changed);
      document.removeEventListener("fullscreenchange", screen);
    };
  }, []);

  // Where each layer is on the collage: the background where it sits, the rest as they
  // say, and one being moved or let go where it's going.
  const whereIs = (l: Row): Box => {
    let b = boxOf(l);
    if (isBackground(layers, l)) {
      const p = pictureOf(str(l["picture"]), address);
      if (p) {
        const sideways = num(l["turns"]) % 2 === 1;
        b = backgroundAt(backdrop, sideways ? p.height : p.width, sideways ? p.width : p.height);
      }
    }
    const g = placed.current.going;
    if (g && g.id === l["id"]) {
      const near = (a: number, z: number) => Math.abs(a - z) < 0.01;
      if (near(b.x, g.box.x) && near(b.y, g.box.y) && near(b.w, g.box.w) && near(b.h, g.box.h) && !placed.current.now) placed.current.going = undefined;
      else b = g.box;
    }
    return b;
  };
  const onBoard = (b: Box): Box => {
    const w = placed.current;
    return { x: w.board.x + b.x * w.scale, y: w.board.y + b.y * w.scale, w: b.w * w.scale, h: b.h * w.scale };
  };

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
    placed.current.board = board;
    placed.current.scale = scale;
    if (!full) {
      c.save();
      c.shadowColor = "rgba(0, 0, 0, 0.18)";
      c.shadowBlur = 14;
      c.shadowOffsetY = 2;
      c.fillStyle = "#fff";
      c.fillRect(board.x, board.y, bw, bh);
      c.restore();
    }
    c.fillStyle = "#fff";
    c.fillRect(board.x, board.y, bw, bh);
    let framed: Box | undefined;
    c.save();
    c.beginPath();
    c.rect(board.x, board.y, bw, bh);
    c.clip();
    for (const l of layers) {
      const b = onBoard(whereIs(l));
      if (isWords(l)) {
        const size = num(l["size"]) * scale;
        c.font = `${num(l["weight"], 700)} ${size}px ${wordsFamily}`;
        c.textBaseline = "top";
        const lines = str(l["words"]).split("\n");
        const off = Math.max(1, size * 0.04);
        lines.forEach((line, i) => {
          c.fillStyle = "rgba(0, 0, 0, 0.45)";
          c.fillText(line, b.x + off / 2, b.y + i * size * 1.2 + off);
          c.fillStyle = colorOf(str(l["color"]), "#ffffff");
          c.fillText(line, b.x, b.y + i * size * 1.2);
        });
      } else {
        const path = str(l["picture"]);
        const p = pictureOf(path, address);
        if (p) {
          const shown = orientedOf(path, p, ((num(l["turns"]) % 4) + 4) % 4, num(l["mirrored"]) !== 0) as CanvasImageSource & { width: number; height: number };
          if (isBackground(layers, l)) {
            const covers = b.x <= board.x && b.y <= board.y && b.x + b.w >= board.x + bw && b.y + b.h >= board.y + bh;
            if (!covers) {
              c.fillStyle = edgeColorOf(path, shown);
              c.fillRect(board.x, board.y, bw, bh);
              if (str(data["around"]) !== "color") {
                // Its outermost pixels drawn out to the edges.
                const sw = shown.width, sh = shown.height;
                const strip = (sx: number, sy: number, sW: number, sH: number, dx: number, dy: number, dW: number, dH: number) => {
                  if (dW > 0 && dH > 0) c.drawImage(shown, sx, sy, sW, sH, dx, dy, dW, dH);
                };
                const left = b.x - board.x, right = board.x + bw - (b.x + b.w), top = b.y - board.y, bottom = board.y + bh - (b.y + b.h);
                strip(0, 0, 1, sh, board.x, b.y, left + 1, b.h);
                strip(sw - 1, 0, 1, sh, b.x + b.w - 1, b.y, right + 1, b.h);
                strip(0, 0, sw, 1, b.x, board.y, b.w, top + 1);
                strip(0, sh - 1, sw, 1, b.x, b.y + b.h - 1, b.w, bottom + 1);
                strip(0, 0, 1, 1, board.x, board.y, left + 1, top + 1);
                strip(sw - 1, 0, 1, 1, b.x + b.w - 1, board.y, right + 1, top + 1);
                strip(0, sh - 1, 1, 1, board.x, b.y + b.h - 1, left + 1, bottom + 1);
                strip(sw - 1, sh - 1, 1, 1, b.x + b.w - 1, b.y + b.h - 1, right + 1, bottom + 1);
              }
            }
            c.drawImage(shown, b.x, b.y, b.w, b.h);
          } else {
            const crop = cropOf(l);
            c.drawImage(shown, crop.x * shown.width, crop.y * shown.height, crop.w * shown.width, crop.h * shown.height, b.x, b.y, b.w, b.h);
          }
        }
      }
      if (l["id"] === picked && !full) framed = b;
    }
    c.restore();
    // The lines a moved layer snapped to, across the whole collage.
    const guide = placed.current.guide;
    if (placed.current.now && guide) {
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
    if (layers.length === 0 && !full && empty) {
      c.fillStyle = style.getPropertyValue("--color-muted").trim() || "#5b6577";
      c.font = `13px ${wordsFamily}`;
      c.textAlign = "center";
      c.textBaseline = "middle";
      c.fillText(empty, board.x + bw / 2, board.y + bh / 2);
      c.textAlign = "start";
    }
    // The picked layer's frame and corners, over everything.
    if (framed) {
      c.strokeStyle = accent;
      c.lineWidth = 2;
      c.strokeRect(framed.x, framed.y, framed.w, framed.h);
      const pickedRow = layers.find((l) => l["id"] === picked);
      if (pickedRow && !isBackground(layers, pickedRow) && !isWords(pickedRow) && place) {
        for (const [x, y] of [
          [framed.x, framed.y],
          [framed.x + framed.w, framed.y],
          [framed.x + framed.w, framed.y + framed.h],
          [framed.x, framed.y + framed.h],
        ]) {
          c.fillStyle = "#fff";
          c.fillRect(x! - handle / 2, y! - handle / 2, handle, handle);
          c.strokeRect(x! - handle / 2, y! - handle / 2, handle, handle);
        }
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
    const w = placed.current, g = w.now!;
    const dx = (p.x - g.at.x) / w.scale, dy = (p.y - g.at.y) / w.scale, close = 8 / w.scale;
    const snaps = snap && !e.altKey;
    const to =
      g.what === "moving"
        ? moved(g.from, dx, dy, w.lines, W, H, close, snaps)
        : g.what === "sliding"
          ? slid(g.from, dx, dy, g.across, g.down, W, H, close, snaps)
          : resized(g.from, g.corner, dx, dy, !e.shiftKey, w.lines, close, snaps);
    w.guide = to;
    return to.at;
  };
  const pickRow = (id: string) => pick && setPicked(pick, id || undefined);

  const press = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (full) {
      void document.exitFullscreen();
      return;
    }
    const w = placed.current;
    if (w.scale <= 0) return;
    const p = at(e);
    const pickedRow = layers.find((l) => l["id"] === picked);
    if (pickedRow && place && !isBackground(layers, pickedRow) && !isWords(pickedRow)) {
      const b = whereIs(pickedRow);
      const corner = cornerAt(onBoard(b), p, handle);
      if (corner >= 0) {
        w.now = { what: "resizing", id: picked, from: b, at: p, corner, across: [0, 0], down: [0, 0] };
        w.lines = linesFor(picked);
        e.currentTarget.setPointerCapture(e.pointerId);
        return;
      }
    }
    for (let i = layers.length - 1; i >= 0; i--) {
      const l = layers[i]!;
      if (i === 0 && !isWords(l)) break;
      const b = whereIs(l);
      const on = onBoard(b);
      if (p.x < on.x || p.x > on.x + on.w || p.y < on.y || p.y > on.y + on.h) continue;
      const id = str(l["id"]);
      pickRow(id);
      if (place) {
        w.now = { what: "moving", id, from: b, at: p, corner: 0, across: [0, 0], down: [0, 0] };
        w.lines = linesFor(id);
        e.currentTarget.setPointerCapture(e.pointerId);
      }
      return;
    }
    const inside = p.x >= w.board.x && p.x <= w.board.x + w.board.w && p.y >= w.board.y && p.y <= w.board.y + w.board.h;
    if (!inside || layers.length === 0 || isWords(layers[0]!)) {
      pickRow("");
      return;
    }
    const bottom = layers[0]!;
    pickRow(str(bottom["id"]));
    const b = whereIs(bottom);
    if (slide && slides(backdrop, b)) {
      w.now = { what: "sliding", id: str(bottom["id"]), from: b, at: p, corner: 0, across: slideRange(W, b.w), down: slideRange(H, b.h) };
      e.currentTarget.setPointerCapture(e.pointerId);
    }
  };
  const move = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const w = placed.current;
    if (!w.now) return;
    w.going = { id: w.now.id, box: goingTo(at(e), e) };
    paint();
  };
  const release = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const w = placed.current;
    const g = w.now;
    if (!g) return;
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
    if (g.what === "sliding") {
      const fraction = (v: number, [least, most]: [number, number]) => (most > least ? (v - least) / (most - least) : 0.5);
      void runner.reply(slide!, { id: subject, slide_x: fraction(to.x, g.across), slide_y: fraction(to.y, g.down) }).then(back);
    } else {
      void runner.reply(place!, { id: g.id, x: to.x, y: to.y, w: to.w, h: to.h }).then(back);
    }
    paint();
  };
  const cursorAt = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const w = placed.current;
    if (w.now) return;
    const p = at(e);
    let cursor = "default";
    const pickedRow = layers.find((l) => l["id"] === picked);
    const corner = pickedRow && place && !isBackground(layers, pickedRow) && !isWords(pickedRow) ? cornerAt(onBoard(whereIs(pickedRow)), p, handle) : -1;
    if (corner === 0 || corner === 2) cursor = "nwse-resize";
    else if (corner === 1 || corner === 3) cursor = "nesw-resize";
    else if (layers.some((l, i) => (i > 0 || isWords(l)) && ((on) => p.x >= on.x && p.x <= on.x + on.w && p.y >= on.y && p.y <= on.y + on.h)(onBoard(whereIs(l))))) cursor = place ? "move" : "pointer";
    else if (slide && layers.length > 0 && !isWords(layers[0]!) && slides(backdrop, whereIs(layers[0]!))) cursor = "grab";
    e.currentTarget.style.cursor = cursor;
  };

  // Esc lets the picked layer go; the arrows nudge it, by ten with Shift.
  useKeys(picked ? ["escape"] : undefined, () => pickRow(""));
  const nudge = (dx: number, dy: number) => {
    const l = layers.find((r) => r["id"] === picked);
    if (!l || !place || isBackground(layers, l)) return;
    const b = boxOf(l);
    const to = { x: b.x + dx, y: b.y + dy, w: b.w, h: b.h };
    placed.current.going = { id: picked, box: to };
    void runner.reply(place, { id: picked, ...to });
    paint();
  };
  useKeys(picked && place ? ["left"] : undefined, () => nudge(-1, 0));
  useKeys(picked && place ? ["right"] : undefined, () => nudge(1, 0));
  useKeys(picked && place ? ["up"] : undefined, () => nudge(0, -1));
  useKeys(picked && place ? ["down"] : undefined, () => nudge(0, 1));
  useKeys(picked && place ? ["shift+left"] : undefined, () => nudge(-10, 0));
  useKeys(picked && place ? ["shift+right"] : undefined, () => nudge(10, 0));
  useKeys(picked && place ? ["shift+up"] : undefined, () => nudge(0, -10));
  useKeys(picked && place ? ["shift+down"] : undefined, () => nudge(0, 10));

  // The buttons over the picked layer, just above it, or below when there's no room.
  const overAt = frame && children && !full ? { left: frame.x + frame.w / 2, top: frame.y - 12 } : undefined;
  const below = overAt !== undefined && overAt.top < 52;
  return (
    <div ref={holder} className="one-collage" style={{ position: "relative", flex: 1, minHeight: fills ? 0 : 560, width: "100%", height: fills ? "100%" : 560 }}>
      <canvas
        ref={canvasRef}
        role="img"
        aria-label="A picture made of layers"
        style={{ position: "absolute", inset: 0, width: "100%", height: "100%", touchAction: "none" }}
        onPointerDown={press}
        onPointerMove={(e) => (placed.current.now ? move(e) : cursorAt(e))}
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
          <OverLayer.Provider value>{children}</OverLayer.Provider>
        </div>
      )}
    </div>
  );
}
