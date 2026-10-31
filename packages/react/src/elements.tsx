// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The elements as the periodic table shows them: each a square with its number, symbol
// and weight, tinted by its family, at its place in the table; and a reaction drawn as
// those squares, 2[H]₂ + [O]₂ → 2[H]₂[O], over its equation.

import type { CSSProperties, ReactNode } from "react";
import { elements, weights } from "./chem.js";
import type { ChemFormula, ChemReaction, ChemUnit } from "./chem.js";

const names = [
  "Hydrogen", "Helium", "Lithium", "Beryllium", "Boron", "Carbon", "Nitrogen", "Oxygen", "Fluorine", "Neon",
  "Sodium", "Magnesium", "Aluminum", "Silicon", "Phosphorus", "Sulfur", "Chlorine", "Argon", "Potassium", "Calcium",
  "Scandium", "Titanium", "Vanadium", "Chromium", "Manganese", "Iron", "Cobalt", "Nickel", "Copper", "Zinc",
  "Gallium", "Germanium", "Arsenic", "Selenium", "Bromine", "Krypton", "Rubidium", "Strontium", "Yttrium", "Zirconium",
  "Niobium", "Molybdenum", "Technetium", "Ruthenium", "Rhodium", "Palladium", "Silver", "Cadmium", "Indium", "Tin",
  "Antimony", "Tellurium", "Iodine", "Xenon", "Cesium", "Barium", "Lanthanum", "Cerium", "Praseodymium", "Neodymium",
  "Promethium", "Samarium", "Europium", "Gadolinium", "Terbium", "Dysprosium", "Holmium", "Erbium", "Thulium", "Ytterbium",
  "Lutetium", "Hafnium", "Tantalum", "Tungsten", "Rhenium", "Osmium", "Iridium", "Platinum", "Gold", "Mercury",
  "Thallium", "Lead", "Bismuth", "Polonium", "Astatine", "Radon", "Francium", "Radium", "Actinium", "Thorium",
  "Protactinium", "Uranium", "Neptunium", "Plutonium", "Americium", "Curium", "Berkelium", "Californium", "Einsteinium", "Fermium",
  "Mendelevium", "Nobelium", "Lawrencium", "Rutherfordium", "Dubnium", "Seaborgium", "Bohrium", "Hassium", "Meitnerium", "Darmstadtium",
  "Roentgenium", "Copernicium", "Nihonium", "Flerovium", "Moscovium", "Livermorium", "Tennessine", "Oganesson",
];

export type Family = "alkali" | "alkaline" | "transition" | "lanthanide" | "actinide" | "post-transition" | "metalloid" | "nonmetal" | "halogen" | "noble";

export interface Element {
  number: number;
  symbol: string;
  name: string;
  weight: number;
  row: number; // 1 to 7 its period's, and 9 and 10 the lanthanides' and actinides' under the table
  column: number; // 1 to 18
  family: Family;
}

// Where an element is in the table, by its number: the s block at the left, p at the
// right, d between, and f in two rows of its own under the rest.
function place(n: number): [row: number, column: number] {
  if (n === 1) return [1, 1];
  if (n === 2) return [1, 18];
  const starts = [3, 11, 19, 37, 55, 87];
  const period = starts.filter((s) => n >= s).length + 1;
  const o = n - starts[period - 2]! + 1;
  if (period <= 3) return [period, o <= 2 ? o : o + 10];
  if (period <= 5) return [period, o];
  if (o <= 2) return [period, o];
  if (o <= 17) return [period + 3, o];
  return [period, o - 14];
}

const halogens = ["F", "Cl", "Br", "I", "At", "Ts"];
const metalloids = ["B", "Si", "Ge", "As", "Sb", "Te"];
const nonmetals = ["H", "C", "N", "O", "P", "S", "Se"];

function familyOf(symbol: string, n: number, column: number): Family {
  if (column === 18) return "noble";
  if (halogens.includes(symbol)) return "halogen";
  if (metalloids.includes(symbol)) return "metalloid";
  if (nonmetals.includes(symbol)) return "nonmetal";
  if (n >= 57 && n <= 71) return "lanthanide";
  if (n >= 89 && n <= 103) return "actinide";
  if (column === 1) return "alkali";
  if (column === 2) return "alkaline";
  if (column <= 12) return "transition";
  return "post-transition";
}

export const table: Element[] = elements.map((symbol, at) => {
  const [row, column] = place(at + 1);
  return { number: at + 1, symbol, name: names[at]!, weight: weights[symbol]!, row, column, family: familyOf(symbol, at + 1, column) };
});
const bySymbol = new Map(table.map((e) => [e.symbol, e]));
export const elementOf = (symbol: string) => bySymbol.get(symbol);

// Each family's hue, mixed into the page's own colors so it reads by day and at night.
const hues: Record<Family, string> = {
  alkali: "#e5484d",
  alkaline: "#f76b15",
  transition: "#d4a017",
  lanthanide: "#d6409f",
  actinide: "#8e4ec6",
  "post-transition": "#12a594",
  metalloid: "#46a758",
  nonmetal: "#0090ff",
  halogen: "#00a2c7",
  noble: "#6e56cf",
};
export const tint = (family: Family): CSSProperties => ({
  background: `color-mix(in srgb, ${hues[family]} 16%, var(--color-surface, #ffffff))`,
  borderColor: `color-mix(in srgb, ${hues[family]} 60%, var(--color-surface, #ffffff))`,
});

const weighed = (w: number) => (Number.isInteger(w) ? `(${w})` : String(w));

// An element's square, in ems of the words around it: its number at the top, its symbol
// large, and its weight under it; named too when it's drawn whole, as a key held is.
export function ElementSquare({ element: e, whole = false, style }: { element: Element; whole?: boolean; style?: CSSProperties }) {
  return (
    <span
      role="img"
      aria-label={e.name}
      title={`${e.name}, ${e.number}`}
      style={{
        display: "inline-grid",
        gridTemplateRows: whole ? "auto 1fr auto auto" : "auto 1fr auto",
        width: whole ? "5.5em" : "2.5em",
        height: whole ? "5.5em" : "2.5em",
        boxSizing: "border-box",
        padding: whole ? "0.35em 0.4em" : "0.12em 0.18em",
        border: "1px solid",
        borderRadius: whole ? "0.4em" : "0.2em",
        color: "var(--color-ink, #1b2230)",
        fontFamily: "var(--font-sans, sans-serif)",
        fontStyle: "normal",
        lineHeight: 1,
        verticalAlign: "middle",
        ...tint(e.family),
        ...style,
      }}
    >
      <span style={{ fontSize: whole ? "0.85em" : "0.5em", textAlign: "left" }}>{e.number}</span>
      <span style={{ alignSelf: "center", textAlign: "center", fontSize: whole ? "2.2em" : "1.05em", fontWeight: 700 }}>{e.symbol}</span>
      {whole && <span style={{ textAlign: "center", fontSize: "0.75em", whiteSpace: "nowrap" }}>{e.name}</span>}
      <span style={{ textAlign: "center", fontSize: whole ? "0.7em" : "0.4em", opacity: 0.75, marginTop: whole ? "0.2em" : 0 }}>{weighed(e.weight)}</span>
    </span>
  );
}

const small = (words: string, style: CSSProperties): ReactNode => (words ? <span style={{ fontSize: "0.7em", ...style }}>{words}</span> : null);

function Units({ units }: { units: ChemUnit[] }) {
  return (
    <>
      {units.map((u, at) => {
        const known = u.element ? elementOf(u.element) : undefined;
        return (
          <span key={at} style={{ display: "inline-flex", alignItems: "flex-end", gap: "0.08em" }}>
            {known ? (
              <ElementSquare element={known} />
            ) : u.element ? (
              <span>{u.element}</span>
            ) : (
              <span style={{ display: "inline-flex", flexWrap: "wrap", alignItems: "center", gap: "0.08em" }}>
                <span style={{ fontSize: "1.6em", fontWeight: 300 }}>{u.bracket}</span>
                <Units units={u.inside ?? []} />
                <span style={{ fontSize: "1.6em", fontWeight: 300 }}>{u.bracket === "[" ? "]" : ")"}</span>
              </span>
            )}
            {small(u.count, { marginBottom: "-0.1em" })}
          </span>
        );
      })}
    </>
  );
}

function Formula({ formula: f }: { formula: ChemFormula }) {
  if (f.electron) return <span>e{small("−", { verticalAlign: "super" })}</span>;
  return (
    <span style={{ display: "inline-flex", flexWrap: "wrap", alignItems: "center", gap: "0.12em" }}>
      {f.number && <span>{f.number}</span>}
      <Units units={f.units} />
      {small(f.charge.replace("-", "−"), { alignSelf: "flex-start" })}
    </span>
  );
}

const arrowFaces: Record<string, string> = { "->": "→", "<-": "←", "<->": "↔", "<=>": "⇌", "=": "=" };

// A reaction as the elements' squares: each taking part as many as its number says, of
// its formulas' squares, each as many as its count; + between them, and its arrows.
export function ReactionSquares({ reaction }: { reaction: ChemReaction }) {
  const parts: ReactNode[] = [];
  reaction.sides.forEach((side, s) => {
    if (s > 0) parts.push(<span key={`a${s}`} style={{ fontSize: "1.3em" }}>{arrowFaces[reaction.arrows[s - 1]!.kind]}</span>);
    side.forEach((term, t) => {
      if (t > 0) parts.push(<span key={`p${s}.${t}`}>+</span>);
      parts.push(
        <span key={`t${s}.${t}`} style={{ display: "inline-flex", flexWrap: "wrap", alignItems: "center", justifyContent: "center", gap: "0.2em" }}>
          {term.number && <span style={{ fontWeight: 600 }}>{term.number}</span>}
          {term.formulas.map((f, at) => (
            <span key={at} style={{ display: "inline-flex", alignItems: "center", gap: "0.2em" }}>
              {at > 0 && <span>·</span>}
              <Formula formula={f} />
            </span>
          ))}
        </span>,
      );
    });
  });
  return <div style={{ display: "flex", flexWrap: "wrap", alignItems: "center", justifyContent: "center", gap: "0.35em 0.5em", marginBottom: "0.6em", fontFamily: "var(--font-sans, sans-serif)" }}>{parts}</div>;
}
