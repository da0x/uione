// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Chemical equations written as they'd be typed, 2H2 + O2 -> 2H2O, read as a reaction
// that's set as a textbook sets one, checked to balance, and weighed (math/README.md).
// Set through one::math, as ce(...) inside an equation. The twin of one::math's
// chem.hpp, both held to math/tests/chem.tsv and, set, to math/tests/cases.tsv.

import { isGreekName } from "./math.js";
import type { MathNode, MathProblem } from "./math.js";

// Each element by its symbol, and its standard atomic weight; for one with no stable
// isotope, its longest-lived isotope's mass number.
const weights: Record<string, number> = {
  H: 1.008, He: 4.0026, Li: 6.94, Be: 9.0122, B: 10.81, C: 12.011, N: 14.007, O: 15.999, F: 18.998, Ne: 20.18,
  Na: 22.99, Mg: 24.305, Al: 26.982, Si: 28.085, P: 30.974, S: 32.06, Cl: 35.45, Ar: 39.948, K: 39.098, Ca: 40.078,
  Sc: 44.956, Ti: 47.867, V: 50.942, Cr: 51.996, Mn: 54.938, Fe: 55.845, Co: 58.933, Ni: 58.693, Cu: 63.546, Zn: 65.38,
  Ga: 69.723, Ge: 72.63, As: 74.922, Se: 78.971, Br: 79.904, Kr: 83.798, Rb: 85.468, Sr: 87.62, Y: 88.906, Zr: 91.224,
  Nb: 92.906, Mo: 95.95, Tc: 98, Ru: 101.07, Rh: 102.91, Pd: 106.42, Ag: 107.87, Cd: 112.41, In: 114.82, Sn: 118.71,
  Sb: 121.76, Te: 127.6, I: 126.9, Xe: 131.29, Cs: 132.91, Ba: 137.33, La: 138.91, Ce: 140.12, Pr: 140.91, Nd: 144.24,
  Pm: 145, Sm: 150.36, Eu: 151.96, Gd: 157.25, Tb: 158.93, Dy: 162.5, Ho: 164.93, Er: 167.26, Tm: 168.93, Yb: 173.05,
  Lu: 174.97, Hf: 178.49, Ta: 180.95, W: 183.84, Re: 186.21, Os: 190.23, Ir: 192.22, Pt: 195.08, Au: 196.97, Hg: 200.59,
  Tl: 204.38, Pb: 207.2, Bi: 208.98, Po: 209, At: 210, Rn: 222, Fr: 223, Ra: 226, Ac: 227, Th: 232.04,
  Pa: 231.04, U: 238.03, Np: 237, Pu: 244, Am: 243, Cm: 247, Bk: 247, Cf: 251, Es: 252, Fm: 257,
  Md: 258, No: 259, Lr: 266, Rf: 267, Db: 268, Sg: 269, Bh: 270, Hs: 269, Mt: 278, Ds: 281,
  Rg: 282, Cn: 285, Nh: 286, Fl: 289, Mc: 290, Lv: 293, Ts: 294, Og: 294,
};

// The elements in the periodic table's order, as its keys are written.
export const elements = Object.keys(weights);

// What a formula is made of: an element, or a group of them in parentheses or brackets,
// each as many times as its count says.
export interface ChemUnit {
  element?: string;
  inside?: ChemUnit[];
  bracket?: "(" | "[";
  count: string; // "" for one
}

// A formula, as many as its number before it says, with its charge; or an electron.
export interface ChemFormula {
  number: string; // "" for one; 5 in CuSO4*5H2O
  units: ChemUnit[];
  charge: string; // "", "+", "2-"
  electron: boolean;
}

// One taking part, as many as its number says, of one formula or several joined, as a
// hydrate's are, and the state it's in.
export interface ChemTerm {
  number: string;
  formulas: ChemFormula[];
  state: string; // "", "s", "l", "g" or "aq"
}

// Sides, each its terms, with an arrow between each and the next, and what's over and
// under it.
export interface ChemArrow {
  kind: "->" | "<-" | "<->" | "<=>" | "=";
  over: string;
  under: string;
}
export interface ChemReaction {
  sides: ChemTerm[][];
  arrows: ChemArrow[];
}

class Failed extends Error {
  constructor(readonly problem: MathProblem) {
    super(problem.says);
  }
}

const isDigit = (c: string | undefined) => c !== undefined && c >= "0" && c <= "9";
const isUpper = (c: string | undefined) => c !== undefined && c >= "A" && c <= "Z";
const isLower = (c: string | undefined) => c !== undefined && c >= "a" && c <= "z";
const states = ["aq", "s", "l", "g"];
const arrows = ["<=>", "<->", "->", "<-", "="] as const;

class Reader {
  private at = 0;

  constructor(private readonly s: string) {}

  whole(): ChemReaction {
    this.space();
    if (this.at >= this.s.length) this.fail("something to react, like 2H2 + O2 -> 2H2O");
    const made: ChemReaction = { sides: [this.side()], arrows: [] };
    for (;;) {
      this.space();
      const arrow = arrows.find((a) => this.s.startsWith(a, this.at));
      if (!arrow) break;
      this.at += arrow.length;
      const over = this.over(), under = over ? this.over() : "";
      made.arrows.push({ kind: arrow, over, under });
      this.space();
      if (this.at >= this.s.length) this.fail(`what it makes after the arrow, like ${arrow} 2H2O`);
      made.sides.push(this.side());
    }
    this.space();
    if (this.at < this.s.length) this.fail(`a + or an arrow between what reacts, not "${this.s[this.at]}"`);
    return made;
  }

  private fail(says: string): never {
    throw new Failed({ at: this.at, says });
  }

  private space() {
    while (this.at < this.s.length && " \t".includes(this.s[this.at]!)) this.at++;
  }

  private digits(): string {
    const from = this.at;
    while (isDigit(this.s[this.at])) this.at++;
    return this.s.slice(from, this.at);
  }

  // What's over or under an arrow, in brackets right after it: ->[heat][Pt].
  private over(): string {
    if (this.s[this.at] !== "[") return "";
    const close = this.s.indexOf("]", this.at);
    if (close < 0) this.fail("a ] after what's over the arrow, like ->[heat]");
    const said = this.s.slice(this.at + 1, close).trim();
    this.at = close + 1;
    return said;
  }

  private side(): ChemTerm[] {
    const terms = [this.term()];
    for (;;) {
      const keep = this.at;
      this.space();
      if (this.s[this.at] !== "+") {
        this.at = keep;
        return terms;
      }
      this.at++;
      this.space();
      terms.push(this.term());
    }
  }

  // 2H2O(l): its number, what it is, and its state.
  private term(): ChemTerm {
    this.space();
    const number = this.number();
    if (number) this.space();
    const formulas = [this.formula()];
    // A hydrate's parts joined by a dot: CuSO4*5H2O.
    while (this.at < this.s.length && "*.·".includes(this.s[this.at]!)) {
      this.at++;
      const part = this.number();
      formulas.push({ ...this.formula(), number: part });
    }
    let state = "";
    if (this.s[this.at] === "(") {
      const close = this.s.indexOf(")", this.at);
      const inside = close < 0 ? "" : this.s.slice(this.at + 1, close);
      if (states.includes(inside)) (state = inside), (this.at = close + 1);
    }
    return { number, formulas, state };
  }

  // A number before a formula: 2, or a fraction, 1/2.
  private number(): string {
    let n = this.digits();
    if (n && this.s[this.at] === "/" && isDigit(this.s[this.at + 1])) {
      this.at++;
      n += "/" + this.digits();
    }
    return n;
  }

  private formula(): ChemFormula {
    // An electron: e, with its charge or without.
    if (this.s[this.at] === "e" && !isLower(this.s[this.at + 1])) {
      this.at++;
      const charge = this.charge();
      if (charge && charge !== "-") this.fail("an electron's charge is -, as e^-");
      return { number: "", units: [], charge: "-", electron: true };
    }
    const units = this.units();
    if (units.length === 0) {
      if (isLower(this.s[this.at])) this.fail("an element starts with a capital, like Na or H");
      this.fail(this.at >= this.s.length ? "something more to finish the equation" : `an element, like H or Na, not "${this.s[this.at]}"`);
    }
    return { number: "", units, charge: this.charge(), electron: false };
  }

  private units(): ChemUnit[] {
    const out: ChemUnit[] = [];
    for (;;) {
      const c = this.s[this.at];
      if (isUpper(c)) {
        const two = c! + (this.s[this.at + 1] ?? "");
        let symbol = c!;
        if (isLower(this.s[this.at + 1])) {
          if (!(two in weights)) this.fail(`there's no element ${two}`);
          symbol = two;
        } else if (!(symbol in weights)) this.fail(`there's no element ${symbol}`);
        this.at += symbol.length;
        out.push({ element: symbol, count: this.digits() });
      } else if (c === "(" || c === "[") {
        // A state after a formula isn't a group in it.
        const close = this.s.indexOf(")", this.at);
        if (c === "(" && out.length > 0 && states.includes(this.s.slice(this.at + 1, close))) return out;
        this.at++;
        const inside = this.units();
        const shut = c === "(" ? ")" : "]";
        if (inside.length === 0) this.fail(`an element in the ${c === "(" ? "parentheses" : "brackets"}, like ${c === "(" ? "(OH)2" : "[Cu(NH3)4]^2+"}`);
        if (this.s[this.at] !== shut) this.fail(c === "(" ? "a ) to close the group, like (OH)2" : "a ] to close the brackets, like [Cu(NH3)4]^2+");
        this.at++;
        out.push({ inside, bracket: c, count: this.digits() });
      } else return out;
    }
  }

  // ^2-, ^+, ^3+, or in braces, ^{2-}.
  private charge(): string {
    if (this.s[this.at] !== "^") return "";
    this.at++;
    const braced = this.s[this.at] === "{";
    if (braced) this.at++;
    let size = this.digits();
    const sign = this.s[this.at];
    if (sign !== "+" && sign !== "-") this.fail("a charge after ^, like ^2- or ^+");
    this.at++;
    if (!size) size = this.digits();
    if (braced) {
      if (this.s[this.at] !== "}") this.fail("a } to close the charge, like ^{2-}");
      this.at++;
    }
    return (size === "1" ? "" : size) + sign;
  }
}

export function parseChem(written: string): { reaction: ChemReaction } | { problem: MathProblem } {
  try {
    return { reaction: new Reader(written).whole() };
  } catch (e) {
    if (e instanceof Failed) return { problem: e.problem };
    throw e;
  }
}

// A reaction as one::math's tree, to be set as an equation is.
const node = (type: MathNode["type"], text = "", parts: MathNode[] = []): MathNode => ({ type, text, parts });
const row = (parts: MathNode[]): MathNode => (parts.length === 1 ? parts[0]! : node("row", "", parts));
const amount = (n: string): MathNode => (n.includes("/") ? node("fraction", "", n.split("/").map((p) => node("number", p))) : node("number", n));
const charged = (charge: string) => node("words", charge.replace("-", "−"));

function unitAsMath(u: ChemUnit): MathNode {
  const made = u.element ? node("words", u.element) : u.bracket === "[" ? node("bracket", "square", [row(u.inside!.map(unitAsMath))]) : node("group", "", [row(u.inside!.map(unitAsMath))]);
  return u.count ? node("subscript", "", [made, node("number", u.count)]) : made;
}

function formulaAsMath(f: ChemFormula): MathNode {
  const parts = f.electron ? [node("words", "e")] : f.units.map(unitAsMath);
  // The charge over the last unit's count, both stacked on it.
  if (f.charge) parts[parts.length - 1] = node("power", "", [parts[parts.length - 1]!, charged(f.charge)]);
  return row(f.number ? [amount(f.number), ...parts] : parts);
}

function termAsMath(t: ChemTerm): MathNode {
  const parts: MathNode[] = [];
  if (t.number) parts.push(amount(t.number));
  t.formulas.forEach((f, i) => {
    if (i > 0) parts.push(node("symbol", "cdot"));
    const made = formulaAsMath(f);
    parts.push(...(made.type === "row" ? made.parts : [made]));
  });
  if (t.state) parts.push(node("group", "", [node("words", t.state)]));
  return row(parts);
}

// What's over an arrow: a formula as one, a Greek letter as its letter, or words.
function overAsMath(said: string): MathNode {
  if (!said) return node("row");
  if (isGreekName(said)) return node("greek", said);
  const read = parseChem(said);
  if ("reaction" in read && read.reaction.arrows.length === 0 && read.reaction.sides[0]!.length === 1) return termAsMath(read.reaction.sides[0]![0]!);
  return node("words", said);
}

export function chemAsMath(r: ChemReaction): MathNode {
  const sides = r.sides.map((terms) => terms.map(termAsMath).reduce((a, b) => node("infix", "+", [a, b])));
  return sides.reduce((a, b, i) => {
    const arrow = r.arrows[i - 1]!;
    return arrow.kind === "=" ? node("infix", "=", [a, b]) : node("arrow", arrow.kind, [a, b, overAsMath(arrow.over), overAsMath(arrow.under)]);
  });
}

// How many of each element, and the charge, in what's taking part.
type Counted = { atoms: Map<string, number>; charge: number };
const amountOf = (n: string) => (n === "" ? 1 : n.includes("/") ? Number(n.split("/")[0]) / Number(n.split("/")[1]) : Number(n));
function countUnits(units: ChemUnit[], times: number, into: Map<string, number>) {
  for (const u of units) {
    const n = times * amountOf(u.count);
    if (u.element) into.set(u.element, (into.get(u.element) ?? 0) + n);
    else countUnits(u.inside!, n, into);
  }
}
function counted(terms: ChemTerm[]): Counted {
  const atoms = new Map<string, number>();
  let charge = 0;
  for (const t of terms) {
    for (const f of t.formulas) {
      const times = amountOf(t.number) * amountOf(f.number);
      countUnits(f.units, times, atoms);
      if (f.charge) charge += times * (f.charge.endsWith("-") ? -1 : 1) * amountOf(f.charge.slice(0, -1));
    }
  }
  return { atoms, charge };
}
const said = (n: number) => String(Math.round(n * 1000) / 1000);
const chargeSaid = (n: number) => (n === 0 ? "0" : `${said(Math.abs(n))}${n > 0 ? "+" : "−"}`.replace(/^1(?=[+−])/, ""));

// Whether each arrow's two sides hold as many of each element, and as much charge; what
// differs, said, when they don't.
export function chemUnbalanced(r: ChemReaction): string | undefined {
  for (let i = 0; i < r.arrows.length; i++) {
    const left = counted(r.sides[i]!), right = counted(r.sides[i + 1]!);
    const order = [...new Set([...left.atoms.keys(), ...right.atoms.keys()])];
    const differ: string[] = [];
    for (const e of order) {
      const l = left.atoms.get(e) ?? 0, rr = right.atoms.get(e) ?? 0;
      if (Math.abs(l - rr) > 1e-9) differ.push(`${said(l)} ${e} on the left, ${said(rr)} on the right`);
    }
    if (Math.abs(left.charge - right.charge) > 1e-9) differ.push(`a charge of ${chargeSaid(left.charge)} on the left, ${chargeSaid(right.charge)} on the right`);
    if (differ.length) return `not balanced: ${differ.join("; ")}`;
  }
  return undefined;
}

// What one mole of it weighs, in grams, when it's one thing alone, like H2O or
// CuSO4*5H2O: its number before it counted too.
export function molarMass(r: ChemReaction): number | undefined {
  if (r.arrows.length > 0 || r.sides[0]!.length !== 1) return undefined;
  const atoms = counted(r.sides[0]!).atoms;
  if (atoms.size === 0) return undefined;
  let grams = 0;
  for (const [e, n] of atoms) grams += weights[e]! * n;
  return grams;
}

// A reaction written back as its tree, as math/tests/chem.tsv writes it.
export function writtenChem(r: ChemReaction): string {
  const unit = (u: ChemUnit): string => (u.element ?? (u.bracket === "[" ? `[${u.inside!.map(unit).join(" ")}]` : `(${u.inside!.map(unit).join(" ")})`)) + (u.count ? `_${u.count}` : "");
  const formula = (f: ChemFormula) => (f.number ? `${f.number} ` : "") + (f.electron ? "e" : f.units.map(unit).join(" ")) + (f.charge ? `^${f.charge}` : "");
  const term = (t: ChemTerm) => `{${t.number ? `${t.number} ` : ""}${t.formulas.map(formula).join(" * ")}${t.state ? ` (${t.state})` : ""}}`;
  let out = r.sides[0]!.map(term).join(" + ");
  r.arrows.forEach((a, i) => {
    out += ` ${a.kind}${a.over ? `[${a.over}]` : ""}${a.under ? `[${a.under}]` : ""} ` + r.sides[i + 1]!.map(term).join(" + ");
  });
  return out;
}
