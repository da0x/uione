// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Equations written as they'd be typed in a message, read as a tree that's set as a
// book sets them and, where it can be, worked out (math/README.md). The twin of
// one::math's math.hpp, both held to math/tests/cases.tsv.

import { measures } from "./math-measures.js";

export type MathKind =
  | "row" // parts side by side
  | "number" // 6.02e23, as written
  | "name" // x, rate, monthly_rate
  | "greek" // alpha, set as the letter
  | "symbol" // inf, partial, set as their sign
  | "words" // "for every n", set upright
  | "infix" // + - +- * and relations, `text` the one written
  | "negative"
  | "fraction" // parts[0] over parts[1]
  | "power"
  | "subscript"
  | "prime" // `text` its primes
  | "factorial"
  | "percent"
  | "group" // (parts[0]), its parentheses drawn
  | "call" // `text` the function, on parts
  | "root" // sqrt(parts[0]), or root(index, what's under it)
  | "bracket" // `text` abs, floor or ceil
  | "accent" // `text` hat, bar, vec, dot or ddot
  | "big" // `text` sum, prod, int or lim; parts: below, above, what it acts on
  | "differential" // d and a letter: parts[0] the letter
  | "matrix" // parts are rows of cells
  | "cases"; // parts are rows: value, then condition

export interface MathNode {
  type: MathKind;
  text: string;
  parts: MathNode[];
}

export interface MathProblem {
  at: number; // in characters into what was written
  says: string;
}

const greekNames = new Set([
  "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau",
  "upsilon", "phi", "chi", "psi", "omega", "Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma", "Upsilon", "Phi", "Psi", "Omega",
]);
// Functions set upright, taking parentheses or the side-by-side term after them.
const functionNames = new Set([
  "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "ln", "log", "exp", "min", "max", "det", "gcd", "lcm", "sgn", "round", "arg",
]);
const bracketNames = new Set(["abs", "floor", "ceil"]);
const accentNames = new Set(["hat", "bar", "vec", "dot", "ddot"]);
const bigNames = new Set(["sum", "prod", "int", "lim"]);

interface Token {
  type: "end" | "number" | "name" | "words" | "sign";
  text: string;
  at: number;
}

// The signs written as more than one character, longest first, and the same typed as
// themselves.
const signs: [string, string][] = [
  ["<=>", "<=>"], ["<=", "<="], [">=", ">="], ["!=", "!="], ["~=", "~="], ["->", "->"], ["=>", "=>"], ["+-", "+-"], ["-+", "-+"],
  ["≤", "<="], ["≥", ">="], ["≠", "!="], ["≈", "~="], ["→", "->"], ["⇒", "=>"], ["⇔", "<=>"], ["±", "+-"],
  ["∓", "-+"], ["×", "*"], ["·", "*"], ["−", "-"], ["∈", "in"], ["∞", "inf"], ["∂", "partial"], ["√", "sqrt"],
  ["∑", "sum"], ["∏", "prod"], ["∫", "int"], ["π", "pi"],
];

const relationSigns = new Set(["=", "!=", "<", ">", "<=", ">=", "~=", "->", "=>", "<=>"]);
const isDigit = (c: string | undefined) => c !== undefined && c >= "0" && c <= "9";
const isAlpha = (c: string | undefined) => c !== undefined && ((c >= "a" && c <= "z") || (c >= "A" && c <= "Z"));
const node = (type: MathKind, text = "", parts: MathNode[] = []): MathNode => ({ type, text, parts });

class Failed extends Error {
  constructor(readonly problem: MathProblem) {
    super(problem.says);
  }
}

class Reader {
  private at = 0;
  private now: Token = { type: "end", text: "", at: 0 };
  private bars = 0; // inside how many absolute values, where a | closes one

  constructor(private readonly s: string) {
    this.next();
  }

  whole(): MathNode {
    const made = this.relation();
    if (this.now.type !== "end") this.fail(`there's more after the equation here, "${this.now.text}"`);
    return made;
  }

  private fail(says: string): never {
    throw new Failed({ at: this.now.at, says });
  }

  private next() {
    const s = this.s;
    while (this.at < s.length && " \t\n\r".includes(s[this.at]!)) this.at++;
    this.now = { type: "end", text: "", at: this.at };
    if (this.at >= s.length) return;
    const c = s[this.at]!;
    if (isDigit(c) || (c === "." && isDigit(s[this.at + 1]))) {
      const from = this.at;
      while (this.at < s.length && (isDigit(s[this.at]) || s[this.at] === ".")) this.at++;
      // An exponent, 6.02e23, only when digits follow the e.
      if (this.at + 1 < s.length && (s[this.at] === "e" || s[this.at] === "E")) {
        let e = this.at + 1;
        if (s[e] === "+" || s[e] === "-") e++;
        if (isDigit(s[e])) {
          this.at = e;
          while (isDigit(s[this.at])) this.at++;
        }
      }
      this.now = { type: "number", text: s.slice(from, this.at), at: from };
      return;
    }
    if (isAlpha(c)) {
      const from = this.at;
      while (this.at < s.length && (isAlpha(s[this.at]) || isDigit(s[this.at]))) this.at++;
      this.now = { type: "name", text: s.slice(from, this.at), at: from };
      return;
    }
    if (c === '"') {
      const from = this.at++;
      const close = s.indexOf('"', this.at);
      const end = close < 0 ? s.length : close;
      this.now = { type: "words", text: s.slice(this.at, end), at: from };
      this.at = close < 0 ? s.length : close + 1;
      return;
    }
    for (const [written, means] of signs) {
      if (s.startsWith(written, this.at)) {
        this.now = { type: isAlpha(means[0]) ? "name" : "sign", text: means, at: this.at };
        this.at += written.length;
        return;
      }
    }
    const one = String.fromCodePoint(s.codePointAt(this.at)!);
    this.now = { type: "sign", text: one, at: this.at };
    this.at += one.length;
  }

  private sign(t: string) {
    return this.now.type === "sign" && this.now.text === t;
  }
  private word(t: string) {
    return this.now.type === "name" && this.now.text === t;
  }
  private relationSign() {
    return (this.now.type === "sign" && relationSigns.has(this.now.text)) || this.word("in");
  }

  private relation(): MathNode {
    let left = this.sum();
    while (this.relationSign()) {
      const op = this.now.text;
      this.next();
      left = node("infix", op, [left, this.sum()]);
    }
    return left;
  }

  private sum(): MathNode {
    let left = this.product();
    while (this.sign("+") || this.sign("-") || this.sign("+-") || this.sign("-+")) {
      const op = this.now.text;
      this.next();
      left = node("infix", op, [left, this.product()]);
    }
    return left;
  }

  // * and /, a / stacking what's side by side on either side of it.
  private product(): MathNode {
    let left = this.run();
    while (this.sign("*") || this.sign("/")) {
      const over = this.sign("/");
      this.next();
      const right = this.run();
      left = over ? node("fraction", "", [left, right]) : node("infix", "*", [left, right]);
    }
    return left;
  }

  // Whether what's next starts a term side by side with the one before.
  private startsTerm() {
    if (this.now.type === "number" || this.now.type === "words") return true;
    if (this.now.type === "name") return !["to", "if", "otherwise", "in"].includes(this.now.text);
    return this.sign("(") || this.sign("[") || (this.sign("|") && this.bars === 0);
  }

  // Terms side by side, multiplied: 2a, 2 pi r, n(n + 1), sin 2x.
  private run(): MathNode {
    if (this.sign("-")) {
      this.next();
      return node("negative", "", [this.run()]);
    }
    const parts: MathNode[] = [];
    do parts.push(this.term());
    while (this.startsTerm());
    return parts.length === 1 ? parts[0]! : node("row", "", parts);
  }

  // The side-by-side terms after a function or a big operator, up to the next one.
  private actedOn(): MathNode {
    const parts: MathNode[] = [];
    while (this.startsTerm()) {
      if (this.now.type === "name" && (functionNames.has(this.now.text) || bigNames.has(this.now.text)) && parts.length > 0) break;
      parts.push(this.term());
    }
    if (parts.length === 0) this.fail("a function or a sum needs what it acts on after it");
    return parts.length === 1 ? parts[0]! : node("row", "", parts);
  }

  // One term with what follows it: ^, _, ', ! and %.
  private term(): MathNode {
    let base = this.atom();
    for (;;) {
      if (this.sign("^")) {
        this.next();
        base = node("power", "", [base, this.script()]);
      } else if (this.sign("_")) {
        this.next();
        base = node("subscript", "", [base, this.script()]);
      } else if (this.sign("'")) {
        let primes = "";
        while (this.sign("'")) (primes += "'"), this.next();
        base = node("prime", primes, [base]);
      } else if (this.sign("!")) {
        this.next();
        base = node("factorial", "", [base]);
      } else if (this.sign("%")) {
        this.next();
        base = node("percent", "", [base]);
      } else return base;
    }
  }

  // What a power or a subscript holds: one atom, a minus before it, and its own power,
  // read right to left: 2^3^2 is 2^(3^2).
  private script(): MathNode {
    if (this.sign("-")) {
      this.next();
      return node("negative", "", [this.script()]);
    }
    // A word here is a name, as x_max's is, unless parentheses follow it.
    let base: MathNode;
    if (this.now.type === "name" && !greekNames.has(this.now.text) && this.now.text !== "inf" && this.now.text !== "partial") {
      const keepAt = this.at, keep = this.now;
      this.next();
      const called = this.sign("(");
      this.at = keepAt;
      this.now = keep;
      if (called) base = this.atom();
      else {
        base = node("name", this.now.text);
        this.next();
      }
    } else base = this.atom();
    if (this.sign("^")) {
      this.next();
      return node("power", "", [base, this.script()]);
    }
    return base;
  }

  private arguments(): MathNode[] {
    const out: MathNode[] = [];
    this.next(); // (
    if (this.sign(")")) {
      this.next();
      return out;
    }
    for (;;) {
      out.push(this.relation());
      if (this.sign(",")) {
        this.next();
        continue;
      }
      if (!this.sign(")")) this.fail("a ) to close what's in parentheses");
      this.next();
      return out;
    }
  }

  private atom(): MathNode {
    if (this.now.type === "number" || this.now.type === "words") {
      const n = node(this.now.type, this.now.text);
      this.next();
      return n;
    }
    if (this.sign("(")) {
      const inside = this.arguments();
      if (inside.length !== 1) this.fail("one thing in parentheses, or a function before them, like f(x, y)");
      return node("group", "", inside);
    }
    if (this.sign("|")) {
      this.next();
      this.bars++;
      const inside = this.sum();
      this.bars--;
      if (!this.sign("|")) this.fail("a | to close the absolute value");
      this.next();
      return node("bracket", "abs", [inside]);
    }
    if (this.sign("[")) return this.matrix();
    if (this.now.type === "name") return this.named();
    return this.fail(this.now.type === "end" ? "something more to finish the equation" : `an equation, not "${this.now.text}"`);
  }

  // [1, 2; 3, 4]: commas between columns, semicolons between rows.
  private matrix(): MathNode {
    this.next(); // [
    const out = node("matrix");
    let row = node("row");
    for (;;) {
      row.parts.push(this.relation());
      if (this.sign(",")) this.next();
      else if (this.sign(";")) {
        this.next();
        out.parts.push(row);
        row = node("row");
      } else if (this.sign("]")) {
        this.next();
        out.parts.push(row);
        return out;
      } else this.fail("a , ; or ] in the matrix");
    }
  }

  private named(): MathNode {
    const name = this.now.text;
    this.next();
    const parenthesized = this.sign("(");
    if (name === "sqrt" || name === "root") {
      if (!parenthesized) this.fail(`${name} takes parentheses, like sqrt(x)`);
      const inside = this.arguments();
      const wanted = name === "sqrt" ? 1 : 2;
      if (inside.length !== wanted) this.fail(name === "sqrt" ? "sqrt takes one thing, like sqrt(x)" : "root takes its index and what's under it, like root(3, x)");
      return node("root", "", inside);
    }
    if (bracketNames.has(name) || accentNames.has(name)) {
      if (!parenthesized) this.fail(`${name} takes parentheses, like ${name}(x)`);
      const inside = this.arguments();
      if (inside.length !== 1) this.fail(`${name} takes one thing, like ${name}(x)`);
      return node(bracketNames.has(name) ? "bracket" : "accent", name, inside);
    }
    if (name === "cases") {
      if (!parenthesized) this.fail("cases takes parentheses, like cases(x if x >= 0; -x otherwise)");
      return this.cases();
    }
    if (bigNames.has(name)) return this.big(name);
    if (functionNames.has(name)) {
      if (parenthesized) return this.called(name);
      return node("call", name, [this.actedOn()]);
    }
    if (greekNames.has(name)) return node("greek", name);
    if (name === "inf" || name === "partial") return node("symbol", name);
    // dx, dy, dt: a d and a letter is a differential.
    if (name.length === 2 && name[0] === "d") return node("differential", "", [node("name", name.slice(1))]);
    // A longer name followed by parentheses is a function as written, area(x); a letter
    // is multiplied by what's in them, n(n + 1), and set the same as f(x).
    if (parenthesized && name.length > 1) return this.called(name);
    return this.withSubscriptWord(node("name", name), name.length === 1);
  }

  // A function and what's in the parentheses after it, one thing kept in them as it was
  // written, sin(x), so they're drawn.
  private called(name: string): MathNode {
    const inside = this.arguments();
    return inside.length === 1 ? node("call", name, [node("group", "", inside)]) : node("call", name, inside);
  }

  // A longer name's _ joins the next word to it, monthly_rate; after one letter it's a
  // subscript, read as the term goes on.
  private withSubscriptWord(n: MathNode, short: boolean): MathNode {
    while (!short && this.sign("_")) {
      const keepAt = this.at, keep = this.now;
      this.next();
      if (this.now.type !== "name") {
        this.at = keepAt;
        this.now = keep;
        break;
      }
      n.text += "_" + this.now.text;
      this.next();
    }
    return n;
  }

  // sum(k = 1 to n) k^2, int(0 to 1) x dx, int x dx, lim(x -> 0) f(x).
  private big(name: string): MathNode {
    let below = node("row"), above = node("row");
    if (this.sign("(")) {
      this.next();
      below = this.relation();
      if (this.word("to")) {
        this.next();
        above = this.relation();
      }
      if (!this.sign(")")) this.fail("a ) to close the bounds, like sum(k = 1 to n)");
      this.next();
    }
    return node("big", name, [below, above, this.actedOn()]);
  }

  // cases(x if x >= 0; -x otherwise)
  private cases(): MathNode {
    this.next(); // (
    const out = node("cases");
    for (;;) {
      const value = this.sum();
      let condition = node("row");
      if (this.word("if")) {
        this.next();
        condition = this.relation();
      } else if (this.word("otherwise")) {
        this.next();
        condition = node("words", "otherwise");
      }
      out.parts.push(node("row", "", [value, condition]));
      if (this.sign(";")) {
        this.next();
        continue;
      }
      if (!this.sign(")")) this.fail("a ; between cases, or a ) to close them");
      this.next();
      return out;
    }
  }
}

// An equation read, or where and why it can't be.
// What a phone's keyboard, or a page's editing, puts in place of what was typed, read as
// what it stands for: its spaces that don't break as spaces, its curly quotes as
// straight ones, and the dash a double hyphen becomes as a minus. Each is one character
// for one, so where a problem is said stays where it was.
const typedAs: Record<string, string> = { "\u00a0": " ", "\u2009": " ", "\u202f": " ", "\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2013": "\u2212", "\u2014": "\u2212" };
const typedAsAny = new RegExp(`[${Object.keys(typedAs).join("")}]`, "g");

export function parseMath(written: string): { node: MathNode } | { problem: MathProblem } {
  try {
    return { node: new Reader(written.replace(typedAsAny, (c) => typedAs[c] ?? c)).whole() };
  } catch (e) {
    if (e instanceof Failed) return { problem: e.problem };
    throw e;
  }
}

// A tree written out one way, its kinds and parts in parentheses, as the cases are.
export function writtenMath(n: MathNode): string {
  const all = (head: string) => `(${[head, ...n.parts.map(writtenMath)].join(" ")})`;
  switch (n.type) {
    case "row": return all("row");
    case "number":
    case "name": return n.text;
    case "greek": return `greek:${n.text}`;
    case "symbol": return `symbol:${n.text}`;
    case "words": return `"${n.text}"`;
    case "infix":
    case "prime":
    case "bracket":
    case "accent":
    case "big": return all(n.text);
    case "negative": return all("neg");
    case "fraction": return all("frac");
    case "power": return all("^");
    case "subscript": return all("_");
    case "factorial": return all("!");
    case "percent": return all("%");
    case "group": return all("group");
    case "call": return all(`call:${n.text}`);
    case "root": return all("root");
    case "differential": return all("d");
    case "matrix": return all("matrix");
    case "cases": return all("cases");
  }
}

// layout

// One thing drawn: a glyph of the math font, its origin on its baseline; a filled rule,
// its top left; or a line through points, `size` its thickness. In pixels from the
// equation's left end on its baseline, down being more.
export interface MathPlaced {
  type: "glyph" | "rule" | "line";
  code: number;
  x: number;
  y: number;
  size: number; // a glyph's em, or a line's thickness
  width: number; // a rule's
  height: number;
  points: [number, number][];
}

// An equation laid out: how wide it is, how far it rises above its baseline and falls
// below, and what's drawn.
export interface MathLaidOut {
  width: number;
  ascent: number;
  descent: number;
  drawn: MathPlaced[];
}

type Style = "display" | "text" | "script" | "scripts";
const smaller = (s: Style): Style => (s === "display" ? "text" : s === "text" ? "script" : "scripts");
const lowered = (s: Style): Style => (s === "display" || s === "text" ? "script" : "scripts");
const constant = (name: keyof typeof measures.constants): number => measures.constants[name];
const glyphs = measures.glyphs as unknown as Record<string, readonly [number, number, number, number, number]>;
const empty = (): MathLaidOut => ({ width: 0, ascent: 0, descent: 0, drawn: [] });
const place = (type: MathPlaced["type"], code: number, x: number, y: number, size: number, width = 0, height = 0, points: [number, number][] = []): MathPlaced => ({
  type, code, x, y, size, width, height, points,
});

function moved(l: MathLaidOut, dx: number, dy: number): MathLaidOut {
  for (const d of l.drawn) {
    d.x += dx;
    d.y += dy;
    for (const p of d.points) (p[0] += dx), (p[1] += dy);
  }
  return l;
}

// Things side by side, each placed after the one before.
function beside(parts: MathLaidOut[], gap = 0): MathLaidOut {
  const out = empty();
  parts.forEach((p, i) => {
    if (i > 0) out.width += gap;
    moved(p, out.width, 0);
    out.ascent = Math.max(out.ascent, p.ascent);
    out.descent = Math.max(out.descent, p.descent);
    out.width += p.width;
    out.drawn.push(...p.drawn);
  });
  return out;
}

const greekLower = ["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "", "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega"];
function greekCode(n: string): number {
  for (let i = 0; i < greekLower.length; i++) {
    const l = greekLower[i]!;
    if (!l) continue;
    if (l === n) return 0x3b1 + i;
    if (l[0]!.toUpperCase() + l.slice(1) === n) return 0x391 + i;
  }
  return "?".charCodeAt(0);
}

const ungrouped = (n: MathNode): MathNode => (n.type === "group" ? n.parts[0]! : n);
const startsWithDigit = (n: MathNode): boolean =>
  n.type === "number" || (["row", "infix", "power", "subscript"].includes(n.type) && n.parts.length > 0 && startsWithDigit(n.parts[0]!));
const endsWithDigit = (n: MathNode): boolean => n.type === "number" || (["row", "infix"].includes(n.type) && n.parts.length > 0 && endsWithDigit(n.parts[n.parts.length - 1]!));
const longName = (n: MathNode) => n.type === "name" && n.text.length > 1;

class Setter {
  constructor(private readonly em: number) {}

  set(n: MathNode, s: Style): MathLaidOut {
    switch (n.type) {
      case "number": return this.number(n.text, s);
      case "name": return this.name(n.text, s);
      case "greek": return this.glyph(greekCode(n.text), s);
      case "symbol": return this.glyph(n.text === "inf" ? 0x221e : 0x2202, s);
      case "words": return this.word(n.text, s);
      case "row": return this.row(n.parts, s);
      case "infix": return this.infix(n, s);
      case "negative": return beside([this.glyph(0x2212, s), this.set(n.parts[0]!, s)]);
      case "fraction": return this.fraction(n, s);
      case "power": return this.scripts(n.parts[0]!, undefined, n.parts[1], s);
      case "subscript": return this.scripts(n.parts[0]!, n.parts[1], undefined, s);
      case "prime": return beside([this.set(n.parts[0]!, s), this.glyph(n.text.length > 1 ? 0x2033 : 0x2032, s)]);
      case "factorial": return beside([this.set(n.parts[0]!, s), this.glyph(33, s)]);
      case "percent": return beside([this.set(n.parts[0]!, s), this.glyph(37, s)]);
      case "group": return this.around(this.set(n.parts[0]!, s), 40, 41, s);
      case "call": return this.call(n, s);
      case "root": return this.root(n, s);
      case "bracket": {
        const open = n.text === "abs" ? 124 : n.text === "floor" ? 0x230a : 0x2308;
        const close = n.text === "abs" ? 124 : n.text === "floor" ? 0x230b : 0x2309;
        return this.around(this.set(n.parts[0]!, s), open, close, s);
      }
      case "accent": return this.accent(n, s);
      case "big": return this.big(n, s);
      case "differential": return beside([this.word("d", s), this.set(n.parts[0]!, s)]);
      case "matrix": return this.matrix(n, s);
      case "cases": return this.cases(n, s);
    }
  }

  private scale(s: Style) {
    const base = this.em / measures.units;
    if (s === "script") return (base * constant("ScriptPercentScaleDown")) / 100;
    if (s === "scripts") return (base * constant("ScriptScriptPercentScaleDown")) / 100;
    return base;
  }
  private unit(s: Style) {
    return this.scale(s) * measures.units;
  }
  private axis(s: Style) {
    return constant("AxisHeight") * this.scale(s);
  }
  private space(ems: number, s: Style): MathLaidOut {
    return { width: ems * this.unit(s), ascent: 0, descent: 0, drawn: [] };
  }

  private glyph(code: number, s: Style): MathLaidOut {
    const g = glyphs[String(code)];
    const k = this.scale(s);
    if (!g) return { width: 0.5 * this.unit(s), ascent: 0, descent: 0, drawn: [] };
    return { width: g[0] * k, ascent: Math.max(0, g[4] * k), descent: Math.max(0, -g[2] * k), drawn: [place("glyph", code, 0, 0, this.unit(s))] };
  }

  // Characters upright, as numbers and words and longer names are.
  private word(text: string, s: Style, spacedUnderscores = false): MathLaidOut {
    return beside([...text].map((c) => this.glyph(spacedUnderscores && c === "_" ? 32 : c.charCodeAt(0), s)));
  }

  // A number as written, 6.02e23 as 6.02 × 10 raised to 23.
  private number(text: string, s: Style): MathLaidOut {
    const e = text.search(/[eE]/);
    if (e < 0) return this.word(text, s);
    const ten = this.word("10", s);
    const power = this.word(text.slice(e + 1), lowered(s));
    const k = this.scale(s);
    moved(power, ten.width, -constant("SuperscriptShiftUp") * k);
    ten.ascent = Math.max(ten.ascent, constant("SuperscriptShiftUp") * k + power.ascent);
    ten.drawn.push(...power.drawn);
    ten.width += power.width;
    return beside([this.word(text.slice(0, e), s), this.space(2 / 18, s), this.glyph(0xd7, s), this.space(2 / 18, s), ten]);
  }

  // A letter in math italic; a longer name upright, its underscores spaces.
  private name(text: string, s: Style): MathLaidOut {
    if (text.length !== 1 || !isAlpha(text)) return this.word(text, s, true);
    const c = text.charCodeAt(0);
    const code = c >= 97 ? (text === "h" ? 0x210e : 0x1d44e + (c - 97)) : 0x1d434 + (c - 65);
    return this.glyph(code, s);
  }

  private row(parts: MathNode[], s: Style): MathLaidOut {
    const out: MathLaidOut[] = [];
    parts.forEach((p, i) => {
      // A function or a big operator after something has a thin space before it.
      if (i > 0 && (p.type === "call" || p.type === "big") && s !== "script" && s !== "scripts") out.push(this.space(3 / 18, s));
      out.push(this.set(p, s));
    });
    return beside(out);
  }

  private infix(n: MathNode, s: Style): MathLaidOut {
    const drawnAs: Record<string, number> = {
      "=": 61, "!=": 0x2260, "<": 60, ">": 62, "<=": 0x2264, ">=": 0x2265, "~=": 0x2248, "->": 0x2192, "=>": 0x21d2, "<=>": 0x21d4,
      in: 0x2208, "+": 43, "-": 0x2212, "+-": 0xb1, "-+": 0x2213, "*": 0xd7,
    };
    const relation = !["+", "-", "+-", "-+", "*"].includes(n.text);
    const small = s === "script" || s === "scripts";
    // a * b side by side; 2 × 3 between numbers; and a · beside a number or a word, so
    // rate · 12 doesn't read as one name.
    if (n.text === "*") {
      const numbers = endsWithDigit(n.parts[0]!) && startsWithDigit(n.parts[1]!);
      const worded = endsWithDigit(n.parts[0]!) || startsWithDigit(n.parts[1]!) || longName(n.parts[0]!) || longName(n.parts[1]!);
      if (!numbers && !worded) return beside([this.set(n.parts[0]!, s), this.space(3 / 18, s), this.set(n.parts[1]!, s)]);
      const gap = small ? 0 : 4 / 18;
      return beside([this.set(n.parts[0]!, s), this.space(gap, s), this.glyph(numbers ? 0xd7 : 0x22c5, s), this.space(gap, s), this.set(n.parts[1]!, s)]);
    }
    const gap = small ? 0 : relation ? 5 / 18 : 4 / 18;
    return beside([this.set(n.parts[0]!, s), this.space(gap, s), this.glyph(drawnAs[n.text] ?? 63, s), this.space(gap, s), this.set(n.parts[1]!, s)]);
  }

  private fraction(n: MathNode, s: Style): MathLaidOut {
    const inner = smaller(s);
    const top = this.set(ungrouped(n.parts[0]!), inner), bottom = this.set(ungrouped(n.parts[1]!), inner);
    const k = this.scale(s), rule = constant("FractionRuleThickness") * k;
    const shown = s === "display";
    let up = (shown ? constant("FractionNumeratorDisplayStyleShiftUp") : constant("FractionNumeratorShiftUp")) * k;
    let down = (shown ? constant("FractionDenominatorDisplayStyleShiftDown") : constant("FractionDenominatorShiftDown")) * k;
    const gapUp = (shown ? constant("FractionNumDisplayStyleGapMin") : constant("FractionNumeratorGapMin")) * k;
    const gapDown = (shown ? constant("FractionDenomDisplayStyleGapMin") : constant("FractionDenominatorGapMin")) * k;
    const a = this.axis(s);
    up = Math.max(up, a + rule / 2 + gapUp + top.descent);
    down = Math.max(down, rule / 2 + gapDown + bottom.ascent - a);
    const pad = 0.1 * this.unit(s), width = Math.max(top.width, bottom.width) + 2 * pad;
    moved(top, (width - top.width) / 2, -up);
    moved(bottom, (width - bottom.width) / 2, down);
    const out: MathLaidOut = { width, ascent: up + top.ascent, descent: down + bottom.descent, drawn: [...top.drawn, ...bottom.drawn, place("rule", 0, 0, -a - rule / 2, 0, width, rule)] };
    // A little room either side, so it doesn't touch what's beside it.
    return beside([this.space(0.1, s), out, this.space(0.1, s)]);
  }

  // A base with a subscript, a superscript, or both stacked.
  private scripts(baseNode: MathNode, below: MathNode | undefined, above: MathNode | undefined, s: Style): MathLaidOut {
    // x_1^2: the subscript and the superscript stacked on x.
    if (above && baseNode.type === "subscript") return this.scripts(baseNode.parts[0]!, baseNode.parts[1], above, s);
    const base = this.set(baseNode, s);
    const k = this.scale(s), small = lowered(s);
    const out = base;
    const baseWidth = base.width;
    let right = 0;
    if (above) {
      const sup = this.set(ungrouped(above), small);
      let up = Math.max(constant("SuperscriptShiftUp") * k, base.ascent - constant("SuperscriptBaselineDropMax") * k);
      up = Math.max(up, constant("SuperscriptBottomMin") * k + sup.descent);
      moved(sup, baseWidth, -up);
      out.ascent = Math.max(out.ascent, up + sup.ascent);
      right = Math.max(right, sup.width);
      out.drawn.push(...sup.drawn);
    }
    if (below) {
      const sub = this.set(ungrouped(below), small);
      let down = Math.max(constant("SubscriptShiftDown") * k, base.descent + constant("SubscriptBaselineDropMin") * k);
      down = Math.max(down, sub.ascent - constant("SubscriptTopMax") * k);
      moved(sub, baseWidth, down);
      out.descent = Math.max(out.descent, down + sub.descent);
      right = Math.max(right, sub.width);
      out.drawn.push(...sub.drawn);
    }
    out.width = baseWidth + right + constant("SpaceAfterScript") * k;
    return out;
  }

  private call(n: MathNode, s: Style): MathLaidOut {
    const inside: MathLaidOut[] = [];
    n.parts.forEach((p, i) => {
      if (i > 0) inside.push(beside([this.glyph(44, s), this.space(3 / 18, s)]));
      inside.push(this.set(p, s));
    });
    const args = beside(inside);
    const named = this.name(n.text, s);
    // One thing after it: in the parentheses it was written in, sin(x), or after a thin
    // space, sin x.
    if (n.parts.length === 1) return n.parts[0]!.type === "group" ? beside([named, args]) : beside([named, this.space(3 / 18, s), args]);
    return beside([named, this.around(args, 40, 41, s)]);
  }

  // Something between two delimiters as tall as it, drawn as glyphs while it's no taller
  // than a line, and as lines past that.
  private around(inner: MathLaidOut, open: number, close: number, s: Style): MathLaidOut {
    const em = this.unit(s), a = this.axis(s);
    const half = Math.max(inner.ascent - a, inner.descent + a);
    if (half <= 0.62 * em) return beside([this.glyph(open, s), inner, this.glyph(close, s)]);
    const top = -a - half - 0.05 * em, bottom = -a + half + 0.05 * em, width = 0.32 * em, line = 0.06 * em;
    return beside([delimiter(open, top, bottom, width, line), inner, delimiter(close, top, bottom, width, line)]);
  }

  private root(n: MathNode, s: Style): MathLaidOut {
    const inner = this.set(ungrouped(n.parts[n.parts.length - 1]!), s);
    const k = this.scale(s), em = this.unit(s);
    const rule = constant("RadicalRuleThickness") * k;
    const gap = (s === "display" ? constant("RadicalDisplayStyleVerticalGap") : constant("RadicalVerticalGap")) * k;
    const top = -(inner.ascent + gap + rule), bottom = inner.descent + 0.05 * em;
    const signWidth = 0.5 * em;
    // The radical's tick, its stroke down and its long stroke up, then the bar over
    // what's under it.
    const sign = place("line", 0, 0, 0, rule, 0, 0, [
      [0, (top + bottom) * 0.55 + 0.1 * em], [signWidth * 0.25, (top + bottom) * 0.5], [signWidth * 0.55, bottom], [signWidth, top + rule / 2],
      [signWidth + inner.width + 0.1 * em, top + rule / 2],
    ]);
    moved(inner, signWidth + 0.05 * em, 0);
    const out: MathLaidOut = { width: signWidth + inner.width + 0.15 * em, ascent: -top + constant("RadicalExtraAscender") * k, descent: bottom, drawn: [...inner.drawn, sign] };
    if (n.parts.length === 2) {
      // root(3, x): its index small, over the tick.
      const index = this.set(n.parts[0]!, "scripts");
      const raise = (top + bottom) * 0.55 - index.descent - 0.05 * em;
      moved(index, 0, raise);
      const shift = Math.max(0, index.width - signWidth * 0.3);
      moved(out, shift, 0);
      out.width += shift;
      out.ascent = Math.max(out.ascent, -raise + index.ascent);
      out.drawn.push(...index.drawn);
    }
    return out;
  }

  private accent(n: MathNode, s: Style): MathLaidOut {
    const inner = this.set(n.parts[0]!, s);
    const em = this.unit(s), k = this.scale(s);
    const over = -(Math.max(inner.ascent, constant("AccentBaseHeight") * k) + 0.08 * em);
    const out = inner;
    if (n.text === "bar") {
      const thick = constant("OverbarRuleThickness") * k;
      out.drawn.push(place("rule", 0, 0, over - thick, 0, inner.width, thick));
      out.ascent = -over + thick;
      return out;
    }
    if (n.text === "vec") {
      const y = over - 0.08 * em, head = 0.12 * em;
      out.drawn.push(place("line", 0, 0, 0, 0.05 * em, 0, 0, [[0, y], [inner.width, y], [inner.width - head, y - head], [inner.width, y], [inner.width - head, y + head]]));
      out.ascent = -y + head;
      return out;
    }
    // A hat, or a dot or two, drawn as lines, the font having no marks of its own.
    const middle = inner.width / 2, line = 0.06 * em;
    if (n.text === "hat") {
      const y = over - 0.04 * em, half = 0.18 * em, rise = 0.14 * em;
      out.drawn.push(place("line", 0, 0, 0, line, 0, 0, [[middle - half, y], [middle, y - rise], [middle + half, y]]));
      out.ascent = Math.max(out.ascent, -(y - rise) + line);
      return out;
    }
    const y = over - 0.06 * em, dot = 0.1 * em;
    const at = n.text === "dot" ? [middle] : [middle - 0.12 * em, middle + 0.12 * em];
    for (const x of at) out.drawn.push(place("line", 0, 0, 0, dot, 0, 0, [[x, y], [x, y]]));
    out.ascent = Math.max(out.ascent, -y + dot);
    return out;
  }

  private big(n: MathNode, s: Style): MathLaidOut {
    const em = this.unit(s), k = this.scale(s);
    const shown = s === "display";
    let sign: MathLaidOut;
    if (n.text === "lim") sign = this.word("lim", s);
    else {
      const code = n.text === "sum" ? 0x2211 : n.text === "prod" ? 0x220f : 0x222b;
      // Drawn larger when shown on its own, centered on the axis.
      const grow = shown ? 1.6 : 1.15;
      sign = this.glyph(code, s);
      for (const d of sign.drawn) d.size *= grow;
      sign.width *= grow;
      const g = glyphs[String(code)];
      const gk = k * grow;
      const middle = g ? ((g[4] + g[2]) / 2) * gk : 0;
      const shift = middle - this.axis(s);
      moved(sign, 0, shift);
      if (g) {
        sign.ascent = g[4] * gk - shift;
        sign.descent = -g[2] * gk + shift;
      }
    }
    const below = n.parts[0]!, above = n.parts[1]!;
    const hasBelow = !(below.type === "row" && below.parts.length === 0);
    const hasAbove = !(above.type === "row" && above.parts.length === 0);
    const body = this.set(n.parts[2]!, s);
    let op: MathLaidOut = sign;
    if (n.text === "int") {
      // An integral's bounds beside it, at its top and bottom.
      let right = 0;
      const signWidth = sign.width, signAscent = sign.ascent, signDescent = sign.descent;
      if (hasAbove) {
        const a = this.set(above, lowered(s));
        moved(a, signWidth * 0.85, -signAscent + a.ascent * 0.6);
        right = Math.max(right, a.width);
        op.drawn.push(...a.drawn);
      }
      if (hasBelow) {
        const b = this.set(below, lowered(s));
        moved(b, signWidth * 0.45, signDescent);
        right = Math.max(right, b.width - signWidth * 0.4);
        op.descent = Math.max(op.descent, signDescent + b.descent);
        op.drawn.push(...b.drawn);
      }
      op.width = signWidth + right + 0.1 * em;
    } else {
      // Limits centered under and over it.
      const b = hasBelow ? this.set(below, lowered(s)) : empty();
      const a = hasAbove ? this.set(above, lowered(s)) : empty();
      const width = Math.max(sign.width, a.width, b.width);
      moved(sign, (width - sign.width) / 2, 0);
      op = { width, ascent: sign.ascent, descent: sign.descent, drawn: [...sign.drawn] };
      if (hasAbove) {
        const rise = sign.ascent + constant("UpperLimitGapMin") * k + a.descent;
        moved(a, (width - a.width) / 2, -rise);
        op.ascent = rise + a.ascent;
        op.drawn.push(...a.drawn);
      }
      if (hasBelow) {
        const drop = sign.descent + constant("LowerLimitGapMin") * k + b.ascent;
        moved(b, (width - b.width) / 2, drop);
        op.descent = drop + b.descent;
        op.drawn.push(...b.drawn);
      }
    }
    return beside([op, this.space(3 / 18, s), body]);
  }

  private matrix(n: MathNode, s: Style): MathLaidOut {
    const inner: Style = s === "display" ? "text" : s;
    const cells = n.parts.map((r) => r.parts.map((c) => this.set(c, inner)));
    const columns = Math.max(0, ...n.parts.map((r) => r.parts.length));
    return this.grid(cells, columns, s, 91, 93, false);
  }

  private cases(n: MathNode, s: Style): MathLaidOut {
    const inner: Style = s === "display" ? "text" : s;
    const cells = n.parts.map((r) => {
      const condition = r.parts[1]!;
      const none = condition.type === "row" && condition.parts.length === 0;
      const said = none ? empty() : condition.type === "words" ? this.set(condition, inner) : beside([this.word("if", inner), this.space(0.3, inner), this.set(condition, inner)]);
      return [this.set(r.parts[0]!, inner), said];
    });
    return this.grid(cells, 2, s, 123, 0, true);
  }

  // Cells in rows and columns, centered on the axis, between delimiters; a cases'
  // columns start at their left, a matrix's are centered.
  private grid(cells: MathLaidOut[][], columns: number, s: Style, open: number, close: number, leftAligned: boolean): MathLaidOut {
    const em = this.unit(s);
    const widths = new Array<number>(columns).fill(0), ascents = cells.map(() => 0), descents = cells.map(() => 0);
    cells.forEach((row, r) =>
      row.forEach((cell, c) => {
        widths[c] = Math.max(widths[c]!, cell.width);
        ascents[r] = Math.max(ascents[r]!, Math.max(cell.ascent, 0.7 * em));
        descents[r] = Math.max(descents[r]!, Math.max(cell.descent, 0.25 * em));
      }),
    );
    const gapX = leftAligned ? 1.0 * em : 0.8 * em, gapY = 0.3 * em;
    let height = 0;
    cells.forEach((_, r) => (height += ascents[r]! + descents[r]! + (r ? gapY : 0)));
    const top = -this.axis(s) - height / 2;
    const body = empty();
    let y = top;
    cells.forEach((row, r) => {
      if (r) y += gapY;
      y += ascents[r]!;
      let x = 0;
      for (let c = 0; c < columns; c++) {
        const cell = row[c];
        if (cell) {
          moved(cell, x + (leftAligned ? 0 : (widths[c]! - cell.width) / 2), y);
          body.drawn.push(...cell.drawn);
        }
        x += widths[c]! + (c + 1 < columns ? gapX : 0);
      }
      body.width = Math.max(body.width, x);
      y += descents[r]!;
    });
    body.ascent = -top;
    body.descent = top + height;
    const line = 0.06 * em, width = 0.36 * em;
    const parts = [delimiter(open, top, top + height, width, line), this.space(0.15, s), body];
    if (close) parts.push(this.space(0.15, s), delimiter(close, top, top + height, width, line));
    return beside(parts);
  }
}

// A tall delimiter drawn as a line from its top to its bottom.
function delimiter(code: number, top: number, bottom: number, width: number, line: number): MathLaidOut {
  const out: MathLaidOut = { width, ascent: -top, descent: bottom, drawn: [] };
  const p = place("line", 0, 0, 0, line);
  const inset = width * 0.25, far = width * 0.75, middle = (top + bottom) / 2;
  if (code === 40 || code === 41) {
    // A curve, as a parenthesis bows away from what it holds.
    const left = code === 40;
    for (let i = 0; i <= 16; i++) {
      const t = i / 16, y = top + (bottom - top) * t, bow = Math.sin(t * 3.14159265358979) * (far - inset);
      p.points.push([left ? far - bow : inset + bow, y]);
    }
  } else if (code === 91 || code === 93) {
    const left = code === 91, edge = left ? inset : far, tip = left ? far : inset;
    p.points = [[tip, top], [edge, top], [edge, bottom], [tip, bottom]];
  } else if (code === 123 || code === 125) {
    const left = code === 123, edge = left ? far : inset, mid = left ? inset : far, h = bottom - top;
    p.points = [[edge, top], [(edge + mid) / 2, top + h * 0.05], [(edge + mid) / 2, middle - h * 0.05], [mid, middle], [(edge + mid) / 2, middle + h * 0.05], [(edge + mid) / 2, bottom - h * 0.05], [edge, bottom]];
  } else if (code === 0x230a || code === 0x230b) {
    const left = code === 0x230a, edge = left ? inset : far, tip = left ? far : inset;
    p.points = [[edge, top], [edge, bottom], [tip, bottom]];
  } else if (code === 0x2308 || code === 0x2309) {
    const left = code === 0x2308, edge = left ? inset : far, tip = left ? far : inset;
    p.points = [[tip, top], [edge, top], [edge, bottom]];
  } else {
    p.points = [[width / 2, top], [width / 2, bottom]];
  }
  out.drawn.push(p);
  return out;
}

// An equation set `em` pixels to the em, as it's shown on its own lines, or as it's set
// in a line of text.
export function layOutMath(n: MathNode, em: number, displayed: boolean): MathLaidOut {
  return new Setter(em).set(n, displayed ? "display" : "text");
}

// The font an equation's glyphs are drawn in.
export const mathFont = measures.font;
