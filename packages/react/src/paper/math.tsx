// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An equation: its words in one::math's notation, set as math. While the caret's in it,
// it's its words, in a well; otherwise its equation, aligned as the block says. Both are
// in the one place, so it keeps the room of the larger and the page doesn't move. On a
// phone it's written apart, its equation over its words as they're typed.

import { Equation } from "../equation.js";
import { parseMath } from "../math.js";
import type { ApartKeys, BlockKind } from "./kinds.js";
import { inCode, well } from "./kinds.js";

// Each key: what it's called, the notation its face is set from, what it puts, and what
// it puts around words chosen.
type Key = [name: string, face: string, put: string, around?: string];
const row = (name: string, keys: Key[]): ApartKeys => ({ name, keys: keys.map(([name, face, put, around]) => ({ name, face: <Equation written={face} />, put, around })) });
const greek = (names: string[]): Key[] => names.map((n) => [n[0] === n[0]!.toUpperCase() ? `Capital ${n.toLowerCase()}` : n, n, n]);
const applied = (names: string[]): Key[] => names.map((n) => [n, `${n}(x)`, `${n}(‸)`, `${n}(§)‸`]);

// Everything the notation writes, by what it's for.
const mathKeys: ApartKeys[] = [
  row("Basic", [
    ["Plus", "a + b", " + "],
    ["Minus", "a - b", " - "],
    ["Plus or minus", "a +- b", " +- "],
    ["Times", "a * b", " * "],
    ["Fraction", "a / b", "‸ / ", "(§) / ‸"],
    ["Parentheses", "(x)", "(‸)", "(§)‸"],
    ["Equals", "a = b", " = "],
    ["Not equal", "a != b", " != "],
    ["Less than", "a < b", " < "],
    ["Greater than", "a > b", " > "],
    ["At most", "a <= b", " <= "],
    ["At least", "a >= b", " >= "],
    ["About", "a ~= b", " ~= "],
    ["Percent", "5%", "%"],
  ]),
  row("Powers", [
    ["Squared", "x^2", "^2", "(§)^2‸"],
    ["Power", "x^n", "^(‸)", "(§)^(‸)"],
    ["Subscript", "x_i", "_‸"],
    ["Square root", "sqrt(x)", "sqrt(‸)", "sqrt(§)‸"],
    ["Cube root", "root(3, x)", "root(3, ‸)", "root(3, §)‸"],
    ["Root", "root(n, x)", "root(‸, )", "root(‸, §)"],
    ["Absolute value", "|x|", "|‸|", "|§|‸"],
    ["Factorial", "n!", "!"],
    ["Prime", "f'", "'"],
    ["Exponential", "e^x", "e^(‸)", "e^(§)‸"],
  ]),
  row("Greek", greek(["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "lambda", "mu", "pi", "rho", "sigma", "tau", "phi", "omega", "Gamma", "Delta", "Theta", "Lambda", "Pi", "Sigma", "Phi", "Omega"])),
  row("Calculus", [
    ["Sum", "sum(k = 1 to n) a_k", "sum(k = 1 to n) ‸"],
    ["Product", "prod(i = 1 to n) x_i", "prod(i = 1 to n) ‸"],
    ["Integral", "int(a to b) f dx", "int(0 to 1) ‸ dx", "int(0 to 1) § dx‸"],
    ["Integral without bounds", "int f dx", "int ‸ dx", "int § dx‸"],
    ["Limit", "lim(x -> 0) f", "lim(x -> 0) ‸"],
    ["Derivative", "d / dx f", "d / dx ‸"],
    ["dy over dx", "dy / dx", "dy / dx"],
    ["Partial", "partial f", "partial "],
    ["Infinity", "inf", "inf"],
    ["Tends to", "x -> a", " -> "],
  ]),
  row("Functions", applied(["sin", "cos", "tan", "ln", "log", "exp", "min", "max", "floor", "ceil", "round"])),
  row("Layout", [
    ["Matrix", "[a, b; c, d]", "[‸, ; , ]"],
    ["Row", "[a, b, c]", "[‸, , ]"],
    ["Cases", "cases(a if x > 0; b otherwise)", "cases(‸ if ; otherwise)"],
    ["Hat", "hat(x)", "hat(‸)", "hat(§)‸"],
    ["Bar", "bar(x)", "bar(‸)", "bar(§)‸"],
    ["Vector", "vec(v)", "vec(‸)", "vec(§)‸"],
    ["Dot", "dot(x)", "dot(‸)", "dot(§)‸"],
    ["Two dots", "ddot(x)", "ddot(‸)", "ddot(§)‸"],
    ["Words", '"text"', '"‸"', '"§"‸'],
    ["Implies", "a => b", " => "],
    ["If and only if", "a <=> b", " <=> "],
    ["In", "x in A", " in "],
  ]),
];

export const math: BlockKind = {
  look: inCode,
  room: [10, 18],
  joins: false,
  apart: {
    name: "Equation",
    keys: mathKeys,
    problem: (words) => {
      const read = parseMath(words);
      return words.trim() === "" ? undefined : "problem" in read ? read.problem.says : undefined;
    },
  },
  showing: ({ words, focused, readOnly, zoom, align, toWords }) => {
    const set = words.trim() !== "" && "node" in parseMath(words);
    const raw = !set || (focused && !readOnly);
    return {
      raw,
      // Not shown while its equation is, but still there to take the caret, as a hidden
      // element can't.
      words: raw ? well : { opacity: 0 },
      over: set ? (
        <div
          style={{ gridArea: "1 / 1", alignSelf: "center", textAlign: align, fontSize: 16 * zoom, fontStyle: "normal", cursor: readOnly ? "default" : "text", visibility: raw ? "hidden" : "visible" }}
          onMouseDown={(e) => {
            if (readOnly) return;
            e.preventDefault();
            toWords();
          }}
          // A tap on a phone: its click is what lets the caret in, and the keyboard up.
          onClick={() => {
            if (!readOnly) toWords();
          }}
        >
          <Equation written={words} displayed />
        </div>
      ) : undefined,
    };
  },
};
