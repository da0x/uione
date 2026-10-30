// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// SVG coming in: known by what it is, sized from what it says, never read as pixels.

import { absolutePath, pictureSize, svgIn, svgSize } from "../src/vectors.js";

const red = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 12"><rect width="24" height="12" fill="#e4572e"/></svg>';

describe("SVG coming in", () => {
  it("is known by its type, its name, or what it starts with, and nothing else is", async () => {
    expect(await svgIn(new File([red], "icon.svg"))).toBe(red);
    expect(await svgIn(new Blob(["﻿  <?xml version=\"1.0\"?>" + red]))).toContain("<svg");
    expect(await svgIn(new Blob([red], { type: "image/svg+xml" }))).toBe(red);
    expect(await svgIn(new File(["\x89PNG"], "a.png", { type: "image/png" }))).toBeUndefined();
    expect(await svgIn(new Blob(["just words"]))).toBeUndefined();
  });

  it("is as big as its width and height say, or its viewBox when they don't, and a percent says nothing", async () => {
    expect(svgSize(red)).toEqual({ w: 24, h: 12 });
    expect(svgSize('<svg xmlns="http://www.w3.org/2000/svg" width="40px" height="20" viewBox="0 0 4 2"/>')).toEqual({ w: 40, h: 20 });
    expect(svgSize('<svg xmlns="http://www.w3.org/2000/svg" width="100%" height="100%" viewBox="0 0 8 6"/>')).toEqual({ w: 8, h: 6 });
    expect(svgSize('<svg xmlns="http://www.w3.org/2000/svg"/>')).toBeUndefined();
    expect(await pictureSize(new File([red], "icon.svg"))).toEqual({ w: 24, h: 12 });
  });

  it("leaves any other picture's size to the browser, the way up its camera says it was held", async () => {
    const was = globalThis.createImageBitmap;
    const asked: unknown[] = [];
    globalThis.createImageBitmap = (async (_: Blob, options?: unknown) => {
      asked.push(options);
      return { width: 20, height: 40, close() {} };
    }) as unknown as typeof createImageBitmap;
    try {
      expect(await pictureSize(new File(["\xff\xd8"], "harbor.jpg", { type: "image/jpeg" }))).toEqual({ w: 20, h: 40 });
      expect(asked).toEqual([{ imageOrientation: "from-image" }]);
    } finally {
      globalThis.createImageBitmap = was;
    }
  });

  it("reads a path's commands as only M, L, C and Z, its points where they are", () => {
    // Relative moves and lines, across and down, and a close back to where it began.
    expect(absolutePath("m10 20 l5 0 h5 v5 H10 z m1 1")).toBe("M 10 20 L 15 20 L 20 20 L 20 25 L 10 25 Z M 11 21");
    // A smooth curve mirrors the last control point; a quadratic is its cubic.
    expect(absolutePath("M0 0 C0 10 10 10 10 0 S20 -10 20 0")).toBe("M 0 0 C 0 10 10 10 10 0 C 10 -10 20 -10 20 0");
    expect(absolutePath("M0 0 Q 3 3 6 0")).toBe("M 0 0 C 2 2 4 2 6 0");
    // Pairs after a move are lines, and a transform moves every point.
    expect(absolutePath("M0 0 10 0 10 10", (x, y) => [x * 2 + 1, y * 2])).toBe("M 1 0 L 21 0 L 21 20");
  });

  it("draws an arc as cubic curves that end where it does, a half circle as two", () => {
    const half = absolutePath("M0 0 A 5 5 0 0 1 10 0");
    expect(half.match(/C/g)?.length).toBe(2);
    expect(half.endsWith(" 10 0")).toBe(true);
    // Through the top of the circle, at its middle.
    expect(half).toContain(" 5 -5");
  });
});
