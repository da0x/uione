// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A chemical equation: its words as ce( ) reads them, 2H2 + O2 -> 2H2O, set as a
// textbook sets one. Written apart, it says as it's typed whether it balances, or, one
// thing alone, what a mole of it weighs.

import { chemUnbalanced, elements, molarMass, parseChem } from "../chem.js";
import type { ApartKeys } from "./kinds.js";
import { equationKind, row } from "./math.js";
import type { Key } from "./math.js";

const named = (symbols: string[]): Key[] => symbols.map((e) => [e, `ce(${e})`, e]);

// What a reaction's written with, by what it's for.
const chemKeys: ApartKeys[] = [
  row("Common", named(["H", "C", "N", "O", "F", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "K", "Ca", "Fe", "Cu", "Zn", "Ag", "Br", "I", "Ba", "Pt", "Au", "Pb"])),
  row("Arrows", [
    ["Plus", "ce(H2 + O2)", " + "],
    ["Reacts to make", "ce(H2 -> 2H)", " -> "],
    ["Equilibrium", "ce(H2 <=> 2H)", " <=> "],
    ["Both ways", "ce(H2 <-> 2H)", " <-> "],
    ["Made from", "ce(H2 <- 2H)", " <- "],
    ["With heat", "ce(H2 ->[Delta] 2H)", " ->[Delta] "],
    ["With something over the arrow", "ce(H2 ->[Pt] 2H)", " ->[‸] "],
    ["Balanced as written", "ce(H2 = 2H)", " = "],
  ]),
  row("States", [
    ["Solid", "ce(Fe(s))", "(s)"],
    ["Liquid", "ce(H2O(l))", "(l)"],
    ["Gas", "ce(O2(g))", "(g)"],
    ["Dissolved", "ce(Na^+(aq))", "(aq)"],
    ["Hydrate", "ce(CuSO4*5H2O)", "*"],
    ["Group", "ce((OH)2)", "(‸)", "(§)‸"],
    ["Brackets", "ce([Cu(NH3)4])", "[‸]", "[§]‸"],
  ]),
  row("Charges", [
    ["Plus one", "ce(Na^+)", "^+"],
    ["Minus one", "ce(Cl^-)", "^-"],
    ["Plus two", "ce(Ca^2+)", "^2+"],
    ["Minus two", "ce(O^2-)", "^2-"],
    ["Plus three", "ce(Al^3+)", "^3+"],
    ["Minus three", "ce(N^3-)", "^3-"],
    ["Electron", "ce(e^-)", "e^-"],
  ]),
  row("Elements", named(elements)),
];

export const chem = equationKind(
  {
    name: "Chemistry",
    keys: chemKeys,
    note: (words) => {
      const read = parseChem(words);
      if (!("reaction" in read)) return undefined;
      const weighed = molarMass(read.reaction);
      if (weighed !== undefined) return `${weighed.toFixed(2)} g/mol`;
      if (read.reaction.arrows.length === 0) return undefined;
      return chemUnbalanced(read.reaction) ?? "Balanced";
    },
  },
  (words) => `ce(${words})`,
);
