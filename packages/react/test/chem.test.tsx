// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// chem.ts held to math/tests/chem.tsv, as one::math's chem.hpp is: every chemical
// equation there read as the reaction beside it, or turned down for the reason beside it;
// and one that reads balanced, or not, and weighed, as it says.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { chemUnbalanced, elements, molarMass, parseChem, writtenChem } from "../src/chem.js";

const here = dirname(fileURLToPath(import.meta.url));
const cases = readFileSync(join(here, "../../../math/tests/chem.tsv"), "utf8")
  .split("\n")
  .filter((line) => line && !line.startsWith("#") && line.includes("\t"))
  .map((line) => line.split("\t") as [string, string, string?, string?]);

describe("chemical equations read as one::math reads them", () => {
  it("has the cases, and every element", () => {
    expect(cases.length).toBeGreaterThan(20);
    expect(elements).toHaveLength(118);
  });
  it.each(cases)("%s", (written, wanted, balance, mass) => {
    const read = parseChem(written);
    const got = "reaction" in read ? writtenChem(read.reaction) : `! ${read.problem.says}`;
    if (wanted.startsWith("! ")) return expect(got.startsWith(wanted)).toBe(true);
    expect(got).toBe(wanted);
    if (!("reaction" in read)) return;
    expect(chemUnbalanced(read.reaction) ?? "balanced").toBe(balance);
    const weighed = molarMass(read.reaction);
    expect(weighed === undefined ? "-" : weighed.toFixed(2)).toBe(mass);
  });
});
