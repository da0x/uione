// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The elements at their places in the periodic table, in their families, and a reaction
// drawn as their squares over its equation.

import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";
import { parseChem } from "../src/chem.js";
import { elementOf, ReactionSquares, table } from "../src/elements.js";

describe("the elements", () => {
  it("puts each in its family", () => {
    const families = ["H", "Na", "Ca", "Fe", "Ce", "U", "Al", "Si", "Cl", "Ne"].map((s) => elementOf(s)?.family);
    expect(families).toEqual(["nonmetal", "alkali", "alkaline", "transition", "lanthanide", "actinide", "post-transition", "metalloid", "halogen", "noble"]);
  });

  it("names and weighs every one", () => {
    expect(table.length).toBe(118);
    expect(table.every((e) => e.name && e.weight > 0)).toBe(true);
    expect(elementOf("Og")).toMatchObject({ number: 118, name: "Oganesson" });
  });

  it("draws a reaction as its elements' squares, with its numbers, counts and arrow", () => {
    const read = parseChem("2Na + Cl2 -> 2NaCl");
    if (!("reaction" in read)) throw new Error(read.problem.says);
    const drawn = renderToStaticMarkup(<ReactionSquares reaction={read.reaction} />);
    expect(drawn.match(/aria-label="Sodium"/g)?.length).toBe(2);
    expect(drawn.match(/aria-label="Chlorine"/g)?.length).toBe(2);
    expect(drawn).toContain("→");
    expect(drawn).toContain(">2<");
  });

  it("draws a group in its brackets, and an electron as it's written", () => {
    const read = parseChem("Ca(OH)2 + e^- -> Ca");
    if (!("reaction" in read)) throw new Error(read.problem.says);
    const drawn = renderToStaticMarkup(<ReactionSquares reaction={read.reaction} />);
    expect(drawn).toContain(">(<");
    expect(drawn).toContain(">)<");
    expect(drawn).toContain("e<span");
  });
});
