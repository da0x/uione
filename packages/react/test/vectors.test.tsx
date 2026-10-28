// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// SVG coming in: known by what it is, sized from what it says, never read as pixels.

import { pictureSize, svgIn, svgSize } from "../src/vectors.js";

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
});
