// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a collage's layers go, held to the same cases as one::screens's collage.cpp,
// so the web puts a layer where the desktop and the terminal do.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { backgroundAt, cornerAt, cropEdgesAt, cropped, moved, resized, sitting, slid, slides, snapLines, wholeOf } from "../src/collage.js";
import type { Backdrop, Box, Guided } from "../src/collage.js";

type Numbers = number[];
interface Case {
  what: string;
  sits?: string;
  size?: Numbers;
  zoom?: number;
  slide?: Numbers;
  picture?: Numbers;
  at?: Numbers;
  box?: Numbers;
  shown?: Numbers;
  crop?: Numbers;
  others?: Numbers[];
  across?: Numbers;
  down?: Numbers;
  from?: Numbers;
  by?: Numbers;
  corner?: number;
  keep_shape?: boolean;
  close?: number;
  snapping?: boolean;
  slides?: boolean;
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
const backdropOf = (k: Case): Backdrop => ({
  sits: k.sits ?? "",
  width: k.size?.[0] ?? 0,
  height: k.size?.[1] ?? 0,
  zoom: k.zoom ?? 1,
  slideX: k.slide?.[0] ?? 0.5,
  slideY: k.slide?.[1] ?? 0.5,
});
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
    expect(cases.length).toBeGreaterThanOrEqual(40);
  });

  it.each(cases.map((k, i) => [`${i + 1}, ${k.what}`, k] as const))("case %s", (_, k) => {
    const b = backdropOf(k);
    switch (k.what) {
      case "sitting":
        expect(same(sitting(b.sits, k.picture![0]!, k.picture![1]!, b.width, b.height), boxOf(k.box!))).toBe(true);
        break;
      case "background_at":
        expect(backgroundAt(b, k.picture![0]!, k.picture![1]!)).toSatisfy((got: Box) => same(got, boxOf(k.box!)));
        break;
      case "slides":
        expect(slides(b, boxOf(k.at!))).toBe(k.slides);
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
      case "slid": {
        const got = slid(boxOf(k.from!), k.by![0]!, k.by![1]!, k.across as [number, number], k.down as [number, number], b.width, b.height, k.close!, k.snapping!);
        expect(same(got.at, boxOf(k.box!)) && guidedAs(got, k)).toBe(true);
        break;
      }
      case "cropped":
        expect(same(cropped(boxOf(k.kept!), k.edges!, k.by![0]!, k.by![1]!, boxOf(k.whole!)), boxOf(k.box!))).toBe(true);
        break;
      case "corner_at":
        expect(cornerAt(boxOf(k.box!), { x: k.at![0]!, y: k.at![1]! }, k.handle!)).toBe(k.corner);
        break;
      case "crop_edges_at":
        expect(cropEdgesAt(boxOf(k.box!), { x: k.at![0]!, y: k.at![1]! }, k.handle!)).toBe(k.edges);
        break;
      default:
        throw new Error(`there's no case of ${k.what}`);
    }
  });
});
