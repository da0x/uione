// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { generate, roles, schemes, written, type Scheme } from "../src/palette.js";

// The themes some seeds make, as one::screens makes them in C++: a seed is the same
// theme in every app.
describe("a theme from a palette", () => {
  it("is the one palettes.txt says each seed and scheme make", () => {
    const kept = readFileSync(resolve(process.cwd(), "../../screens/tests/golden/palettes.txt"), "utf8").trim().split("\n");
    expect(kept.length).toBe(45);
    for (const line of kept) {
      const [seed, scheme, ...colors] = line.split(" ");
      const made = generate(Number(seed), scheme as Scheme);
      const said = roles.flatMap((role) => [made.colors[role].light, made.colors[role].dark]);
      expect(`${seed} ${scheme} ${said.join(" ")}`).toBe(`${seed} ${scheme} ${colors.join(" ")}`);
    }
  });

  it("is written as a project says one, to paste in", () => {
    const said = written(generate(48213, "triadic"));
    expect(said.startsWith('define theme triadic_48213 "Triadic 48213" {\n\tpage     #')).toBe(true);
    expect(said.endsWith("}\n")).toBe(true);
    expect(schemes).toContain("split_complementary");
  });
});
