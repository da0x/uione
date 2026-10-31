// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A chemical equation: its words as ce( ) reads them, 2H2 + O2 -> 2H2O, set as a
// textbook sets one. Written apart, it says as it's typed whether it balances, or, one
// thing alone, what a mole of it weighs.

import { chemUnbalanced, molarMass, parseChem } from "../chem.js";
import { ElementSquare, ReactionSquares, table, tint } from "../elements.js";
import { Equation } from "../equation.js";
import { parseMath } from "../math.js";
import type { ApartKeys } from "./kinds.js";
import { drawnKind, row } from "./math.js";
import type { Key } from "./math.js";

const named = (symbols: string[]): Key[] => symbols.map((e) => [e, `ce(${e})`, e]);

// Every element at its place in the periodic table, each held shown whole.
const periodic: ApartKeys = {
  name: "Elements",
  columns: 18,
  keys: table.map((e) => ({
    name: e.name,
    face: <span style={{ display: "grid", placeItems: "center", width: "100%", height: "100%", border: "1px solid", borderRadius: 3, boxSizing: "border-box", fontSize: "max(9px, min(13px, 2.4cqi))", fontWeight: 600, ...tint(e.family) }}>{e.symbol}</span>,
    put: e.symbol,
    at: [e.row, e.column] as [number, number],
    held: <ElementSquare element={e} whole />,
  })),
};

// What a reaction's written with, by what it's for.
const chemKeys: ApartKeys[] = [
  periodic,
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
];

const written = (words: string) => `ce(${words})`;

// The reaction's elements' squares over its equation, in the middle of it, the two
// placed together as the block's aligned. The squares wrap at the page's edge; an
// equation wider than the page is scrolled across.
function drawn(words: string) {
  const read = parseChem(words);
  // Kept from widening the page's column, so it's the page that's as wide as they can be.
  return (
    <div style={{ contain: "inline-size" }}>
      <div style={{ display: "inline-flex", flexDirection: "column", alignItems: "center", maxWidth: "100%", verticalAlign: "top" }}>
        {"reaction" in read && <ReactionSquares reaction={read.reaction} />}
        <div style={{ maxWidth: "100%", overflowX: "auto" }}>
          <Equation written={written(words)} displayed />
        </div>
      </div>
    </div>
  );
}

export const chem = drawnKind(
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
  (words) => {
    const read = parseMath(written(words));
    return "problem" in read ? read.problem.says : undefined;
  },
  drawn,
);
