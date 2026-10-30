// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// diagram.ts held to math/tests/diagrams.txt, as one::math's diagram.hpp is: every
// diagram there drawn with the same shapes, wires, dots and faces, each place to within a
// hundredth of an em, or turned down for the same reason at the same place.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";
import { layOutDiagram, writtenDiagram } from "../src/diagram.js";
import { DiagramView } from "../src/diagram-view.js";

const here = dirname(fileURLToPath(import.meta.url));
const diagrams = readFileSync(join(here, "../../../math/tests/diagrams.txt"), "utf8")
  .split("\n== ")
  .slice(1)
  .map((block) => {
    const [written, ...lines] = block.trimEnd().split("\n");
    return [written!, lines] as const;
  });

describe("diagrams drawn as diagram.hpp draws them", () => {
  it("has the diagrams", () => expect(diagrams.length).toBeGreaterThan(10));
  it.each(diagrams)("%s", (written, lines) => {
    const laid = layOutDiagram(written);
    if (!("diagram" in laid)) {
      expect([`! ${laid.problem.says} at ${laid.problem.at}`]).toEqual(lines);
      return;
    }
    const got = writtenDiagram(laid.diagram).trimEnd().split("\n");
    expect(got.length).toBe(lines.length);
    const numbers = (line: string) => line.split(/[ ,]/).filter((t) => t !== "" && !Number.isNaN(Number(t))).map(Number);
    got.forEach((line, i) => {
      const want = lines[i]!;
      const words = (l: string) => (l.startsWith("face ") ? l.split(" ").slice(4).join(" ") : l.replace(/[-0-9., ]+/g, " ").trim());
      expect(words(line)).toBe(words(want));
      const g = numbers(line.startsWith("face ") ? line.split(" ").slice(0, 4).join(" ") : line);
      const w = numbers(want.startsWith("face ") ? want.split(" ").slice(0, 4).join(" ") : want);
      expect(g.length).toBe(w.length);
      g.forEach((v, k) => expect(Math.abs(v - w[k]!), `${want}`).toBeLessThanOrEqual(0.011));
    });
  });
});

describe("a diagram drawn on a page", () => {
  it("draws its blocks, wires, arrows, dots and faces, as wide as it's laid out, in ems", () => {
    const written = "in r; r - y -> *k -> 1/s -> y; out y";
    const laid = layOutDiagram(written);
    if (!("diagram" in laid)) throw new Error(laid.problem.says);
    const svg = renderToStaticMarkup(<DiagramView written={written} />);
    expect(svg).toContain(`width="${laid.diagram.width}em"`);
    expect(svg.match(/<circle/g)?.length).toBe(1 + laid.diagram.dots.length);
    expect(svg.match(/<polyline/g)?.length).toBeGreaterThanOrEqual(laid.diagram.wires.length);
    expect(svg.match(/<polygon/g)?.length).toBe(1 + laid.diagram.wires.filter((w) => w.arrow).length);
  });

  it("shows one that can't be read as it's written, saying why when pointed at", () => {
    expect(renderToStaticMarkup(<DiagramView written="in x; x -> y" />)).toContain('title="nothing between x and y');
  });
});
