// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a collage's layers go, held to the same cases as one::screens's collage.cpp,
// so the web puts a layer where the desktop and the terminal do.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { colorShifter, svgElement, cornerAt, cropEdgesAt, cropped, drawn, drawnLine, svgDocument, halo, harmonicFill, matched, moved, resized, shifted, sitting, snapLines, statsOf, wholeOf } from "../src/collage.js";
import type { Box, Guided } from "../src/collage.js";

type Numbers = number[];
interface Case {
  what: string;
  shape?: { kind: string; points: string; fill: string; stroke: string; stroke_width: number; corners: number }; // svg: a shape's parts
  svg?: string;
  // svg_document: each layer as it's saved
  layers?: {
    kind: "shape" | "words" | "picture" | "svg";
    box: Numbers;
    opacity?: number;
    shape?: { kind: string; points: string; fill: string; stroke: string; stroke_width: number; corners: number };
    words?: string;
    color?: string;
    family?: string;
    size?: number;
    weight?: number;
    italic?: boolean;
    href?: string;
    crop?: Numbers;
    turns?: number;
    mirrored?: boolean;
    corners?: number;
    svg?: string;
    natural?: Numbers;
  }[];
  covered?: Numbers[]; // harmonic_fill: x, y and three channels of each covered cell
  cells?: Numbers[]; // halo: x, y and three channels of each of the picture's cells; stats_of: three channels of each
  weight?: number;
  mean?: Numbers;
  spread?: Numbers;
  pictures?: { mean: Numbers; spread: Numbers; tones?: Numbers; weight: number }[];
  tones?: Numbers;
  shift?: { gain: Numbers; offset: Numbers; from?: Numbers; to?: Numbers };
  rgb?: Numbers;
  cast?: number;
  strength?: number;
  shifts?: { gain: Numbers; offset: Numbers; from?: Numbers; to?: Numbers }[];
  reach?: number;
  inner?: number;
  expect?: Numbers[];
  sits?: string;
  size?: Numbers;
  picture?: Numbers;
  at?: Numbers;
  box?: Numbers;
  shown?: Numbers;
  crop?: Numbers;
  others?: Numbers[];
  across?: Numbers;
  down?: Numbers;
  from?: Numbers;
  to?: Numbers;
  square?: boolean;
  straight?: boolean;
  points?: string;
  click?: number;
  by?: Numbers;
  corner?: number;
  keep_shape?: boolean;
  close?: number;
  snapping?: boolean;
  kept?: Numbers;
  edges?: number;
  whole?: Numbers;
  handle?: number;
  guide_x?: number;
  guide_y?: number;
  guide_corner?: Numbers[];
}

const here = dirname(fileURLToPath(import.meta.url));
const cases = (JSON.parse(readFileSync(join(here, "../../../screens/tests/collage-cases.json"), "utf8")) as { cases: Case[] }).cases;

const boxOf = (n: Numbers): Box => ({ x: n[0]!, y: n[1]!, w: n[2]!, h: n[3]! });
const near = (a: number, b: number) => Math.abs(a - b) < 1e-6;
const same = (a: Box, b: Box) => near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h);
const sizeOf = (k: Case) => ({ width: k.size?.[0] ?? 0, height: k.size?.[1] ?? 0 });
// A gesture's guides as the case says them, or none.
const guidedAs = (g: Guided, k: Case) =>
  (k.guide_x === undefined ? g.guideX === undefined : g.guideX !== undefined && near(g.guideX, k.guide_x)) &&
  (k.guide_y === undefined ? g.guideY === undefined : g.guideY !== undefined && near(g.guideY, k.guide_y)) &&
  (k.guide_corner === undefined
    ? g.corner === undefined
    : g.corner !== undefined &&
      near(g.corner[0].x, k.guide_corner[0]![0]!) &&
      near(g.corner[0].y, k.guide_corner[0]![1]!) &&
      near(g.corner[1].x, k.guide_corner[1]![0]!) &&
      near(g.corner[1].y, k.guide_corner[1]![1]!));

describe("where a collage's layers go", () => {
  it("has the cases the desktop is held to", () => {
    expect(cases.length).toBeGreaterThanOrEqual(30);
  });

  it.each(cases.map((k, i) => [`${i + 1}, ${k.what}`, k] as const))("case %s", (_, k) => {
    const b = sizeOf(k);
    switch (k.what) {
      case "sitting":
        expect(same(sitting(k.sits ?? "", k.picture![0]!, k.picture![1]!, b.width, b.height), boxOf(k.box!))).toBe(true);
        break;
      case "whole_of":
        expect(same(wholeOf(boxOf(k.shown!), boxOf(k.crop!)), boxOf(k.box!))).toBe(true);
        break;
      case "snap_lines":
        expect(snapLines(b.width, b.height, k.others!.map(boxOf))).toEqual({ across: k.across, down: k.down });
        break;
      case "moved":
      case "resized": {
        const lines = snapLines(b.width, b.height, k.others!.map(boxOf));
        const from = boxOf(k.from!);
        const [dx, dy] = k.by as [number, number];
        const got =
          k.what === "moved"
            ? moved(from, dx, dy, lines, b.width, b.height, k.close!, k.snapping!)
            : resized(from, k.corner!, dx, dy, k.keep_shape!, lines, k.close!, k.snapping!);
        expect(same(got.at, boxOf(k.box!)) && guidedAs(got, k)).toBe(true);
        break;
      }
      case "cropped":
        expect(same(cropped(boxOf(k.kept!), k.edges!, k.by![0]!, k.by![1]!, boxOf(k.whole!)), boxOf(k.box!))).toBe(true);
        break;
      case "drawn_line": {
        const got = drawnLine({ x: k.from![0]!, y: k.from![1]! }, { x: k.to![0]!, y: k.to![1]! }, k.straight!, k.click!, b.width, b.height);
        expect([same(got.at, boxOf(k.box!)), got.points]).toEqual([true, k.points]);
        break;
      }
      case "drawn":
        expect(same(drawn({ x: k.from![0]!, y: k.from![1]! }, { x: k.to![0]!, y: k.to![1]! }, k.square!, k.click!, b.width, b.height), boxOf(k.box!))).toBe(true);
        break;
      case "corner_at":
        expect(cornerAt(boxOf(k.box!), { x: k.at![0]!, y: k.at![1]! }, k.handle!)).toBe(k.corner);
        break;
      case "crop_edges_at":
        expect(cropEdgesAt(boxOf(k.box!), { x: k.at![0]!, y: k.at![1]! }, k.handle!)).toBe(k.edges);
        break;
      case "harmonic_fill": {
        const [w, h] = k.size as [number, number];
        const colors = new Array<number>(w * h * 3).fill(0), covered = new Array<boolean>(w * h).fill(false);
        for (const c of k.covered!) {
          const at = c[1]! * w + c[0]!;
          covered[at] = true;
          for (let i = 0; i < 3; i++) colors[at * 3 + i] = c[2 + i]!;
        }
        const got = harmonicFill(w, h, colors, covered);
        for (const e of k.expect!) {
          const at = e[1]! * w + e[0]!;
          for (let i = 0; i < 3; i++) expect(Math.abs(got[at * 3 + i]! - e[2 + i]!)).toBeLessThanOrEqual(k.close!);
        }
        break;
      }
      case "halo": {
        const [w, h] = k.size as [number, number];
        const colors = new Array<number>(w * h * 3).fill(0);
        for (const c of k.cells!) for (let i = 0; i < 3; i++) colors[(c[1]! * w + c[0]!) * 3 + i] = c[2 + i]!;
        const got = halo(w, h, colors, k.reach!, k.inner!);
        for (const e of k.expect!) {
          const at = e[1]! * (w + 2 * k.reach!) + e[0]!;
          for (let i = 0; i < 4; i++) expect(Math.abs(got[at * 4 + i]! - e[2 + i]!)).toBeLessThanOrEqual(k.close!);
        }
        break;
      }
      case "stats_of": {
        const got = statsOf(k.cells!.flat(), k.weight!);
        expect(got.weight).toBe(k.weight);
        for (let i = 0; i < 3; i++) {
          expect(Math.abs(got.mean[i]! - k.mean![i]!)).toBeLessThanOrEqual(k.close!);
          expect(Math.abs(got.spread[i]! - k.spread![i]!)).toBeLessThanOrEqual(k.close!);
        }
        if (k.tones) {
          expect(got.tones?.length).toBe(k.tones.length);
          k.tones.forEach((t, i) => expect(Math.abs(got.tones![i]! - t)).toBeLessThanOrEqual(k.close!));
        }
        break;
      }
      case "matched": {
        const got = matched(
          k.pictures!.map((p) => ({ mean: p.mean as [number, number, number], spread: p.spread as [number, number, number], tones: p.tones, weight: p.weight })),
          k.strength!,
          k.cast!,
        );
        expect(got.length).toBe(k.shifts!.length);
        got.forEach((shift, j) => {
          for (let i = 0; i < 3; i++) {
            expect(Math.abs(shift.gain[i] - k.shifts![j]!.gain[i]!)).toBeLessThanOrEqual(k.close!);
            expect(Math.abs(shift.offset[i] - k.shifts![j]!.offset[i]!)).toBeLessThanOrEqual(k.close!);
          }
          for (const curve of ["from", "to"] as const) {
            const want = k.shifts![j]![curve] ?? [];
            expect(shift[curve]?.length ?? 0).toBe(want.length);
            want.forEach((t, i) => expect(Math.abs(shift[curve]![i]! - t)).toBeLessThanOrEqual(k.close!));
          }
        });
        break;
      }
      case "shifted": {
        const s = k.shift!;
        const got = shifted({ gain: s.gain as [number, number, number], offset: s.offset as [number, number, number], from: s.from, to: s.to }, k.rgb![0]!, k.rgb![1]!, k.rgb![2]!);
        const want = k.expect as unknown as number[]; // one color, red, green and blue
        for (let i = 0; i < 3; i++) expect(Math.abs(got[i] - want[i]!)).toBeLessThanOrEqual(k.close!);
        break;
      }
      case "svg_document": {
        const layers = k.layers!.map((l) => ({
          kind: l.kind,
          at: boxOf(l.box),
          opacity: l.opacity,
          drawn: l.shape && { kind: l.shape.kind, points: l.shape.points, fill: l.shape.fill, stroke: l.shape.stroke, strokeWidth: l.shape.stroke_width, corners: l.shape.corners },
          words: l.words,
          color: l.color,
          family: l.family,
          size: l.size,
          weight: l.weight,
          italic: l.italic,
          href: l.href,
          crop: l.crop && boxOf(l.crop),
          turns: l.turns,
          mirrored: l.mirrored,
          corners: l.corners,
          svg: l.svg,
          natural: l.natural && boxOf(l.natural),
        }));
        expect(svgDocument(b.width, b.height, layers)).toBe(k.svg);
        break;
      }
      case "svg": {
        const s = k.shape!;
        expect(svgElement({ kind: s.kind, points: s.points, fill: s.fill, stroke: s.stroke, strokeWidth: s.stroke_width, corners: s.corners }, k.box![0]!, k.box![1]!, k.box![2]!, k.box![3]!)).toBe(k.svg);
        break;
      }
      default:
        throw new Error(`there's no case of ${k.what}`);
    }
  });
});

describe("a shift made ready", () => {
  it("moves a picture's colors as shifted moves each one, to within rounding, as the desktop's does", () => {
    const toned = { gain: [1, 0.8, 1.2] as [number, number, number], offset: [0, 6, -9] as [number, number, number], from: [0, 40, 128, 200, 255], to: [10, 60, 120, 210, 250] };
    const plain = { gain: [1.1, 0.9, 1] as [number, number, number], offset: [-5, 0, 4] as [number, number, number] };
    for (const s of [toned, plain]) {
      const shift = colorShifter(s);
      let worst = 0, opaque = true;
      for (let r = 0; r < 256; r += 5) {
        for (let g = 0; g < 256; g += 7) {
          for (let b = 0; b < 256; b += 11) {
            const pixel = new Uint8ClampedArray([r, g, b, 255]);
            shift(pixel);
            const want = shifted(s, r, g, b).map((v) => Math.max(0, Math.min(255, Math.round(v))));
            for (let k = 0; k < 3; k++) worst = Math.max(worst, Math.abs(pixel[k]! - want[k]!));
            opaque = opaque && pixel[3] === 255;
          }
        }
      }
      expect(worst).toBeLessThanOrEqual(1);
      expect(opaque).toBe(true);
    }
  });
});
