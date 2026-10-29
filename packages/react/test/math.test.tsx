// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// math.ts held to math/tests/cases.tsv, as one::math is: every equation there read as the
// tree beside it, or turned down for the reason beside it.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";
import { Equation } from "../src/equation.js";
import { layOutMath, mathFont, parseMath, writtenMath } from "../src/math.js";

const here = dirname(fileURLToPath(import.meta.url));
const cases = readFileSync(join(here, "../../../math/tests/cases.tsv"), "utf8")
  .split("\n")
  .filter((line) => line && !line.startsWith("#") && line.includes("\t"))
  .map((line) => line.split("\t") as [string, string]);

describe("equations read as one::math reads them", () => {
  it("has the cases", () => expect(cases.length).toBeGreaterThan(30));
  it.each(cases)("%s", (written, wanted) => {
    const read = parseMath(written);
    const got = "node" in read ? writtenMath(read.node) : `! ${read.problem.says}`;
    if (wanted.startsWith("! ")) expect(got.startsWith(wanted)).toBe(true);
    else expect(got).toBe(wanted);
  });
});

// Every equation that reads set as one::math sets it: the same size, and the same glyphs,
// rules and lines in the same places, to within a hundredth of a pixel.
const layouts = readFileSync(join(here, "../../../math/tests/layouts.txt"), "utf8")
  .split("== ")
  .slice(1)
  .map((block) => {
    const [written, ...lines] = block.trimEnd().split("\n");
    return [written!, lines] as const;
  });

describe("an equation typed on a phone", () => {
  it("reads its keyboard's spaces, quotes and dashes as what they stand for", () => {
    for (const [typed, plain] of [
      ["x\u00a0=\u00a02", "x = 2"],
      ["x\u2009= 2", "x = 2"],
      ["a\u2019 = 2", "a' = 2"],
      ["x = a \u2014 b", "x = a \u2212 b"],
    ]) {
      expect(parseMath(typed!)).toEqual(parseMath(plain!));
      expect("node" in parseMath(typed!)).toBe(true);
    }
  });
});

describe("equations set as one::math sets them", () => {
  it("has the layouts", () => expect(layouts.length).toBeGreaterThan(30));
  it.each(layouts)("%s", (written, lines) => {
    const read = parseMath(written);
    if (!("node" in read)) throw new Error(read.problem.says);
    const laid = layOutMath(read.node, 20, true);
    const numbers = (line: string) => line.split(/[ ,]/).filter((t) => t !== "" && !Number.isNaN(Number(t))).map(Number);
    const near = (got: number[], want: number[]) => {
      expect(got.length).toBe(want.length);
      got.forEach((g, i) => expect(Math.abs(g - want[i]!)).toBeLessThanOrEqual(0.011));
    };
    near([laid.width, laid.ascent, laid.descent], numbers(lines[0]!));
    expect(laid.drawn.length).toBe(lines.length - 1);
    laid.drawn.forEach((d, i) => {
      const line = lines[i + 1]!;
      const kind = line.split(" ")[0];
      expect(d.type).toBe(kind);
      if (d.type === "glyph") {
        expect(d.code.toString(16)).toBe(line.split(" ")[1]);
        near([d.x, d.y, d.size], numbers(line.split(" ").slice(2).join(" ")));
      } else if (d.type === "rule") near([d.x, d.y, d.width, d.height], numbers(line));
      else near([d.size, ...d.points.flat()], numbers(line));
    });
  });
});

describe("an equation drawn on a page", () => {
  it("draws its glyphs in the math font, its rules and its lines, as wide as it's set, in ems", () => {
    const html = renderToStaticMarkup(<Equation written="x = (-b +- sqrt(b^2 - 4a c)) / 2a" displayed />);
    expect(html).toContain('role="img"');
    expect(html).toContain(`font-family="${mathFont}"`);
    expect(html).toContain("<rect");
    expect(html).toContain("<polyline");
    expect(html).toMatch(/width="[0-9.]+em"/);
  });
  it("shows one that can't be read as it's written, saying why", () => {
    const html = renderToStaticMarkup(<Equation written="(a + b" />);
    expect(html).toContain("<code");
    expect(html).toContain("a ) to close");
  });
});
