// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A block diagram: its words in one::diagram's notation, a line at a time, drawn as its
// blocks and the wires between them, laid out by itself. While the caret's in it, it's
// its words, in a well, Enter starting a line; otherwise its drawing. Written apart, it's
// drawn over its words as they're typed.

import { layOutDiagram } from "../diagram.js";
import { DiagramView } from "../diagram-view.js";
import type { ApartKeys, BlockKind } from "./kinds.js";
import { drawnKind, row } from "./math.js";

// What a diagram's written with.
const diagramKeys: ApartKeys[] = [
  row("Blocks", [
    ["Input", '"in x"', "in ‸"],
    ["Output", '"out y"', "out ‸"],
    ["Into", "a -> b", " -> "],
    ["Gain", "k", " -> *‸"],
    ["Integrator", "1/s", " -> 1/s"],
    ["Transfer function", "(s + 1)/(s + 2)", " -> (‸)/()"],
    ["Plus", "a + b", " + "],
    ["Minus", "a - b", " - "],
    ["Name under it", '"name"', ' "‸"'],
    ["Block of its own", "f(a -> b)", "‸( -> )"],
  ]),
];

const problem = (words: string) => {
  const laid = layOutDiagram(words);
  return "problem" in laid ? laid.problem.says : undefined;
};

export const diagram: BlockKind = {
  ...drawnKind({ name: "Diagram", keys: diagramKeys, lines: true }, problem, (words) => <DiagramView written={words} />),
  enter: (words, caret, selecting) => (!selecting && caret === words.length && words.endsWith("\n") ? "leave" : "line"),
};
